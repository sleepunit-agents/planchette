#include "hocket/engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>

#include "hocket/form.hpp"
#include "hocket/generators.hpp"
#include "hocket/rng.hpp"
#include "internal.hpp"

namespace hocket {

using detail::ceilDiv;
using detail::clampi;
using detail::floorDiv;
using detail::floorMod;
using detail::State;

namespace {

constexpr int kPref[12] = {0, 7, 4, 3, 10, 11, 9, 2, 5, 8, 6, 1};                             // §3.1
constexpr int kLong[16] = {10, 11, 12, 14, 16, 18, 19, 22, 24, 26, 28, 30, 32, 35, 37, 40};  // §4.2
constexpr int kChangeQuarters[5] = {1, 2, 4, 8, 16};  // §4.5: 1/4, 1/2, 1, 2, 4 bars, in quarter bars

// §1.2: the first level that sets a value wins.
int pick(std::optional<int> v, std::optional<int> g, int global) { return v ? *v : g ? *g : global; }

struct Resolved {
    int range, wander, accent, gatePercent, octaveLow, octaveHigh;
};
Resolved resolve(const Song& s, const Group& g, const Voice& v) {
    return {pick(v.overrides.range, g.overrides.range, s.range),
            pick(v.overrides.wander, g.overrides.wander, s.wander),
            pick(v.overrides.accent, g.overrides.accent, s.accent),  // §4.3: a voice's own, for its own notes
            pick(v.overrides.gatePercent, g.overrides.gatePercent, s.gatePercent),
            pick(v.overrides.octaveLow, std::nullopt, s.octaveLow),
            pick(v.overrides.octaveHigh, std::nullopt, s.octaveHigh)};
}

// §5.6: a step's owner under Tutti: every member of the group plays it.
constexpr int kTutti = -2;

int normalVelocity(int accent) { return 100 - (60 * accent) / 100; }
int accentVelocity(int accent) { return std::min(127, 100 + (27 * accent) / 100); }
std::int64_t gateTicks(std::int64_t st, int gate, int gatePercent) {
    return std::max<std::int64_t>(1, st * (gate == 1 ? 1 : gate == 2 ? 2 : 4) * gatePercent / 400);
}

// ---- one group during one bar -----------------------------------------------------------------------------

class World;

// What a group plays while a bar's state holds: its patterns, mutations applied, and who owns what.
class GroupPlay {
public:
    GroupPlay(World* w, const State& st, std::size_t gi, std::int64_t bar) : w_(w), st_(st), g_(st.song.groups[gi]), gi_(gi), bar_(bar) {
        const Song& s = st.song;
        L_ = clampi(g_.steps, 1, 64);
        stepTicks_ = detail::stepTicksIn(st, g_);  // §18.3: an irama level sets it from the harmony
        anchor_ = st.anchor.count(g_.id) ? st.anchor.at(g_.id) : 0;
        stepsBefore_ = st.stepsBefore.count(g_.id) ? st.stepsBefore.at(g_.id) : 0;
        // §4.6: a fill bar plays at a higher Density and ratchets more; the bar and phrase clocks say when.
        const std::int64_t P = static_cast<std::int64_t>(clampi(g_.fillEvery, 0, 16)) * std::max(1, s.phraseLength);
        fill_ = g_.fillNow || s.fill != 0 || (P > 0 && floorMod(bar, P) >= P - std::min<std::int64_t>(clampi(g_.fillBars, 1, 16), P));
        const int amount = clampi(g_.fillAmount, 1, 16);
        ratchet_ = clampi(g_.ratchet, 0, 100);
        if (fill_) ratchet_ = std::min(100, ratchet_ + static_cast<int>(rdiv(50 * amount, 16)));
        density_ = fill_ ? std::min(16, clampi(g_.density, 1, 16) + amount) : clampi(g_.density, 1, 16);
        // §16.2: a clocked generator's hits are read at t; the mutation map is kept but not applied while it plays.
        clocked_ = kindIn(s, SlotKind::Rhythm, gi, -1) == GenKind::Clocked;
        // §16.6: Interlock reads its source group's hits; its own slots are taken in Random's order.
        resultant_ = clocked_ && resultantSource(s, gi).has_value();  // §16.6: no hits of its own
        if (const auto src = clocked_ ? interlockSource(s, gi) : std::nullopt) {
            interlock_ = static_cast<int>(*src);
            random_ = hits(g_.rhythmSeed, L_, density_);
        }
        hit_ = clocked_ ? std::vector<bool>(static_cast<std::size_t>(L_), false) : detail::hitsOf(g_, st.mut, density_);
        if (st.mut.rhythm.count(g_.id) && !clocked_) cells_ = st.mut.rhythm.at(g_.id);
        for (int i = 0; i < static_cast<int>(g_.voices.size()); ++i)
            if (g_.voices[i].weight > 0 && !g_.voices[i].mute) eligible_.push_back(i);
        for (int i = static_cast<int>(g_.voices.size()) - 1; i >= 0; --i)
            if (g_.voices[static_cast<std::size_t>(i)].type != VoiceType::Chord) tune_ = i;
        for (bool h : hit_) hitSlots_ += h;
        if (g_.deal == DealMode::OnGenerate) ownersFor(hash(g_.dealSeed, {"deal"}), owners_);
        // Each line's pool indices per slot, its pitch mutations applied (§3.3, §7.5).
        const int n = static_cast<int>(ranked(s.root, s.root, s.scale).size());
        for (const Voice& v : g_.voices) {
            const int vi = static_cast<int>(lines_.size());
            const Resolved r = resolve(s, g_, v);
            const int ps = poolSize(n, r.range);
            const bool reg = v.type != VoiceType::Chord && std::string(playing(SlotKind::Notes, v.notesGen).name) == "register";
            // the voice's Notes generator (§16.5); a Register's are read at t, so there's no slot pattern to make
            std::vector<int> idx = reg ? std::vector<int>(static_cast<std::size_t>(L_), 0) : notePositions(v, L_, ps, r.wander);
            Line line;
            line.ps = ps;
            line.gen = playing(SlotKind::Notes, v.notesGen).name;
            line.clocked = v.type != VoiceType::Chord && kindIn(s, SlotKind::Notes, gi, vi) == GenKind::Clocked;
            if (line.clocked && line.gen == "kotekan") line.source = kotekanSource(s, gi, vi);  // §16.6
            line.order = orderOf(v.notesGen);
            line.tonic = onTonic(v.notesGen);
            line.dice = paramValue(SlotKind::Notes, v.notesGen, "pink", "dice");
            line.bits = paramValue(SlotKind::Notes, v.notesGen, "register", "bits");
            const auto it = st.mut.pitch.find(v.id);
            if (it != st.mut.pitch.end() && !line.clocked)
                for (const auto& [slot, u] : it->second)
                    if (slot < L_) idx[slot] = static_cast<int>(u % static_cast<std::uint64_t>(ps));
            indices_.push_back(std::move(idx));
            lines_.push_back(line);
        }
    }

    // §16.3: how a line's position is read (Ranked or Height) and from where (the chord root, or Pedal's tonic).
    NoteOrder order(int vi) const { return lines_[static_cast<std::size_t>(vi)].order; }
    bool tonic(int vi) const { return lines_[static_cast<std::size_t>(vi)].tonic; }
    // §16.6: the voice a Kotekan line reads, when it's live (group, voice).
    const std::optional<std::pair<std::size_t, int>>& kotekan(int vi) const { return lines_[static_cast<std::size_t>(vi)].source; }
    int pool(int vi) const { return lines_[static_cast<std::size_t>(vi)].ps; }
    // A line's pool position at a step: a static generator's at the slot; a clocked one's at the step's t (§16.3).
    int position(int vi, std::int64_t absStep, int slot) const;
    bool clocked() const { return clocked_; }
    int density() const { return density_; }
    // §5.6: a member of the group's deal (weight above 0, not muted), which Tutti gives every hit.
    bool member(int vi) const { return std::find(eligible_.begin(), eligible_.end(), vi) != eligible_.end(); }
    // §5.5: the group's tune, its first Lead or Bass voice (−1: none), whose note Alternate and Bells deal by pitch.
    int tune() const { return tune_; }
    bool partitioned() const { return (g_.deal == DealMode::Alternate || g_.deal == DealMode::Bells) && tune_ >= 0; }

    const Song& song() const { return st_.song; }
    const Group& group() const { return g_; }
    std::int64_t stepTicks() const { return stepTicks_; }
    std::int64_t anchor() const { return anchor_; }
    // §16.3's t: the group's steps counted from bar 0, through every re-anchor.
    std::int64_t t(std::int64_t absStep) const { return stepsBefore_ + absStep; }

    bool accent(int slot) const {
        const auto it = cells_.find(slot);
        return it != cells_.end() && it->second.hit ? it->second.accent : accentAt(g_.rhythmSeed, slot);
    }
    int gate(int slot) const {
        const auto it = cells_.find(slot);
        return it != cells_.end() && it->second.hit ? it->second.gate : gateAt(g_.rhythmSeed, slot, g_.density);
    }

    int slot(std::int64_t absStep) const {
        int slot = slotAt(absStep, L_, g_.direction, g_.offset);
        if (g_.jitter > 0 && roll(hash(g_.rhythmSeed, {"jitter", absStep})) < g_.jitter) {
            const int side = (hash(g_.rhythmSeed, {"jitter", absStep, "side"}) & 1) ? 1 : -1;
            slot = ((slot + side) % L_ + L_) % L_;
        }
        return slot;
    }
    bool hit(int slot) const { return hit_[slot]; }
    // Whether the step is a hit: a static generator's slot, or a clocked one's at the step's t (§16.3).
    bool hitAt(std::int64_t absStep, int slot) const;
    bool fill() const { return fill_; }

    // §4.6: whether a hit at this step plays, and how many times it sounds there (1, or a ratchet's 2…Repeats).
    // At Probability 100 and Ratchet 0 nothing is drawn.
    bool plays(std::int64_t bar, std::int64_t absStep) const {
        return g_.probability >= 100 || roll(hash(g_.rhythmSeed, {"probability", bar, absStep})) < g_.probability;
    }
    int repeats(std::int64_t bar, std::int64_t absStep) const {
        if (ratchet_ <= 0 || roll(hash(g_.rhythmSeed, {"ratchet", bar, absStep})) >= ratchet_) return 1;
        const int r = roll(hash(g_.rhythmSeed, {"ratchet", bar, absStep, "count"}));
        return 2 + r * (clampi(g_.repeats, 2, 4) - 1) / 100;
    }

    int owner(std::int64_t absStep, int slot) const {
        if (eligible_.empty()) return -1;
        switch (g_.deal) {
            case DealMode::OnGenerate: return owners_[slot];
            case DealMode::EveryCycle: {
                std::vector<int> o;
                ownersFor(hash(g_.dealSeed, {"deal", floorDiv(absStep, L_)}), o);
                return o[slot];
            }
            case DealMode::RoundRobin:
                return eligible_[static_cast<std::size_t>(floorMod(hitIndex(absStep), static_cast<std::int64_t>(eligible_.size())))];
            case DealMode::Alea:
                return weighted(hash(g_.dealSeed, {"alea", hitIndex(absStep)}), 0, static_cast<int>(eligible_.size()));
            case DealMode::Series: {
                // A shuffle bag: every member once, in an order of the bag's own, then a new bag. A bag never starts
                // with the member the last one ended on.
                const std::int64_t E = static_cast<std::int64_t>(eligible_.size()), n = hitIndex(absStep);
                const std::int64_t bag = floorDiv(n, E), at = floorMod(n, E);
                auto order = [&](std::int64_t b) {
                    std::vector<int> o = priorityOrder(hash(g_.dealSeed, {"series", b}), static_cast<int>(E));
                    return o;
                };
                std::vector<int> o = order(bag);
                if (E > 1 && o.front() == order(bag - 1).back()) std::swap(o[0], o[1]);
                return eligible_[static_cast<std::size_t>(o[static_cast<std::size_t>(at)])];
            }
            case DealMode::Group: {
                // Runs of `span` hits, each run a member by weight; a run tries not to repeat the one before.
                const int E = static_cast<int>(eligible_.size());
                const std::int64_t run = floorDiv(hitIndex(absStep), static_cast<std::int64_t>(clampi(g_.dealSpan, 1, 16)));
                const int now = weighted(hash(g_.dealSeed, {"group", run}), 0, E);
                const int before = weighted(hash(g_.dealSeed, {"group", run - 1}), 0, E);
                if (E > 1 && now == before) {
                    for (int k = 0; k < E; ++k)
                        if (eligible_[static_cast<std::size_t>(k)] == now) return eligible_[static_cast<std::size_t>((k + 1) % E)];
                }
                return now;
            }
            case DealMode::Tendency: {
                // A window of `span` members, moving one member per pass through the pattern, back and forth across
                // the group; each hit goes to a member inside it, by weight.
                const int E = static_cast<int>(eligible_.size()), w = std::min(E, clampi(g_.dealSpan, 1, 16));
                const int positions = E - w + 1;
                const std::int64_t cycle = floorDiv(absStep, L_);
                int at = 0;
                if (positions > 1) {
                    const std::int64_t p = floorMod(cycle, static_cast<std::int64_t>(2 * (positions - 1)));
                    at = static_cast<int>(p < positions ? p : 2 * (positions - 1) - p);
                }
                return weighted(hash(g_.dealSeed, {"tendency", hitIndex(absStep)}), at, at + w);
            }
            case DealMode::Solo: {
                const int m = clampi(g_.soloMember, 0, static_cast<int>(g_.voices.size()) - 1);
                return g_.voices[m].mute ? -1 : m;
            }
            case DealMode::Alternate: case DealMode::Bells: return partitionOwner(absStep, slot);  // §5.5: by pitch
            case DealMode::Tutti: return kTutti;  // §5.6: everyone
        }
        return -1;
    }

private:
    // The n-th hit since the anchor (counting this step's slot as a hit or not): whole cycles, then this cycle's hits.
    std::int64_t hitIndex(std::int64_t absStep) const {
        if (clocked_) return clockedHitIndex(absStep);
        const std::int64_t cycle = floorDiv(absStep, L_);
        std::int64_t n = cycle * hitSlots_;
        for (std::int64_t a = cycle * L_; a < absStep; ++a) n += hit_[slotAt(a, L_, g_.direction, g_.offset)];
        return n;
    }

