// The generators (SPEC §16): the list, and what each makes.
#include "hocket/generators.hpp"

#include <algorithm>
#include <numeric>

#include "hocket/engine.hpp"
#include "hocket/rng.hpp"

namespace hocket {

namespace {
int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
}  // namespace

const std::vector<Generator>& generators() {
    // Appendix B's names, frozen once released. Rhythm first, then Notes; new ones are only ever added.
    static const std::vector<Generator> all = {
        {"random", "Random", true, false, {}, true, true, ""},
        // §16.4: Pulses hits as evenly as the steps allow, turned by Rotation. Its own count of hits: no Density.
        {"euclid", "Even", true, false,
         {{"pulses", "Pulses", 1, 64, 4, 4}, {"rotation", "Rotation", 0, 63, 0, 0}},
         false, true, "Pulses set the hits; Density still shapes the gates"},
        // §16.4: from Barlow's indispensability (the meter) at Wild 0 to the seeded order (Random) at Wild 16.
        {"metric", "Metric", true, false,
         {{"wild", "Wild", 0, 16, 8, 8}, {"beat", "Beat", 1, 16, 4, 4}},
         true, true, ""},
        {"wander", "Wander", false, true, {}, true, true, ""},
        // §16.5: §3.3's roots, the other notes from Voss's dice; Riff repeats each pass, Run never does.
        {"pink", "1/f", false, true,
         {{"dice", "Dice", 1, 6, 4, 4}, {"loop", "Loop", 0, 1, 0, 0, true, false, "Riff|Run"}},
         true, true, ""},
        // §16.7: a seeded shift register stepped on the group's clock, locked → slipping → random, and inverted.
        {"register", "Register", true, true,
         {{"length", "Length", 2, 16, 16, 8}, {"lock", "Lock", 0, 17, 12, 12}, {"bits", "Bits", 1, 8, 8, 8}},
         true, false, "Wander isn't read: the register picks the notes"},
        // §16.5: Wander's line, measured from the key's tonic instead of the chord root.
        {"pedal", "Pedal", false, true, {}, true, true, ""},
        // §16.6: plays where its source group rests, in Random's order (Density 16: the whole complement).
        {"interlock", "Interlock", true, false,
         {{"source", "Source", 0, 0, 0, 0, false, false, "", true}},
         true, true, ""},
        // §16.6: the source group's composite inside a register band, doubled on this group's lines.
        {"resultant", "Resultant", true, false,
         {{"source", "Source", 0, 0, 0, 0, false, false, "", true},
          {"low", "Low", 0, 127, 60, 60},
          {"high", "High", 0, 127, 84, 84},
          {"velocity", "Velocity", 0, 1, 0, 0, true, true, "As played|Accented"}},
         false, true, "The source's notes in the band: this group's own rhythm isn't read"},
        // §16.6: the sangsih, made from the source voice's line (the polos) around the chord root (the pokok).
        {"kotekan", "Kotekan", false, true,
         {{"source", "Source", 0, 0, 0, 0, false, false, "", true},
          {"mode", "Mode", 0, 3, 1, 1, true, true, "Nyog cag|Telu|Empat|Nyok cok"},
          {"lead", "Lead-in", 1, 16, 2, 2}},
         true, false, "Wander isn't read: the source's line picks the notes"},
    };
    return all;
}

std::vector<const Generator*> generatorsFor(SlotKind slot) {
    std::vector<const Generator*> out;
    for (const Generator& g : generators())
        if (slot == SlotKind::Rhythm ? g.rhythm : g.notes) out.push_back(&g);
    return out;
}

const Generator* findGenerator(SlotKind slot, const std::string& name) {
    for (const Generator* g : generatorsFor(slot))
        if (name == g->name) return g;
    return nullptr;
}

const GenParam* findParam(const Generator& gen, const std::string& key) {
    for (const GenParam& p : gen.params)
        if (key == p.key) return &p;
    return nullptr;
}

const char* defaultGenerator(SlotKind slot) { return slot == SlotKind::Rhythm ? "random" : "wander"; }

const Generator& playing(SlotKind slot, const GenSlot& s) {
    if (const Generator* g = findGenerator(slot, s.use)) return *g;
    return *findGenerator(slot, defaultGenerator(slot));
}

int paramValue(SlotKind slot, const GenSlot& s, const Generator& gen, const GenParam& p) {
    const auto g = s.params.find(gen.name);
    if (g != s.params.end()) {
        const auto v = g->second.find(p.key);
        if (v != g->second.end()) return clampi(v->second, p.lo, p.hi);
    }
    return slot == SlotKind::Rhythm ? p.rhythmDefault : p.notesDefault;
}

int paramValue(SlotKind slot, const GenSlot& s, const char* gen, const char* key) {
    const Generator* g = findGenerator(slot, gen);
    const GenParam* p = g ? findParam(*g, key) : nullptr;
    return p ? paramValue(slot, s, *g, *p) : 0;
}

std::optional<GenTarget> generatorTarget(const std::string& key) {
    const auto a = key.find('.');
    const auto b = a == std::string::npos ? std::string::npos : key.find('.', a + 1);
    if (b == std::string::npos) return std::nullopt;
    const std::string slot = key.substr(0, a), name = key.substr(a + 1, b - a - 1), param = key.substr(b + 1);
    if (slot != "rhythm" && slot != "notes") return std::nullopt;
    const SlotKind sk = slot == "rhythm" ? SlotKind::Rhythm : SlotKind::Notes;
    const Generator* g = findGenerator(sk, name);
    const GenParam* p = g ? findParam(*g, param) : nullptr;
    if (!p || !p->destination) return std::nullopt;
    return GenTarget{sk, g, p};
}

std::string generatorKey(SlotKind slot, const Generator& gen, const GenParam& p) {
    return std::string(slot == SlotKind::Rhythm ? "rhythm." : "notes.") + gen.name + "." + p.key;
}

GenKind kindOf(SlotKind slot, const GenSlot& s) {
    const std::string name = playing(slot, s).name;
    if (name == "register" || name == "interlock" || name == "kotekan" || name == "resultant") return GenKind::Clocked;
    if (name == "pink" && paramValue(slot, s, "pink", "loop") == 1) return GenKind::Clocked;  // Run
    return GenKind::Static;
}

// ---- §16.6 derived generators ----------------------------------------------------------------------------

std::uint64_t sourceOf(const GenSlot& s, const char* gen) {
    const auto it = s.sources.find(gen);
    return it == s.sources.end() ? 0 : it->second;
}

namespace {
// The graph the derived slots make (§16.6), with the deal's edges among them (§5.5). Positions are (group, voice), the
// voice −1 for a group's rhythm slot, so they compare in song order.
using Pos = std::pair<std::size_t, int>;
struct Edge {
    Pos to;
    bool derived;  // a derived generator's source (a slot that can play its default); false: Alternate or Bells' tune
};
// §5.5: the tune a partitioned group's other lines play (−1: not partitioned, or no tune).
int tuneOf(const Group& g) {
    if (g.deal != DealMode::Alternate && g.deal != DealMode::Bells) return -1;
    for (std::size_t i = 0; i < g.voices.size(); ++i)
        if (g.voices[i].type != VoiceType::Chord) return static_cast<int>(i);
    return -1;
}
// The slot a derived slot names: a group's Interlock or Resultant names a group; a Lead or Bass voice's Kotekan names a
// Lead or Bass voice.
std::optional<Pos> named(const Song& song, Pos at) {
    const Group& g = song.groups[at.first];
    if (at.second < 0) {
        const std::string name = playing(SlotKind::Rhythm, g.rhythmGen).name;  // Interlock and Resultant name a group
        if (name != "interlock" && name != "resultant") return std::nullopt;
        const std::uint64_t id = sourceOf(g.rhythmGen, name.c_str());
        for (std::size_t i = 0; i < song.groups.size(); ++i)
            if (id != 0 && song.groups[i].id == id && i != at.first) return Pos{i, -1};
        return std::nullopt;
    }
    const Voice& v = g.voices[static_cast<std::size_t>(at.second)];
    if (v.type == VoiceType::Chord || std::string(playing(SlotKind::Notes, v.notesGen).name) != "kotekan") return std::nullopt;
    const std::uint64_t id = sourceOf(v.notesGen, "kotekan");
    if (id == 0 || id == v.id) return std::nullopt;
    for (std::size_t i = 0; i < song.groups.size(); ++i)
        for (std::size_t j = 0; j < song.groups[i].voices.size(); ++j)
            if (song.groups[i].voices[j].id == id && song.groups[i].voices[j].type != VoiceType::Chord) return Pos{i, static_cast<int>(j)};
    return std::nullopt;
}
// Where a slot's material actually comes from as it plays: a line in a partitioned group plays its tune's note before
// anything else (§5.5), so that edge wins; otherwise the derived source.
std::optional<Edge> edge(const Song& song, Pos at) {
    if (at.second >= 0) {
        const Group& g = song.groups[at.first];
        const int tune = tuneOf(g);
        if (tune >= 0 && tune != at.second && g.voices[static_cast<std::size_t>(at.second)].type != VoiceType::Chord)
            return Edge{{at.first, tune}, false};
    }
    if (const auto n = named(song, at)) return Edge{*n, true};
    return std::nullopt;
}
// The source when it's live. A chain that comes back here is a loop, broken at its last derived slot in song order
// (§16.6); a deal's edge can't be broken, so a loop through a tune is broken at a derived slot too (§5.5).
std::optional<Pos> live(const Song& song, Pos at) {
    const std::optional<Pos> first = named(song, at);
    if (!first) return std::nullopt;
    std::vector<Pos> chain{at};
    std::vector<bool> derived{true};
    Pos cur = *first;
    for (std::size_t n = 0; n <= 64 * 65; ++n) {
        if (cur == at) {
            for (std::size_t k = 0; k < chain.size(); ++k)
                if (derived[k] && chain[k] > at) return first;  // a later derived slot in the loop closes it
            return std::nullopt;                              // this one is the last: it plays the default
        }
        if (std::find(chain.begin(), chain.end(), cur) != chain.end()) return first;  // a loop elsewhere: it ends there
        const std::optional<Edge> next = edge(song, cur);
        if (!next) return first;
        chain.push_back(cur);
        derived.push_back(next->derived);
        cur = next->to;
    }
    return first;
}
}  // namespace

namespace {
std::optional<std::size_t> groupSource(const Song& song, std::size_t gi, const char* name) {
    if (gi >= song.groups.size() || std::string(playing(SlotKind::Rhythm, song.groups[gi].rhythmGen).name) != name) return std::nullopt;
    if (const auto p = live(song, {gi, -1})) return p->first;
    return std::nullopt;
}
}  // namespace

std::optional<std::size_t> interlockSource(const Song& song, std::size_t gi) { return groupSource(song, gi, "interlock"); }
std::optional<std::size_t> resultantSource(const Song& song, std::size_t gi) { return groupSource(song, gi, "resultant"); }

std::optional<std::pair<std::size_t, int>> kotekanSource(const Song& song, std::size_t gi, int vi) {
    if (gi >= song.groups.size() || vi < 0 || vi >= static_cast<int>(song.groups[gi].voices.size())) return std::nullopt;
    return live(song, {gi, vi});
}

const Generator& playingIn(const Song& song, SlotKind slot, std::size_t gi, int vi) {
    const Group& g = song.groups[gi];
    const GenSlot& s = slot == SlotKind::Rhythm ? g.rhythmGen : g.voices[static_cast<std::size_t>(vi)].notesGen;
    const Generator& gen = playing(slot, s);
    const std::string name = gen.name;
    if ((name == "interlock" && !interlockSource(song, gi)) || (name == "resultant" && !resultantSource(song, gi)) ||
        (name == "kotekan" && !kotekanSource(song, gi, vi)))
        return *findGenerator(slot, defaultGenerator(slot));
    return gen;
}

GenKind kindIn(const Song& song, SlotKind slot, std::size_t gi, int vi) {
    const Group& g = song.groups[gi];
    const GenSlot& s = slot == SlotKind::Rhythm ? g.rhythmGen : g.voices[static_cast<std::size_t>(vi)].notesGen;
    if (&playingIn(song, slot, gi, vi) != &playing(slot, s)) return GenKind::Static;  // a default standing in
    return kindOf(slot, s);
}

std::optional<int> handoffRedraw(std::uint64_t pitchSeed, std::int64_t slot, int poolSize, int avoid, const std::function<int(int)>& pitchAt) {
    Stream r(hash(pitchSeed, {"handoff", slot}));
    for (int i = 0; i < 3; ++i) {
        const int y = pitchAt(static_cast<int>(r.below(static_cast<std::uint64_t>(std::max(1, poolSize)))));
        if (((y % 12) + 12) % 12 != avoid) return y;
    }
    return std::nullopt;
}

int resultantNote(int note, int low, int high, int transpose) {
    if (low > high) std::swap(low, high);
    if (note < low || note > high) return -1;
    return clampi(note + 12 * transpose, 0, 127);
}

int kotekanNote(int mode, const std::vector<int>& pool, int centre, int x, bool anticipate, int nextRoot) {
    if (pool.empty()) return x;
    auto in = [&](int n) { return std::find(pool.begin(), pool.end(), ((n % 12) + 12) % 12) != pool.end(); };
    int base = x;
    while (!in(base)) --base;  // the highest note of B's pool at or below A's
    std::vector<int> height(pool.begin(), pool.end());
    auto above = [&](int pc) { return ((pc - centre) % 12 + 12) % 12; };
    std::stable_sort(height.begin(), height.end(), [&](int a, int b) { return above(a) < above(b); });
    const int bpc = ((base % 12) + 12) % 12;
    const int d = static_cast<int>(std::find(height.begin(), height.end(), bpc) - height.begin());
    auto up = [&](int k) {
        int y = base;
        for (int i = 0; i < k; ++i)
            do ++y;
            while (!in(y));
        while (y - base > 12) y -= 12;
        return y;
    };
    switch (clampi(mode, 0, 3)) {
        case 0: return base;                                // nyog cag: A's own note, one line split in two
        case 1: return d % 2 == 0 ? up(2) : base;           // telu: low → high, the middle in unison
        case 2: return d % 2 == 0 ? up(3) : up(1);          // empat: A's pair mirrored in the pair above
        default:
            if (anticipate) {                               // nyok cok: the coming core tone, the nearest one
                const int r = ((nextRoot % 12) + 12) % 12;
                const int lo = base - (((base - r) % 12) + 12) % 12;
                return base - lo < lo + 12 - base ? lo : lo + 12;
            }
            return up(1);
    }
}

// ---- §16.7 the Register ----------------------------------------------------------------------------------

Register::Register(std::uint64_t seed) : seed_(seed), bits_(16) {
    const std::uint64_t h0 = hash(seed, {"register", "init"});
    for (int i = 0; i < 16; ++i) bits_[static_cast<std::size_t>(15 - i)] = static_cast<std::uint8_t>((h0 >> i) & 1);  // b(−1 − i)
}

int Register::bit(std::int64_t t) const { return bits_[static_cast<std::size_t>(t + 16)]; }

bool Register::flips(std::int64_t t, int lock) const {
    lock = clampi(lock, 0, 17);
    if (lock == 17) return true;  // the inverted end: every bit that comes round flips, and nothing is drawn
    const std::uint64_t d = 16 - static_cast<std::uint64_t>(lock);
    return Stream(hash(seed_, {"register", t})).below(512) < d * d;
}

void Register::step(int n, int lock) {
    const std::int64_t t = next();
    n = clampi(n, 2, 16);
    bits_.push_back(static_cast<std::uint8_t>(bit(t - n) ^ (flips(t, lock) ? 1 : 0)));
}

void Register::flip(const std::vector<int>& back) {
    const std::int64_t t0 = next();
    for (int q : back)
        if (q >= 0 && q < 16) bits_[static_cast<std::size_t>(t0 - 1 - q + 16)] ^= 1;
}

int Register::value(std::int64_t t, int w) const {
    w = clampi(w, 1, 8);
    int v = 0;
    for (int i = 0; i < w; ++i) v |= bit(t - i) << (w - 1 - i);
    return v;
}

std::vector<int> registerVary(std::uint64_t s, std::uint64_t targetID, int depth, int n) {
    n = clampi(n, 2, 16);
    std::vector<int> Q = priorityOrder(hash(s, {"register", targetID}), n);
    Q.resize(static_cast<std::size_t>(std::min(std::max(depth, 0), n)));
    return Q;
}

bool registerHit(int v, int w, int density) {
    return static_cast<std::int64_t>(v) < static_cast<std::int64_t>(rdiv((std::int64_t{1} << clampi(w, 1, 8)) * clampi(density, 1, 16), 16));
}

int registerPosition(int v, int w, int poolSize) { return static_cast<int>((static_cast<std::int64_t>(v) * poolSize) >> clampi(w, 1, 8)); }

// ---- §16.5 1/f Wander ------------------------------------------------------------------------------------

int pinkAt(std::uint64_t pitchSeed, int ps, int dice, std::int64_t time) {
    const int m = ps - 1;
    if (m < 1) return 0;
    dice = clampi(dice, 1, 6);
    std::int64_t sum = 0;
    for (int i = 0; i < dice; ++i) sum += static_cast<std::int64_t>(Stream(hash(pitchSeed, {"pink", i, time >> i})).below(static_cast<std::uint64_t>(m)));
    return 1 + static_cast<int>(rdiv(sum, dice));
}

std::vector<bool> euclidHits(int L, int pulses, int rotation) {
    L = clampi(L, 1, 64);
    const std::int64_t k = clampi(pulses, 1, L), rot = rotation % L;
    std::vector<bool> out(static_cast<std::size_t>(L));
    for (std::int64_t slot = 0; slot < L; ++slot) {
        const std::int64_t x = ((slot - rot) % L + L) % L;
        out[static_cast<std::size_t>(slot)] = (x * k) % L < k;
    }
    return out;
}

namespace {
std::vector<int> primeFactors(int n) {
    std::vector<int> out;
    for (int p = 2; n > 1; ++p)
        while (n % p == 0) out.push_back(p), n /= p;
    return out;
}
}  // namespace

std::vector<int> metricStrata(int L, int beat) {
    L = clampi(L, 1, 64);
    const int B = std::gcd(clampi(beat, 1, 16), L);
    std::vector<int> out = primeFactors(L / B);                 // the bar's beats, ascending
    std::vector<int> sub = primeFactors(B);                     // the beat's subdivisions, descending
    std::sort(sub.rbegin(), sub.rend());
    out.insert(out.end(), sub.begin(), sub.end());
    return out;
}

std::vector<int> barlowUpper(int p) {
    if (p <= 1) return {0};
    if (p == 2) return {1, 0};
    std::vector<int> f = primeFactors(p - 1);
    std::sort(f.rbegin(), f.rend());
    std::vector<int> u = indispensability(f);
    const int k = p / 4;
    for (int& v : u)
        if (v >= k) ++v;
    u.insert(u.begin() + (p - 2), k);
    return u;
}

std::vector<int> indispensability(const std::vector<int>& strata) {
    int L = 1;
    for (int q : strata) L *= q;
    std::vector<std::vector<int>> U;
    for (int q : strata) U.push_back(barlowUpper(q));
    std::vector<int> out(static_cast<std::size_t>(L), 0);
    for (int s = 0; s < L; ++s) {
        const int x = ((s - 1) % L + L) % L;  // Barlow reads the pulse before
        int total = 0, above = 1;
        for (std::size_t j = 0; j < strata.size(); ++j) {
            int below = 1;
            for (std::size_t i = j + 1; i < strata.size(); ++i) below *= strata[i];
            const int q = strata[j], d = (x / below) % q;
            total += above * U[j][static_cast<std::size_t>((d + 1) % q)];
            above *= q;
        }
        out[static_cast<std::size_t>(s)] = total;
    }
    return out;
}

std::vector<bool> metricHits(std::uint64_t rhythmSeed, int L, int density, int wild, int beat) {
    L = clampi(L, 1, 64);
    wild = clampi(wild, 0, 16);
    const std::vector<int> psi = indispensability(metricStrata(L, beat));
    const std::vector<int> P = priorityOrder(hash(rhythmSeed, {"hits"}), L);
    std::vector<int> R(static_cast<std::size_t>(L));
    for (int i = 0; i < L; ++i) R[static_cast<std::size_t>(P[static_cast<std::size_t>(i)])] = i;
    std::vector<int> order(static_cast<std::size_t>(L));
    std::iota(order.begin(), order.end(), 0);
    auto score = [&](int s) { return (16 - wild) * (L - 1 - psi[static_cast<std::size_t>(s)]) + wild * R[static_cast<std::size_t>(s)]; };
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int sa = score(a), sb = score(b);
        return sa != sb ? sa < sb : R[static_cast<std::size_t>(a)] < R[static_cast<std::size_t>(b)];
    });
    const int n = std::max(1, static_cast<int>(rdiv(static_cast<std::int64_t>(L) * clampi(density, 1, 16), 16)));
    std::vector<bool> out(static_cast<std::size_t>(L), false);
    for (int j = 0; j < n && j < L; ++j) out[static_cast<std::size_t>(order[static_cast<std::size_t>(j)])] = true;
    return out;
}

