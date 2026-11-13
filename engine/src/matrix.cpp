// The change matrix's replay (SPEC §7.3, §7.5): the state at every bar, worked out from bar 0.
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

#include "hocket/form.hpp"
#include "hocket/generators.hpp"
#include "hocket/rng.hpp"
#include "internal.hpp"

namespace hocket::detail {

namespace {

Group* groupById(Song& s, std::uint64_t id) {
    for (Group& g : s.groups)
        if (g.id == id) return &g;
    return nullptr;
}
const Group* groupById(const Song& s, std::uint64_t id) { return groupById(const_cast<Song&>(s), id); }

Voice* voiceById(Song& s, std::uint64_t id, Group** owner = nullptr) {
    for (Group& g : s.groups)
        for (Voice& v : g.voices)
            if (v.id == id) {
                if (owner) *owner = &g;
                return &v;
            }
    return nullptr;
}
const Voice* voiceById(const Song& s, std::uint64_t id, const Group** owner = nullptr) {
    Group* g = nullptr;
    const Voice* v = voiceById(const_cast<Song&>(s), id, &g);
    if (owner) *owner = g;
    return v;
}

// The full span of a parameter (§10).
std::pair<int, int> spanOf(const Song& s, Scope scope, std::uint64_t target, Param p) {
    switch (p) {
        case Param::Density: return {scope == Scope::Voice ? 0 : 1, 16};  // §5.8: a member at 0 sits out
        case Param::Range: case Param::Wander: case Param::Voicing: case Param::Length: return {1, 16};
        case Param::Accent: case Param::Swing: return {0, 100};
        case Param::GatePercent: return {1, 200};
        case Param::OctaveLow: case Param::OctaveHigh: return {-1, 2};
        case Param::Steps: return {1, 64};
        case Param::Offset: return {0, 63};
        case Param::Weight: return {0, 16};
        case Param::ChordSize: return {1, 6};
        case Param::Probability: case Param::Ratchet: return {0, 100};
        case Param::Repeats: return {2, 4};
        case Param::HumanVelocity: return {0, 32};
        case Param::HumanTiming: return {0, 100};
        case Param::Handoff: return {0, 50};
        case Param::Fill: case Param::ModeMoves: case Param::VoiceLeading: case Param::CleanHandoffs: return {0, 1};
        case Param::SoloMember: {
            const Group* g = scope == Scope::Group ? groupById(s, target) : nullptr;
            return {0, g ? std::max(0, static_cast<int>(g->voices.size()) - 1) : 0};
        }
        case Param::DealMode: return {0, 10};
        case Param::Articulation: return {0, 3};
        case Param::Direction: return {0, 2};
        case Param::ChangesEvery: return {0, 4};
        case Param::Root: return {0, 11};
        case Param::Scale: return {0, static_cast<int>(kScales.size()) - 1};
        default: return {0, 0};
    }
}

int pick3(std::optional<int> a, std::optional<int> b, int c) { return a ? *a : b ? *b : c; }

// A parameter's current value at a target, if the target has it.
std::optional<int> get(const Song& s, Scope scope, std::uint64_t target, Param p) {
    switch (scope) {
        case Scope::Global:
            switch (p) {
                case Param::Range: return s.range;
                case Param::Wander: return s.wander;
                case Param::Accent: return s.accent;
                case Param::GatePercent: return s.gatePercent;
                case Param::OctaveLow: return s.octaveLow;
                case Param::OctaveHigh: return s.octaveHigh;
                case Param::Root: return s.root;
                case Param::Scale: return s.scale;
                case Param::Fill: return s.fill ? 1 : 0;
                case Param::ModeMoves: return s.modeMoves ? 1 : 0;
                default: return std::nullopt;
            }
        case Scope::Harmony:
            switch (p) {
                case Param::Range: return s.harmony.range;
                case Param::Wander: return s.harmony.wander;
                case Param::Length: return s.harmony.length;
                case Param::ChangesEvery: return s.harmony.changesEvery;
                default: return std::nullopt;
            }
        case Scope::Group: {
            const Group* g = groupById(s, target);
            if (!g) return std::nullopt;
            switch (p) {
                case Param::Range: return pick3(g->overrides.range, std::nullopt, s.range);
                case Param::Wander: return pick3(g->overrides.wander, std::nullopt, s.wander);
                case Param::Accent: return pick3(g->overrides.accent, std::nullopt, s.accent);
                case Param::GatePercent: return pick3(g->overrides.gatePercent, std::nullopt, s.gatePercent);
                case Param::Density: return g->density;
                case Param::Steps: return g->steps;
                case Param::Swing: return g->swing;
                case Param::Offset: return g->offset;
                case Param::SoloMember: return g->soloMember;
                case Param::DealMode: return static_cast<int>(g->deal);
                case Param::Direction: return static_cast<int>(g->direction);
                case Param::Probability: return g->probability;
                case Param::Ratchet: return g->ratchet;
                case Param::Repeats: return g->repeats;
                case Param::Fill: return g->fillNow ? 1 : 0;
                case Param::Handoff: return g->handoff;
                case Param::CleanHandoffs: return g->cleanHandoffs ? 1 : 0;
                default: return std::nullopt;
            }
        }
        case Scope::Voice: {
            const Group* g = nullptr;
            const Voice* v = voiceById(s, target, &g);
            if (!v) return std::nullopt;
            switch (p) {
                case Param::Range: return pick3(v->overrides.range, g->overrides.range, s.range);
                case Param::Wander: return pick3(v->overrides.wander, g->overrides.wander, s.wander);
                case Param::OctaveLow: return pick3(v->overrides.octaveLow, std::nullopt, s.octaveLow);
                case Param::OctaveHigh: return pick3(v->overrides.octaveHigh, std::nullopt, s.octaveHigh);
                case Param::Accent: return pick3(v->overrides.accent, g->overrides.accent, s.accent);
                case Param::GatePercent: return pick3(v->overrides.gatePercent, g->overrides.gatePercent, s.gatePercent);
                case Param::Weight: return v->weight;
                case Param::Density: return v->density;
                case Param::Voicing: return v->voicing;
                case Param::ChordSize: return v->chordSize;
                case Param::Articulation: return static_cast<int>(v->articulation);
                case Param::VoiceLeading: return v->nearest ? 1 : 0;
                case Param::HumanVelocity: return v->humanVelocity;
                case Param::HumanTiming: return v->humanTiming;
                default: return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

void set(Song& s, Scope scope, std::uint64_t target, Param p, int x) {
    switch (scope) {
        case Scope::Global:
            switch (p) {
                case Param::Range: s.range = x; break;
                case Param::Wander: s.wander = x; break;
                case Param::Accent: s.accent = x; break;
                case Param::GatePercent: s.gatePercent = x; break;
                case Param::OctaveLow: s.octaveLow = x; break;
                case Param::OctaveHigh: s.octaveHigh = x; break;
                case Param::Root: s.root = x; break;
                case Param::Scale: s.scale = x; break;
                case Param::Fill: s.fill = x ? 1 : 0; break;
                case Param::ModeMoves: s.modeMoves = x ? 1 : 0; break;
                default: break;
            }
            break;
        case Scope::Harmony:
            switch (p) {
                case Param::Range: s.harmony.range = x; break;
                case Param::Wander: s.harmony.wander = x; break;
                case Param::Length: s.harmony.length = x; break;
                case Param::ChangesEvery: s.harmony.changesEvery = x; break;
                default: break;
            }
            break;
        case Scope::Group: {
            Group* g = groupById(s, target);
            if (!g) break;
            switch (p) {
                case Param::Range: g->overrides.range = x; break;
                case Param::Wander: g->overrides.wander = x; break;
                case Param::Accent: g->overrides.accent = x; break;
                case Param::GatePercent: g->overrides.gatePercent = x; break;
                case Param::Density: g->density = x; break;
                case Param::Steps: g->steps = x; break;
                case Param::Swing: g->swing = x; break;
                case Param::Offset: g->offset = x; break;
                case Param::SoloMember: g->soloMember = x; break;
                case Param::DealMode: g->deal = static_cast<DealMode>(x); break;
                case Param::Direction: g->direction = static_cast<Direction>(x); break;
                case Param::Probability: g->probability = x; break;
                case Param::Ratchet: g->ratchet = x; break;
                case Param::Repeats: g->repeats = x; break;
                case Param::Handoff: g->handoff = x; break;
                case Param::CleanHandoffs: g->cleanHandoffs = x != 0; break;
                case Param::Fill: g->fillNow = x != 0; break;
                default: break;
            }
            break;
        }
        case Scope::Voice: {
            Voice* v = voiceById(s, target);
            if (!v) break;
            switch (p) {
                case Param::Range: v->overrides.range = x; break;
                case Param::Wander: v->overrides.wander = x; break;
                case Param::OctaveLow: v->overrides.octaveLow = x; break;
                case Param::OctaveHigh: v->overrides.octaveHigh = x; break;
                case Param::Accent: v->overrides.accent = x; break;
                case Param::GatePercent: v->overrides.gatePercent = x; break;
                case Param::Weight: v->weight = x; break;
                case Param::Density: v->density = x; break;
                case Param::Voicing: v->voicing = x; break;
                case Param::ChordSize: v->chordSize = x; break;
                case Param::Articulation: v->articulation = static_cast<Articulation>(x); break;
                case Param::VoiceLeading: v->nearest = x != 0; break;
                case Param::HumanVelocity: v->humanVelocity = x; break;
                case Param::HumanTiming: v->humanTiming = x; break;
                default: break;
            }
            break;
        }
    }
}

// ---- destinations by key (§17.2) ----------------------------------------------------------------------------

using Key = std::tuple<int, std::uint64_t, int, std::string>;
constexpr int kGenParam = -1;  // a Key's param for a generator's parameter, named by its key

Key keyOf(Scope scope, std::uint64_t target, Param p) { return {static_cast<int>(scope), target, static_cast<int>(p), std::string()}; }

// A row's destination: its param, or its key. A legacy key was made its param when the song was read (sanitize).
Key rowKey(const Row& r) {
    if (r.key.empty()) return keyOf(r.scope, r.target, r.param);
    return {static_cast<int>(r.scope), r.target, kGenParam, r.key};
}

using GenDest = GenTarget;
std::optional<GenTarget> genDest(const std::string& key) { return generatorTarget(key); }

// The slot a generator destination moves: a Group's Rhythm, a Lead or Bass voice's Notes. Anywhere else, none.
GenSlot* slotOf(Song& s, Scope scope, std::uint64_t target, SlotKind slot) {
    if (slot == SlotKind::Rhythm && scope == Scope::Group) {
        Group* g = groupById(s, target);
        return g ? &g->rhythmGen : nullptr;
    }
    if (slot == SlotKind::Notes && scope == Scope::Voice) {
        Voice* v = voiceById(s, target);
        return v && v->type != VoiceType::Chord ? &v->notesGen : nullptr;
    }
    return nullptr;
}

// §18: the long-form destinations, by key alone (they have no param number): a group's setting, pick-one.
struct FormKey {
    const char* key;
    int lo, hi;
    int Group::*field;
};
const FormKey* formKey(const std::string& key) {
    static const FormKey keys[] = {{"irama", 0, 5, &Group::irama}, {"tihai", 0, 1, &Group::tihaiNow}};
    for (const FormKey& f : keys)
        if (key == f.key) return &f;
    return nullptr;
}

std::optional<int> get(const Song& s, const Key& k) {
    const auto& [scope, target, p, gen] = k;
    if (p != kGenParam) return get(s, static_cast<Scope>(scope), target, static_cast<Param>(p));
    if (const FormKey* f = formKey(gen)) {
        const Group* g = scope == static_cast<int>(Scope::Group) ? groupById(s, target) : nullptr;
        if (!g) return std::nullopt;
        return g->*(f->field);
    }
    const auto d = genDest(gen);
    const GenSlot* slot = d ? slotOf(const_cast<Song&>(s), static_cast<Scope>(scope), target, d->slot) : nullptr;
    if (!slot) return std::nullopt;
    return paramValue(d->slot, *slot, *d->gen, *d->param);  // whether or not the generator is playing (§17.2)
}

void set(Song& s, const Key& k, int x) {
    const auto& [scope, target, p, gen] = k;
    if (p != kGenParam) return set(s, static_cast<Scope>(scope), target, static_cast<Param>(p), x);
    if (const FormKey* f = formKey(gen)) {
        if (Group* g = scope == static_cast<int>(Scope::Group) ? groupById(s, target) : nullptr) g->*(f->field) = clampi(x, f->lo, f->hi);
        return;
    }
    const auto d = genDest(gen);
    if (GenSlot* slot = d ? slotOf(s, static_cast<Scope>(scope), target, d->slot) : nullptr) slot->params[d->gen->name][d->param->key] = x;
}

std::pair<int, int> spanOf(const Song& s, const Key& k) {
    const auto& [scope, target, p, gen] = k;
    if (p != kGenParam) return spanOf(s, static_cast<Scope>(scope), target, static_cast<Param>(p));
    if (const FormKey* f = formKey(gen)) return {f->lo, f->hi};
    const auto d = genDest(gen);
    return d ? std::pair<int, int>{d->param->lo, d->param->hi} : std::pair<int, int>{0, 0};
}

bool isNumeric(const Key& k) {
    if (std::get<2>(k) != kGenParam) return isNumericParam(static_cast<Param>(std::get<2>(k)));
    const auto d = genDest(std::get<3>(k));
    return d && !d->param->choice;
}
bool isPick(const Key& k) {
    if (std::get<2>(k) != kGenParam) return isPickParam(static_cast<Param>(std::get<2>(k)));
    if (formKey(std::get<3>(k))) return true;
    const auto d = genDest(std::get<3>(k));
    return d && d->param->choice;
}

// §17.2: a cord's destination as the replay's key. None for a key this build doesn't know, and for Mute (a level's
// alone, read apart).
std::optional<Key> destKey(const Dest& d) {
    if (const auto p = paramFromKey(d.key)) return keyOf(d.scope, d.target, *p);
    if (genDest(d.key) || formKey(d.key)) return Key{static_cast<int>(d.scope), d.target, kGenParam, d.key};
    return std::nullopt;
}

// A trigger cord as the row it acts like (§17.3): its destination, Max shift, Bounds, Lasts, Allowed and Seed.
Row rowOf(const Cord& c) {
    Row r;
    r.id = c.id;
    r.scope = c.dest.scope;
    r.target = c.dest.target;
    if (const auto p = paramFromKey(c.dest.key)) r.param = *p;
    else r.key = c.dest.key;
    r.maxShift = c.maxShift;
    r.min = c.min, r.max = c.max;
    r.lasts = c.lasts;
    r.fixedSeed = c.fixedSeed;
    r.allowed = c.allowed;
    r.chance = c.chance;
    return r;
}

std::uint64_t cordH(const Fire& f, const Cord& c) { return hash(f.u, {"cord", c.id}); }  // hc: two cords on one trigger roll apart

// §17.3: whether a trigger cord acts when its source fires: its chance roll.
bool cordActs(const Fire& f, const Cord& cord) { return roll(hash(cordH(f, cord), {"chance"})) < cord.chance; }

// §17.3, As given: amount · Scale, clamped to ±Max shift. Random: none (the row's own roll).
std::optional<int> givenDelta(const Fire& f, const Cord& cord) {
    if (cord.move != 1) return std::nullopt;
    const std::int64_t k = clampi(cord.maxShift, 1, 64);
    return static_cast<int>(std::max(-k, std::min(k, static_cast<std::int64_t>(f.amount) * cord.scale)));
}

std::int64_t rsdiv(std::int64_t a, std::int64_t b) { return a >= 0 ? rdiv(a, b) : -rdiv(-a, b); }  // §2.6

// Bounces off the edges instead of sticking (§7.3).
int reflect(int v, int lo, int hi) {
    if (hi <= lo) return lo;
    const std::int64_t period = 2 * static_cast<std::int64_t>(hi - lo);
    const std::int64_t x = floorMod(v - lo, period);
    return static_cast<int>(lo + (x <= hi - lo ? x : period - x));
}

// −k…+k without 0, uniform.
int shift(std::uint64_t h, int maxShift) {
    const int k = clampi(maxShift, 1, 64);  // wider than any span; keeps 2k far from overflow
    const int x = static_cast<int>(Stream(h).below(static_cast<std::uint64_t>(2 * k)));
    return x < k ? x - k : x - k + 1;
}

// Scale distance (§7.2): how many notes must move, be added or be dropped.
int scaleDistance(int a, int b) {
    const unsigned ma = kScales[a].mask, mb = kScales[b].mask;
    int onlyA = 0, onlyB = 0;
    for (int i = 0; i < 12; ++i) {
        onlyA += ((ma >> i) & 1) && !((mb >> i) & 1);
        onlyB += ((mb >> i) & 1) && !((ma >> i) & 1);
    }
    return std::max(onlyA, onlyB);
}

int seedKey(Param p) { return static_cast<int>(p); }

// The slot a group plays at a step, jitter included (§4.4).
int playedSlot(const Group& g, std::int64_t absStep) {
    const int L = clampi(g.steps, 1, 64);
    int slot = slotAt(absStep, L, g.direction, g.offset);
    if (g.jitter > 0 && roll(hash(g.rhythmSeed, {"jitter", absStep})) < g.jitter) {
        const int side = (hash(g.rhythmSeed, {"jitter", absStep, "side"}) & 1) ? 1 : -1;
        slot = ((slot + side) % L + L) % L;
    }
    return slot;
}

}  // namespace

std::int64_t iramaStep(const Song& s, const Group& g, int level) {
    if (level <= 0) return kDivision[static_cast<std::size_t>(clampi(g.beatDivision, 0, 23))];
    const std::int64_t ct = changeTicks(s);
    std::int64_t n = std::int64_t{1} << (clampi(level, 1, 5) - 1);
    while (n > 1 && ct % n != 0) n /= 2;
    return ct / n;
}

// The addresses, for the form's letters (§18.1).
std::optional<DestKey> addressOf(const Dest& d) { return destKey(d); }
std::optional<int> valueAt(const Song& s, const DestKey& k) { return get(s, k); }
void setValue(Song& s, const DestKey& k, int x) { set(s, k, x); }
std::pair<int, int> spanAt(const Song& s, const DestKey& k) { return spanOf(s, k); }
bool numericKey(const DestKey& k) { return isNumeric(k); }
bool pickKey(const DestKey& k) { return isPick(k); }

}  // namespace hocket::detail

namespace hocket {

using detail::floorMod;

namespace {
// §17.2's table: param n's key is kParamKeys[n]. Frozen (§2.4): new ones are only ever added.
constexpr const char* kParamKeys[] = {
    "range", "wander", "accent", "gatePercent", "octaveLow", "octaveHigh", "density", "steps", "swing", "offset", "weight", "voicing",
    "chordSize", "length", "root", "scale", "soloMember", "deal", "direction", "articulation", "changesEvery", "reroll.rhythm",
    "reroll.pitch", "reroll.deal", "reroll.all", "vary.pitch", "vary.rhythm", "probability", "ratchet", "repeats", "fill", "modeMoves",
    "nearest", "humanVelocity", "humanTiming", "handoff", "cleanHandoffs"};
static_assert(sizeof(kParamKeys) / sizeof(kParamKeys[0]) == static_cast<int>(kLastParam) + 1, "a key for every param");
}  // namespace

const char* paramKey(Param p) {
    const int i = static_cast<int>(p);
    return i >= 0 && i <= static_cast<int>(kLastParam) ? kParamKeys[i] : "";
}

std::optional<Param> paramFromKey(const std::string& key) {
    for (int i = 0; i <= static_cast<int>(kLastParam); ++i)
        if (key == kParamKeys[i]) return static_cast<Param>(i);
    return std::nullopt;
}

bool rowTurn(const Song& song, const Row& row, std::int64_t bar) {
    if (!row.on || bar <= 0) return false;  // the song starts as written: rows act from bar 1 on
    if (row.clock == 2) {  // §18.2: every so many gong cycles, Offset in bars
        const std::int64_t period = std::max<std::int64_t>(1, row.every) * gongBars(song);
        return floorMod(bar, period) == floorMod(row.offset, period);
    }
    if (row.clock == 3) {  // §18.2: every so many kenong strokes, counted from bar 0
        if (!kenongAt(song, bar)) return false;
        const std::int64_t every = std::max(1, row.every);
        return floorMod(kenongStrokes(song, bar) - 1, every) == floorMod(row.offset, every);
    }
    const std::int64_t period = std::max<std::int64_t>(1, static_cast<std::int64_t>(row.every) * (row.inPhrases ? std::max(1, song.phraseLength) : 1));
    return floorMod(bar, period) == floorMod(row.offset, period);
}

bool fires(const Song& song, const Row& row, std::int64_t bar) {
    if (!rowTurn(song, row, bar)) return false;
    // The chance roll is on the matrix's own seed (§7.3), which Generate re-rolls: not the master seed.
    return roll(hash(hash(song.matrixSeed, {"matrix", row.id, bar}), {"chance"})) < row.chance;
}

Firing nextFiring(const Song& song, const Row& row, std::int64_t bar, int turns) {
    Firing f;
    if (!row.on) return f;
    const std::int64_t period = std::max<std::int64_t>(1, static_cast<std::int64_t>(row.every) * (row.inPhrases ? std::max(1, song.phraseLength) : 1));
    std::int64_t b = std::max<std::int64_t>(1, bar);
    b += floorMod(floorMod(row.offset, period) - b, period);  // the row's first turn at or after `bar` (never bar 0)
    for (int t = 0; t < turns; ++t, b += period) {
        if (fires(song, row, b)) {
            f.bar = b;
            return f;
        }
        ++f.skipped;
    }
    return f;
}

Firing nextCordFiring(const Song& song, const Cord& cord, std::int64_t bar, int turns) {
    Firing f;
    // A cord that can't act (off, a level, a source or output that's gone, Mute, a key this build doesn't know).
    const ModSource* src = sourceById(song, cord.source);
    if (!cord.on || !src || cord.dest.key == "mute" || !detail::destKey(cord.dest)) return f;
    // Compiled once. Each turn is the output's own next firing: closed form for a convergence, however far; for a
    // scanned output, kScanBars bars from `bar` in all. So no song file can make this answer slow.
    const CompiledOutput out = compileOutput(song, *src, cord.output);
    if (!out.valid || out.kind != OutputKind::Trigger) return f;
    const std::int64_t limit = out.exact ? std::numeric_limits<std::int64_t>::max() / 4 : std::max<std::int64_t>(1, bar) + kScanBars;
    std::int64_t from = std::max<std::int64_t>(1, bar);
    for (int t = 0; t < turns; ++t) {
        const auto b = out.next(from, limit);
        if (!b) break;
        const std::optional<Fire> fire = out.fire(*b);
        if (fire && detail::cordActs(*fire, cord)) {
            f.bar = *b;
            return f;
        }
        ++f.skipped;
        from = *b + 1;
    }
    return Firing{};
}

}  // namespace hocket

namespace hocket::detail {

void fromVoices(Song& s);  // §5.8, below

std::vector<int> progressionOf(const Song& song, const Mutations& mut) {
    // §3.3's note array over the changes, with change 0 moved to the front of the order: it is always a root, so a
    // progression starts at home, and Wander decides how far the rest go.
    const Harmony& h = song.harmony;
    const int L = clampi(h.length, 1, 16);
    const int ps = poolSize(static_cast<int>(ranked(song.root, song.root, song.scale).size()), h.range);
    const int wander = clampi(h.wander, 1, 16);
    std::vector<int> P = priorityOrder(hash(h.seed, {"progression"}), L);
    const auto zero = std::find(P.begin(), P.end(), 0);
    std::rotate(P.begin(), zero, zero + 1);
    const int roots = ps == 1 ? L : std::max(1, static_cast<int>(rdiv(static_cast<std::int64_t>(L) * (17 - wander), 16)));
    std::vector<int> idx(L, 0);
    // Which chords it uses is the seed's too: the non-tonic changes cycle through the pool in an order of its own (a
    // priority order, so a bigger pool adds chords without reshuffling the rest). Taking the pool's first chords in
    // turn, every seed played the same few.
    std::vector<int> Q;
    if (ps > 1) Q = priorityOrder(hash(h.seed, {"chords"}), ps - 1);
    for (int j = 0; j < L; ++j) {
        const int k = ps > 1 ? (L - 1 - j) % (ps - 1) : 0;
        idx[P[j]] = j < roots ? 0 : 1 + (Q.empty() ? k : Q[static_cast<std::size_t>(k)]);
    }
    // §3.8, Mode moves: Wander still says which changes are home; the others are chosen in order, each by weight from
    // the chord before it (and whether the next change is home), with a draw of its own.
    if (song.modeMoves != 0 && song.colour > 0 && ps > 1) {
        const std::vector<int> rk = harmonyRanked(song);
        std::vector<bool> home(static_cast<std::size_t>(L), false);
        for (int j = 0; j < roots && j < L; ++j) home[static_cast<std::size_t>(P[j])] = true;
        for (int k = 1; k < L; ++k) {
            if (home[static_cast<std::size_t>(k)]) continue;
            const int prev = rk[static_cast<std::size_t>(idx[static_cast<std::size_t>(k - 1)])];
            const bool nextHome = home[static_cast<std::size_t>((k + 1) % L)];
            std::vector<int> w;
            std::uint64_t total = 0;
            for (int c = 1; c < ps; ++c) {
                w.push_back(moveWeight(song, prev, rk[static_cast<std::size_t>(c)], nextHome));
                total += static_cast<std::uint64_t>(w.back());
            }
            std::uint64_t r = Stream(hash(h.seed, {"moves", k})).below(total);
            int c = 1;
            while (r >= static_cast<std::uint64_t>(w[static_cast<std::size_t>(c - 1)])) r -= static_cast<std::uint64_t>(w[static_cast<std::size_t>(c - 1)]), ++c;
            idx[static_cast<std::size_t>(k)] = c;
        }
    }
    for (const auto& [k, u] : mut.harmony)
        if (k > 0 && k < L) idx[k] = static_cast<int>(u % static_cast<std::uint64_t>(ps));
    return idx;
}

std::vector<bool> hitsOf(const Group& g, const Mutations& mut) { return hitsOf(g, mut, g.density); }

std::vector<bool> hitsOf(const Group& g, const Mutations& mut, int density) {
    const int L = clampi(g.steps, 1, 64);
    std::vector<bool> h = rhythmHits(g, density);  // the group's Rhythm generator (§16.4)
    const auto it = mut.rhythm.find(g.id);
    if (it != mut.rhythm.end())
        for (const auto& [slot, cell] : it->second)
            if (slot < L) h[slot] = cell.hit;
    return h;
}

Replay::Replay(const Song& base) : base_(base), plain_(base) {
    plain_.mod = Mod();
    const std::size_t rows = base.matrix.size();
    recs_.assign(rows + base.mod.cords.size(), ItemRec{});
    itemPos_.assign(recs_.size(), static_cast<std::size_t>(-1));
    for (std::size_t r = 0; r < rows; ++r) {
        byId_.emplace(base.matrix[r].id, r);
        fillConds_ = fillConds_ || base.matrix[r].cond.kind == Condition::Kind::Fill;
    }
    for (const Cord& c : base.mod.cords) {
        CordPlan p;
        if (const ModSource* src = sourceById(base, c.source)) p.out = compileOutput(base, *src, c.output);
        p.key = destKey(c.dest);
        p.mute = c.dest.key == "mute";
        p.levelOk = levelMayDrive(c.dest.key);
        p.row = rowOf(c);
        plans_.push_back(std::move(p));
    }
    // §17.5's items: the rows and the trigger cords that are on, in §17.4's order. Levels and items that are off aren't.
    for (std::size_t i = 0; i < recs_.size(); ++i) {
        const bool item = i < rows ? base.matrix[i].on
                                   : base.mod.cords[i - rows].on && plans_[i - rows].out.valid && plans_[i - rows].out.kind == OutputKind::Trigger;
        if (i >= rows) {
            byId_.emplace(base.mod.cords[i - rows].id, i);
            fillConds_ = fillConds_ || base.mod.cords[i - rows].cond.kind == Condition::Kind::Fill;
        }
        if (item) itemPos_[i] = items_.size(), items_.push_back(i);
    }
}

bool Replay::passes(const Condition& c, std::size_t i, std::int64_t n, std::int64_t bar, Scope scope, std::uint64_t target,
                    const State* fillState) const {
    using K = Condition::Kind;
    bool pass = true;
    auto actedAtLatest = [&](std::size_t j) { return recs_[j].last >= 0 && recs_[j].acted; };  // false before any opportunity
    switch (c.kind) {
        case K::None: return true;
        case K::Unknown: return false;  // a condition this build doesn't know: the item does nothing
        case K::AB: {
            const int B = clampi(c.b, 1, 8), A = clampi(c.a, 1, B);
            pass = floorMod(n, B) == A - 1;
            break;
        }
        case K::First: pass = n == 0; break;
        case K::Pre: {
            const std::size_t at = itemPos_[i];
            pass = at != static_cast<std::size_t>(-1) && at > 0 && actedAtLatest(items_[at - 1]);  // the item just before
            break;
        }
        case K::Nei: {
            const auto it = byId_.find(c.item);
            pass = it != byId_.end() && itemPos_[it->second] != static_cast<std::size_t>(-1) && actedAtLatest(it->second);
            break;
        }
        case K::Gong: pass = gongAt(base_, bar); break;      // §18.2
        case K::Kenong: pass = kenongAt(base_, bar); break;
        case K::Fill: {
            // §4.6 on the state before this bar's triggers: the song's Fill, or the destination's group filling.
            const Song& s = fillState->song;
            pass = s.fill != 0;
            const Group* g = nullptr;
            if (scope == Scope::Group) g = groupById(s, target);
            if (scope == Scope::Voice) voiceById(s, target, &g);
            if (g && !pass) {
                const std::int64_t P = static_cast<std::int64_t>(clampi(g->fillEvery, 0, 16)) * std::max(1, s.phraseLength);
                pass = g->fillNow || (P > 0 && floorMod(bar, P) >= P - std::min<std::int64_t>(clampi(g->fillBars, 1, 16), P));
            }
            break;
        }
    }
    return c.negate ? !pass : pass;
}

const State& Replay::at(std::int64_t bar) {
    bar = std::max<std::int64_t>(0, bar);
    while (static_cast<std::int64_t>(states_.size()) <= bar) advance();
    return states_[static_cast<std::size_t>(bar)];
}

State Replay::build(std::int64_t) const {
    State st;
    st.song = plain_;
    Song& s = st.song;
    st.mut = perm_;
    for (const Layer& l : layers_) st.layers.push_back(l.item);
    auto applySeed = [&](const Key& k, std::uint64_t seed, Mutations& mut) {
        const auto& [scope, target, p, gen] = k;
        if (scope == static_cast<int>(Scope::Harmony)) {
            s.harmony.seed = seed;
            mut.harmony.clear();
        } else if (scope == static_cast<int>(Scope::Group)) {
            if (Group* g = groupById(s, target)) {
                if (p == seedKey(Param::RerollRhythm)) g->rhythmSeed = seed, mut.rhythm.erase(target);
                else g->dealSeed = seed;
            }
        } else if (Voice* v = voiceById(s, target)) {
            v->pitchSeed = seed;
            mut.pitch.erase(target);
        }
    };
    for (const auto& [k, seed] : seeds_) applySeed(k, seed, st.mut);
    for (const auto& [k, c] : choices_) set(s, k, c);
    for (const auto& [k, n] : offsets_) {
        if (std::get<2>(k) == static_cast<int>(Param::Root)) {
            s.root = static_cast<int>(floorMod(base_.root + 7 * n.offset, 12));
            continue;
        }
        const std::optional<int> b = get(base_, k);
        if (b) set(s, k, reflect(*b + n.offset, n.lo, n.hi));
    }
    for (const Layer& l : layers_) {
        for (const auto& [k, seed] : l.change.seeds) applySeed(k, seed, st.mut);
        for (const auto& [k, c] : l.change.choices) set(s, k, c);
        for (const auto& [k, d] : l.change.deltas) {
            if (std::get<2>(k) == static_cast<int>(Param::Root)) {
                s.root = static_cast<int>(floorMod(s.root + 7 * d, 12));
                continue;
            }
            const std::optional<int> cur = get(s, k);
            if (cur) set(s, k, reflect(*cur + d, l.change.lo, l.change.hi));
        }
        for (const auto& [v, slots] : l.change.mut.pitch)
            for (const auto& [slot, u] : slots) st.mut.pitch[v][slot] = u;
        for (const auto& [g, slots] : l.change.mut.rhythm)
            for (const auto& [slot, cell] : slots) st.mut.rhythm[g][slot] = cell;
        for (const auto& [k, u] : l.change.mut.harmony) st.mut.harmony[k] = u;
        for (const RegisterFlip& f : l.change.mut.registers) st.mut.registers.push_back(f);  // after the permanent ones, in layer order
    }
    return st;
}

Replay::Change Replay::fire(const Row& row, std::int64_t bar, std::uint64_t h, std::uint64_t fixed, std::optional<int> given,
                            const State& pre) const {
    const Song& s = pre.song;
    Change c;
    const Key key = rowKey(row);
    if (std::get<2>(key) == kGenParam && !genDest(row.key) && !formKey(row.key)) return c;  // a key this build doesn't know: nothing
    const std::uint64_t seed = row.fixedSeed ? fixed : h;
    // §17.3, As given: amount · Scale, clamped to ±Max shift. A delta of 0 does nothing at all (no layer either).
    if (given && *given == 0 && (isNumeric(key) || (std::get<2>(key) == static_cast<int>(Param::Root)))) {
        c.skip = true;
        return c;
    }
    auto delta = [&] { return given ? *given : shift(hash(h, {"amount"}), row.maxShift); };
    const std::int64_t bt = barTicks(s);
    const int k = clampi(row.maxShift, 1, 64);

    // Uniform among the candidates, from the row's "choice" roll.
    auto choose = [&](const std::vector<int>& cands) -> std::optional<int> {
        if (cands.empty()) return std::nullopt;
        return cands[static_cast<std::size_t>(Stream(hash(h, {"choice"})).below(cands.size()))];
    };

    const bool byParam = std::get<2>(key) != kGenParam;
    if (byParam && row.param == Param::Root) {
        c.deltas[key] = delta();
    } else if (byParam && row.param == Param::Scale) {
        std::vector<int> cands;
        for (int x : row.allowed)
            if (x >= 0 && x < static_cast<int>(kScales.size()) && x != s.scale) {
                const int d = scaleDistance(s.scale, x);
                if (d >= 1 && d <= k) cands.push_back(x);
            }
        if (const auto x = choose(cands)) c.choices[key] = *x;
    } else if (isNumeric(key)) {
        const auto [lo, hi] = spanOf(s, key);
        c.lo = row.min ? std::max(lo, *row.min) : lo;
        c.hi = row.max ? std::min(hi, *row.max) : hi;
        if (get(s, key)) c.deltas[key] = delta();
    } else if (isPick(key)) {
        const std::optional<int> cur = get(s, key);
        if (cur) {
            const auto [lo, hi] = spanOf(s, key);
            std::vector<int> options;
            if (row.allowed.empty())
                for (int x = lo; x <= hi; ++x) options.push_back(x);
            else
                for (int x : row.allowed)
                    if (x >= lo && x <= hi) options.push_back(x);
            std::sort(options.begin(), options.end());
            options.erase(std::unique(options.begin(), options.end()), options.end());
            // Distance is places in the list of options (§7.2); the current value may not be in it.
            const auto at = std::lower_bound(options.begin(), options.end(), *cur) - options.begin();
            const bool in = at < static_cast<std::ptrdiff_t>(options.size()) && options[at] == *cur;
            std::vector<int> cands;
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (in && static_cast<std::ptrdiff_t>(i) == at) continue;
                const std::int64_t d = in ? std::abs(static_cast<std::int64_t>(i) - at) : 1;
                if (d <= k || k >= static_cast<int>(options.size())) cands.push_back(options[i]);
            }
            if (const auto x = choose(cands)) c.choices[key] = *x;
        }
    } else if (!byParam) {
        // a generator's parameter is a number or a choice: nothing else
    } else if (row.param >= Param::RerollRhythm && row.param <= Param::RerollAll) {
        const bool rhythm = row.param == Param::RerollRhythm || row.param == Param::RerollAll;
        const bool deal = row.param == Param::RerollDeal || row.param == Param::RerollAll;
        const bool pitch = row.param == Param::RerollPitch || row.param == Param::RerollAll;
        auto group = [&](const Group& g) {
            if (rhythm) c.seeds[keyOf(Scope::Group, g.id, Param::RerollRhythm)] = hash(seed, {"rhythm", g.id});
            if (deal) c.seeds[keyOf(Scope::Group, g.id, Param::RerollDeal)] = hash(seed, {"deal", g.id});
            if (pitch)
                for (const Voice& v : g.voices)
                    c.seeds[keyOf(Scope::Voice, v.id, Param::RerollPitch)] = hash(seed, {"pitch", v.id});
        };
        if (row.scope == Scope::Harmony) {
            c.seeds[keyOf(Scope::Harmony, 0, Param::RerollAll)] = hash(seed, {"harmony"});
        } else if (row.scope == Scope::Global) {
            for (const Group& g : s.groups) group(g);
            if (row.param == Param::RerollAll) c.seeds[keyOf(Scope::Harmony, 0, Param::RerollAll)] = hash(seed, {"harmony"});
        } else if (row.scope == Scope::Group) {
            if (const Group* g = groupById(s, row.target)) group(*g);
        } else if (const Voice* v = voiceById(s, row.target)) {
            if (pitch) c.seeds[keyOf(Scope::Voice, v->id, Param::RerollPitch)] = hash(seed, {"pitch", v->id});
        }
    } else if (row.param == Param::VaryPitch || row.param == Param::VaryRhythm) {
        // A window longer than any pattern touches every slot anyway: cap it, so no row can make this loop long.
        const std::int64_t start = bar * bt, end = (bar + std::min(row.lasts, 64)) * bt;
        // The slots a group plays in the layer's window (§7.5); a permanent vary may touch any slot.
        auto window = [&](const Group& g) {
            const int L = clampi(g.steps, 1, 64);
            std::vector<bool> in(L, row.lasts == 0);
            if (row.lasts > 0) {
                const auto it = pre.anchor.find(g.id);
                const std::int64_t anchor = it == pre.anchor.end() ? 0 : it->second;
                const std::int64_t st = stepTicksIn(pre, g);
                for (std::int64_t a = ceilDiv(start - anchor, st); anchor + a * st < end; ++a) in[playedSlot(g, a)] = true;
            }
            return in;
        };
        // §16.2, §16.7: on a clocked generator Vary doesn't rewrite slots. A Register's bits are flipped instead; any
        // other clocked generator is left alone, and a Vary with nothing but those does nothing at all.
        int targets = 0, skipped = 0;
        auto stepT0 = [&](const Group& g) {  // the t of the group's first step at or after this bar line
            const auto a = pre.anchor.find(g.id);
            const auto b = pre.stepsBefore.find(g.id);
            const std::int64_t anchor = a == pre.anchor.end() ? 0 : a->second, before = b == pre.stepsBefore.end() ? 0 : b->second;
            return before + ceilDiv(bar * bt - anchor, stepTicksIn(pre, g));
        };
        auto flipRegister = [&](int kind, const Group& g, std::uint64_t target, std::uint64_t regSeed, const GenSlot& slot) {
            const SlotKind sk = kind == 0 ? SlotKind::Rhythm : SlotKind::Notes;
            RegisterFlip f;
            f.kind = kind, f.target = target, f.seed = regSeed, f.t0 = stepT0(g);
            f.back = registerVary(seed, target, k, paramValue(sk, slot, "register", "length"));
            c.mut.registers.push_back(f);
        };
        // §16.6: a derived generator whose source isn't live plays the default, and varies as it.
        auto kind = [&](SlotKind sk, const Group& g, const Voice* v) {
            const std::size_t gi = static_cast<std::size_t>(&g - s.groups.data());
            return kindIn(s, sk, gi, v ? static_cast<int>(v - g.voices.data()) : -1);
        };
        auto clockedSkip = [&](SlotKind sk, const GenSlot& slot, GenKind gk, const std::function<void()>& onRegister) {
            if (gk != GenKind::Clocked) return false;
            if (std::string(playing(sk, slot).name) == "register") onRegister();
            else ++skipped;
            return true;
        };
        auto varyVoice = [&](const Group& g, const Voice& v) {
            if (v.type == VoiceType::Chord) return;
            ++targets;
            if (clockedSkip(SlotKind::Notes, v.notesGen, kind(SlotKind::Notes, g, &v), [&] { flipRegister(1, g, v.id, v.pitchSeed, v.notesGen); })) return;
            const int L = clampi(g.steps, 1, 64);
            const std::vector<bool> in = window(g);
            const std::vector<int> P = priorityOrder(hash(v.pitchSeed, {"notes"}), L);
            int left = k;
            for (auto it = P.rbegin(); it != P.rend() && left > 0; ++it)
                if (in[*it]) c.mut.pitch[v.id][*it] = hash(seed, {v.id, *it}), --left;
        };
        auto varyRhythm = [&](const Group& g) {
            ++targets;
            if (clockedSkip(SlotKind::Rhythm, g.rhythmGen, kind(SlotKind::Rhythm, g, nullptr), [&] { flipRegister(0, g, g.id, g.rhythmSeed, g.rhythmGen); })) return;
            const int L = clampi(g.steps, 1, 64);
            const std::vector<bool> in = window(g);
            const std::vector<bool> now = hitsOf(g, pre.mut);
            std::vector<int> removable, addable;
            const std::vector<int> P = priorityOrder(hash(g.rhythmSeed, {"hits"}), L);
            for (auto it = P.rbegin(); it != P.rend(); ++it)
                if (in[*it] && now[*it]) removable.push_back(*it);
            for (int slot : priorityOrder(hash(seed, {"rhythm"}), L))
                if (in[slot] && !now[slot]) addable.push_back(slot);
            const int n = std::min<int>(k, static_cast<int>(std::min(removable.size(), addable.size())));
            for (int i = 0; i < n; ++i) {
                c.mut.rhythm[g.id][removable[i]] = RhythmCell{false, false, 1};
                c.mut.rhythm[g.id][addable[i]] = RhythmCell{true, accentAt(seed, addable[i]), gateAt(seed, addable[i], g.density)};
            }
        };
        if (row.param == Param::VaryRhythm) {
            for (const Group& g : s.groups)
                if (row.scope == Scope::Global || (row.scope == Scope::Group && g.id == row.target)) varyRhythm(g);
        } else if (row.scope == Scope::Harmony) {
            // Vary Harmony: some of the progression's changes, never change 0 (it stays home).
            const int L = clampi(s.harmony.length, 1, 16);
            std::vector<bool> in(L, row.lasts == 0);
            if (row.lasts > 0) {
                const std::int64_t ct = changeTicks(s);
                for (std::int64_t t = pre.harmonyAnchor + ceilDiv(start - pre.harmonyAnchor, ct) * ct; t < end; t += ct)
                    in[static_cast<std::size_t>(floorMod(floorDiv(t - pre.harmonyAnchor, ct), L))] = true;
            }
            std::vector<int> P = priorityOrder(hash(s.harmony.seed, {"progression"}), L);
            const auto zero = std::find(P.begin(), P.end(), 0);
            std::rotate(P.begin(), zero, zero + 1);
            int left = k;
            for (auto it = P.rbegin(); it != P.rend() && left > 0; ++it)
                if (*it != 0 && in[*it]) c.mut.harmony[*it] = hash(seed, {"harmony", *it}), --left;
        } else {
            for (const Group& g : s.groups)
                for (const Voice& v : g.voices)
                    if (row.scope == Scope::Global || (row.scope == Scope::Group && g.id == row.target) ||
                        (row.scope == Scope::Voice && v.id == row.target))
                        varyVoice(g, v);
        }
        c.skip = targets > 0 && skipped == targets;
    }
    return c;
}

void Replay::advance() {
    const std::int64_t b = static_cast<std::int64_t>(states_.size());
    // First, the layers whose time is up go.
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&](const Layer& l) { return l.until <= b; }), layers_.end());
    auto anchored = [&](State& st) {
        // §18.3: the irama a group plays is the one its state had at the last gong; its step follows from it.
        const bool gong = gongAt(base_, b);
        for (const Group& g : st.song.groups) {
            const auto was = states_.empty() ? st.irama.end() : states_.back().irama.find(g.id);
            st.irama[g.id] = gong || states_.empty() || was == states_.back().irama.end() ? clampi(g.irama, 0, 5) : was->second;
            if (st.irama[g.id] > 0) st.step[g.id] = iramaStep(st.song, g, st.irama[g.id]);
        }
        if (states_.empty()) {
            for (const Group& g : st.song.groups) st.anchor[g.id] = 0, st.stepsBefore[g.id] = 0;
            st.harmonyAnchor = 0;
            return;
        }
        const State& prev = states_.back();
        const std::int64_t bt = barTicks(st.song);
        for (const Group& g : st.song.groups) {
            const Group* p = groupById(prev.song, g.id);
            const bool moved = !p || p->steps != g.steps || p->beatDivision != g.beatDivision || p->offset != g.offset ||
                               p->direction != g.direction || stepTicksIn(prev, *p) != stepTicksIn(st, g);
            st.anchor[g.id] = moved ? b * bt : prev.anchor.at(g.id);
            // §16.3: the steps of the stretch that ends here, at its own step, join the count before the anchor.
            const std::int64_t before = prev.stepsBefore.count(g.id) ? prev.stepsBefore.at(g.id) : 0;
            st.stepsBefore[g.id] = moved && p ? before + ceilDiv(b * bt - prev.anchor.at(g.id), stepTicksIn(prev, *p)) : before;
        }
        const bool moved = prev.song.harmony.changesEvery != st.song.harmony.changesEvery || prev.song.harmony.length != st.song.harmony.length;
        st.harmonyAnchor = moved ? b * bt : prev.harmonyAnchor;
    };

