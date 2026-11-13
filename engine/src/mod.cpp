// Modulation sources (SPEC §17.1, §17.9): their parameters, their outputs, and what each output does at a bar.
#include "hocket/mod.hpp"

#include "hocket/form.hpp"
#include "hocket/song.hpp"

#include <algorithm>
#include <memory>

#include "hocket/generators.hpp"
#include "hocket/rng.hpp"
#include "hocket/song.hpp"
#include "internal.hpp"

namespace hocket {

using detail::clampi;
using detail::floorDiv;
using detail::floorMod;

namespace {

const int kLfoPeriods[4] = {17, 19, 23, 29};  // §17.9: periods that rarely line up

// Appendix B's order. Frozen: a kind's name, its keys and its arithmetic, once released (§2.4).
const std::vector<SourceKind>& kinds() {
    static const std::vector<SourceKind> k = {
        {"lfo",
         "LFOs",
         {{"count", "LFOs", 1, 4, 2}},
         "lfos",
         4,
         {{"period", "Period", 1, 64, 17, false, "", kLfoPeriods},
          {"unit", "Unit", 0, 1, 0, true, "bars|phrases"},
          {"shape", "Shape", 0, 3, 0, true, "Triangle|Saw up|Saw down|Square"},
          {"phase", "Phase", -1, 4095, 0}},
         "converge"},
        {"accumulator",
         "Accumulator",
         {{"every", "Every", 1, 256, 1},
          {"inPhrases", "Unit", 0, 1, 1, true, "bars|phrases"},
          {"offset", "Offset", -256, 256, 0},
          {"step", "Step", -16, 16, 1},
          {"positions", "Positions", 2, 64, 8},
          {"start", "Start", 0, 63, 0},
          {"edge", "Edge", 0, 2, 1, true, "Wrap|Pendulum|Hold"},
          {"reset", "Reset after", 0, 64, 0}},
         "", 0, {}, ""},
        {"dice", "1/f dice", {{"dice", "Dice", 1, 8, 5}, {"unit", "Unit", 0, 1, 0, true, "bars|phrases"}}, "", 0, {}, ""},
        {"arc", "Energy arc", {{"span", "Span", 8, 256, 64}, {"end", "End", 0, 1, 0, true, "Loop|Hold"}}, "", 0, {}, ""},
        // §18.2: the colotomic clock as a source. 0: the song's gong cycle and kenongs; otherwise its own.
        {"gong", "Gong cycle", {{"cycle", "Gong cycle", 0, 16, 0}, {"kenongs", "Kenongs", 0, 16, 0}}, "", 0, {}, ""},
    };
    return k;
}

const SourceParam* paramOf(const SourceKind& k, const char* key) {
    for (const SourceParam& p : k.params)
        if (std::string(p.key) == key) return &p;
    return nullptr;
}
const SourceParam* perOf(const SourceKind& k, const char* key) {
    for (const SourceParam& p : k.per)
        if (std::string(p.key) == key) return &p;
    return nullptr;
}

// "lfo3" → 2; anything else → −1.
int lfoIndex(const std::string& output) {
    if (output.size() != 4 || output.compare(0, 3, "lfo") != 0 || output[3] < '1' || output[3] > '4') return -1;
    return output[3] - '1';
}

}  // namespace

const std::vector<SourceKind>& sourceKinds() { return kinds(); }

const SourceKind* findSourceKind(const std::string& name) {
    for (const SourceKind& k : kinds())
        if (name == k.name) return &k;
    return nullptr;
}

int sourceValue(const ModSource& s, const char* key) {
    const SourceKind* k = findSourceKind(s.kind);
    const SourceParam* p = k ? paramOf(*k, key) : nullptr;
    if (!p) return 0;
    const auto it = s.params.find(key);
    return it == s.params.end() ? p->def : clampi(it->second, p->lo, p->hi);
}

int itemValue(const ModSource& s, int i, const char* key) {
    const SourceKind* k = findSourceKind(s.kind);
    const SourceParam* p = k ? perOf(*k, key) : nullptr;
    if (!p || i < 0 || i >= k->listMax) return 0;
    const int def = p->defs ? p->defs[i] : p->def;
    const auto list = s.lists.find(k->listKey);
    if (list == s.lists.end() || i >= static_cast<int>(list->second.size())) return def;
    const auto& item = list->second[static_cast<std::size_t>(i)];
    const auto it = item.find(key);
    return it == item.end() ? def : clampi(it->second, p->lo, p->hi);
}

std::vector<int> sourceSet(const ModSource& s) {
    const SourceKind* k = findSourceKind(s.kind);
    if (!k || !*k->setKey) return {};
    const int count = sourceValue(s, "count");
    std::vector<int> out;
    const auto it = s.sets.find(k->setKey);
    if (it == s.sets.end()) {
        for (int i = 0; i < count; ++i) out.push_back(i);  // missing: all of them
        return out;
    }
    for (int i : it->second)
        if (i >= 0 && i < count) out.push_back(i);  // members beyond Count are ignored
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<SourceOutput> outputsOf(const ModSource& s) {
    std::vector<SourceOutput> out;
    if (s.kind == "lfo") {
        const int n = sourceValue(s, "count");
        for (int i = 0; i < n; ++i) out.push_back({"lfo" + std::to_string(i + 1), OutputKind::Level, true});
        out.push_back({"converge", OutputKind::Trigger});
    } else if (s.kind == "accumulator") {
        out = {{"value", OutputKind::Level, false}, {"step", OutputKind::Trigger}};
    } else if (s.kind == "dice") {
        out = {{"level", OutputKind::Level, true}, {"change", OutputKind::Trigger}};
    } else if (s.kind == "arc") {
        out = {{"level", OutputKind::Level, false}, {"section", OutputKind::Trigger}};
    } else if (s.kind == "gong") {
        out = {{"gong", OutputKind::Trigger}, {"kenong", OutputKind::Trigger}, {"position", OutputKind::Level, false}};
    }
    return out;
}

std::optional<SourceOutput> outputOf(const ModSource& s, const std::string& name) {
    for (const SourceOutput& o : outputsOf(s))
        if (o.name == name) return o;
    return std::nullopt;
}

// ---- LFOs (§17.9) ---------------------------------------------------------------------------------------------

int lfoLevel(std::int64_t b, std::int64_t P, int shape, std::int64_t phase) {
    P = std::max<std::int64_t>(1, P);
    const std::int64_t x = floorMod(b + phase, P);
    const std::int64_t q = rdiv(2000 * x, P);  // 0 … 2000
    switch (shape) {
        case 1: return static_cast<int>(q - 1000);                            // Saw up
        case 2: return static_cast<int>(1000 - q);                            // Saw down
        case 3: return q < 1000 ? 1000 : -1000;                               // Square
        default: return static_cast<int>(q < 1000 ? 2 * q - 1000 : 3000 - 2 * q);  // Triangle
    }
}

std::int64_t lfoPeriodBars(const Song& song, const ModSource& s, int i) {
    const std::int64_t period = itemValue(s, i, "period");
    return period * (itemValue(s, i, "unit") == 1 ? std::max(1, song.phraseLength) : 1);
}

std::int64_t lfoPhase(const Song& song, const ModSource& s, int i) {
    const std::int64_t P = lfoPeriodBars(song, s, i);
    const int phase = itemValue(s, i, "phase");
    if (phase < 0) return static_cast<std::int64_t>(Stream(hash(song.matrixSeed, {"mod", s.id, "phase", i})).below(static_cast<std::uint64_t>(P)));
    return floorMod(phase, P);  // a phase above P − 1 (the period was shortened since) is taken mod P
}

// ---- Accumulator (§17.9) ----------------------------------------------------------------------------------------

std::int64_t accumulatorTicks(std::int64_t b, std::int64_t period, std::int64_t offset) {
    // The bars b' in 1 … b with b' mod period == offset mod period.
    if (b <= 0) return 0;
    const std::int64_t o = floorMod(offset, period);
    if (o == 0) return b / period;
    return b < o ? 0 : (b - o) / period + 1;
}

int accumulatorPosition(std::int64_t c, int step, int S, int start, int edge, int reset) {
    if (reset > 0) c = floorMod(c, reset);
    const std::int64_t raw = start + c * step;
    if (edge == 0) return static_cast<int>(floorMod(raw, S));  // Wrap
    if (edge == 2) return static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(S - 1, raw)));  // Hold
    // Pendulum: reflect(raw, 0, S − 1), §2.6
    const std::int64_t p = 2 * static_cast<std::int64_t>(S - 1), x = floorMod(raw, p);
    return static_cast<int>(x <= S - 1 ? x : p - x);
}