    // Under a clocked generator the hits differ pass to pass, so they're counted as they played: the hits from the
    // anchor to this bar (each bar at its own settings), then this bar's before the step.
    std::int64_t clockedHitIndex(std::int64_t absStep) const;

    int partitionOwner(std::int64_t absStep, int slot) const;

    // A member of eligible_[from, to) by weight, from a hash.
    int weighted(std::uint64_t h, int from, int to) const {
        std::int64_t total = 0;
        for (int k = from; k < to; ++k) total += g_.voices[eligible_[static_cast<std::size_t>(k)]].weight;
        if (total <= 0) return eligible_[static_cast<std::size_t>(from)];
        std::int64_t r = static_cast<std::int64_t>(Stream(h).below(static_cast<std::uint64_t>(total)));
        for (int k = from; k < to; ++k) {
            r -= g_.voices[eligible_[static_cast<std::size_t>(k)]].weight;
            if (r < 0) return eligible_[static_cast<std::size_t>(k)];
        }
        return eligible_[static_cast<std::size_t>(to - 1)];
    }

    // §5.2: a largest-remainder split of the L slots by weight, dealt in the deal order.
    void ownersFor(std::uint64_t h, std::vector<int>& out) const {
        out.assign(L_, -1);
        if (eligible_.empty()) return;
        std::int64_t total = 0;
        for (int i : eligible_) total += g_.voices[i].weight;
        std::vector<std::int64_t> counts, rem;
        std::int64_t given = 0;
        for (int i : eligible_) {
            const std::int64_t w = g_.voices[i].weight;
            counts.push_back((L_ * w) / total);
            rem.push_back((L_ * w) % total);
            given += counts.back();
        }
        std::vector<int> order(eligible_.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rem[a] > rem[b]; });
        for (std::int64_t k = 0; given < L_; ++k, ++given) counts[order[k % order.size()]]++;
        const std::vector<int> P = priorityOrder(h, L_);
        std::size_t at = 0;
        for (std::size_t m = 0; m < eligible_.size(); ++m)
            for (std::int64_t c = 0; c < counts[m]; ++c) out[P[at++]] = eligible_[m];
    }

    struct Line {
        int ps = 1;
        std::string gen;
        bool clocked = false, tonic = false;
        NoteOrder order = NoteOrder::Ranked;
        int dice = 4, bits = 8;
        std::optional<std::pair<std::size_t, int>> source;  // Kotekan's, when live (§16.6)
    };
    World* w_;
    const State& st_;
    const Group& g_;
    std::size_t gi_;
    std::int64_t bar_;
    bool clocked_ = false;
    int interlock_ = -1;         // §16.6: the source group an Interlock reads (−1: not one)
    bool resultant_ = false;     // a live Resultant: its voices play the source's band, and it has no hits
    int tune_ = -1;              // §5.5: the first Lead or Bass voice
    std::vector<bool> random_;   // and Random's hits at this Density, which its slots must be in
    int density_ = 8;
    std::vector<Line> lines_;
    int L_ = 16;
    std::int64_t stepTicks_ = 240, anchor_ = 0, stepsBefore_ = 0;
    std::vector<bool> hit_;
    bool fill_ = false;
    int ratchet_ = 0;
    std::map<int, RhythmCell> cells_;
    std::vector<int> eligible_, owners_;
    std::vector<std::vector<int>> indices_;
    std::int64_t hitSlots_ = 0;
};

// ---- the song over time -----------------------------------------------------------------------------------

// A step of one group: which bar's state it plays under, and what it plays.
struct Step {
    bool valid = false;
    std::int64_t bar = 0, absStep = 0;
    std::int64_t t = 0;        // §16.3: the group's steps from bar 0
    std::int64_t nominal = 0;  // where the step is on the grid
    std::int64_t tick = 0;     // where it sounds (swing applied)
    int slot = 0;
    bool hit = false;
    int owner = -1;
    bool land = false;  // §18.4: a tihai's landing on the sam, a hit and accented
};

// The replay, each bar's group plays, and the walk from step to step across bar lines. A step plays under the state
// of the bar it starts in; a group's steps restart from its anchor where the matrix moved its clock (§4.4).
class World {
public:
    // `hold` ≥ 1: Freeze (§9, §17.6) holds from that bar line on, every later bar playing the state of bar hold − 1.
    explicit World(const Song& song, std::int64_t hold = -1)
        : replay_(song), barTicks_(hocket::barTicks(song)), hold_(hold >= 1 ? hold : -1) {
        // §18.4: only a song that has a tihai somewhere looks for one.
        for (const Group& g : song.groups) tihai_ = tihai_ || g.tihai > 0 || g.tihaiNow;
        for (const Row& r : song.matrix) tihai_ = tihai_ || r.key == "tihai";
        for (const Cord& c : song.mod.cords) tihai_ = tihai_ || c.dest.key == "tihai";
    }

    std::int64_t barTicks() const { return barTicks_; }
    std::int64_t barOf(std::int64_t tick) const { return floorDiv(tick, barTicks_); }
    // The state a bar plays under: the replay's, or under Freeze the held one. The replay itself never holds.
    const State& state(std::int64_t bar) { return replay_.at(hold_ >= 1 && bar >= hold_ ? hold_ - 1 : bar); }
    const State& replayed(std::int64_t bar) { return replay_.at(bar); }
    std::int64_t hold() const { return hold_; }

    const GroupPlay& play(std::int64_t bar, std::size_t gi) {
        bar = std::max<std::int64_t>(0, bar);
        auto& slot = plays_[{bar, gi}];
        if (!slot) slot = std::make_unique<GroupPlay>(this, state(bar), gi, bar);
        return *slot;
    }

    // §16.3: the t of a group's first step at or after a bar line (its steps in the bar run from here to the next's).
    std::int64_t firstT(std::size_t gi, std::int64_t bar) {
        const State& st = state(bar);
        const Group& g = st.song.groups[gi];
        const std::int64_t anchor = st.anchor.count(g.id) ? st.anchor.at(g.id) : 0;
        const std::int64_t before = st.stepsBefore.count(g.id) ? st.stepsBefore.at(g.id) : 0;
        return before + ceilDiv(bar * barTicks_ - anchor, detail::stepTicksIn(st, g));
    }

    // §16.7: the value v(t) of a register as a bar's state has it: the seed in force, the flips Vary made to it, and
    // stepped from t = 0 with the n and Lock of each bar its steps play under. Kind 0: group gi's rhythm; 1: voice
    // `voice` of group gi's notes. Each register is worked out once and kept.
    int registerValue(int kind, std::size_t gi, std::uint64_t voice, std::int64_t bar, std::int64_t t, int w) {
        // Which register a bar plays is worked out once per bar.
        auto& slot = regOfBar_[std::make_tuple(bar, kind, gi, voice)];
        if (!slot) slot = &registerOf(kind, gi, voice, bar);
        return advance(*slot, gi, t).reg.value(t, w);
    }

private:
    struct RegState;
    RegState& registerOf(int kind, std::size_t gi, std::uint64_t voice, std::int64_t bar) {
        const State& st = state(bar);
        const Group& g = st.song.groups[gi];
        std::uint64_t target = g.id, seed = g.rhythmSeed;
        if (kind == 1)
            for (const Voice& v : g.voices)
                if (v.id == voice) target = v.id, seed = v.pitchSeed;
        RegKey key{kind, gi, target, seed, {}};
        for (const RegisterFlip& f : st.mut.registers)
            if (f.kind == kind && f.target == target && f.seed == seed) key.flips.push_back(f);
        std::stable_sort(key.flips.begin(), key.flips.end(), [](const RegisterFlip& a, const RegisterFlip& b) { return a.t0 < b.t0; });
        auto it = registers_.find(key);
        if (it == registers_.end()) it = registers_.emplace(key, RegState{Register(seed), kind, target, key.flips, 0, -1, 0, 0, 2}).first;
        return it->second;
    }
    RegState& advance(RegState& r, std::size_t gi, std::int64_t t) {
        while (r.reg.next() <= t) {
            const std::int64_t tt = r.reg.next();
            if (tt >= r.barEnd) {  // into the next bar its steps play under: that bar's n and Lock
                while (firstT(gi, r.bar + 1) <= tt) ++r.bar;
                r.barEnd = firstT(gi, r.bar + 1);
                const State& at = state(r.bar);
                const GenSlot* slot = &at.song.groups[gi].rhythmGen;
                if (r.kind == 1)
                    for (const Voice& v : at.song.groups[gi].voices)
                        if (v.id == r.target) slot = &v.notesGen;
                const SlotKind sk = r.kind == 0 ? SlotKind::Rhythm : SlotKind::Notes;
                r.n = paramValue(sk, *slot, "register", "length");
                r.lock = paramValue(sk, *slot, "register", "lock");
            }
            for (; r.flip < r.flips.size() && r.flips[r.flip].t0 <= tt; ++r.flip)
                if (r.flips[r.flip].t0 == tt) r.reg.flip(r.flips[r.flip].back);
            r.reg.step(r.n, r.lock);
        }
        return r;
    }

public:

    // The hits a clocked group played from its anchor to a bar line, each bar at its own settings.
    std::int64_t clockedHitsBefore(std::size_t gi, std::int64_t bar) {
        const auto key = std::make_pair(gi, bar);
        if (const auto it = hitsBefore_.find(key); it != hitsBefore_.end()) return it->second;
        // Back to the bar where this anchor began, then forward.
        const std::uint64_t id = state(bar).song.groups[gi].id;
        const std::int64_t anchor = state(bar).anchor.at(id);
        std::int64_t b0 = bar;
        while (b0 > 0 && state(b0 - 1).anchor.at(id) == anchor && !hitsBefore_.count({gi, b0})) --b0;
        std::int64_t n = hitsBefore_.count({gi, b0}) ? hitsBefore_.at({gi, b0}) : 0;
        hitsBefore_[{gi, b0}] = n;
        for (std::int64_t b = b0; b < bar; ++b) {
            const GroupPlay& p = play(b, gi);
            for (std::int64_t a = ceilDiv(b * barTicks_ - p.anchor(), p.stepTicks()); p.anchor() + a * p.stepTicks() < (b + 1) * barTicks_; ++a)
                n += p.hitAt(a, p.slot(a));
            hitsBefore_[{gi, b + 1}] = n;
        }
        return n;
    }

    // §16.6: whether a group has a hit on its grid in [from, to): its generator's, before Probability, at the step's
    // slot without Jitter.
    bool hitIn(std::size_t gi, std::int64_t from, std::int64_t to) {
        const Hop hop(*this);
        if (tooDeep()) return false;  // the backstop: no source hit
        from = std::max<std::int64_t>(0, from);
        for (std::int64_t bar = barOf(from); bar * barTicks_ < to; ++bar) {
            const GroupPlay& p = play(bar, gi);
            const Group& g = p.group();
            const int L = clampi(g.steps, 1, 64);
            for (std::int64_t a = ceilDiv(std::max(from, bar * barTicks_) - p.anchor(), p.stepTicks());; ++a) {
                const std::int64_t at = p.anchor() + a * p.stepTicks();
                if (at >= to || at >= (bar + 1) * barTicks_) break;
                if (p.hitAt(a, slotAt(a, L, g.direction, g.offset))) return true;
            }
        }
        return false;
    }

    // The first step of a group whose grid position is at or after `tick`.
    Step stepFrom(std::size_t gi, std::int64_t tick) {
        tick = std::max<std::int64_t>(0, tick);
        for (std::int64_t bar = barOf(tick);; ++bar) {
            const GroupPlay& p = play(bar, gi);
            const std::int64_t from = std::max(tick, bar * barTicks_);
            const std::int64_t a = ceilDiv(from - p.anchor(), p.stepTicks());
            if (p.anchor() + a * p.stepTicks() < (bar + 1) * barTicks_) return make(bar, gi, a);
        }
    }
    Step after(std::size_t gi, const Step& s) { return stepFrom(gi, s.nominal + 1); }
    // §16.6: the step of a group whose grid tick is the latest at or before `tick`, its place only: no hit, no owner,
    // so reading a source never asks a deal (which, under Alternate or Bells, asks a pitch, §5.5).
    Step gridAt(std::size_t gi, std::int64_t tick) {
        for (std::int64_t bar = barOf(std::max<std::int64_t>(0, tick)); bar >= 0 && tick >= 0; --bar) {
            const GroupPlay& p = play(bar, gi);
            const std::int64_t to = std::min(tick, (bar + 1) * barTicks_ - 1);
            const std::int64_t a = floorDiv(to - p.anchor(), p.stepTicks());
            if (p.anchor() + a * p.stepTicks() >= bar * barTicks_) return place(bar, gi, a);
        }
        return Step{};
    }

    // A guard on the derived generators' recursion (§16.6): each hop from a slot into its source is one level. Loops
    // are broken in the song's graph already; this is the backstop for anything that slips past, and past it every
    // hop plays its plain material (the line's own note, the plain deal, no source hit).
    struct Hop {
        explicit Hop(World& w) : w_(w) {
            ++w_.depth_;
            if (w_.depth_ > highWater()) highWater() = w_.depth_;
        }
        ~Hop() { --w_.depth_; }
        Hop(const Hop&) = delete;
        Hop& operator=(const Hop&) = delete;
        World& w_;
    };
    bool tooDeep() const { return depth_ > kMaxHops; }
    // §5.7: a played line hit's final note, kept once worked out, by (group, grid tick).
    std::optional<int> heard(std::size_t gi, std::int64_t nominal) const {
        const auto it = heard_.find({gi, nominal});
        return it == heard_.end() ? std::nullopt : std::optional<int>(it->second);
    }
    void setHeard(std::size_t gi, std::int64_t nominal, int note) { heard_[{gi, nominal}] = note; }
    static constexpr int kMaxHops = 64;
    static int& highWater() {
        static thread_local int h = 0;
        return h;
    }
    // The step before s, if there is one.
    Step before(std::size_t gi, const Step& s) {
        for (std::int64_t bar = barOf(s.nominal - 1); bar >= 0 && s.nominal > 0; --bar) {
            const GroupPlay& p = play(bar, gi);
            const std::int64_t to = std::min(s.nominal, (bar + 1) * barTicks_) - 1;
            const std::int64_t a = floorDiv(to - p.anchor(), p.stepTicks());
            const std::int64_t nominal = p.anchor() + a * p.stepTicks();
            if (nominal >= bar * barTicks_) return make(bar, gi, a);
        }
        return Step{};
    }