    // §17.4, step 2: the matrix's rows in matrix order, then the trigger cords in mod.cords order. Each item's turn is
    // its clock, then its condition (§17.5), then its chance: a failed condition rolls nothing, and still counts.
    std::vector<std::uint8_t> acts(recs_.size(), 0);
    std::optional<State> fillState;
    if (fillConds_ && b > 0) fillState = build(b);
    auto turn = [&](std::size_t i, const Condition& cond, Scope scope, std::uint64_t target, bool chanceOk) {
        ItemRec& rec = recs_[i];
        const bool acted = passes(cond, i, rec.count, b, scope, target, fillState ? &*fillState : nullptr) && chanceOk;
        rec.count++, rec.last = b, rec.acted = acted;
        acts[i] = acted ? 2 : 1;
        return acted;
    };
    for (std::size_t r = 0; r < base_.matrix.size(); ++r) {
        const Row& row = base_.matrix[r];
        if (!rowTurn(base_, row, b)) continue;
        const std::uint64_t h = hash(base_.matrixSeed, {"matrix", row.id, b});
        // The chance roll is drawn only if the condition passes (it's §7.3's own roll, so it's the same either way).
        if (!turn(r, row.cond, row.scope, row.target, roll(hash(h, {"chance"})) < row.chance)) continue;
        State pre = build(b);
        anchored(pre);
        Change c = fire(row, b, h, hash(base_.matrixSeed, {"matrix", row.id}), std::nullopt, pre);
        if (c.skip) continue;
        commit(r, b, row.lasts, std::move(c));
    }
    for (std::size_t ci = 0; ci < base_.mod.cords.size(); ++ci) {
        const Cord& cord = base_.mod.cords[ci];
        const CordPlan& plan = plans_[ci];
        // A trigger on Mute, or on a key this build doesn't know, does nothing (§17.3).
        if (!cord.on || !plan.out.valid || plan.out.kind != OutputKind::Trigger) continue;
        const std::optional<Fire> f = plan.out.fire(b);
        if (!f) continue;
        const std::size_t i = base_.matrix.size() + ci;
        if (!turn(i, cord.cond, cord.dest.scope, cord.dest.target, cordActs(*f, cord))) continue;
        if (plan.mute || !plan.key) continue;  // it acted, and does nothing: a trigger on Mute, or a key this build doesn't know
        State pre = build(b);
        anchored(pre);
        Change c = fire(plan.row, b, cordH(*f, cord), hash(base_.matrixSeed, {"mod", cord.id}), givenDelta(*f, cord), pre);
        if (c.skip) continue;
        commit(base_.matrix.size() + ci, b, plan.row.lasts, std::move(c));
    }
    State st = build(b);
    anchored(st);
    st.acts = std::move(acts);
    levels(st, b);  // §17.4, step 3: on top of what the triggers built; never part of an offset or a layer
    fromVoices(st.song);  // §5.8: last, so the members take their places in the Density the bar ended up with
    states_.push_back(std::move(st));
}

void Replay::commit(std::size_t item, std::int64_t b, int lasts, Change c) {
    if (lasts == 0) {
        for (const auto& [k, d] : c.deltas) {
            Numeric& n = offsets_[k];
            if (static_cast<Param>(std::get<2>(k)) == Param::Root) {
                n.offset = static_cast<int>(floorMod(n.offset + d, 12));
                continue;
            }
            n.lo = c.lo, n.hi = c.hi;  // the last mover's Bounds stay with the offset (§17.4)
            const std::optional<int> base = get(base_, k);
            if (base) n.offset = reflect(*base + n.offset + d, n.lo, n.hi) - *base;
        }
        for (const auto& [k, x] : c.choices) choices_[k] = x;
        for (const auto& [k, seed] : c.seeds) {
            seeds_[k] = seed;
            const auto& [scope, target, p, gen] = k;
            if (scope == static_cast<int>(Scope::Harmony)) perm_.harmony.clear();
            else if (scope == static_cast<int>(Scope::Voice)) perm_.pitch.erase(target);
            else if (p == seedKey(Param::RerollRhythm)) perm_.rhythm.erase(target);
        }
        for (const auto& [v, slots] : c.mut.pitch)
            for (const auto& [slot, u] : slots) perm_.pitch[v][slot] = u;
        for (const auto& [g, slots] : c.mut.rhythm)
            for (const auto& [slot, cell] : slots) perm_.rhythm[g][slot] = cell;
        for (const auto& [k, u] : c.mut.harmony) perm_.harmony[k] = u;
        for (const RegisterFlip& f : c.mut.registers) perm_.registers.push_back(f);
    } else {
        // An item that acts again while its layer is active replaces it; layers apply in §17.4's order.
        layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&](const Layer& l) { return l.item == item; }), layers_.end());
        layers_.push_back({item, b + lasts, std::move(c)});
        std::stable_sort(layers_.begin(), layers_.end(), [](const Layer& x, const Layer& y) { return x.item < y.item; });
    }
}