// ---- 1/f dice (§17.9) ---------------------------------------------------------------------------------------------

int diceSum(std::uint64_t matrixSeed, std::uint64_t id, std::int64_t c, int dice) {
    int sum = 0;
    for (int i = 0; i < dice; ++i) sum += static_cast<int>(Stream(hash(matrixSeed, {"mod", id, "die", i, c >> i})).below(16));
    return sum;
}

// ---- Energy arc (§17.9) -------------------------------------------------------------------------------------------

namespace {
// The default shape: (position ‰ of the span, level ‰). Frozen (§2.4).
constexpr int kArc[7][2] = {{0, 0}, {250, 150}, {600, 600}, {650, 250}, {900, 1000}, {960, 1000}, {1000, 0}};
std::int64_t rsdiv64(std::int64_t a, std::int64_t b) { return a >= 0 ? rdiv(a, b) : -rdiv(-a, b); }
}  // namespace

ArcPoint arcAt(std::int64_t b, std::int64_t N, bool loop) {
    N = std::max<std::int64_t>(1, N);
    b = std::max<std::int64_t>(0, b);
    const std::int64_t x = loop ? floorMod(b, N) : std::min(b, N);
    ArcPoint a;
    a.pos = static_cast<int>(rdiv(1000 * x, N));
    for (int j = 1; j <= 6; ++j)
        if (a.pos < kArc[j][0] || j == 6) {  // segment j: x_{j−1} ≤ pos < x_j; pos 1000 is segment 6's
            const int x0 = kArc[j - 1][0], y0 = kArc[j - 1][1], x1 = kArc[j][0], y1 = kArc[j][1];
            a.segment = j;
            a.level = static_cast<int>(y0 + rsdiv64(static_cast<std::int64_t>(a.pos - x0) * (y1 - y0), x1 - x0));
            break;
        }
    return a;
}