    // Lets go of bars before `bar`: a long set doesn't hold every bar it ever played.
    void forget(std::int64_t bar) {
        plays_.erase(plays_.begin(), plays_.lower_bound({bar, 0}));
        for (auto it = heard_.begin(); it != heard_.end();) it = it->first.second < bar * barTicks_ ? heard_.erase(it) : std::next(it);
        progressions_.erase(progressions_.begin(), progressions_.lower_bound(bar));
    }

    // §4.5: the current chord's root at a tick.
    int chordRoot(std::int64_t tick) { return chordAt(tick).first; }
    // ...and whether it's the home chord turned major (§3.8).

    std::pair<int, bool> chordAt(std::int64_t tick) {
        const std::int64_t bar = std::max<std::int64_t>(0, barOf(tick));
        const State& st = state(bar);
        auto it = progressions_.find(bar);
        if (it == progressions_.end()) {
            Progression p;
            p.index = detail::progressionOf(st.song, st.mut);
            p.major = homeMajor(st.song, p.index);
            p.roots = harmonyRanked(st.song);
            it = progressions_.emplace(bar, std::move(p)).first;
        }
        const Progression& prog = it->second;
        const std::int64_t k = floorMod(floorDiv(tick - st.harmonyAnchor, changeTicks(st.song)), static_cast<std::int64_t>(prog.index.size()));
        const std::size_t at = static_cast<std::size_t>(k);
        return {prog.roots[static_cast<std::size_t>(prog.index[at])], prog.major[at]};
    }

private:
    int depth_ = 0;
    std::map<std::pair<std::size_t, std::int64_t>, int> heard_;
    Step place(std::int64_t bar, std::size_t gi, std::int64_t a) {
        const GroupPlay& p = play(bar, gi);
        Step s;
        s.valid = true;
        s.bar = bar;
        s.absStep = a;
        s.t = p.t(a);
        s.nominal = p.anchor() + a * p.stepTicks();
        s.tick = s.nominal + ((a & 1) ? (p.group().swing * p.stepTicks()) / 200 : 0);
        s.slot = p.slot(a);
        return s;
    }
    // §18.4: what a step is in a tihai: nothing, a statement of the phrase (its step in the first statement), a gap,
    // or the landing on the sam.
    enum class Role { None, Phrase, Gap, Land };
    std::pair<Role, std::int64_t> tihaiRole(std::int64_t bar, std::size_t gi, std::int64_t a) {
        const GroupPlay& p = play(bar, gi);
        const std::int64_t A = p.anchor(), T = p.stepTicks(), tick = A + a * T;
        const std::int64_t reach = (64 * T) / barTicks_ + 2;  // a window is at most 3·16 + 2·8 = 64 steps
        for (std::int64_t B = std::max<std::int64_t>(1, barOf(tick) - 1); B <= barOf(tick) + reach; ++B) {
            const State& st = state(B - 1);
            const Group& g = st.song.groups[gi];
            const std::int64_t phrase = std::max(1, st.song.phraseLength);
            const std::int64_t E = clampi(g.tihai, 0, 16), unit = g.tihaiUnit == 1 ? gongBars(st.song) : phrase;
            std::int64_t P = 0;  // the sam's period, in bars
            if (E > 0 && floorMod(B, E * unit) == 0) P = E * unit;
            else if (g.tihaiNow && floorMod(B, phrase) == 0) P = phrase;
            if (P == 0) continue;
            const GroupPlay& q = play(B - 1, gi);  // the grid it lands from: the bar before the sam's
            if (q.anchor() != A || q.stepTicks() != T) continue;
            const std::int64_t N = clampi(g.tihaiLength, 1, 16), gap = clampi(g.tihaiGap, 0, 8);
            const std::int64_t sam = ceilDiv(B * barTicks_ - A, T), a0 = sam - (3 * N + 2 * gap);  // start + 3N + 2g = sam
            if (a < a0 || a > sam) continue;
            if (a0 < 0 || A + a0 * T < (B - P) * barTicks_) continue;  // it doesn't fit after the last sam: no tihai
            const GroupPlay& r = play(barOf(A + a0 * T), gi);
            if (r.anchor() != A || r.stepTicks() != T) continue;  // the window crosses a re-anchor
            if (a == sam) return {Role::Land, sam};
            const std::int64_t k = (a - a0) % (N + gap);
            return k < N ? std::pair<Role, std::int64_t>{Role::Phrase, a0 + k} : std::pair<Role, std::int64_t>{Role::Gap, a};
        }
        return {Role::None, a};
    }

    Step make(std::int64_t bar, std::size_t gi, std::int64_t a) {
        Step s = plain(bar, gi, a);
        if (!tihai_) return s;
        const auto [role, src] = tihaiRole(bar, gi, a);
        if (role == Role::Gap) s.hit = false, s.owner = -1;
        if (role == Role::Land) {
            const GroupPlay& p = play(bar, gi);
            s.hit = true, s.land = true, s.owner = p.owner(a, s.slot);
        }
        if (role == Role::Phrase && src != a) {
            // A second or third statement: the first's step (its slot, hit, owner, pool position), at this one's time.
            const GroupPlay& p = play(bar, gi);
            const Step c = plain(std::max<std::int64_t>(0, barOf(p.anchor() + src * p.stepTicks())), gi, src);
            s.bar = c.bar, s.absStep = c.absStep, s.t = c.t, s.slot = c.slot, s.hit = c.hit, s.owner = c.owner;
        }
        return s;
    }

    Step plain(std::int64_t bar, std::size_t gi, std::int64_t a) {
        const GroupPlay& p = play(bar, gi);
        Step s;
        s.valid = true;
        s.bar = bar;
        s.absStep = a;
        s.t = p.t(a);
        s.nominal = p.anchor() + a * p.stepTicks();
        s.tick = s.nominal + ((a & 1) ? (p.group().swing * p.stepTicks()) / 200 : 0);
        s.slot = p.slot(a);
        s.hit = p.hitAt(a, s.slot) && p.plays(bar, a);  // a hit that doesn't play is a rest (§4.6)
        if (s.hit) s.owner = p.owner(a, s.slot);
        return s;
    }