std::vector<bool> rhythmHits(const Group& g, int density, std::int64_t) {
    const int L = clampi(g.steps, 1, 64);
    const Generator& gen = playing(SlotKind::Rhythm, g.rhythmGen);
    const std::string name = gen.name;
    if (name == "euclid")
        return euclidHits(L, paramValue(SlotKind::Rhythm, g.rhythmGen, "euclid", "pulses"),
                          paramValue(SlotKind::Rhythm, g.rhythmGen, "euclid", "rotation"));
    if (name == "register") {
        // A clocked generator has no slot pattern: this is its first pass at the group's own knobs, for a view.
        Register r(g.rhythmSeed);
        const int n = paramValue(SlotKind::Rhythm, g.rhythmGen, "register", "length"), lock = paramValue(SlotKind::Rhythm, g.rhythmGen, "register", "lock");
        const int w = paramValue(SlotKind::Rhythm, g.rhythmGen, "register", "bits");
        std::vector<bool> out;
        for (int slot = 0; slot < L; ++slot) {
            r.step(n, lock);
            out.push_back(registerHit(r.value(slot, w), w, density));
        }
        return out;
    }
    if (name == "metric")
        return metricHits(g.rhythmSeed, L, density, paramValue(SlotKind::Rhythm, g.rhythmGen, "metric", "wild"),
                          paramValue(SlotKind::Rhythm, g.rhythmGen, "metric", "beat"));
    return hits(g.rhythmSeed, L, density);  // Random (§4.1)
}