// ---- every source -----------------------------------------------------------------------------------------------

CompiledOutput compileOutput(const Song& song, const ModSource& s, const std::string& output) {
    CompiledOutput c;
    const auto o = outputOf(s, output);
    if (!o) return c;
    c.valid = true, c.kind = o->kind, c.bipolar = o->bipolar;
    // By default a trigger's next firing is tried bar by bar, never more than kScanBars of them a call. Set after the
    // kind's own `fire` exists (below), through this shared pointer to it.
    auto self = std::make_shared<std::function<std::optional<Fire>(std::int64_t)>>();
    c.next = [self](std::int64_t from, std::int64_t limit) -> std::optional<std::int64_t> {
        from = std::max<std::int64_t>(1, from);
        const std::int64_t end = std::min(limit, from + kScanBars);
        for (std::int64_t b = from; b < end; ++b)
            if ((*self)(b)) return b;
        return std::nullopt;
    };
    const std::uint64_t seed = song.matrixSeed, id = s.id;
    if (s.kind == "lfo") {
        struct Term {
            std::int64_t P, phase;
            int shape;
        };
        auto term = [&](int i) { return Term{lfoPeriodBars(song, s, i), lfoPhase(song, s, i), itemValue(s, i, "shape")}; };
        if (output == "converge") {
            std::vector<Term> set;
            for (int i : sourceSet(s)) set.push_back(term(i));
            c.fire = [set, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0 || set.empty()) return std::nullopt;  // a set with no member left never fires
                for (const Term& t : set)
                    if (floorMod(b + t.phase, t.P) != 0) return std::nullopt;
                return Fire{1, hash(seed, {"mod", id, "converge", b})};
            };
            // Every x == 0 at once: b ≡ −phase_i (mod P_i). Its solutions are x0 + k·lcm, or there are none (phases
            // that never line up), so the next one is closed form however far away it is.
            std::vector<std::pair<std::int64_t, std::int64_t>> eqs;
            for (const Term& t : set) eqs.push_back({floorMod(-t.phase, t.P), t.P});
            const auto sol = set.empty() ? std::nullopt : crt(eqs);
            c.exact = true;
            c.next = [sol](std::int64_t from, std::int64_t limit) -> std::optional<std::int64_t> {
                if (!sol) return std::nullopt;
                const auto [x0, L] = *sol;
                from = std::max<std::int64_t>(1, from);
                const std::int64_t b = from + floorMod(x0 - from, L);
                if (b >= limit) return std::nullopt;
                return b;
            };
        } else {
            const Term t = term(lfoIndex(output));
            c.level = [t](std::int64_t b) { return lfoLevel(std::max<std::int64_t>(0, b), t.P, t.shape, t.phase); };
        }
    }
    if (s.kind == "accumulator") {
        const std::int64_t period = std::max<std::int64_t>(1, static_cast<std::int64_t>(sourceValue(s, "every")) *
                                                                  (sourceValue(s, "inPhrases") == 1 ? std::max(1, song.phraseLength) : 1));
        const std::int64_t offset = sourceValue(s, "offset");
        int step = sourceValue(s, "step");
        if (step == 0) step = 1;  // not 0 (§17.9): a stored 0 is the default
        const int S = sourceValue(s, "positions"), start = std::min(sourceValue(s, "start"), S - 1), edge = sourceValue(s, "edge"),
                  reset = sourceValue(s, "reset");
        auto p = [=](std::int64_t b) { return accumulatorPosition(accumulatorTicks(b, period, offset), step, S, start, edge, reset); };
        if (output == "value") {
            c.level = [p, S](std::int64_t b) { return static_cast<int>(rdiv(1000 * static_cast<std::int64_t>(p(b)), S - 1)); };
        } else {
            const std::int64_t o = floorMod(offset, period);
            c.fire = [p, period, o, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0 || floorMod(b, period) != o) return std::nullopt;  // a tick
                const int now = p(b), before = p(b - 1);
                if (now == before) return std::nullopt;  // only where p changed
                return Fire{now - before, hash(seed, {"mod", id, "step", b})};
            };
        }
    } else if (s.kind == "dice") {
        const int D = sourceValue(s, "dice");
        const std::int64_t unit = sourceValue(s, "unit") == 1 ? std::max(1, song.phraseLength) : 1;
        auto sum = [=](std::int64_t b) { return diceSum(seed, id, floorDiv(std::max<std::int64_t>(0, b), unit), D); };
        if (output == "level") {
            c.level = [sum, D](std::int64_t b) { return static_cast<int>(rdiv(2000 * static_cast<std::int64_t>(sum(b)), 15 * D) - 1000); };
        } else {
            c.fire = [sum, unit, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0 || floorMod(b, unit) != 0) return std::nullopt;  // a clock
                const int now = sum(b), before = sum(b - 1);
                if (now == before) return std::nullopt;
                return Fire{now > before ? 1 : -1, hash(seed, {"mod", id, "change", b})};
            };
        }
    } else if (s.kind == "arc") {
        const std::int64_t N = static_cast<std::int64_t>(sourceValue(s, "span")) * std::max(1, song.phraseLength);
        const bool loop = sourceValue(s, "end") == 0;
        if (output == "level") {
            c.level = [N, loop](std::int64_t b) { return arcAt(b, N, loop).level; };
        } else {
            c.fire = [N, loop, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0) return std::nullopt;
                const int now = arcAt(b, N, loop).segment;
                if (now == arcAt(b - 1, N, loop).segment) return std::nullopt;
                return Fire{now, hash(seed, {"mod", id, "section", b})};
            };
        }
    }
    if (s.kind == "gong") {
        // §18.2: the song's clock, or the source's own cycle and kenongs.
        Song clock;
        clock.phraseLength = song.phraseLength;
        clock.gongCycle = sourceValue(s, "cycle") > 0 ? sourceValue(s, "cycle") : song.gongCycle;
        clock.kenongs = sourceValue(s, "kenongs") > 0 ? sourceValue(s, "kenongs") : song.kenongs;
        const std::int64_t N = gongBars(clock);
        const int K = kenongCount(clock);
        if (output == "position") {
            c.level = [N](std::int64_t b) { return static_cast<int>(rdiv(1000 * floorMod(std::max<std::int64_t>(0, b), N), N)); };
        } else if (output == "gong") {
            c.fire = [N, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0 || floorMod(b, N) != 0) return std::nullopt;
                return Fire{1, hash(seed, {"mod", id, "gong", b})};
            };
            c.exact = true;
            c.next = [N](std::int64_t from, std::int64_t limit) -> std::optional<std::int64_t> {
                from = std::max<std::int64_t>(1, from);
                const std::int64_t b = from + floorMod(-from, N);
                if (b >= limit) return std::nullopt;
                return b;
            };
        } else {
            c.fire = [N, K, seed, id](std::int64_t b) -> std::optional<Fire> {
                if (b <= 0) return std::nullopt;
                const std::int64_t p = floorMod(b, N);
                if (floorMod(p * K, N) >= K) return std::nullopt;
                const std::int64_t j = p * K / N;
                return Fire{static_cast<int>(j == 0 ? K : j), hash(seed, {"mod", id, "kenong", b})};
            };
        }
    }
    if (c.fire) *self = c.fire;
    else if (c.kind == OutputKind::Trigger) c.valid = false;  // a trigger this build can't work out
    return c;
}