    detail::Replay replay_;
    std::int64_t barTicks_;
    std::int64_t hold_ = -1;
    bool tihai_ = false;
    std::map<std::pair<std::int64_t, std::size_t>, std::unique_ptr<GroupPlay>> plays_;
    struct RegKey {
        int kind;
        std::size_t gi;
        std::uint64_t target, seed;
        std::vector<RegisterFlip> flips;
        bool operator<(const RegKey& o) const { return std::tie(kind, gi, target, seed, flips) < std::tie(o.kind, o.gi, o.target, o.seed, o.flips); }
    };
    struct RegState {
        Register reg;
        int kind;
        std::uint64_t target;
        std::vector<RegisterFlip> flips;
        std::int64_t bar;     // the bar the next step plays under
        std::int64_t barEnd;  // the t its steps end at (−1: not looked up yet)
        std::size_t flip;     // the next flip to apply
        int lock, n;          // that bar's
    };
    std::map<RegKey, RegState> registers_;
    std::map<std::tuple<std::int64_t, int, std::size_t, std::uint64_t>, RegState*> regOfBar_;
    std::map<std::pair<std::size_t, std::int64_t>, std::int64_t> hitsBefore_;
    struct Progression {
        std::vector<int> index;  // into roots
        std::vector<bool> major;  // a home chord turned major (§3.8)
        std::vector<int> roots;  // harmonyRanked
    };
    std::map<std::int64_t, Progression> progressions_;
};

bool GroupPlay::hitAt(std::int64_t absStep, int slot) const {
    if (!clocked_) return hit_[static_cast<std::size_t>(slot)];
    if (resultant_) return false;
    if (interlock_ >= 0) {  // §16.6: a hit where the source has none on this step's grid span, in Random's order
        const std::int64_t T = anchor_ + absStep * stepTicks_;
        return random_[static_cast<std::size_t>(slot)] && !w_->hitIn(static_cast<std::size_t>(interlock_), T, T + stepTicks_);
    }
    const int w = paramValue(SlotKind::Rhythm, g_.rhythmGen, "register", "bits");
    return registerHit(w_->registerValue(0, gi_, 0, bar_, t(absStep), w), w, density_);
}

std::int64_t GroupPlay::clockedHitIndex(std::int64_t absStep) const {
    const std::int64_t first = ceilDiv(bar_ * w_->barTicks() - anchor_, stepTicks_);
    std::int64_t n = w_->clockedHitsBefore(gi_, bar_);
    for (std::int64_t a = first; a < absStep; ++a) n += hitAt(a, slot(a));
    for (std::int64_t a = absStep; a < first; ++a) n -= hitAt(a, slot(a));
    return n;
}

int GroupPlay::position(int vi, std::int64_t absStep, int slot) const {
    const Line& l = lines_[static_cast<std::size_t>(vi)];
    const int at = indices_[static_cast<std::size_t>(vi)][static_cast<std::size_t>(slot)];
    if (!l.clocked) return at;
    if (l.source) return 0;  // Kotekan makes its note from its source's (linePitch), not from a position
    if (l.gen == "pink")  // Run: the roots as Wander's, the rest from the dice at t (§16.5)
        return at == 0 ? 0 : pinkAt(g_.voices[static_cast<std::size_t>(vi)].pitchSeed, l.ps, l.dice, t(absStep));
    return registerPosition(w_->registerValue(1, gi_, g_.voices[static_cast<std::size_t>(vi)].id, bar_, t(absStep), l.bits), l.bits, l.ps);
}

// §4.6: a ratcheted step's n repeats, evenly inside the step (from where it sounds to the next grid line), each
// lasting Gate % / 200 of its gap, their velocity ramped from v.
struct Repeat {
    std::int64_t start, end;
    int velocity;
};
std::vector<Repeat> ratchetOf(const Step& s, std::int64_t stepTicks, int n, int gatePercent, int v, int ramp) {
    std::vector<Repeat> out;
    const std::int64_t span = s.nominal + stepTicks - s.tick;
    for (int i = 0; i < n; ++i) {
        const std::int64_t a = s.tick + i * span / n, b = s.tick + (i + 1) * span / n;
        const std::int64_t len = std::max<std::int64_t>(1, (b - a) * gatePercent / 200);
        const std::int64_t num = static_cast<std::int64_t>(v) * (100 * (n - 1) + static_cast<std::int64_t>(clampi(ramp, -100, 100)) * i);
        out.push_back({a, a + len, static_cast<int>(std::max<std::int64_t>(1, std::min<std::int64_t>(127, rdiv(num, 100 * (n - 1)))))});
    }
    return out;
}

// ---- Humanize (§4.7) --------------------------------------------------------------------------------------

// A note's velocity, moved by up to ±humanVelocity: a draw of its own, from the voice, the tick and the note.
int humanVelocity(const Song& song, const Voice& v, std::int64_t at, int note, int velocity) {
    const int H = clampi(v.humanVelocity, 0, 32);
    if (H == 0) return velocity;
    const std::int64_t r = static_cast<std::int64_t>(Stream(hash(song.masterSeed, {"humanize", v.id, at, note, "velocity"})).below(2001));
    return clampi(velocity + static_cast<int>((r - 1000) * H / 1000), 1, 127);
}

// How late a line's step sounds: up to humanTiming % of the room between where it sounds and the next grid line, so
// it never leaves its step.
std::int64_t humanNudge(const Song& song, const Voice& v, const Step& s, std::int64_t stepTicks) {
    const int T = clampi(v.humanTiming, 0, 100);
    const std::int64_t room = s.nominal + stepTicks - s.tick;
    if (T == 0 || !s.valid || room <= 1) return 0;
    const std::int64_t r = static_cast<std::int64_t>(Stream(hash(song.masterSeed, {"humanize", v.id, s.nominal, "timing"})).below(1001));
    return (room - 1) * T * r / 100000;
}

// ---- Lead and Bass ----------------------------------------------------------------------------------------

// `forcePos` ≥ 0: the line's note at that pool position instead of its generator's (§5.7's redraw).
int linePitch(World& w, std::size_t gi, int vi, const Step& s, int forcePos = -1) {
    const World::Hop hop(w);
    const bool deep = w.tooDeep();  // past the backstop: the line's own note, nothing derived
    const GroupPlay& p = w.play(s.bar, gi);
    // §5.5: under Alternate and Bells every line plays the tune's note.
    if (!deep && p.partitioned() && vi != p.tune() && p.group().voices[static_cast<std::size_t>(vi)].type != VoiceType::Chord)
        return linePitch(w, gi, p.tune(), s);
    const Song& song = p.song();
    const Group& g = p.group();
    const Voice& v = g.voices[vi];
    const Resolved r = resolve(song, g, v);
    const auto [root, major] = w.chordAt(s.tick);
    const int centre = p.tonic(vi) ? song.root : root;  // Pedal measures from the key's tonic (§16.5)
    const std::vector<int> rk = lineRanked(song, centre);
    const int pos = clampi(forcePos >= 0 ? forcePos : p.position(vi, s.absStep, s.slot), 0, std::min(p.pool(vi), static_cast<int>(rk.size())) - 1);
    auto third = [&](int x) { return major && x == (song.root + 3) % 12 ? (song.root + 4) % 12 : x; };  // §3.8: a major home chord's
    if (const auto& src = deep || forcePos >= 0 ? std::nullopt : p.kotekan(vi)) {
        // §16.6, Kotekan: from the source's note at its step at this step (played or not), over this voice's pool
        // around the chord root, in the source's register.
        const Step a = w.gridAt(src->first, s.nominal);
        if (a.valid) {
            const int x = linePitch(w, src->first, src->second, a);
            std::vector<int> pool;
            for (int i = 0; i < std::min(p.pool(vi), static_cast<int>(rk.size())); ++i) pool.push_back(third(rk[static_cast<std::size_t>(i)]));
            const int mode = paramValue(SlotKind::Notes, v.notesGen, "kotekan", "mode");
            bool anticipate = false;
            int nextRoot = root;
            if (mode == 3) {  // Nyok cok: the next change, and whether it's within Lead-in steps
                const State& st = w.state(s.bar);
                const std::int64_t ct = changeTicks(st.song);
                const std::int64_t next = st.harmonyAnchor + (floorDiv(s.nominal - st.harmonyAnchor, ct) + 1) * ct;
                anticipate = next - s.nominal <= paramValue(SlotKind::Notes, v.notesGen, "kotekan", "lead") * p.stepTicks();
                nextRoot = w.chordAt(next).first;
            }
            const Voice& av = w.play(a.bar, src->first).group().voices[static_cast<std::size_t>(src->second)];
            return clampi(kotekanNote(mode, pool, root, x, anticipate, nextRoot) + 12 * (v.octaveTranspose - av.octaveTranspose), 0, 127);
        }
    }
    int pc = third(rk[static_cast<std::size_t>(pos)]);
    if (p.order(vi) == NoteOrder::Height) {
        // §16.3: the pool sorted upwards from the centre, and placed upwards from it (centre … centre + 11).
        std::vector<int> pool;
        for (int i = 0; i < std::min(p.pool(vi), static_cast<int>(rk.size())); ++i) pool.push_back(third(rk[static_cast<std::size_t>(i)]));
        std::stable_sort(pool.begin(), pool.end(), [&](int a, int b) { return floorMod(a - centre, 12) < floorMod(b - centre, 12); });
        pc = centre + static_cast<int>(floorMod(pool[static_cast<std::size_t>(pos)] - centre, 12));
    }
    const int lo = r.octaveLow, hi = std::max(r.octaveLow, r.octaveHigh);
    const int lane = octaveLaneAt(v.pitchSeed, s.slot, g.density, song.octaveJumps);
    const int oct = lo == hi ? lo : clampi(lo + static_cast<int>(rdiv((lane + 1) * (hi - lo), 3)), lo, hi);
    return clampi(36 + pc + 12 * oct + 12 * v.octaveTranspose, 0, 127);
}

// `heedMute` false: as if neither the group nor the voice were muted (a Resultant's composite, §16.6).
// §5.5: the member that owns the tune's note at a step.
int GroupPlay::partitionOwner(std::int64_t absStep, int slot) const {
    std::vector<int> E;
    for (int i : eligible_)
        if (g_.voices[static_cast<std::size_t>(i)].type != VoiceType::Chord) E.push_back(i);
    if (E.empty() || tune_ < 0) return -1;
    if (w_->tooDeep()) {  // the backstop (§16.6): the plain deal
        std::vector<int> o;
        ownersFor(hash(g_.dealSeed, {"deal"}), o);
        return o[static_cast<std::size_t>(slot)];
    }
    Step s;
    s.valid = true, s.bar = bar_, s.absStep = absStep, s.t = t(absStep), s.slot = slot;
    s.nominal = anchor_ + absStep * stepTicks_;
    s.tick = s.nominal + ((absStep & 1) ? (g_.swing * stepTicks_) / 200 : 0);
    const std::int64_t l = ladderPlace(linePitch(*w_, gi_, tune_, s), st_.song.root, st_.song.scale);
    const std::int64_t N = static_cast<std::int64_t>(E.size());
    const std::int64_t at = g_.deal == DealMode::Bells ? floorDiv(l, clampi(g_.dealSpan, 1, 16)) : l;
    return E[static_cast<std::size_t>(floorMod(at, N))];
}

// ---- Clean handoffs (§5.7) --------------------------------------------------------------------------------

bool lineHit(World& w, std::size_t gi, const Step& s) {  // a played hit, one Lead or Bass member's
    return s.valid && s.hit && s.owner >= 0 && w.play(s.bar, gi).group().voices[static_cast<std::size_t>(s.owner)].type != VoiceType::Chord;
}
bool cleansAt(World& w, std::size_t gi, const Step& s) {  // whether this hit is tested, if it's a handoff
    const GroupPlay& p = w.play(s.bar, gi);
    return p.group().cleanHandoffs && !p.partitioned() && lineHit(w, gi, s) && !p.kotekan(s.owner);
}
// The hit before s that played, at most 256 steps back.
Step playedBefore(World& w, std::size_t gi, const Step& s) {
    Step p = w.before(gi, s);
    for (int k = 0; p.valid && !p.hit && k < 256; ++k) p = w.before(gi, p);
    return p.valid && p.hit ? p : Step{};
}
// Up to three seeded draws of another pool position for a note on the pitch class `avoid`; none: x as it was.
int redrawn(World& w, std::size_t gi, const Step& s, int x, int avoid) {
    const GroupPlay& p = w.play(s.bar, gi);
    const int vi = s.owner;
    return handoffRedraw(p.group().voices[static_cast<std::size_t>(vi)].pitchSeed, s.slot, p.pool(vi), avoid,
                         [&](int c) { return linePitch(w, gi, vi, s, c); })
        .value_or(x);
}
// The note voice vi plays at s: its line's, or under Clean handoffs the final one, a fold from the start of the run of
// handoffs it ends (each kept once worked out).
int heardPitch(World& w, std::size_t gi, int vi, const Step& s) {
    if (s.owner != vi || !cleansAt(w, gi, s)) return linePitch(w, gi, vi, s);
    std::vector<Step> run;
    Step cur = s;
    int prev = 0;
    for (;;) {
        if (const auto h = w.heard(gi, cur.nominal)) {
            prev = *h;
            break;
        }
        const Step p = cleansAt(w, gi, cur) ? playedBefore(w, gi, cur) : Step{};
        // Not a handoff (or one from a Kotekan, whose notes are a figure on purpose): the run starts here, as drawn.
        if (!lineHit(w, gi, p) || p.owner == cur.owner || w.play(p.bar, gi).kotekan(p.owner)) {
            prev = linePitch(w, gi, cur.owner, cur);
            w.setHeard(gi, cur.nominal, prev);
            break;
        }
        run.push_back(cur);
        cur = p;
    }
    for (auto it = run.rbegin(); it != run.rend(); ++it) {
        int x = linePitch(w, gi, it->owner, *it);
        if (x % 12 == prev % 12) x = redrawn(w, gi, *it, x, prev % 12);
        w.setHeard(gi, it->nominal, x);
        prev = x;
    }
    return prev;
}

void lineNotes(World& w, std::size_t gi, int vi, std::int64_t from, std::int64_t to, std::vector<Note>& out, bool heedMute = true) {
    auto owned = [&](const Step& s) { return s.valid && s.hit && (s.owner == vi || (s.owner == kTutti && w.play(s.bar, gi).member(vi))); };
    auto gateOf = [&](const Step& s) { return w.play(s.bar, gi).gate(s.slot); };
    auto voice = [&](const Step& s) -> const Voice& { return w.play(s.bar, gi).group().voices[vi]; };
    auto repeats = [&](const Step& s) { return w.play(s.bar, gi).repeats(s.bar, s.absStep); };
    // §4.3: adjacent gate-3 hits on one pitch merge, when the voice ties. A ratchet's repeats never do (§4.6).
    auto ties = [&](const Step& a, const Step& b) {
        return voice(a).tie && owned(b) && gateOf(a) == 3 && gateOf(b) == 3 && heardPitch(w, gi, vi, a) == heardPitch(w, gi, vi, b) &&
               repeats(a) == 1 && repeats(b) == 1;
    };

    for (Step s = w.stepFrom(gi, from); s.nominal < to; s = w.after(gi, s)) {
        if (!owned(s)) continue;
        const GroupPlay& p = w.play(s.bar, gi);
        if (heedMute && (p.group().mute || voice(s).mute)) continue;
        if (const int n = repeats(s); n > 1) {
            const Resolved rr = resolve(p.song(), p.group(), voice(s));
            const int v = p.accent(s.slot) || s.land ? accentVelocity(rr.accent) : normalVelocity(rr.accent);
            const int pitch = heardPitch(w, gi, vi, s);
            // A ratchet fills its step already: its repeats move in velocity (all by the step's one draw), not in time.
            for (const Repeat& r : ratchetOf(s, p.stepTicks(), n, rr.gatePercent, v, p.group().ramp))
                out.push_back({r.start, r.end, voice(s).channel, pitch, humanVelocity(p.song(), voice(s), s.nominal, pitch, r.velocity), voice(s).id});
            continue;
        }
        const Step prev = w.before(gi, s);
        if (owned(prev) && ties(prev, s)) continue;  // the previous note carries this one
        Step last = s;
        for (Step n = w.after(gi, last); ties(last, n); n = w.after(gi, last)) last = n;
        // The last hit's length, and whether it overlaps the next on purpose (legato).
        const GroupPlay& lp = w.play(last.bar, gi);
        const Resolved r = resolve(lp.song(), lp.group(), voice(last));
        const int gate = gateOf(last);
        const Step next = w.after(gi, last);
        std::int64_t end = last.tick + gateTicks(lp.stepTicks(), gate, r.gatePercent);
        bool legato = false;
        if (gate == 3 && r.gatePercent >= 100 && owned(next)) {
            legato = true;
            end = last.tick + (heardPitch(w, gi, vi, next) == heardPitch(w, gi, vi, last) ? lp.stepTicks() * 99 / 100
                                                                                           : lp.stepTicks() * 11 / 10);
        }
        // Mono (§11): the voice's own next note ends this one, unless legato overlaps it on purpose.
        // Cut (§5.3): any other member's next note ends it too.
        // The search looks a step past the end: humanize (§4.7) can move the end that far, and must stop at the hit.
        // Off, a hit past the end changes nothing (it can't be before the end).
        std::int64_t cut = -1;
        for (Step n = next; n.valid && n.nominal < end + lp.stepTicks(); n = w.after(gi, n)) {
            if (!n.hit || n.owner == -1) continue;
            const bool mine = owned(n);
            if (mine && legato && n.nominal == next.nominal) break;
            if (mine || w.play(n.bar, gi).group().overlap == Overlap::Cut) {
                cut = std::max(n.tick, s.tick + 1);
                break;
            }
        }
        // §4.7: the note sounds late by its step's nudge, and ends late by the last tied step's (a legato note by
        // the next note's, so it still overlaps it); a note cut by a hit still ends at that hit.
        const std::int64_t start = s.tick + humanNudge(p.song(), voice(s), s, p.stepTicks());
        end += legato ? humanNudge(w.play(next.bar, gi).song(), voice(next), next, w.play(next.bar, gi).stepTicks())
                      : humanNudge(lp.song(), voice(last), last, lp.stepTicks());
        if (cut >= 0) end = std::min(end, cut);
        // §5.5: at a handoff under Alternate or Bells, the note overlaps the next member's by Handoff % of a step.
        if (lp.partitioned() && next.valid && next.hit && next.owner >= 0 && next.owner != vi)
            end = std::max(end, next.tick + lp.stepTicks() * clampi(lp.group().handoff, 0, 50) / 100);
        end = std::max(end, start + 1);
        const Resolved rs = resolve(p.song(), p.group(), voice(s));
        const int vel = p.accent(s.slot) || s.land ? accentVelocity(rs.accent) : normalVelocity(rs.accent);  // a tihai lands accented
        const int pitch = heardPitch(w, gi, vi, s);
        out.push_back({start, end, voice(s).channel, pitch, humanVelocity(p.song(), voice(s), s.nominal, pitch, vel), voice(s).id});
    }
}

// ---- Resultant (§16.6) ------------------------------------------------------------------------------------

// A group's composite: every note its Lead and Bass voices play, mutes aside. A group on a live Resultant plays its
// own source's band, so a chain of them reads through.
void resultantNotes(World& w, std::size_t gi, std::int64_t from, std::int64_t to, std::vector<Note>& out, bool heedMute);
void composite(World& w, std::size_t gi, std::int64_t from, std::int64_t to, std::vector<Note>& out) {
    const World::Hop hop(w);
    if (w.tooDeep()) return;  // the backstop: nothing
    const Song& song = w.state(w.barOf(std::max<std::int64_t>(0, from))).song;
    if (resultantSource(song, gi)) return resultantNotes(w, gi, from, to, out, false);
    for (int vi = 0; vi < static_cast<int>(song.groups[gi].voices.size()); ++vi)
        if (song.groups[gi].voices[static_cast<std::size_t>(vi)].type != VoiceType::Chord) lineNotes(w, gi, vi, from, to, out, false);
}

// The source's composite inside the band [Low, High], on each of this group's Lead and Bass voices, moved by its
// transpose: the inherent pattern, doubled.
void resultantNotes(World& w, std::size_t gi, std::int64_t from, std::int64_t to, std::vector<Note>& out, bool heedMute) {
    const auto src = resultantSource(w.state(w.barOf(std::max<std::int64_t>(0, from))).song, gi);
    if (!src) return;
    std::vector<Note> c;
    composite(w, *src, from, to, c);
    std::stable_sort(c.begin(), c.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    for (const Note& n : c) {
        const State& st = w.state(w.barOf(n.start));
        const Group& g = st.song.groups[gi];
        const int lo = paramValue(SlotKind::Rhythm, g.rhythmGen, "resultant", "low"), hi = paramValue(SlotKind::Rhythm, g.rhythmGen, "resultant", "high");
        if (resultantNote(n.note, lo, hi, 0) < 0 || (heedMute && g.mute)) continue;
        const bool accented = paramValue(SlotKind::Rhythm, g.rhythmGen, "resultant", "velocity") == 1;
        for (const Voice& r : g.voices) {
            if (r.type == VoiceType::Chord || (heedMute && r.mute)) continue;
            const int note = resultantNote(n.note, lo, hi, r.octaveTranspose);
            const int vel = accented ? accentVelocity(resolve(st.song, g, r).accent) : n.velocity;
            out.push_back({n.start, n.end, r.channel, note, humanVelocity(st.song, r, n.start, note, vel), r.id});
        }
    }
}

// ---- Chord ------------------------------------------------------------------------------------------------

// §3.4a: a Chord voice's notes at a tick. In a fixed Voicing, the chord as §3.4 builds it. On "nearest", the chain
// from the start of the pass through the progression: the pass's first chord in the fixed Voicing, then each change
// the inversion closest to the one before.
std::vector<int> voicedAt(World& w, std::size_t gi, int vi, std::int64_t tick) {
    auto fixedAt = [&](std::int64_t t) {
        const std::int64_t bar = w.barOf(t);
        const auto [root, major] = w.chordAt(t);
        return chordNotes(w.state(bar).song, w.play(bar, gi).group().voices[static_cast<std::size_t>(vi)], root, major);
    };
    tick = std::max<std::int64_t>(0, tick);
    const State& st = w.state(w.barOf(tick));
    const std::int64_t ct = changeTicks(st.song), cycle = ct * clampi(st.song.harmony.length, 1, 16);
    const std::int64_t t0 = st.harmonyAnchor + floorDiv(tick - st.harmonyAnchor, cycle) * cycle;
    std::vector<int> chord = fixedAt(t0);
    for (std::int64_t t = t0 + ct; t <= tick; t += ct) {
        const Voice& v = w.play(w.barOf(t), gi).group().voices[static_cast<std::size_t>(vi)];
        const std::vector<int> next = fixedAt(t);
        chord = v.nearest ? nearestInversion(chord, next, hash(v.pitchSeed, {"nearest", floorDiv(t - st.harmonyAnchor, ct)})) : next;
    }
    return chord;
}

void chordVoiceNotes(World& w, std::size_t gi, int vi, std::int64_t from, std::int64_t to, std::vector<Note>& out) {
    from = std::max<std::int64_t>(0, from);
    auto voiceAt = [&](std::int64_t bar) -> const Voice& { return w.play(bar, gi).group().voices[vi]; };
    auto accentOf = [&](std::int64_t bar) {
        const GroupPlay& p = w.play(bar, gi);
        return resolve(p.song(), p.group(), voiceAt(bar)).accent;
    };
    auto hitVelocity = [&](const Step& s) {
        return w.play(s.bar, gi).accent(s.slot) || s.land ? accentVelocity(accentOf(s.bar)) : normalVelocity(accentOf(s.bar));
    };
    auto owned = [&](const Step& s) { return s.valid && s.hit && (s.owner == vi || (s.owner == kTutti && w.play(s.bar, gi).member(vi))); };
    auto silent = [&](std::int64_t bar) { return w.play(bar, gi).group().mute || voiceAt(bar).mute; };

    // Pulse (v0.4): a chord on each hit the voice owns, for the hit's gate.
    for (Step s = w.stepFrom(gi, from); s.nominal < to; s = w.after(gi, s)) {
        if (!owned(s) || silent(s.bar) || voiceAt(s.bar).articulation != Articulation::Pulse) continue;
        const GroupPlay& p = w.play(s.bar, gi);
        const int gatePercent = resolve(p.song(), p.group(), voiceAt(s.bar)).gatePercent;
        const auto [root, major] = w.chordAt(s.tick);
        const std::vector<int> chord = voiceAt(s.bar).nearest ? voicedAt(w, gi, vi, s.tick) : chordNotes(p.song(), voiceAt(s.bar), root, major);
        if (const int n = p.repeats(s.bar, s.absStep); n > 1) {  // a ratcheted Pulse chord (§4.6)
            for (const Repeat& r : ratchetOf(s, p.stepTicks(), n, gatePercent, hitVelocity(s), p.group().ramp))
                for (int note : chord)
                    out.push_back({r.start, r.end, voiceAt(s.bar).channel, note, humanVelocity(p.song(), voiceAt(s.bar), s.nominal, note, r.velocity),
                                   voiceAt(s.bar).id});
            continue;
        }
        const std::int64_t len = gateTicks(p.stepTicks(), p.gate(s.slot), gatePercent);
        for (int n : chord)
            out.push_back({s.tick, s.tick + len, voiceAt(s.bar).channel, n, humanVelocity(p.song(), voiceAt(s.bar), s.nominal, n, hitVelocity(s)),
                           voiceAt(s.bar).id});
    }

    // Sustain and Restrike: the chord holds from change to change. At a change, notes both chords share carry on;
    // a restrike (a hit the voice owns), the start of each pass through the progression, and a re-anchored harmony
    // strike everything afresh. So the walk starts at the fresh point at or before `from` and stops at the first fresh
    // point at or after `to`, where every note started before `to` has ended.
    struct Boundary {
        std::int64_t tick;
        bool fresh;
        int velocity;
    };
    auto boundariesIn = [&](std::int64_t bar) {
        std::vector<Boundary> bs;
        const State& st = w.state(bar);
        const std::int64_t ct = changeTicks(st.song);
        const std::int64_t cycle = ct * clampi(st.song.harmony.length, 1, 16);
        const std::int64_t b0 = bar * w.barTicks(), b1 = (bar + 1) * w.barTicks();
        for (std::int64_t t = st.harmonyAnchor + ceilDiv(b0 - st.harmonyAnchor, ct) * ct; t < b1; t += ct)
            bs.push_back({t, floorMod(t - st.harmonyAnchor, cycle) == 0, normalVelocity(accentOf(bar))});
        const Articulation a = voiceAt(bar).articulation;
        if (a == Articulation::RestrikeHits || a == Articulation::RestrikeAccents)
            for (Step s = w.stepFrom(gi, b0); s.nominal < b1; s = w.after(gi, s))
                if (owned(s) && (a == Articulation::RestrikeHits || w.play(s.bar, gi).accent(s.slot)))
                    bs.push_back({s.tick, true, hitVelocity(s)});
        std::stable_sort(bs.begin(), bs.end(), [](const Boundary& x, const Boundary& y) { return x.tick < y.tick; });
        // A restrike on a change is one boundary: fresh, at the hit's velocity.
        std::vector<Boundary> merged;
        for (const Boundary& b : bs) {
            if (!merged.empty() && merged.back().tick == b.tick) {
                merged.back().fresh = merged.back().fresh || b.fresh;
                merged.back().velocity = b.velocity;
                continue;
            }
            merged.push_back(b);
        }
        return merged;
    };
    const State& st0 = w.state(w.barOf(from));
    const std::int64_t cycle0 = changeTicks(st0.song) * clampi(st0.song.harmony.length, 1, 16);
    const std::int64_t start = st0.harmonyAnchor + floorDiv(from - st0.harmonyAnchor, cycle0) * cycle0;

    std::map<int, std::pair<std::int64_t, int>> sounding;  // note → (start, velocity)
    int channel = voiceAt(w.barOf(start)).channel;
    std::uint64_t id = voiceAt(w.barOf(start)).id;
    auto close = [&](int n, std::int64_t at) {
        const auto [s0, vel] = sounding[n];
        if (s0 >= from && s0 < to && at > s0) out.push_back({s0, at, channel, n, vel, id});
        sounding.erase(n);
    };
    auto closeAll = [&](std::int64_t at) {
        std::vector<int> all;
        for (const auto& kv : sounding) all.push_back(kv.first);
        for (int n : all) close(n, at);
    };
    for (std::int64_t bar = w.barOf(start);; ++bar) {
        for (const Boundary& b : boundariesIn(bar)) {
            if (b.tick < start) continue;
            if (b.fresh && b.tick >= to) {
                closeAll(b.tick);
                return;
            }
            const Articulation a = voiceAt(bar).articulation;
            if (silent(bar) || a == Articulation::Pulse) {
                closeAll(b.tick);
                continue;
            }
            const auto [root, major] = w.chordAt(b.tick);
            const std::vector<int> chord =
                voiceAt(bar).nearest ? voicedAt(w, gi, vi, b.tick) : chordNotes(w.state(bar).song, voiceAt(bar), root, major);
            std::vector<int> ending;
            for (const auto& kv : sounding)
                if (b.fresh || !std::binary_search(chord.begin(), chord.end(), kv.first)) ending.push_back(kv.first);
            for (int n : ending) close(n, b.tick);
            channel = voiceAt(bar).channel;
            id = voiceAt(bar).id;
            for (int n : chord)
                if (!sounding.count(n)) sounding[n] = {b.tick, humanVelocity(w.state(bar).song, voiceAt(bar), b.tick, n, b.velocity)};
        }
        if ((bar + 1) * w.barTicks() >= to) {
            bool open = false;
            for (const auto& kv : sounding) open |= kv.second.first < to;
            if (!open) return;
            // A fresh point comes within one pass of the progression (64 bars at most); this is only a backstop.
            if (bar > w.barOf(to) + 64) {
                closeAll((bar + 1) * w.barTicks());
                return;
            }
        }
    }
}

}  // namespace

// ---- the pieces -------------------------------------------------------------------------------------------

std::vector<int> priorityOrder(std::uint64_t h) {
    std::vector<int> a(64);
    std::iota(a.begin(), a.end(), 0);
    Stream s(h);
    for (int i = 63; i >= 1; --i) std::swap(a[i], a[static_cast<std::size_t>(s.below(static_cast<std::uint64_t>(i) + 1))]);
    return a;
}

std::vector<int> priorityOrder(std::uint64_t h, int L) {
    std::vector<int> out;
    for (int x : priorityOrder(h))
        if (x < L) out.push_back(x);
    return out;
}

int derivedDepthHighWater(bool reset) {
    const int h = World::highWater();
    if (reset) World::highWater() = 0;
    return h;
}

int ladderPlace(int note, int root, int scale) {
    const std::uint16_t mask = kScales[static_cast<std::size_t>(clampi(scale, 0, static_cast<int>(kScales.size()) - 1))].mask;
    int K = 0;
    for (int i = 0; i < 12; ++i) K += (mask >> i) & 1;
    const std::int64_t d = note - root;
    const int pc = static_cast<int>(floorMod(d, 12));
    int up = 0;
    for (int i = 0; i <= pc; ++i) up += (mask >> i) & 1;
    return static_cast<int>(floorDiv(d, 12) * K + up - 1);
}

std::vector<int> ranked(int r, int root, int scale) {
    const std::uint16_t mask = kScales[clampi(scale, 0, static_cast<int>(kScales.size()) - 1)].mask;
    std::vector<int> out;
    for (int i : kPref) {
        const int pc = (r + i) % 12;
        if (mask & (1u << (((pc - root) % 12 + 12) % 12))) out.push_back(pc);
    }
    return out;
}

namespace {
bool inScale(int pc, int root, int scale) {
    const std::uint16_t mask = kScales[clampi(scale, 0, static_cast<int>(kScales.size()) - 1)].mask;
    return mask & (1u << floorMod(pc - root, 12));
}

// Moves some entries of a list toward a place, `colour`/16 of the way, keeping the rest in order. `where` gives an
// entry's target place and its class at a tie (0 moved forward, 1 staying, 2 moved back).
template <typename F>
std::vector<int> pull(const std::vector<int>& list, int colour, F where) {
    struct Key {
        int pos, cls, i;
    };
    std::vector<Key> keys;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const auto [target, cls] = where(i, list[static_cast<std::size_t>(i)]);
        keys.push_back({i + (target - i) * clampi(colour, 0, 16) / 16, cls, i});
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return std::tie(a.pos, a.cls, a.i) < std::tie(b.pos, b.cls, b.i); });
    std::vector<int> out;
    for (const Key& k : keys) out.push_back(list[static_cast<std::size_t>(k.i)]);
    return out;
}
}  // namespace