// §5.8, Density from voices: what the group plays is worked out from its members. The bar's own copy only: the
// base, the offsets and the layers keep the group's Density and the members' Weights as they were set.
void fromVoices(Song& s) {
    for (Group& g : s.groups) {
        if (!g.densityFromVoices) continue;
        int sum = 0;
        for (Voice& v : g.voices) {
            const int d = v.mute ? 0 : clampi(v.density, 0, 16);
            v.weight = d;                // in place of Weight (§5.2)
            if (d == 0) v.mute = true;   // a member at 0 sits out, in every deal mode
            sum += d;
        }
        const int D = clampi(g.density, 1, 16);
        g.density = sum >= 16 ? D : std::max(1, static_cast<int>(rdiv(static_cast<std::int64_t>(D) * sum, 16)));
    }
}

void Replay::levels(State& st, std::int64_t b) const {
    Song& s = st.song;
    std::map<Key, int> sum;  // each destination's contributions, in the order they're first met
    std::vector<Key> order;
    std::vector<std::pair<Scope, std::uint64_t>> muted;
    for (std::size_t ci = 0; ci < base_.mod.cords.size(); ++ci) {
        const Cord& cord = base_.mod.cords[ci];
        const CordPlan& plan = plans_[ci];
        if (!cord.on || !plan.out.valid || plan.out.kind != OutputKind::Level) continue;  // not a level (a trigger, or gone)
        const int level = plan.out.level(b);
        if (plan.mute) {
            if (cord.dest.scope != Scope::Group && cord.dest.scope != Scope::Voice) continue;
            if (cord.invert ? level >= cord.threshold : level < cord.threshold) muted.push_back({cord.dest.scope, cord.dest.target});
            continue;
        }
        if (!plan.levelOk || !plan.key) continue;
        const Key& k = *plan.key;
        if (!sum.count(k)) order.push_back(k);
        sum[k] += static_cast<int>(rsdiv(static_cast<std::int64_t>(level) * cord.depth, 1000));
    }
    // v is the value after the triggers, for every destination at once: a level on a group's Range doesn't move the
    // v of a level on one of its voices.
    std::vector<std::optional<int>> v;
    for (const Key& k : order) v.push_back(get(s, k));
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!v[i]) continue;
        const auto [lo, hi] = spanOf(s, order[i]);
        set(s, order[i], clampi(*v[i] + sum[order[i]], lo, hi));  // clamped, not reflected: it lets go as the level comes back
    }
    for (const auto& [scope, target] : muted) {
        if (scope == Scope::Group) {
            if (Group* g = groupById(s, target)) g->mute = true;
        } else if (Voice* x = voiceById(s, target)) {
            x->mute = true;
        }
    }
}

}  // namespace hocket::detail