std::optional<std::pair<std::int64_t, std::int64_t>> crt(const std::vector<std::pair<std::int64_t, std::int64_t>>& congruences) {
    // Extended Euclid: g = gcd(a, b) = a·x + b·y.
    std::function<std::int64_t(std::int64_t, std::int64_t, std::int64_t&, std::int64_t&)> egcd =
        [&](std::int64_t a, std::int64_t b, std::int64_t& x, std::int64_t& y) -> std::int64_t {
        if (b == 0) {
            x = 1, y = 0;
            return a;
        }
        std::int64_t x1 = 0, y1 = 0;
        const std::int64_t g = egcd(b, a % b, x1, y1);
        x = y1, y = x1 - (a / b) * y1;
        return g;
    };
    std::int64_t r = 0, m = 1;  // so far: b ≡ r (mod m)
    for (const auto& [ri, mi0] : congruences) {
        const std::int64_t mi = std::max<std::int64_t>(1, mi0);
        std::int64_t p = 0, q = 0;
        const std::int64_t g = egcd(m, mi, p, q);
        const std::int64_t d = floorMod(ri, mi) - r;
        if (floorMod(d, g) != 0) return std::nullopt;  // they never line up
        const std::int64_t mg = mi / g;
        // r + m·t ≡ ri (mod mi): t ≡ (d / g) · p (mod mi / g). Each factor is reduced first: m stays ≤ the product of
        // the periods (≤ 1024⁴ = 2⁴⁰), and every product here stays far inside 64 bits.
        const std::int64_t t = floorMod(floorMod(d / g, mg) * floorMod(p, mg), mg);
        const std::int64_t L = m / g * mi;
        r = floorMod(r + m * t, L);
        m = L;
    }
    return std::make_pair(r, m);
}