std::vector<int> colourNotes(int root, int scale) {
    int notes = 0;
    for (int i = 0; i < 12; ++i) notes += inScale(root + i, root, scale);
    if (notes < 7) return {};  // a pentatonic, a hexatonic (whole tone, blues): a colour of its own, not a mode's
    static const int ionian[] = {0, 2, 4, 5, 7, 9, 11}, aeolian[] = {0, 2, 3, 5, 7, 8, 10};
    std::vector<int> overMajor, overMinor;
    for (int i = 0; i < 12; ++i) {
        if (!inScale(root + i, root, scale)) continue;
        if (std::find(std::begin(ionian), std::end(ionian), i) == std::end(ionian)) overMajor.push_back(i);
        if (std::find(std::begin(aeolian), std::end(aeolian), i) == std::end(aeolian)) overMinor.push_back(i);
    }
    const bool major = overMajor.size() != overMinor.size() ? overMajor.size() < overMinor.size() : inScale(root + 4, root, scale);
    std::vector<int> out = major ? overMajor : overMinor;
    if (out.empty() || out.size() > 2) return {};  // the scale is major or minor, or too far from both
    for (int& i : out) i = (root + i) % 12;
    return out;
}

namespace {
// §3.7: a mode of the major scale has a parent major key (F Phrygian's is D♭); −1 for a scale with no colour notes.
int parentOf(const Song& song) {
    if (colourNotes(song.root, song.scale).empty()) return -1;
    static const int ionian[] = {0, 2, 4, 5, 7, 9, 11};
    for (int t = 0; t < 12; ++t) {
        bool same = true;
        for (int pc = 0; pc < 12; ++pc) {
            const bool inMajor = std::find(std::begin(ionian), std::end(ionian), floorMod(pc - t, 12)) != std::end(ionian);
            same = same && inMajor == inScale(pc, song.root, song.scale);
        }
        if (same && t != song.root) return t;
    }
    return -1;
}

bool movesOn(const Song& song) { return song.modeMoves != 0 && song.colour > 0; }

// §3.8: a Lead or Bass voice on Pedal holds the tonic under the harmony.
bool tonicPedal(const Song& song) {
    for (const Group& g : song.groups)
        for (const Voice& v : g.voices)
            if (v.type != VoiceType::Chord && onTonic(v.notesGen)) return true;
    return false;
}

// §3.8: with Mode moves on and no tonic pedal, a chord whose root has no perfect fifth (not the tonic's) is built in
// fourths: each note the scale's fourth above the last (perfect, else augmented, else the third below it).
bool quartal(const Song& song, int r) {
    return movesOn(song) && floorMod(r - song.root, 12) != 0 && !inScale(r + 7, song.root, song.scale) && !tonicPedal(song);
}
std::vector<int> fourths(const Song& song, int r) {
    const int r0 = static_cast<int>(floorMod(r, 12));
    std::vector<int> out{r0};
    for (int x = r0;;) {
        int next = -1;
        for (int step : {5, 6, 4})
            if (inScale(x + step, song.root, song.scale)) {
                next = (x + step) % 12;
                break;
            }
        if (next < 0 || std::find(out.begin(), out.end(), next) != out.end()) break;
        out.push_back(next);
        x = next;
    }
    return out;
}
}  // namespace