std::vector<int> notePositions(const Voice& v, int L, int ps, int wander) {
    std::vector<int> idx = noteIndices(hash(v.pitchSeed, {"notes"}), L, ps, wander);  // Wander (§3.3), and Pedal's
    const Generator& gen = playing(SlotKind::Notes, v.notesGen);
    if (std::string(gen.name) == "pink") {
        // The same roots; the other slots from the dice. Riff reads them at the slot; Run (clocked) at t, so this is the
        // first pass of a Run.
        const int dice = paramValue(SlotKind::Notes, v.notesGen, "pink", "dice");
        for (int slot = 0; slot < static_cast<int>(idx.size()); ++slot)
            if (idx[static_cast<std::size_t>(slot)] != 0) idx[static_cast<std::size_t>(slot)] = pinkAt(v.pitchSeed, ps, dice, slot);
    } else if (std::string(gen.name) == "register") {
        // The first pass of the register at the voice's own knobs, for a view (the engine steps it with the matrix's).
        Register r(v.pitchSeed);
        const int n = paramValue(SlotKind::Notes, v.notesGen, "register", "length"), lock = paramValue(SlotKind::Notes, v.notesGen, "register", "lock");
        const int w = paramValue(SlotKind::Notes, v.notesGen, "register", "bits");
        for (int slot = 0; slot < static_cast<int>(idx.size()); ++slot) {
            r.step(n, lock);
            idx[static_cast<std::size_t>(slot)] = registerPosition(r.value(slot, w), w, ps);
        }
    }
    return idx;
}

NoteOrder orderOf(const GenSlot& s) {
    return std::string(playing(SlotKind::Notes, s).name) == "register" ? NoteOrder::Height : NoteOrder::Ranked;
}
bool onTonic(const GenSlot& s) { return std::string(playing(SlotKind::Notes, s).name) == "pedal"; }

}  // namespace hocket