std::optional<Fire> triggerAt(const Song& song, const ModSource& s, const std::string& output, std::int64_t b) {
    const CompiledOutput c = compileOutput(song, s, output);
    if (!c.valid || c.kind != OutputKind::Trigger) return std::nullopt;
    return c.fire(b);
}

std::optional<int> levelAt(const Song& song, const ModSource& s, const std::string& output, std::int64_t b) {
    const CompiledOutput c = compileOutput(song, s, output);
    if (!c.valid || c.kind != OutputKind::Level) return std::nullopt;  // an lfo beyond Count does nothing
    return c.level(b);
}

std::vector<std::int64_t> triggerBars(const Song& song, const ModSource& s, const std::string& output, std::int64_t bar, int count,
                                      std::int64_t horizon) {
    std::vector<std::int64_t> out;
    const CompiledOutput c = compileOutput(song, s, output);
    if (!c.valid || c.kind != OutputKind::Trigger) return out;
    // A scanned output tries at most kScanBars bars in all; an exact one answers each in closed form.
    const std::int64_t limit = bar + (c.exact ? horizon : std::min(horizon, kScanBars));
    for (std::int64_t from = bar; static_cast<int>(out.size()) < count;) {
        const auto b = c.next(from, limit);
        if (!b) break;
        out.push_back(*b);
        from = *b + 1;
    }
    return out;
}