std::vector<int> chordRanked(const Song& song, int r) {
    if (quartal(song, r)) return fourths(song, r);
    std::vector<int> rk = ranked(r, song.root, song.scale);
    if (song.colour <= 0 || inScale(r + 7, song.root, song.scale)) return rk;
    // No fifth: the chord's fifth is the one the scale has, ♭5 on a minor third (diminished), else ♯5 (augmented).
    const bool minor = inScale(r + 3, song.root, song.scale) && !inScale(r + 4, song.root, song.scale);
    // Two named arrays, not a conditional of two braced lists: a list's array lives only to the end of the expression
    // that makes it, so the loop would read it after it had gone (GCC at -O2 did, and ranked the chord wrongly).
    static constexpr int kMinorFirst[2] = {6, 8}, kMajorFirst[2] = {8, 6};
    const int* fifths = minor ? kMinorFirst : kMajorFirst;
    for (int k = 0; k < 2; ++k) {
        const int f = fifths[k];
        const auto it = std::find(rk.begin(), rk.end(), (r + f) % 12);
        if (it == rk.end()) continue;
        const int pc = *it;
        rk.erase(it);
        rk.insert(rk.begin() + std::min<std::ptrdiff_t>(1, static_cast<std::ptrdiff_t>(rk.size())), pc);
        break;
    }
    return rk;
}

namespace {
// Whether a root's chord (§3.4's, three notes) holds one of the mode's colour notes.
bool colourChord(const Song& song, int x, const std::vector<int>& colour) {
    const std::vector<int> triad = chordRanked(song, x);
    for (std::size_t k = 0; k < std::min<std::size_t>(3, triad.size()); ++k)
        if (std::find(colour.begin(), colour.end(), triad[k]) != colour.end()) return true;
    return false;
}
}  // namespace

std::vector<int> moveChords(const Song& song) {
    std::vector<int> out;
    if (!movesOn(song)) return out;
    const std::vector<int> colour = colourNotes(song.root, song.scale);
    const int parent = parentOf(song);
    for (int x : ranked(song.root, song.root, song.scale)) {
        if (x == song.root || x == parent) continue;
        const bool major = inScale(x + 4, song.root, song.scale) && inScale(x + 7, song.root, song.scale);
        if (major && colourChord(song, x, colour)) out.push_back(x);
    }
    return out;
}

std::vector<int> harmonyRanked(const Song& song) {
    const std::vector<int> base = ranked(song.root, song.root, song.scale);
    if (song.colour <= 0) return base;
    const std::vector<int> colour = colourNotes(song.root, song.scale);
    if (movesOn(song)) {
        // §3.8: the tonic, then the mode's move chords, then the rest with the colour chords pulled forward. Nothing
        // is sent to the back: the weights (moveWeight) decide what a chord may follow.
        const std::vector<int> moves = moveChords(song);
        std::vector<int> rest;
        for (int x : base)
            if (x != song.root && std::find(moves.begin(), moves.end(), x) == moves.end()) rest.push_back(x);
        std::vector<int> out{song.root};
        out.insert(out.end(), moves.begin(), moves.end());
        for (int x : pull(rest, song.colour, [&](int i, int x) -> std::pair<int, int> {
                 return colourChord(song, x, colour) ? std::pair<int, int>{0, 0} : std::pair<int, int>{i, 1};
             }))
            out.push_back(x);
        return out;
    }
    const int last = static_cast<int>(base.size()) - 1;
    // A mode of the major scale has a parent major key (F Phrygian's is D♭). Its tonic chord pulls the music into
    // that key, so a mode keeps it at the back (modal practice avoids the cadence into the relative major).
    const int parent = parentOf(song);
    return pull(base, song.colour, [&](int i, int x) -> std::pair<int, int> {
        if (i == 0) return {0, 1};  // the tonic stays home
        if (x == parent) return {last, 2};
        if (!inScale(x + 7, song.root, song.scale)) return {last, 2};  // a chord with no fifth goes to the back
        if (colourChord(song, x, colour)) return {1, 0};  // right after the tonic
        return {i, 1};
    });
}

int moveWeight(const Song& song, int prev, int root, bool nextIsTonic) {
    const int c = clampi(song.colour, 0, 16);
    const int parent = parentOf(song);
    // The stacked-thirds V–I into the relative major: the one move that takes the music out of the mode.
    if (parent >= 0 && floorMod(root, 12) == parent && floorMod(prev, 12) == (parent + 7) % 12) return 1;
    int w = 4;
    if (colourChord(song, root, colourNotes(song.root, song.scale))) w += 4 * c / 16;
    const std::vector<int> moves = moveChords(song);
    const bool nearHome = floorMod(prev - song.root, 12) == 0 || nextIsTonic;
    if (nearHome && std::find(moves.begin(), moves.end(), static_cast<int>(floorMod(root, 12))) != moves.end()) w += 8 * c / 16;
    return w;
}

std::vector<bool> homeMajor(const Song& song, const std::vector<int>& prog) {
    std::vector<bool> out(prog.size(), false);
    // Phrygian's home chord may turn major after ♭II (the Andalusian cadence's end, Phrygian dominant): a mode whose
    // colour is the ♭2, on a minor tonic with a perfect fifth.
    if (!movesOn(song) || prog.empty()) return out;
    const std::vector<int> colour = colourNotes(song.root, song.scale);
    const int flat2 = (song.root + 1) % 12;
    if (std::find(colour.begin(), colour.end(), flat2) == colour.end() || !inScale(song.root + 3, song.root, song.scale) ||
        inScale(song.root + 4, song.root, song.scale) || !inScale(song.root + 7, song.root, song.scale))
        return out;
    const std::vector<int> rk = harmonyRanked(song);
    const int L = static_cast<int>(prog.size());
    const int chance = 75 * clampi(song.colour, 0, 16) / 16;
    for (int k = 0; k < L; ++k) {
        const int before = prog[static_cast<std::size_t>(floorMod(k - 1, L))];
        if (prog[static_cast<std::size_t>(k)] != 0 || before < 0 || before >= static_cast<int>(rk.size())) continue;
        if (rk[static_cast<std::size_t>(before)] == flat2 && roll(hash(song.harmony.seed, {"home", k})) < chance) out[static_cast<std::size_t>(k)] = true;
    }
    return out;
}

std::vector<int> lineRanked(const Song& song, int r) {
    const std::vector<int> base = ranked(r, song.root, song.scale);
    if (song.colour <= 0) return base;
    const std::vector<int> colour = colourNotes(song.root, song.scale);
    if (colour.empty()) return base;
    return pull(base, song.colour, [&](int i, int x) -> std::pair<int, int> {
        if (i > 3 && std::find(colour.begin(), colour.end(), x) != colour.end()) return {3, 0};  // just after the chord tones
        return {i, 1};
    });
}

int poolSize(int n, int range) {
    return clampi(static_cast<int>(rdiv(static_cast<std::int64_t>(n - 1) * (clampi(range, 1, 16) - 1), 15)) + 1, 1, n);
}

std::vector<int> noteIndices(std::uint64_t h, int L, int ps, int wander) {
    wander = clampi(wander, 1, 16);
    const int roots = ps == 1 ? L : std::max(1, static_cast<int>(rdiv(static_cast<std::int64_t>(L) * (17 - wander), 16)));
    const int m = ps - 1;
    const std::vector<int> P = priorityOrder(h, L);
    std::vector<int> idx(L, 0);
    for (int j = 0; j < L; ++j) idx[P[j]] = j < roots ? 0 : 1 + (L - 1 - j) % m;
    return idx;
}

std::vector<bool> hits(std::uint64_t rhythmSeed, int L, int density) {
    const int n = std::max(1, static_cast<int>(rdiv(static_cast<std::int64_t>(L) * clampi(density, 1, 16), 16)));
    const std::vector<int> P = priorityOrder(hash(rhythmSeed, {"hits"}), L);
    std::vector<bool> out(L, false);
    for (int j = 0; j < n && j < L; ++j) out[P[j]] = true;
    return out;
}

bool accentAt(std::uint64_t rhythmSeed, int slot) { return roll(hash(rhythmSeed, {"accent", slot})) < 50; }

int gateAt(std::uint64_t rhythmSeed, int slot, int density) {
    const int d = clampi(density, 1, 16);
    const int a = roll(hash(rhythmSeed, {"gate", slot, "a"})), b = roll(hash(rhythmSeed, {"gate", slot, "b"}));
    const int c = 5 + 3 * (d - 1);
    return a < kLong[d - 1] ? (b < c ? 3 : 2) : (b < c ? 2 : 1);
}

int octaveLaneAt(std::uint64_t pitchSeed, int slot, int density, int jumps) {
    const int d = clampi(density, 1, 16);
    const int a = roll(hash(pitchSeed, {"octave", slot, "a"})), b = roll(hash(pitchSeed, {"octave", slot, "b"}));
    const int oct = (10 + (20 * (d - 1)) / 15) * clampi(jumps, 0, 16) / 16;  // Octave jumps scales the chance (nested)
    return a >= oct ? 0 : b < 50 ? 1 : b < 85 ? -1 : 2;
}

int slotAt(std::int64_t absStep, int steps, Direction direction, int offset) {
    const std::int64_t L = std::max(1, steps);
    std::int64_t idx = 0;
    switch (direction) {
        case Direction::Forward: idx = floorMod(absStep, L); break;
        case Direction::Backward: idx = (L - 1) - floorMod(absStep, L); break;
        case Direction::Pendulum:
            if (L <= 1) {
                idx = 0;
            } else {
                const std::int64_t p = 2 * L - 2, r = floorMod(absStep, p);
                idx = r < L ? r : p - r;
            }
            break;
    }
    return static_cast<int>((idx + offset) % L);
}

std::int64_t barTicks(const Song& song) {
    const int unit = song.beatUnit == 2 || song.beatUnit == 8 || song.beatUnit == 16 ? song.beatUnit : 4;
    return static_cast<std::int64_t>(kTicksPerBeat) * 4 / unit * clampi(song.barLength, 1, 16);
}

std::int64_t changeTicks(const Song& song) {
    return barTicks(song) * kChangeQuarters[clampi(song.harmony.changesEvery, 0, 4)] / 4;
}

std::vector<int> progression(const Song& song) { return detail::progressionOf(song, {}); }

int chordRootAt(const Song& song, std::int64_t tick) { return World(song).chordRoot(tick); }

Song stateAt(const Song& song, std::int64_t bar) {
    if (hasForm(song)) {  // §18.1: the section's letter, at the section's own bar
        const Section sec = sectionAt(song, bar);
        return stateAt(letterSong(song, sec.entry, sec.position), std::max<std::int64_t>(0, bar) - sec.start);
    }
    Song out = detail::Replay(song).at(bar).song;
    out.mod = song.mod;  // the replay's states leave it out; the song as it plays still has it
    return out;
}

std::int64_t stepCount(const Song& song, std::size_t group, std::int64_t tick) {
    if (group >= song.groups.size()) return -1;
    World w(song);
    return w.stepFrom(group, tick).t;
}

std::vector<int> chordNotes(const Song& song, const Voice& v, int r, bool homeMajor) {
    std::vector<int> rk = chordRanked(song, r);
    // §3.8: a major home chord takes the major third for the minor one.
    if (homeMajor && floorMod(r - song.root, 12) == 0)
        for (int& pc : rk)
            if (pc == (song.root + 3) % 12) pc = (song.root + 4) % 12;
    const int size = clampi(v.chordSize, 1, std::min(6, static_cast<int>(rk.size())));
    const int lo = v.overrides.octaveLow ? *v.overrides.octaveLow : song.octaveLow;
    const int base = 36 + 12 * (lo + 1) + 12 * v.octaveTranspose;
    std::vector<int> n;
    if (quartal(song, r)) {
        // A chord in fourths is stacked, not folded into one octave: each note a fourth above the last (§3.8).
        n.push_back(base + rk[0]);
        for (int i = 1; i < size; ++i) n.push_back(n.back() + static_cast<int>(floorMod(rk[static_cast<std::size_t>(i)] - rk[static_cast<std::size_t>(i - 1)], 12)));
    } else {
        for (int i = 0; i < size; ++i) n.push_back(base + rk[i]);
    }
    std::sort(n.begin(), n.end());
    const int voicing = clampi(v.voicing, 1, 16);
    switch ((voicing - 1) % 8 + 1) {
        case 1: break;
        case 2: if (n.size() >= 2) n[0] += 12; break;
        case 3: if (n.size() >= 3) n[0] += 12, n[1] += 12; break;
        case 4: n.push_back(n.back() + 12); break;
        case 5: n.push_back(n[0] - 12); break;
        case 6: if (n.size() >= 3) { n[1] += 12; n.push_back(n[0] + 12); } break;
        case 7: { const int first = n[0]; for (int& x : n) x += 12; n.push_back(first); } break;
        case 8: n.push_back(n.back() + 7); break;
    }
    std::sort(n.begin(), n.end());
    if (voicing > 8 && n.size() > 2) n.erase(n.begin() + 1);
    for (int& x : n) x = clampi(x, 0, 127);
    std::sort(n.begin(), n.end());
    n.erase(std::unique(n.begin(), n.end()), n.end());
    return n;
}

std::vector<int> nearestInversion(const std::vector<int>& prev, const std::vector<int>& chord, std::uint64_t h) {
    if (prev.empty() || chord.size() < 2) return chord;
    // The inversions, lowest first: the top note down an octave, n − 1 times, and the bottom note up, n − 1 times.
    const std::size_t n = chord.size();
    std::vector<std::vector<int>> cands;
    std::vector<int> c = chord;
    for (std::size_t i = 1; i < n; ++i) {
        c.back() -= 12;
        std::sort(c.begin(), c.end());
        cands.insert(cands.begin(), c);
    }
    cands.push_back(chord);
    c = chord;
    for (std::size_t i = 1; i < n; ++i) {
        c.front() += 12;
        std::sort(c.begin(), c.end());
        cands.push_back(c);
    }
    // How far the voices move: note for note when the sizes match, else each note to its nearest in the other chord.
    auto distance = [&](const std::vector<int>& x) {
        std::int64_t d = 0;
        if (x.size() == prev.size()) {
            for (std::size_t k = 0; k < x.size(); ++k) d += std::abs(x[k] - prev[k]);
            return d;
        }
        auto nearest = [](int a, const std::vector<int>& in) {
            int best = 1 << 20;
            for (int b : in) best = std::min(best, std::abs(a - b));
            return best;
        };
        for (int a : x) d += nearest(a, prev);
        for (int b : prev) d += nearest(b, x);
        return d;
    };
    std::int64_t best = -1;
    std::vector<std::size_t> tied;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (cands[i].front() < 0 || cands[i].back() > 127) continue;
        const std::int64_t d = distance(cands[i]);
        if (best < 0 || d < best) best = d, tied.clear();
        if (d == best) tied.push_back(i);
    }
    if (tied.empty()) return chord;
    // A tie is the seed's: drawn, so it's the same every time.
    const std::size_t pick = tied.size() == 1 ? 0 : static_cast<std::size_t>(Stream(h).below(tied.size()));
    return cands[tied[pick]];
}

// ---- notes and events -------------------------------------------------------------------------------------

namespace {

bool noteOrder(const Note& a, const Note& b) {
    return std::tie(a.start, a.channel, a.note, a.end) < std::tie(b.start, b.channel, b.note, b.end);
}

std::vector<Note> collect(World& w, const Song& song, std::int64_t from, std::int64_t to) {
    std::vector<Note> out;
    for (std::size_t gi = 0; gi < song.groups.size(); ++gi) {
        const bool resultant = resultantSource(song, gi).has_value();  // §16.6: its lines play the source's band
        if (resultant) resultantNotes(w, gi, from, to, out, true);
        for (int vi = 0; vi < static_cast<int>(song.groups[gi].voices.size()); ++vi) {
            if (song.groups[gi].voices[vi].type == VoiceType::Chord) chordVoiceNotes(w, gi, vi, from, to, out);
            else if (!resultant) lineNotes(w, gi, vi, from, to, out);
        }
    }
    std::stable_sort(out.begin(), out.end(), noteOrder);
    return out;
}

// The chords that are ringing at a tick (started before it, ending after): what a jump to there should sound.
std::vector<Note> ringingAt(World& w, const Song& song, std::int64_t tick) {
    std::vector<Note> out;
    if (tick <= 0) return out;
    const State& st = w.state(w.barOf(tick));
    const std::int64_t lookback = changeTicks(st.song) * clampi(st.song.harmony.length, 1, 16) + w.barTicks();
    for (std::size_t gi = 0; gi < song.groups.size(); ++gi)
        for (int vi = 0; vi < static_cast<int>(song.groups[gi].voices.size()); ++vi) {
            if (song.groups[gi].voices[vi].type != VoiceType::Chord) continue;
            std::vector<Note> before;
            chordVoiceNotes(w, gi, vi, std::max<std::int64_t>(0, tick - lookback), tick, before);
            for (const Note& n : before)
                if (n.end > tick) out.push_back(n);
        }
    return out;
}

// The song over time (§18.1). Without a form, one World. Under a form, each section plays its letter's song from that
// song's own bar 0, cut at the section's end: a World per letter (a primed one per place in the string), shared by
// every section that plays it, so a letter that returns is the same notes again. Freeze (§9.2) holds the section
// it holds in: past that section's end it plays on, until Freeze lets go.
class Track {
public:
    Track(const Song& song, std::int64_t hold) : song_(song), hold_(hold >= 1 ? hold : -1), form_(hasForm(song)), bt_(hocket::barTicks(song)) {
        if (!form_) plain_ = std::make_unique<World>(song_, hold_);
    }

    struct Span {
        World* world = nullptr;
        const Song* song = nullptr;
        std::int64_t start = 0, end = -1;  // bars; −1: no end
        Section section;
    };

    std::int64_t barTicks() const { return bt_; }
    std::int64_t barOf(std::int64_t tick) const { return floorDiv(tick, bt_); }
    bool form() const { return form_; }
    const Song& song() const { return song_; }

    // The span a bar plays in. `held`: as Freeze has it (or the form's own, for "Next: bar N").
    Span at(std::int64_t bar, bool held = true) {
        bar = std::max<std::int64_t>(0, bar);
        Span sp;
        if (!form_) {
            sp.world = held ? plain_.get() : &unheld();
            sp.song = &song_;
            return sp;
        }
        const bool frozen = held && hold_ >= 1 && bar >= hold_;
        const Section sec = sectionAt(song_, frozen ? hold_ - 1 : bar);
        Letter& l = letter(sec, frozen ? hold_ - sec.start : -1);
        sp.world = l.world.get();
        sp.song = &l.song;
        sp.start = sec.start;
        sp.end = frozen || sec.bars < 0 ? -1 : sec.start + sec.bars;
        sp.section = sec;
        return sp;
    }

    std::vector<Note> collect(std::int64_t from, std::int64_t to) {
        if (!form_) return hocket::collect(*plain_, song_, from, to);
        std::vector<Note> out;
        for (std::int64_t t = std::max<std::int64_t>(0, from); t < to;) {
            const Span sp = at(barOf(t));
            const std::int64_t s0 = sp.start * bt_, e = sp.end < 0 ? std::numeric_limits<std::int64_t>::max() : sp.end * bt_;
            const std::int64_t upto = std::min(to, e);
            for (Note n : hocket::collect(*sp.world, *sp.song, t - s0, upto - s0)) {
                n.start += s0, n.end = e - s0 <= n.end ? e : n.end + s0;
                if (n.start < e) out.push_back(n);  // a section's notes end by its end; one swung past it is dropped
            }
            t = upto;
        }
        std::stable_sort(out.begin(), out.end(), noteOrder);
        return out;
    }

    std::vector<Note> ringing(std::int64_t tick) {
        if (!form_) return ringingAt(*plain_, song_, tick);
        const Span sp = at(barOf(tick));
        const std::int64_t s0 = sp.start * bt_;
        std::vector<Note> out;
        if (tick <= s0) return out;  // a section starts afresh: nothing rings into it
        for (Note n : ringingAt(*sp.world, *sp.song, tick - s0)) {
            n.start += s0, n.end += s0;
            if (sp.end >= 0) n.end = std::min(n.end, sp.end * bt_);
            if (n.end > tick) out.push_back(n);
        }
        return out;
    }

    void forget(std::int64_t bar) {
        if (!form_) return plain_->forget(bar);
        const Span sp = at(std::max<std::int64_t>(0, bar + 70));
        sp.world->forget(bar - sp.start);
    }

private:
    struct Letter {
        Song song;
        std::unique_ptr<World> world;
    };
    Letter& letter(const Section& sec, std::int64_t hold) {
        const std::int64_t pos = sec.entry.primed ? sec.position : -1;
        auto& slot = letters_[std::make_tuple(sec.entry.letter, pos, hold)];
        if (!slot) {
            slot = std::make_unique<Letter>();
            slot->song = letterSong(song_, sec.entry, sec.position);
            slot->world = std::make_unique<World>(slot->song, hold);
        }
        return *slot;
    }
    World& unheld() {
        if (hold_ < 1) return *plain_;
        if (!replay_) replay_ = std::make_unique<World>(song_);
        return *replay_;
    }

    const Song& song_;
    std::int64_t hold_;
    bool form_;
    std::int64_t bt_;
    std::unique_ptr<World> plain_, replay_;
    std::map<std::tuple<char, std::int64_t, std::int64_t>, std::unique_ptr<Letter>> letters_;
};

}  // namespace

std::vector<Note> notes(const Song& song, std::int64_t from, std::int64_t to) {
    Track t(song, -1);
    return t.collect(from, to);
}

std::vector<Note> notes(const Song& song, std::int64_t from, std::int64_t to, std::int64_t hold) {
    Track t(song, hold);
    return t.collect(from, to);
}

struct Player::Impl {
    explicit Impl(const Song& s) : song(std::make_unique<Song>(s)), world(std::make_unique<Track>(*song, -1)) {}
    std::unique_ptr<Song> song;  // the track's replay holds on to it
    std::unique_ptr<Track> world;
    bool playing = false;
    std::int64_t next = 0;
    // Freeze (§9, §17.6): asked for, the bar it holds from (−1: not holding), and whether a jump let go of it.
    bool freeze = false;
    std::int64_t hold = -1;
    bool released = false;

    void rebuild() { world = std::make_unique<Track>(*song, hold); }
    // A jump, a Stop: Freeze lets go, and is switched off (§2.1).
    void release() {
        if (freeze || hold >= 0) released = true;
        freeze = false;
        if (hold >= 0) hold = -1, rebuild();
    }
    // What's sounding: (channel, note) → its end, and an id so a newer note's off isn't taken for an older one's.
    struct Sounding {
        std::int64_t end;
        std::uint64_t serial;
    };
    std::map<std::pair<int, int>, Sounding> sounding;
    std::uint64_t serial = 0;
    // Notes whose step was in an earlier block but whose swing puts them in a later one.
    std::vector<Note> later;

    void offsBefore(std::int64_t to, std::vector<Event>& out) {
        std::vector<std::pair<std::int64_t, std::pair<int, int>>> due;
        for (const auto& [key, s] : sounding)
            if (s.end < to) due.push_back({s.end, key});
        std::sort(due.begin(), due.end());
        for (const auto& [t, key] : due) {
            out.push_back({t, Event::Off, key.first, key.second, 0});
            sounding.erase(key);
        }
    }
};

Player::Player(const Song& song) : impl_(std::make_unique<Impl>(song)) {}
Player::~Player() = default;

void Player::setFreeze(bool on) { impl_->freeze = on; }
bool Player::frozen() const { return impl_->hold >= 0; }
std::int64_t Player::holdFrom() const { return impl_->hold; }
bool Player::takeReleased() {
    const bool r = impl_->released;
    impl_->released = false;
    return r;
}

void Player::setSong(const Song& song) {
    // The new world first, then the song it reads: the old pair goes together. A Freeze holds on in the new song.
    auto next = std::make_unique<Song>(song);
    auto world = std::make_unique<Track>(*next, impl_->hold);
    impl_->world = std::move(world);
    impl_->song = std::move(next);
    impl_->later.clear();  // swung notes of the old song's last steps: the new song decides from here
}

std::vector<Event> Player::allOff(std::int64_t at) {
    std::vector<Event> out;
    for (const auto& [key, s] : impl_->sounding) out.push_back({at, Event::Off, key.first, key.second, 0});
    impl_->sounding.clear();
    return out;
}

std::vector<Event> Player::process(std::int64_t from, std::int64_t to, bool playing) {
    Impl& p = *impl_;
    if (!playing) {
        std::vector<Event> out;
        if (p.playing) out = allOff(from);
        p.playing = false;
        p.release();
        return out;
    }
    const bool jump = !p.playing || from != p.next;
    if (jump) p.release();
    // Freeze takes hold, or lets go, on the next bar line (bar 1 at the earliest: bar 0 has nothing before it).
    if (p.freeze != (p.hold >= 0)) {
        const std::int64_t bt = p.world->barTicks();
        const std::int64_t line = std::max(bt, ceilDiv(from, bt) * bt);
        if (line < to) {
            std::vector<Event> out = line > from ? span(from, line, jump) : std::vector<Event>{};
            p.hold = p.freeze ? line / bt : -1;
            p.rebuild();
            std::vector<Event> rest = span(line, to, jump && line == from);
            out.insert(out.end(), rest.begin(), rest.end());
            return out;
        }
    }
    return span(from, to, jump);
}

std::vector<Event> Player::span(std::int64_t from, std::int64_t to, bool jump) {
    Impl& p = *impl_;
    std::vector<Event> out;
    std::vector<Note> starting;
    if (jump) {
        // Play from somewhere new (§6.3): end what was sounding, and pick up the chords already ringing here.
        out = allOff(from);
        p.later.clear();
        for (Note n : p.world->ringing(from)) {
            n.start = from;
            starting.push_back(n);
        }
    }
    p.playing = true;
    p.next = to;
    std::vector<Note> pending;
    pending.swap(p.later);
    for (const Note& n : p.world->collect(from, to)) pending.push_back(n);
    for (const Note& n : pending) (n.start < to ? starting : p.later).push_back(n);
    std::stable_sort(starting.begin(), starting.end(), noteOrder);

    // §11, a block at a time: at each tick, offs first, then ons; an on over a sounding note ends it first.
    for (const Note& n : starting) {
        p.offsBefore(n.start + 1, out);
        const auto key = std::make_pair(n.channel, n.note);
        if (p.sounding.count(key)) {
            out.push_back({n.start, Event::Off, n.channel, n.note, 0});
            p.sounding.erase(key);
        }
        out.push_back({n.start, Event::On, n.channel, n.note, n.velocity});
        p.sounding[key] = {n.end, p.serial++};
    }
    p.offsBefore(to, out);
    // The furthest back anything looks is one pass of the harmony (64 bars at most) for a chord's start.
    p.world->forget(p.world->barOf(from) - 70);
    std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) {
        return std::tie(a.tick, a.kind, a.channel, a.note) < std::tie(b.tick, b.kind, b.channel, b.note);
    });
    return out;
}