// ---- cords ----------------------------------------------------------------------------------------------------

const ModSource* sourceById(const Song& song, std::uint64_t id) {
    for (const ModSource& s : song.mod.sources)
        if (s.id == id) return &s;
    return nullptr;
}

std::optional<OutputKind> cordKind(const Song& song, const Cord& c) {
    const ModSource* s = sourceById(song, c.source);
    if (!s) return std::nullopt;
    const auto o = outputOf(*s, c.output);
    if (!o) return std::nullopt;
    return o->kind;
}

bool levelMayDrive(const std::string& key) {
    if (key == "mute") return true;
    if (const auto p = paramFromKey(key))
        // Not the ones that move a clock (they re-anchor, §4.4), nor Root, Scale or a pick-one (§17.3).
        return isNumericParam(*p) && *p != Param::Steps && *p != Param::Offset && *p != Param::Length;
    const auto g = generatorTarget(key);
    return g && !g->param->choice;
}

std::map<std::uint64_t, std::string> formLetters(const Song& song) {
    std::map<std::uint64_t, std::string> out;
    char next = 'B';
    auto add = [&](std::uint64_t id, bool fixed) {
        out[id] = std::string(1, next) + (fixed ? "" : "*");
        next = next == 'Z' ? 'Z' : static_cast<char>(next + 1);
    };
    for (const Row& r : song.matrix)
        if (r.lasts > 0) add(r.id, r.fixedSeed);
    for (const Cord& c : song.mod.cords)
        if (c.lasts > 0 && cordKind(song, c) == OutputKind::Trigger) add(c.id, c.fixedSeed);
    return out;
}

ModSource& addSource(Song& song, const std::string& kind) {
    ModSource s;
    s.id = newId(song);
    s.kind = kind;
    const SourceKind* k = findSourceKind(kind);
    s.name = k ? k->label : kind;
    song.mod.sources.push_back(s);
    return song.mod.sources.back();
}

Cord& addCord(Song& song, std::uint64_t source, const std::string& output) {
    Cord c;
    c.id = newId(song);
    c.source = source;
    c.output = output;
    song.mod.cords.push_back(c);
    return song.mod.cords.back();
}

bool removeSource(Song& song, std::uint64_t id) {
    auto& v = song.mod.sources;
    const auto n = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const ModSource& s) { return s.id == id; }), v.end());
    return v.size() != n;
}

bool removeCord(Song& song, std::uint64_t id) {
    auto& v = song.mod.cords;
    const auto n = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Cord& c) { return c.id == id; }), v.end());
    return v.size() != n;
}

}  // namespace hocket