std::vector<Event> events(const std::vector<Note>& ns) {
    struct Raw {
        std::int64_t tick;
        int kind;
        std::size_t note;
    };
    std::vector<Raw> raw;
    for (std::size_t i = 0; i < ns.size(); ++i) {
        raw.push_back({ns[i].start, Event::On, i});
        raw.push_back({ns[i].end, Event::Off, i});
    }
    std::stable_sort(raw.begin(), raw.end(), [&](const Raw& a, const Raw& b) {
        return std::tie(a.tick, a.kind, ns[a.note].channel, ns[a.note].note, a.note) <
               std::tie(b.tick, b.kind, ns[b.note].channel, ns[b.note].note, b.note);
    });
    std::map<std::pair<int, int>, std::size_t> sounding;  // (channel, note) → the note that holds it
    std::vector<Event> out;
    for (const Raw& r : raw) {
        const Note& n = ns[r.note];
        const auto key = std::make_pair(n.channel, n.note);
        if (r.kind == Event::On) {
            if (sounding.count(key)) out.push_back({r.tick, Event::Off, n.channel, n.note, 0});
            sounding[key] = r.note;
            out.push_back({r.tick, Event::On, n.channel, n.note, n.velocity});
        } else {
            auto it = sounding.find(key);
            if (it == sounding.end() || it->second != r.note) continue;  // already ended by a newer note
            sounding.erase(it);
            out.push_back({r.tick, Event::Off, n.channel, n.note, 0});
        }
    }
    return out;
}

// ---- what the editor shows -------------------------------------------------------------------------------

std::string noteName(int pc) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[floorMod(pc, 12)];
}

std::string chordName(const Song& song, int root, int size, bool homeMajor) {
    // §3.4's chord: the first `size` notes of ranked(root). Named by what's in it, from the root.
    std::vector<int> rk = chordRanked(song, root);
    if (homeMajor && floorMod(root - song.root, 12) == 0)
        for (int& pc : rk)
            if (pc == (song.root + 3) % 12) pc = (song.root + 4) % 12;
    std::vector<bool> has(12, false);
    for (int i = 0; i < std::min<int>(clampi(size, 1, 6), static_cast<int>(rk.size())); ++i) has[floorMod(rk[i] - root, 12)] = true;
    std::string n = noteName(root);
    const bool M3 = has[4], m3 = has[3] && !M3, p5 = has[7], d5 = has[6] && !p5, a5 = has[8] && !p5 && M3;
    const bool m7 = has[10], M7 = has[11] && !m7, six = has[9] && !m7 && !M7;
    if (!M3 && !m3) return n + (has[5] ? "sus4" : has[2] ? "sus2" : p5 ? "5" : "");
    if (m3 && d5) return n + (m7 ? "m7b5" : "dim");
    if (a5) return n + "aug";
    // Upper notes: on a seventh chord they extend it (9, 11); without one they're added (add9, add11).
    const bool nine = has[2], eleven = has[5];
    const bool seventh = m7 || M7;
    const std::string ext = seventh ? (eleven ? "11" : nine ? "9" : "7") : "";
    const std::string add = !seventh ? (nine ? "add9" : eleven ? "add11" : "") : "";
    if (m3) return n + (m7 ? "m" + ext : M7 ? "m(maj" + ext + ")" : six ? "m6" + add : "m" + add);
    return n + (m7 ? ext : M7 ? "maj" + ext : six ? "6" + add : add);
}

struct View::Impl {
    Impl(const Song& s, std::int64_t hold) : song(s), track(song, hold) {}
    Song song;
    Track track;
};

View::View(const Song& song) : impl_(std::make_unique<Impl>(song, -1)) {}
View::View(const Song& song, std::int64_t hold) : impl_(std::make_unique<Impl>(song, hold)) {}
View::~View() = default;

std::vector<Lane> View::lanes(std::int64_t tick) {
    tick = std::max<std::int64_t>(0, tick);
    impl_->track.forget(impl_->track.barOf(tick) - 70);  // an editor open through a long set doesn't keep every bar
    // Under a form (§18.1), the section's letter, at the section's own tick.
    const Track::Span sp = impl_->track.at(impl_->track.barOf(tick));
    World& w = *sp.world;
    tick -= sp.start * w.barTicks();
    const std::int64_t bar = w.barOf(tick);
    std::vector<Lane> out;
    for (std::size_t gi = 0; gi < sp.song->groups.size(); ++gi) {
        const GroupPlay& p = w.play(bar, gi);
        const Group& g = p.group();
        Lane lane;
        lane.name = g.name;
        lane.steps = clampi(g.steps, 1, 64);
        for (const Voice& v : g.voices) lane.channels.push_back(v.channel);
        // Where the group is at this tick (its step under this bar's state).
        const Step now = w.before(gi, w.stepFrom(gi, tick + 1));
        lane.playing = now.valid ? now.slot : -1;
        const auto [root, major] = w.chordAt(tick);
        bool chords = !g.voices.empty();
        lane.held = !g.voices.empty();
        for (const Voice& v : g.voices) {
            chords = chords && v.type == VoiceType::Chord;
            lane.held = lane.held && v.type == VoiceType::Chord && v.articulation == Articulation::Sustain;
        }
        if (chords) lane.chord = chordName(p.song(), root, g.voices.front().chordSize, major);
        const Generator& gen = playing(SlotKind::Rhythm, g.rhythmGen);
        if (std::string(gen.name) != defaultGenerator(SlotKind::Rhythm)) lane.generator = gen.label;
        if (std::string(gen.name) == "register") lane.lock = paramValue(SlotKind::Rhythm, g.rhythmGen, "register", "lock");
        // A clocked generator has no slot pattern: the lane shows the steps it will play next, from here, each in its
        // slot (§16.10).
        std::map<int, std::int64_t> coming;
        if (p.clocked()) {
            const std::int64_t from = now.valid ? now.absStep : 0;
            for (std::int64_t a = from; a < from + lane.steps; ++a) coming.emplace(slotAt(a, lane.steps, g.direction, g.offset), a);
        }
        for (int slot = 0; slot < lane.steps; ++slot) {
            LaneCell c;
            c.hit = p.clocked() ? coming.count(slot) && p.hitAt(coming.at(slot), slot) : p.hit(slot);
            c.accent = p.accent(slot);
            c.gate = p.gate(slot);
            if (c.hit) {
                // The owner as the deal has it for this pass (Deal every cycle and Round robin vary by pass).
                const std::int64_t cycleStart = now.valid ? now.absStep - floorMod(now.absStep, lane.steps) : 0;
                std::int64_t a = cycleStart;
                for (std::int64_t k = cycleStart; k < cycleStart + lane.steps; ++k)
                    if (slotAt(k, lane.steps, g.direction, g.offset) == slot) {
                        a = k;
                        break;
                    }
                if (p.clocked()) a = coming.at(slot);
                c.owner = p.owner(a, slot);
                if (c.owner == kTutti)  // §5.6: every member plays it; the lane shows the first, in its colour and note
                    for (int m = 0; m < static_cast<int>(g.voices.size()) && c.owner == kTutti; ++m)
                        if (p.member(m)) c.owner = m;
                if (c.owner == kTutti) c.owner = -1;
                if (c.owner >= 0) {
                    const Voice& v = g.voices[c.owner];
                    if (v.type == VoiceType::Chord) {
                        c.note = (v.nearest ? voicedAt(w, gi, c.owner, tick) : chordNotes(p.song(), v, root, major)).front();
                    } else {
                        Step s;
                        s.valid = true, s.bar = bar, s.absStep = a, s.slot = slot, s.tick = tick, s.hit = true, s.owner = c.owner;
                        s.nominal = p.anchor() + a * p.stepTicks();  // a Kotekan reads its source at the step's grid tick (§16.6)
                        // §5.7: the note as it's heard, when this is a step that plays (Clean handoffs may have redrawn it)
                        const Step real = w.stepFrom(gi, s.nominal);
                        c.note = real.valid && real.nominal == s.nominal && real.hit && real.owner == c.owner ? heardPitch(w, gi, c.owner, real)
                                                                                                             : linePitch(w, gi, c.owner, s);
                    }
                }
            }
            lane.cells.push_back(c);
        }
        out.push_back(std::move(lane));
    }
    return out;
}

std::vector<std::vector<std::string>> View::form(std::int64_t bar, int bars) {
    const Song& base = impl_->song;
    // Letters by item, whoever pushed the layer (§17.6): the temporary rows, then the temporary trigger cords, are
    // B, C, D, … (formLetters). A letter's prime rows are permanent, so they have none.
    const std::map<std::uint64_t, std::string> letters = formLetters(base);
    // Which groups an item reaches: its group, its voice's group, or all of them (the song, the harmony).
    auto reaches = [&](Scope scope, std::uint64_t target, const Group& g) {
        if (scope == Scope::Global || scope == Scope::Harmony) return true;
        if (scope == Scope::Group) return target == g.id;
        for (const Voice& v : g.voices)
            if (v.id == target) return true;
        return false;
    };
    std::vector<std::vector<std::string>> out(base.groups.size());
    for (std::int64_t b = std::max<std::int64_t>(0, bar); b < std::max<std::int64_t>(0, bar) + bars; ++b) {
        const Track::Span sp = impl_->track.at(b);
        const Song& song = *sp.song;
        const std::size_t rows = song.matrix.size();
        const State& st = sp.world->state(b - sp.start);
        for (std::size_t gi = 0; gi < song.groups.size() && gi < out.size(); ++gi) {
            std::string l = "A";
            for (std::size_t item : st.layers) {
                std::uint64_t id = 0;
                Scope scope = Scope::Global;
                std::uint64_t target = 0;
                if (item < rows) id = song.matrix[item].id, scope = song.matrix[item].scope, target = song.matrix[item].target;
                else if (item - rows < song.mod.cords.size()) {
                    const Cord& c = song.mod.cords[item - rows];
                    id = c.id, scope = c.dest.scope, target = c.dest.target;
                } else continue;
                const auto it = letters.find(id);
                if (it != letters.end() && reaches(scope, target, song.groups[gi])) {
                    l = it->second;
                    break;
                }
            }
            // §18.1: under a form the section's letter leads, and a layer inside it follows: "A", "A′", "B·C".
            if (impl_->track.form()) l = entryName(sp.section.entry) + (l == "A" ? "" : "\xC2\xB7" + l);
            out[gi].push_back(l);
        }
    }
    return out;
}

Firing View::nextAct(std::uint64_t id, std::int64_t bar, int bars) {
    // Where the row or cord is in §17.4's order, in a song (a letter's prime rows come after the song's own).
    auto place = [id](const Song& song) {
        std::size_t at = static_cast<std::size_t>(-1);
        for (std::size_t r = 0; r < song.matrix.size(); ++r)
            if (song.matrix[r].id == id) at = r;
        for (std::size_t c = 0; c < song.mod.cords.size(); ++c)
            if (song.mod.cords[c].id == id) at = song.matrix.size() + c;
        return at;
    };
    Firing f;
    if (place(impl_->song) == static_cast<std::size_t>(-1)) return f;
    for (std::int64_t b = std::max<std::int64_t>(1, bar); b < std::max<std::int64_t>(1, bar) + bars; ++b) {
        const Track::Span sp = impl_->track.at(b, false);  // the replay's, Freeze or not (§17.6)
        const State& st = sp.world->replayed(b - sp.start);
        const std::size_t at = place(*sp.song);
        const std::uint8_t a = at < st.acts.size() ? st.acts[at] : 0;
        if (a == 2) {
            f.bar = b;
            return f;
        }
        f.skipped += a == 1;
    }
    return Firing{};
}

std::vector<std::string> View::chordsByBar(std::int64_t bar, int bars) {
    std::vector<std::string> out;
    for (std::int64_t b = std::max<std::int64_t>(0, bar); b < std::max<std::int64_t>(0, bar) + bars; ++b) {
        const HarmonyView h = harmony(b * impl_->track.barTicks());
        out.push_back(h.names[static_cast<std::size_t>(h.current)]);
    }
    return out;
}

HarmonyView View::harmony(std::int64_t tick) {
    tick = std::max<std::int64_t>(0, tick);
    const Track::Span sp = impl_->track.at(impl_->track.barOf(tick));
    World& w = *sp.world;
    tick -= sp.start * w.barTicks();
    const std::int64_t bar = w.barOf(tick);
    const State& st = w.state(bar);
    const Song& s = st.song;
    int size = 3;  // a triad, unless a Chord voice says how many notes it plays
    for (auto g = s.groups.rbegin(); g != s.groups.rend(); ++g)
        for (auto v = g->voices.rbegin(); v != g->voices.rend(); ++v)
            if (v->type == VoiceType::Chord) size = v->chordSize;  // the first Chord voice wins
    HarmonyView h;
    const std::vector<int> prog = detail::progressionOf(s, st.mut);
    const std::vector<int> rk = harmonyRanked(s);
    const std::vector<bool> major = homeMajor(s, prog);
    for (std::size_t k = 0; k < prog.size(); ++k) {
        h.roots.push_back(rk[static_cast<std::size_t>(prog[k])]);
        h.names.push_back(chordName(s, rk[static_cast<std::size_t>(prog[k])], size, major[k]));
    }
    h.current = static_cast<int>(floorMod(floorDiv(tick - st.harmonyAnchor, changeTicks(s)), static_cast<std::int64_t>(prog.size())));
    h.phraseLength = std::max(1, s.phraseLength);
    h.phraseBar = static_cast<int>(floorMod(bar, h.phraseLength));
    return h;
}

}  // namespace hocket
