// The engine's tests: the spec's claims checked against the code, jump safety, the MIDI rules, and the golden render.
// No framework: each CHECK prints where it failed, and the exit code is the failure count.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <sstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "hocket/engine.hpp"
#include "hocket/generators.hpp"
#include "hocket/golden.hpp"
#include "hocket/mod.hpp"
#include "hocket/rng.hpp"

using namespace hocket;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static std::vector<int> v(std::initializer_list<int> xs) { return std::vector<int>(xs); }

static void arithmetic() {
    // SplitMix64 and FNV-1a against their published values.
    CHECK(Stream(0).next() == 0xE220A8397B1DCDAFull);
    CHECK(fnv1a("") == 0xCBF29CE484222325ull);
    CHECK(fnv1a("a") == 0xAF63DC4C8601EC8Cull);
    // A string part and an integer part with the same 64 bits are different paths (the kind tag).
    CHECK(hash(1, {"a"}) != hash(1, {fnv1a("a")}));
    // Round half up.
    CHECK(rdiv(5, 2) == 3 && rdiv(7, 3) == 2 && rdiv(1, 4) == 0 && rdiv(2, 4) == 1 && rdiv(0, 9) == 0);
    // below(n) stays in range, below(1) is 0, and it isn't stuck.
    Stream s(42);
    std::map<std::uint64_t, int> seen;
    for (int i = 0; i < 2000; ++i) {
        const std::uint64_t x = s.below(7);
        CHECK(x < 7);
        seen[x]++;
    }
    CHECK(seen.size() == 7);
    CHECK(Stream(9).below(1) == 0);
    // The 128-bit product, on edges.
    std::uint64_t hi, lo;
    mul64(~0ull, ~0ull, hi, lo);
    CHECK(hi == 0xFFFFFFFFFFFFFFFEull && lo == 1);
}

static void priority() {
    const std::vector<int> all = priorityOrder(123);
    std::vector<int> sorted = all;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 64; ++i) CHECK(sorted[i] == i);
    // §2.5: shortening keeps the relative order.
    const std::vector<int> p10 = priorityOrder(123, 10);
    std::vector<int> expect;
    for (int x : all)
        if (x < 10) expect.push_back(x);
    CHECK(p10 == expect);
}

static void pitch() {
    // §3.1's table.
    CHECK(ranked(0, 0, 0) == v({0, 7, 4, 11, 9, 2, 5}));       // C major: C G E B A D F
    CHECK(ranked(0, 0, 2) == v({0, 7, 3, 10, 9, 2, 5}));       // Dorian: C G Eb Bb A D F
    CHECK(ranked(0, 0, 11) == v({0, 7, 3, 10, 5}));            // minor pentatonic: C G Eb Bb F
    CHECK(ranked(0, 0, 7) == v({0, 4, 10, 2, 8, 6}));          // whole tone: C E Bb D G# F#
    // §3.4's chords: the first four of ranked(r).
    auto four = [](int r, int scale) { auto x = ranked(r, 0, scale); x.resize(4); return x; };
    CHECK(four(0, 0) == v({0, 7, 4, 11}));   // Cmaj7
    CHECK(four(7, 0) == v({7, 2, 11, 5}));   // G7
    CHECK(four(9, 0) == v({9, 4, 0, 7}));    // Am7
    CHECK(four(4, 0) == v({4, 11, 7, 2}));   // Em7
    CHECK(four(3, 11) == v({3, 10, 7, 0}));  // Eb6 in C minor pentatonic
    // §3.2: Range spreads over the scale's size.
    CHECK(poolSize(7, 1) == 1 && poolSize(7, 16) == 7 && poolSize(5, 16) == 5);
    CHECK(poolSize(7, 3) == 2);  // root and fifth
    CHECK(poolSize(7, 5) == 3);  // and the third: C G E in C major, so Cmaj7 / G7 / Em7
    // §3.3: one notch of Wander changes exactly one slot (16 steps), and Wander 1 is all roots.
    for (int L : {16, 7, 64}) {
        const std::vector<int> w1 = noteIndices(77, L, 5, 1);
        CHECK(std::count(w1.begin(), w1.end(), 0) == L);
        for (int w = 1; w < 16; ++w) {
            const std::vector<int> a = noteIndices(77, L, 5, w), b = noteIndices(77, L, 5, w + 1);
            int diff = 0;
            for (int i = 0; i < L; ++i) diff += a[i] != b[i];
            if (L == 16) CHECK(diff == 1);
            for (int i = 0; i < L; ++i)
                if (a[i] != b[i]) CHECK(a[i] == 0);  // only a root ever moves
        }
    }
    // §3.5: a slot keeps its octave jump as Density rises (the lane can only start jumping).
    for (int slot = 0; slot < 64; ++slot)
        for (int d = 1; d < 16; ++d)
            if (octaveLaneAt(5, slot, d) != 0) CHECK(octaveLaneAt(5, slot, d + 1) == octaveLaneAt(5, slot, d));
}

static void rhythm() {
    // §4.1: Density only adds hits.
    for (int L : {16, 12, 64}) {
        for (int d = 1; d < 16; ++d) {
            const std::vector<bool> a = hits(9, L, d), b = hits(9, L, d + 1);
            for (int i = 0; i < L; ++i)
                if (a[i]) CHECK(b[i]);
        }
        const std::vector<bool> h8 = hits(9, L, 8);
        CHECK(std::count(h8.begin(), h8.end(), true) == L / 2);
    }
    // §4.4: Pendulum, and its Steps = 1 guard.
    std::vector<int> pend;
    for (int a = 0; a < 8; ++a) pend.push_back(slotAt(a, 4, Direction::Pendulum, 0));
    CHECK(pend == v({0, 1, 2, 3, 2, 1, 0, 1}));
    CHECK(slotAt(5, 1, Direction::Pendulum, 0) == 0);
    CHECK(slotAt(0, 4, Direction::Backward, 1) == 0);  // idx 3, offset 1
}

static std::vector<Song> songs() {
    std::vector<Song> out;
    out.push_back(defaultSong(0x0123456789ABCDEFull));
    Song s = defaultSong(0xFEEDFACECAFEBEEFull);
    s.groups[0].swing = 60;
    s.groups[0].jitter = 20;
    s.groups[0].deal = DealMode::EveryCycle;
    s.groups[0].overlap = Overlap::Cut;
    s.groups[0].voices[0].tie = true;
    s.groups[0].steps = 13;
    s.groups[1].deal = DealMode::RoundRobin;
    s.groups[1].direction = Direction::Pendulum;
    s.groups[2].voices[0].articulation = Articulation::RestrikeHits;
    s.groups[2].density = 5;
    s.groups[2].voices[0].voicing = 12;
    s.gatePercent = 180;
    s.harmony.changesEvery = 1;
    s.harmony.length = 6;
    out.push_back(s);
    Song p = defaultSong(7);
    p.groups[2].voices[0].articulation = Articulation::Pulse;
    p.harmony.length = 1;
    p.scale = 11;
    out.push_back(p);
    // §4.6 everywhere: chance, ratchets with a ramp, fills on the phrase clock, a Pulse chord that ratchets, swing.
    Song r = defaultSong(0xABCDEF12345ull);
    r.groups[0].probability = 70, r.groups[0].ratchet = 30, r.groups[0].repeats = 4, r.groups[0].ramp = -60, r.groups[0].swing = 40;
    r.groups[0].fillEvery = 1, r.groups[0].fillBars = 1, r.groups[0].fillAmount = 6, r.groups[0].voices[0].tie = true;
    r.groups[1].probability = 85, r.groups[1].ratchet = 15, r.groups[1].repeats = 3, r.groups[1].fillEvery = 2, r.groups[1].fillBars = 4;
    r.groups[2].voices[0].articulation = Articulation::Pulse, r.groups[2].ratchet = 50, r.groups[2].fillNow = true;
    out.push_back(r);
    // §3.4b: Stagger and Top on each articulation, both leans, every order, with swing and ratchets around them.
    Song h = s;
    h.groups[2].voices[0].stagger = 16, h.groups[2].voices[0].staggerOrder = StaggerOrder::Seeded;
    h.groups[2].voices[0].staggerLean = StaggerLean::Around, h.groups[2].voices[0].top = 12;
    out.push_back(h);
    Song q = r;
    q.groups[2].voices[0].stagger = 10, q.groups[2].voices[0].staggerOrder = StaggerOrder::Down, q.groups[2].voices[0].top = 16;
    out.push_back(q);
    Song n = defaultSong(0x5151);
    n.groups[2].voices[0].stagger = 8, n.groups[2].voices[0].staggerLean = StaggerLean::Around, n.groups[2].voices[0].nearest = true;
    n.groups[2].voices[0].top = 6, n.harmony.changesEvery = 0;
    out.push_back(n);
    return out;
}

static void jumps() {
    // §2.1: any window is exactly the slice of a longer render, so a host jump replays what was played.
    for (const Song& song : songs()) {
        const std::int64_t end = barTicks(song) * 24;
        const std::vector<Note> whole = notes(song, 0, end);
        CHECK(!whole.empty());
        CHECK(whole == notes(song, 0, end));  // and deterministic
        for (std::int64_t cut : {std::int64_t{960}, std::int64_t{3840}, std::int64_t{1234}, std::int64_t{777}}) {
            std::vector<Note> pieces;
            for (std::int64_t a = 0; a < end; a += cut) {
                const std::vector<Note> part = notes(song, a, std::min(end, a + cut));
                pieces.insert(pieces.end(), part.begin(), part.end());
            }
            std::stable_sort(pieces.begin(), pieces.end(), [](const Note& a, const Note& b) {
                return std::tie(a.start, a.channel, a.note, a.end) < std::tie(b.start, b.channel, b.note, b.end);
            });
            CHECK(pieces == whole);
        }
    }
}

static void midiRules() {
    // §11: per (channel, note), ons and offs alternate, nothing is left on, and lengths are positive.
    for (const Song& song : songs()) {
        const std::vector<Note> ns = notes(song, 0, barTicks(song) * 32);
        for (const Note& n : ns) CHECK(n.end > n.start && n.note >= 0 && n.note <= 127 && n.velocity >= 1 && n.velocity <= 127);
        std::map<std::pair<int, int>, bool> on;
        std::int64_t last = 0;
        for (const Event& e : events(ns)) {
            CHECK(e.tick >= last);
            last = e.tick;
            bool& o = on[{e.channel, e.note}];
            CHECK(e.kind == Event::On ? !o : o);
            o = e.kind == Event::On;
        }
        for (const auto& kv : on) CHECK(!kv.second);
    }
}

static void harmony() {
    Song s = defaultSong(99);
    // A progression starts at home, whatever the seed.
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Song t = defaultSong(seed);
        t.harmony.wander = 16;
        CHECK(chordRootAt(t, 0) == t.root);
        CHECK(progression(t)[0] == 0);
    }
    // Harmony Range 1: one chord, on Root, all song.
    s.harmony.range = 1;
    for (int k = 0; k < 8; ++k) CHECK(chordRootAt(s, k * changeTicks(s)) == s.root);
    // Every bass note is the current chord's root or fifth (Range 3 → pool of 2): the shared core.
    s = defaultSong(99);
    const Group& bass = s.groups[1];
    int checked = 0;
    for (const Note& n : notes(s, 0, barTicks(s) * 16)) {
        if (n.voice != bass.voices[0].id) continue;
        const std::vector<int> rk = ranked(chordRootAt(s, n.start - (n.start % 240)), s.root, s.scale);
        CHECK(n.note % 12 == rk[0] || n.note % 12 == rk[1]);
        ++checked;
    }
    CHECK(checked > 0);
    // A sustained chord sounds whole bars: every Chord note starts on a change and lasts at least one.
    for (const Note& n : notes(s, 0, barTicks(s) * 16)) {
        if (n.voice != s.groups[2].voices[0].id) continue;
        CHECK(n.start % changeTicks(s) == 0);
        CHECK(n.end - n.start >= changeTicks(s));
    }
}

// Notes of one voice that start in a bar, as (offset in the bar, channel, note): what a listener hears there.
static std::vector<std::tuple<std::int64_t, int, int>> heard(const Song& s, std::uint64_t voice, std::int64_t bar) {
    std::vector<std::tuple<std::int64_t, int, int>> out;
    const std::int64_t bt = barTicks(s);
    for (const Note& n : notes(s, bar * bt, (bar + 1) * bt))
        if (n.voice == voice) out.emplace_back(n.start - bar * bt, n.channel, n.note);
    return out;
}

static Row row(Scope scope, std::uint64_t target, Param p) {
    Row r;
    r.id = 0x5EED0000 + static_cast<std::uint64_t>(p) * 7 + static_cast<std::uint64_t>(scope);
    r.scope = scope;
    r.target = target;
    r.param = p;
    return r;
}

static void matrix() {
    // §7.4's AAAB: the default song's turnaround varies the leads in the last bar of each phrase, the same way each
    // time (fixed seed), and every other bar is the plain A.
    const Song s = defaultSong(0x0123456789ABCDEFull);
    Song plain = s;
    plain.matrix.clear();
    const std::uint64_t lead = s.groups[0].voices[0].id, lead2 = s.groups[0].voices[1].id;
    for (std::int64_t bar = 0; bar < 12; ++bar) {
        const bool turn = bar % 4 == 3;
        const bool same = heard(s, lead, bar) == heard(plain, lead, bar) && heard(s, lead2, bar) == heard(plain, lead2, bar);
        CHECK(turn ? !same : same);
    }
    CHECK(heard(s, lead, 3) == heard(s, lead, 7) && heard(s, lead2, 3) == heard(s, lead2, 11));
    // Fresh: a different B each time; the A bars stay A.
    Song fresh = s;
    fresh.matrix[0].fixedSeed = false;
    CHECK(heard(fresh, lead, 3) != heard(fresh, lead, 7) || heard(fresh, lead2, 3) != heard(fresh, lead2, 7));
    CHECK(heard(fresh, lead, 5) == heard(plain, lead, 5));
    // Chance 0 never fires.
    Song never = s;
    never.matrix[0].chance = 0;
    for (std::int64_t bar = 0; bar < 8; ++bar) CHECK(heard(never, lead, bar) == heard(plain, lead, bar));
    // Offset 2 is AABA.
    Song aaba = s;
    aaba.matrix[0].offset = 2;
    CHECK(heard(aaba, lead, 2) != heard(plain, lead, 2) || heard(aaba, lead2, 2) != heard(plain, lead2, 2));
    CHECK(heard(aaba, lead, 3) == heard(plain, lead, 3));

    // A numeric row that stays: it moves, bounces inside its bounds, and never leaves them.
    Song drift = plain;
    Row r = row(Scope::Global, 0, Param::Range);
    r.every = 1, r.maxShift = 3, r.min = 6, r.max = 12;
    drift.matrix.push_back(r);
    std::map<int, int> ranges;
    for (std::int64_t bar = 0; bar < 40; ++bar) {
        const int x = stateAt(drift, bar).range;
        CHECK(x >= 6 && x <= 12);
        ranges[x]++;
    }
    CHECK(ranges.size() >= 3);

    // A fill: Density up for one bar, then back exactly.
    Song fill = plain;
    Row f = row(Scope::Group, plain.groups[0].id, Param::Density);
    f.every = 8, f.offset = 7, f.maxShift = 4, f.min = plain.groups[0].density, f.lasts = 1;
    fill.matrix.push_back(f);
    CHECK(stateAt(fill, 7).groups[0].density > plain.groups[0].density);
    CHECK(stateAt(fill, 8).groups[0].density == plain.groups[0].density);
    CHECK(heard(fill, lead, 8) == heard(plain, lead, 8));

    // Root moves around the circle of fifths, one notch at a time.
    Song keys = plain;
    Row k = row(Scope::Global, 0, Param::Root);
    k.every = 2;
    keys.matrix.push_back(k);
    for (std::int64_t bar = 1; bar < 20; ++bar) {
        const int a = stateAt(keys, bar - 1).root, b = stateAt(keys, bar).root;
        CHECK(a == b || (a + 7) % 12 == b || (b + 7) % 12 == a);
    }
    CHECK(stateAt(keys, 9).root != plain.root || stateAt(keys, 5).root != plain.root);

    // Scale changes are opt-in, and a notch is one note of difference.
    Song modes = plain;
    Row m = row(Scope::Global, 0, Param::Scale);
    m.every = 1;
    m.allowed = {0, 1, 2, 3, 4, 5, 11};  // the church modes and minor pentatonic
    modes.matrix.push_back(m);
    int moved = 0;
    for (std::int64_t bar = 1; bar < 16; ++bar) {
        const int a = stateAt(modes, bar - 1).scale, b = stateAt(modes, bar).scale;
        int onlyA = 0, onlyB = 0;
        for (int i = 0; i < 12; ++i) {
            onlyA += ((kScales[a].mask >> i) & 1) && !((kScales[b].mask >> i) & 1);
            onlyB += ((kScales[b].mask >> i) & 1) && !((kScales[a].mask >> i) & 1);
        }
        CHECK(std::max(onlyA, onlyB) <= 1);
        moved += a != b;
    }
    CHECK(moved > 0);
    Song locked = plain;
    Row l = m;
    l.allowed = {};
    locked.matrix.push_back(l);
    for (std::int64_t bar = 0; bar < 8; ++bar) CHECK(stateAt(locked, bar).scale == plain.scale);

    // A pick-one with Max shift 1 steps to a neighbour; Allowed limits it.
    Song dirs = plain;
    Row d = row(Scope::Group, plain.groups[1].id, Param::Direction);
    d.every = 1;
    d.allowed = {0, 2};
    dirs.matrix.push_back(d);
    for (std::int64_t bar = 0; bar < 8; ++bar) {
        const Direction x = stateAt(dirs, bar).groups[1].direction;
        CHECK(x == Direction::Forward || x == Direction::Pendulum);
    }

    // Vary Harmony turns the last chord of a phrase, and every line turns with it; change 0 stays home.
    Song turnH = plain;
    Row h = row(Scope::Harmony, 0, Param::VaryPitch);
    h.every = 4, h.offset = 3, h.maxShift = 1, h.lasts = 1;
    turnH.harmony.wander = 1;  // a tonic-only progression, so any change is the vary
    turnH.harmony.range = 8;
    turnH.matrix.push_back(h);
    CHECK(chordRootAt(turnH, 0) == turnH.root);
    CHECK(chordRootAt(turnH, 3 * barTicks(turnH)) != turnH.root);
    CHECK(chordRootAt(turnH, 4 * barTicks(turnH)) == turnH.root);
}

static void matrixJumps() {
    // Jump safety with every kind of row: any window is the slice of a longer render.
    Song s = defaultSong(0xABCDEF);
    const std::uint64_t g0 = s.groups[0].id, g1 = s.groups[1].id, g2 = s.groups[2].id;
    auto add = [&](Row r) { s.matrix.push_back(r); };
    Row a = row(Scope::Group, g0, Param::Steps); a.every = 3, a.maxShift = 5, a.min = 7, a.max = 24; add(a);
    Row b = row(Scope::Group, g1, Param::RerollRhythm); b.every = 5, b.fixedSeed = false, b.chance = 70; add(b);
    Row c = row(Scope::Group, g0, Param::VaryRhythm); c.every = 2, c.offset = 1, c.maxShift = 3, c.lasts = 1; add(c);
    Row d = row(Scope::Harmony, 0, Param::ChangesEvery); d.every = 8, d.allowed = {1, 2, 3}; add(d);
    Row e = row(Scope::Group, g0, Param::DealMode); e.every = 4, e.maxShift = 3; add(e);
    Row f = row(Scope::Voice, s.groups[2].voices[0].id, Param::Articulation); f.every = 6, f.maxShift = 3; add(f);
    Row g = row(Scope::Group, g2, Param::Density); g.every = 2, g.maxShift = 8, g.lasts = 2; add(g);
    Row h = row(Scope::Harmony, 0, Param::Length); h.every = 7, h.maxShift = 2, h.min = 2, h.max = 8; add(h);
    Row i = row(Scope::Group, g1, Param::Offset); i.every = 5, i.maxShift = 9; add(i);
    Row j = row(Scope::Global, 0, Param::RerollAll); j.every = 16, j.fixedSeed = false; add(j);
    const std::int64_t end = barTicks(s) * 40;
    const std::vector<Note> whole = notes(s, 0, end);
    CHECK(whole == notes(s, 0, end));
    for (std::int64_t cut : {std::int64_t{3840}, std::int64_t{2500}, std::int64_t{11111}}) {
        std::vector<Note> pieces;
        for (std::int64_t at = 0; at < end; at += cut) {
            const std::vector<Note> part = notes(s, at, std::min(end, at + cut));
            pieces.insert(pieces.end(), part.begin(), part.end());
        }
        std::stable_sort(pieces.begin(), pieces.end(), [](const Note& x, const Note& y) {
            return std::tie(x.start, x.channel, x.note, x.end) < std::tie(y.start, y.channel, y.note, y.end);
        });
        CHECK(pieces == whole);
    }
    std::map<std::pair<int, int>, bool> on;
    for (const Event& ev : events(whole)) {
        bool& o = on[{ev.channel, ev.note}];
        CHECK(ev.kind == Event::On ? !o : o);
        o = ev.kind == Event::On;
    }
    for (const Note& n : whole) CHECK(n.end > n.start && n.velocity >= 1 && n.velocity <= 127 && n.note >= 0 && n.note <= 127);
}

static bool eventOrder(const Event& a, const Event& b) {
    return std::tie(a.tick, a.kind, a.channel, a.note) < std::tie(b.tick, b.kind, b.channel, b.note);
}

static void player() {
    std::vector<Song> all = songs();
    all.push_back(defaultSong(0x0123456789ABCDEFull));
    for (const Song& song : all) {
        const std::int64_t end = barTicks(song) * 16;
        // Played a block at a time, in uneven blocks, it is exactly the one-shot render.
        std::vector<Event> want;
        for (const Event& e : events(notes(song, 0, end)))
            if (e.tick < end) want.push_back(e);
        Player p(song);
        std::vector<Event> got;
        const std::int64_t sizes[] = {37, 100, 1000, 7, 480, 2900};
        std::int64_t at = 0;
        for (int i = 0; at < end; ++i) {
            const std::int64_t to = std::min(end, at + sizes[i % 6]);
            for (const Event& e : p.process(at, to, true)) {
                CHECK(e.tick >= at && e.tick < to);
                got.push_back(e);
            }
            at = to;
        }
        std::stable_sort(want.begin(), want.end(), eventOrder);
        std::stable_sort(got.begin(), got.end(), eventOrder);
        CHECK(got.size() == want.size());
        bool same = got.size() == want.size();
        for (std::size_t i = 0; same && i < got.size(); ++i)
            same = got[i].tick == want[i].tick && got[i].kind == want[i].kind && got[i].channel == want[i].channel &&
                   got[i].note == want[i].note && got[i].velocity == want[i].velocity;
        CHECK(same);
    }

    // A jump (§6.3): everything sounding ends at the jump, the chord ringing there sounds again, and from there on
    // the note-ons are the song's own.
    const Song s = defaultSong(0x0123456789ABCDEFull);
    const std::int64_t bt = barTicks(s), J = 9 * bt + 240;  // mid-chord: the bar-8 chord rings through bar 9
    Player p(s);
    std::map<std::pair<int, int>, bool> on;
    auto feed = [&](const std::vector<Event>& es) {
        for (const Event& e : es) {
            bool& o = on[{e.channel, e.note}];
            CHECK(e.kind == Event::On ? !o : o);
            o = e.kind == Event::On;
        }
    };
    for (std::int64_t t = 0; t < 2 * bt; t += 480) feed(p.process(t, t + 480, true));
    const std::vector<Event> at = p.process(J, J + 480, true);
    feed(at);
    std::vector<int> resounded, ringing;
    for (const Event& e : at)
        if (e.tick == J && e.kind == Event::On && e.channel == 3) resounded.push_back(e.note);
    for (const Note& n : notes(s, 8 * bt, J))
        if (n.channel == 3 && n.end > J) ringing.push_back(n.note);
    std::sort(resounded.begin(), resounded.end());
    std::sort(ringing.begin(), ringing.end());
    CHECK(!ringing.empty() && resounded == ringing);
    std::vector<Event> after, want;
    for (std::int64_t t = J + 480; t < 12 * bt; t += 480)
        for (const Event& e : p.process(t, t + 480, true)) after.push_back(e);
    for (const Event& e : events(notes(s, 0, 13 * bt)))
        if (e.kind == Event::On && e.tick >= J + 480 && e.tick < 12 * bt) want.push_back(e);
    std::vector<Event> afterOns;
    for (const Event& e : after)
        if (e.kind == Event::On && e.tick < 12 * bt) afterOns.push_back(e);
    std::stable_sort(afterOns.begin(), afterOns.end(), eventOrder);
    std::stable_sort(want.begin(), want.end(), eventOrder);
    CHECK(afterOns.size() == want.size());
    for (std::size_t i = 0; i < std::min(afterOns.size(), want.size()); ++i)
        CHECK(afterOns[i].tick == want[i].tick && afterOns[i].note == want[i].note && afterOns[i].channel == want[i].channel);
    feed(after);
    // Stop: everything ends, once.
    feed(p.process(12 * bt, 12 * bt + 480, false));
    for (const auto& kv : on) CHECK(!kv.second);
    CHECK(p.process(12 * bt + 480, 12 * bt + 960, false).empty());
}

static void editorViews() {
    // §3.4's table, by name.
    Song c = defaultSong(1);
    c.root = 0, c.scale = 0;  // C major
    CHECK(chordName(c, 0, 4) == "Cmaj7" && chordName(c, 7, 4) == "G7" && chordName(c, 9, 4) == "Am7" && chordName(c, 4, 4) == "Em7");
    // B in C major at Colour 0: PREF ranks the tritone (F) near last, so the chord on B is B D A E, an eleventh with
    // no fifth. With any Colour (§3.7) a chord with no fifth takes the one the scale has: B D F A, half-diminished.
    c.colour = 0;
    CHECK(chordName(c, 11, 4) == "Bm11" && chordName(c, 0, 3) == "C" && chordName(c, 9, 3) == "Am" && chordName(c, 11, 3) == "Bm7");
    c.colour = 1;
    CHECK(chordName(c, 11, 4) == "Bm7b5" && chordName(c, 11, 3) == "Bdim" && chordName(c, 0, 4) == "Cmaj7");
    CHECK(chordName(c, 2, 4) == "Dm7" && chordName(c, 5, 4) == "Fmaj7");
    c.scale = 11;  // C minor pentatonic
    CHECK(chordName(c, 3, 4) == "D#6");
    // The default song's progression, named, starting at home on the tonic.
    const Song s = defaultSong(0x0123456789ABCDEFull);
    View view(s);
    const HarmonyView h = view.harmony(0);
    CHECK(h.names.size() == 4 && h.names[0] == "Am7" && h.current == 0);
    CHECK(view.harmony(barTicks(s) * 2).current == 2 && view.harmony(barTicks(s) * 6).phraseBar == 2);
    // Lanes: one per group, the hits the engine plays, owned by the group's voices, and a playhead.
    // The form strip: the default song's turnaround is B on the last bar of each phrase, for the leads (whose
    // group the row targets), and nowhere else.
    const std::vector<std::vector<std::string>> form = view.form(0, 8);
    CHECK(form.size() == 3);
    CHECK((form[0] == std::vector<std::string>{"A", "A", "A", "B", "A", "A", "A", "B"}));
    CHECK((form[1] == std::vector<std::string>(8, "A")));
    // Reach 8 (§4.5): the seed picks G and E from the pool of four (A E C G).
    CHECK((view.chordsByBar(0, 4) == std::vector<std::string>{"Am7", "G7", "Am7", "Em7"}));
    const std::vector<Lane> lanes = view.lanes(barTicks(s) + 240 * 5);
    CHECK(lanes.size() == 3 && lanes[0].name == "Leads" && lanes[0].steps == 16 && lanes[2].steps == 32);
    CHECK(lanes[0].playing == 5);
    const std::vector<bool> h0 = hits(s.groups[0].rhythmSeed, 16, s.groups[0].density);
    for (int i = 0; i < 16; ++i) {
        CHECK(lanes[0].cells[i].hit == h0[i]);
        if (lanes[0].cells[i].hit) CHECK(lanes[0].cells[i].owner == 0 || lanes[0].cells[i].owner == 1);
        if (lanes[0].cells[i].hit) CHECK(lanes[0].cells[i].note >= 0);
    }
}

static void songChanges() {
    // A knob turned mid-song (Player::setSong on a bar line): what was sounding ends cleanly, nothing sticks, and
    // from the bar line the note-ons are the new song's own.
    const Song a = defaultSong(0x0123456789ABCDEFull);
    Song b = a;
    b.root = 4;  // E
    b.scale = 2;  // Dorian
    b.range = 12;
    const std::int64_t bt = barTicks(a), change = 6 * bt, end = 10 * bt;
    Player p(a);
    std::vector<Event> got;
    for (std::int64_t t = 0; t < end; t += 240) {
        if (t == change) p.setSong(b);
        for (const Event& e : p.process(t, t + 240, true)) got.push_back(e);
    }
    for (const Event& e : p.allOff(end)) got.push_back(e);
    std::map<std::pair<int, int>, bool> on;
    for (const Event& e : got) {
        bool& o = on[{e.channel, e.note}];
        CHECK(e.kind == Event::On ? !o : o);
        o = e.kind == Event::On;
    }
    for (const auto& kv : on) CHECK(!kv.second);
    std::vector<std::tuple<std::int64_t, int, int>> after, want;
    for (const Event& e : got)
        if (e.kind == Event::On && e.tick >= change && e.tick < end) after.emplace_back(e.tick, e.channel, e.note);
    for (const Note& n : notes(b, change, end)) want.emplace_back(n.start, n.channel, n.note);
    std::sort(after.begin(), after.end());
    std::sort(want.begin(), want.end());
    CHECK(after == want);
    // And before the change, the old song's.
    std::vector<std::tuple<std::int64_t, int, int>> before, old;
    for (const Event& e : got)
        if (e.kind == Event::On && e.tick < change) before.emplace_back(e.tick, e.channel, e.note);
    for (const Note& n : notes(a, 0, change)) old.emplace_back(n.start, n.channel, n.note);
    std::sort(before.begin(), before.end());
    std::sort(old.begin(), old.end());
    CHECK(before == old);
}

static void documents() {
    // §2.2: Generate re-rolls what isn't locked and nothing else; the same Generates make the same song.
    Song a = defaultSong(42), b = defaultSong(42);
    a.groups[0].lockRhythm = true;
    a.groups[1].voices[0].lockNotes = true;
    b.groups[0].lockRhythm = true;
    b.groups[1].voices[0].lockNotes = true;
    const Song before = a;
    generate(a);
    generate(b);
    CHECK(a.groups[0].rhythmSeed == before.groups[0].rhythmSeed);
    CHECK(a.groups[0].dealSeed != before.groups[0].dealSeed);
    CHECK(a.groups[1].voices[0].pitchSeed == before.groups[1].voices[0].pitchSeed);
    CHECK(a.groups[0].voices[0].pitchSeed != before.groups[0].voices[0].pitchSeed);
    CHECK(a.harmony.seed != before.harmony.seed && a.generateCount == 1);
    CHECK(a.groups.size() == before.groups.size() && a.matrix.size() == before.matrix.size());
    CHECK(notes(a, 0, barTicks(a) * 4) == notes(b, 0, barTicks(b) * 4));
    CHECK(!(notes(a, 0, barTicks(a) * 4) == notes(before, 0, barTicks(before) * 4)));
    // Building: a new group and voice play at once; adding never re-rolls what's there (§8).
    Song c = defaultSong(42);
    const std::vector<Note> old = notes(c, 0, barTicks(c) * 4);
    Group& g = addGroup(c, "Arp");
    g.density = 12;
    addVoice(c, c.groups.back(), VoiceType::Lead, 5);
    const std::vector<Note> now = notes(c, 0, barTicks(c) * 4);
    int fives = 0;
    for (const Note& n : now) fives += n.channel == 5;
    CHECK(fives > 0);
    std::vector<Note> rest;
    for (const Note& n : now)
        if (n.channel != 5) rest.push_back(n);
    CHECK(rest == old);
    // Removing: the group and its voices go; a row aimed at it stays and does nothing.
    Row& r = addRow(c);
    r.scope = Scope::Group, r.target = c.groups.back().id, r.param = Param::Density, r.every = 1;
    CHECK(removeGroup(c, c.groups.back().id));
    CHECK(notes(c, 0, barTicks(c) * 4) == old);
    CHECK(removeRow(c, r.id) && !removeRow(c, r.id));
    CHECK(removeVoice(c, c.groups[0].voices[1].id) && c.groups[0].voices.size() == 1);
    // Ids never repeat.
    std::map<std::uint64_t, int> ids;
    for (const Group& gg : c.groups) {
        ids[gg.id]++;
        for (const Voice& v : gg.voices) ids[v.id]++;
    }
    for (const Row& rr : c.matrix) ids[rr.id]++;
    for (const auto& kv : ids) CHECK(kv.second == 1);
}

static void hostile() {
    // A song from outside with every value out of range (a session or .planchette file someone sent): sanitize brings
    // it into range, and the engine plays it promptly instead of dividing by zero or looping for ever.
    Song s = defaultSong(5);
    s.barLength = 0, s.phraseLength = -3, s.root = 99, s.scale = 1000, s.range = -5, s.accent = 9999, s.gatePercent = 0;
    s.harmony.changesEvery = 77, s.harmony.length = 0, s.harmony.range = 0;
    s.groups[0].steps = 0, s.groups[0].beatDivision = -1, s.groups[0].density = 400, s.groups[0].direction = static_cast<Direction>(9);
    s.groups[0].deal = static_cast<DealMode>(-4), s.groups[0].soloMember = 50, s.groups[0].overrides.range = 1000;
    s.groups[0].voices[0].channel = 99, s.groups[0].voices[0].type = static_cast<VoiceType>(7), s.groups[0].voices[0].chordSize = 0;
    Row& r = addRow(s);
    r.scope = Scope::Group, r.target = s.groups[0].id, r.param = Param::VaryRhythm;
    r.every = 0, r.lasts = 1000000000, r.maxShift = 2147483647, r.chance = 500;
    Row& q = addRow(s);
    q.param = static_cast<Param>(1234), q.every = -7;
    sanitize(s);
    CHECK(s.barLength == 1 && s.phraseLength == 1 && s.root == 11 && s.scale == 34 && s.accent == 100 && s.gatePercent == 1);
    CHECK(s.groups[0].steps == 1 && s.groups[0].beatDivision == 0 && s.groups[0].direction == Direction::Pendulum);
    CHECK(s.groups[0].voices[0].channel == 16 && s.groups[0].soloMember == 1);
    CHECK(s.matrix.back().every == 1 && s.matrix[s.matrix.size() - 2].lasts == 64);
    const std::vector<Note> ns = notes(s, 0, barTicks(s) * 64);
    for (const Note& n : ns) CHECK(n.end > n.start && n.channel >= 1 && n.channel <= 16 && n.note >= 0 && n.note <= 127);
    // Unsanitized, the engine still doesn't divide by zero or run away (its own guards).
    Song raw = defaultSong(5);
    raw.barLength = 0;
    Row& w = addRow(raw);
    w.scope = Scope::Group, w.target = raw.groups[0].id, w.param = Param::VaryPitch, w.every = 1, w.lasts = 1000000000, w.maxShift = 2147483647;
    CHECK(!notes(raw, 0, barTicks(raw) * 8).empty());
}

static void goldens() {
    const std::string path = std::string(HOCKET_GOLDEN_DIR) + "/default-0123456789abcdef.txt";
    const std::string got = golden(defaultSong(0x0123456789ABCDEFull), 64);
    if (std::getenv("HOCKET_UPDATE_GOLDEN")) {
        std::ofstream(path, std::ios::binary) << got;
        std::fprintf(stderr, "wrote %s\n", path.c_str());
        return;
    }
    std::ifstream f(path, std::ios::binary);
    std::stringstream want;
    want << f.rdbuf();
    if (want.str() != got) {
        std::fprintf(stderr, "golden %s differs (HOCKET_UPDATE_GOLDEN=1 rewrites it, only on purpose)\n", path.c_str());
        ++failures;
    }
}

// One lead, one group: the plainest place to watch §4.6 at work. Density 16 (every step a hit), no matrix.
static Song oneLine(std::uint64_t seed) {
    Song s = defaultSong(seed);
    s.matrix.clear();
    s.groups.resize(1);
    s.groups[0].voices.resize(1);
    s.groups[0].density = 16;
    return s;
}

// (start, note) of every note: what sounds where, ignoring how long.
static std::set<std::pair<std::int64_t, int>> onsets(const std::vector<Note>& ns) {
    std::set<std::pair<std::int64_t, int>> out;
    for (const Note& n : ns) out.insert({n.start, n.note});
    return out;
}

// §4.6: probability, ratchets and fills.
static void chanceRatchetsFills() {
    // Off by default, and the settings that only matter once they're on change nothing while they're off.
    for (const Song& base : songs()) {
        if (base.groups[0].probability != 100) continue;
        Song idle = base;
        for (Group& g : idle.groups) g.repeats = 4, g.ramp = -100, g.fillAmount = 16, g.fillBars = 9;
        CHECK(notes(idle, 0, barTicks(idle) * 24) == notes(base, 0, barTicks(base) * 24));
    }
    const Song plain = oneLine(0x5EED);
    const std::int64_t bt = barTicks(plain), bars = 400;
    const std::size_t all = notes(plain, 0, bt * bars).size();
    CHECK(all == static_cast<std::size_t>(16 * bars));

    // Probability: about p of the hits play, the same ones every render, and the ones that play at p play at any
    // higher p (nested). Each pass is a new draw: the bars aren't all alike.
    std::set<std::pair<std::int64_t, int>> lower;
    for (int p : {25, 50, 75, 90}) {
        Song s = plain;
        s.groups[0].probability = p;
        const std::vector<Note> ns = notes(s, 0, bt * bars);
        const double got = 100.0 * static_cast<double>(ns.size()) / static_cast<double>(all);
        std::printf("  probability %d %%: %.1f %% of %zu hits played\n", p, got, all);
        CHECK(got > p - 3.0 && got < p + 3.0);
        CHECK(ns == notes(s, 0, bt * bars));
        const auto on = onsets(ns);
        CHECK(std::includes(on.begin(), on.end(), lower.begin(), lower.end()));
        lower = on;
        std::set<std::vector<std::int64_t>> bars8;
        for (std::int64_t b = 0; b < 8; ++b) {
            std::vector<std::int64_t> at;
            for (const Note& n : ns)
                if (n.start >= b * bt && n.start < (b + 1) * bt) at.push_back(n.start - b * bt);
            bars8.insert(at);
        }
        CHECK(bars8.size() > 4);
    }
    {
        // A hit that plays is the note it would have been: the same start, pitch and voice.
        Song s = plain;
        s.groups[0].probability = 40;
        const auto some = onsets(notes(s, 0, bt * 32)), every = onsets(notes(plain, 0, bt * 32));
        CHECK(std::includes(every.begin(), every.end(), some.begin(), some.end()));
        // Probability 0: silence. Another rhythm seed: another draw.
        s.groups[0].probability = 0;
        CHECK(notes(s, 0, bt * 32).empty());
        Song a = plain, b = plain;
        a.groups[0].probability = b.groups[0].probability = 50;
        b.groups[0].rhythmSeed ^= 1;
        CHECK(onsets(notes(a, 0, bt * 8)) != onsets(notes(b, 0, bt * 8)));
    }

    // Ratchets: every repeat of a step is inside the step (start and end), the count is 2…Repeats, the repeats are
    // evenly spaced, and with a ramp the velocity moves from the hit's toward hit · (100 + Ramp) / 100.
    for (int swing : {0, 60}) {
        for (int repeats : {2, 3, 4}) {
            Song s = plain;
            s.groups[0].ratchet = 100, s.groups[0].repeats = repeats, s.groups[0].swing = swing, s.groups[0].ramp = 100;
            std::map<std::int64_t, std::vector<Note>> cells;  // step → its notes
            for (const Note& n : notes(s, 0, bt * 64)) cells[n.start / 240].push_back(n);
            CHECK(cells.size() == 16 * 64);
            std::map<int, int> counts;
            bool inside = true, even = true, ramps = true;
            for (const auto& [k, ns] : cells) {
                const int n = static_cast<int>(ns.size());
                ++counts[n];
                const std::int64_t tick = k * 240 + ((k & 1) ? swing * 240 / 200 : 0), span = (k + 1) * 240 - tick;
                for (int i = 0; i < n; ++i) {
                    const Note& x = ns[static_cast<std::size_t>(i)];
                    inside = inside && x.start >= k * 240 && x.end <= (k + 1) * 240 && x.end > x.start;
                    even = even && x.start == tick + i * span / n;
                    if (i) ramps = ramps && x.velocity >= ns[static_cast<std::size_t>(i - 1)].velocity && x.note == ns[0].note;
                }
                ramps = ramps && ns.back().velocity == std::min(127, 2 * ns[0].velocity);
            }
            CHECK(inside && even && ramps);
            for (const auto& [n, c] : counts) CHECK(n >= 2 && n <= repeats);
            for (int n = 2; n <= repeats; ++n) CHECK(counts[n] > static_cast<int>(cells.size()) / (repeats - 1) * 2 / 3);  // each count turns up
        }
    }
    {
        // Ramp −100 fades to 1; Ratchet's chance is about right, nested, and leaves the steps it doesn't ratchet alone.
        Song s = plain;
        s.groups[0].ratchet = 100, s.groups[0].ramp = -100;
        std::map<std::int64_t, std::vector<Note>> cells;
        for (const Note& n : notes(s, 0, bt * 4)) cells[n.start / 240].push_back(n);
        bool fades = true;
        for (const auto& kv : cells) fades = fades && kv.second.back().velocity == 1 && kv.second.front().velocity > 1;
        CHECK(fades);
        std::set<std::int64_t> before;
        for (int r : {20, 50}) {
            Song t = plain;
            t.groups[0].ratchet = r;
            std::map<std::int64_t, int> per;
            for (const Note& n : notes(t, 0, bt * bars)) ++per[n.start / 240];
            std::set<std::int64_t> ratcheted;
            for (const auto& [k, c] : per)
                if (c > 1) ratcheted.insert(k);
            const double got = 100.0 * static_cast<double>(ratcheted.size()) / static_cast<double>(per.size());
            std::printf("  ratchet %d %%: %.1f %% of steps ratcheted\n", r, got);
            CHECK(got > r - 3.0 && got < r + 3.0);
            CHECK(std::includes(ratcheted.begin(), ratcheted.end(), before.begin(), before.end()));
            before = ratcheted;
        }
    }

    // Fills: on the bar and phrase clocks, busier (more onsets, the plain ones all still there), and nothing else moves.
    {
        Song base = oneLine(0xF111);
        base.groups[0].density = 4;
        const std::int64_t b4 = barTicks(base);
        auto starts = [&](const Song& s, std::int64_t bar) {
            std::set<std::int64_t> out;
            for (const Note& n : notes(s, bar * b4, (bar + 1) * b4)) out.insert(n.start - bar * b4);
            return out;
        };
        auto check = [&](const Song& s, const std::function<bool(std::int64_t)>& filled) {
            for (std::int64_t bar = 0; bar < 16; ++bar) {
                const auto a = starts(base, bar), f = starts(s, bar);
                if (filled(bar)) CHECK(f.size() > a.size() && std::includes(f.begin(), f.end(), a.begin(), a.end()));
                else CHECK(notes(s, bar * b4, (bar + 1) * b4) == notes(base, bar * b4, (bar + 1) * b4));
            }
        };
        Song f = base;
        f.groups[0].fillEvery = 1;  // the last bar of every phrase (4 bars)
        check(f, [](std::int64_t b) { return b % 4 == 3; });
        f.groups[0].fillEvery = 2, f.groups[0].fillBars = 4;  // the whole last phrase of every 8 bars
        check(f, [](std::int64_t b) { return b % 8 >= 4; });
        Song now = base;
        now.groups[0].fillNow = true;  // by hand: every bar
        check(now, [](std::int64_t) { return true; });
        Song song = base;
        song.fill = 1;  // the song's switch (the host parameter)
        check(song, [](std::int64_t) { return true; });
        // A fill raises Ratchet too: Fill amount 16 is +50 %.
        Song strong = base;
        strong.groups[0].fillNow = true, strong.groups[0].fillAmount = 16;
        std::map<std::int64_t, int> per;
        for (const Note& n : notes(strong, 0, b4 * 64)) ++per[n.start / 240];
        int ratcheted = 0;
        for (const auto& kv : per) ratcheted += kv.second > 1;
        CHECK(per.size() == 16 * 64 && ratcheted > static_cast<int>(per.size()) * 2 / 5 && ratcheted < static_cast<int>(per.size()) * 3 / 5);
        // The matrix: a Fill row that turns it on for a bar, every 4 bars on bar 2.
        Song m = base;
        Row& r = addRow(m);
        r.scope = Scope::Group, r.target = m.groups[0].id, r.param = Param::Fill, r.every = 4, r.offset = 1, r.lasts = 1;
        CHECK(!stateAt(m, 0).groups[0].fillNow && stateAt(m, 1).groups[0].fillNow && !stateAt(m, 2).groups[0].fillNow &&
              stateAt(m, 5).groups[0].fillNow);
        check(m, [](std::int64_t b) { return b > 0 && b % 4 == 1; });
        // And Probability, Ratchet and Repeats as numeric targets, inside their bounds.
        for (Param p : {Param::Probability, Param::Ratchet, Param::Repeats}) {
            Song n = base;
            Row& q = addRow(n);
            q.scope = Scope::Group, q.target = n.groups[0].id, q.param = p, q.every = 1, q.maxShift = 30;
            std::set<int> seen;
            for (std::int64_t bar = 0; bar < 32; ++bar) {
                const Song st = stateAt(n, bar);  // a copy: a reference into the temporary would dangle
                const Group& g = st.groups[0];
                const int x = p == Param::Probability ? g.probability : p == Param::Ratchet ? g.ratchet : g.repeats;
                CHECK(p == Param::Repeats ? x >= 2 && x <= 4 : x >= 0 && x <= 100);
                seen.insert(x);
            }
            CHECK(seen.size() >= 3);
        }
        // The song's Fill as a target of the song.
        Song gm = base;
        Row& gr = addRow(gm);
        gr.scope = Scope::Global, gr.param = Param::Fill, gr.every = 8, gr.offset = 7, gr.lasts = 1;
        CHECK(stateAt(gm, 7).fill == 1 && stateAt(gm, 8).fill == 0);
    }
    // Out of range from outside: into range.
    Song h = defaultSong(3);
    h.groups[0].probability = 300, h.groups[0].ratchet = -4, h.groups[0].repeats = 9, h.groups[0].ramp = -500, h.groups[0].fillEvery = 99;
    h.groups[0].fillBars = 0, h.groups[0].fillAmount = 0, h.fill = 7;
    sanitize(h);
    CHECK(h.groups[0].probability == 100 && h.groups[0].ratchet == 0 && h.groups[0].repeats == 4 && h.groups[0].ramp == -100);
    CHECK(h.groups[0].fillEvery == 16 && h.groups[0].fillBars == 1 && h.groups[0].fillAmount == 1 && h.fill == 1);
}

// The matrix's own seed, rows from bar 1, the seed's choice of chords, Octave jumps.
static void evolve() {
    // Generate gives the matrix new rolls; Lock matrix keeps them.
    Song s = defaultSong(42);
    const std::uint64_t m0 = s.matrixSeed;
    generate(s);
    CHECK(s.matrixSeed != m0);
    s.matrixLocked = true;
    const std::uint64_t m1 = s.matrixSeed;
    generate(s);
    CHECK(s.matrixSeed == m1);

    // A row on bar 0: the song starts as written, and the row acts from its next turn.
    Song g = defaultSong(7);
    g.matrix.clear();
    Row& r = addRow(g);
    r.scope = Scope::Group, r.target = g.groups[0].id, r.param = Param::Density, r.every = 4, r.maxShift = 3, r.min = 1, r.max = 16;
    CHECK(stateAt(g, 0).groups[0].density == g.groups[0].density && stateAt(g, 4).groups[0].density != g.groups[0].density);

    // Which chords: the seed chooses them from the pool, not just their order.
    auto chordSets = [] {
        std::set<std::vector<int>> sets;
        for (std::uint64_t seed = 1; seed <= 200; ++seed) {
            Song x = defaultSong(seed * 0x9E3779B97F4A7C15ull);
            std::vector<int> p = progression(x);
            std::sort(p.begin(), p.end());
            p.erase(std::unique(p.begin(), p.end()), p.end());
            sets.insert(p);
        }
        return sets.size();
    };
    CHECK(chordSets() >= 3);

    // Octave jumps: 0 keeps every note in the home lane; lanes are nested as it goes up (one notch, one more jump).
    bool none = true, nested = true;
    for (int slot = 0; slot < 64; ++slot)
        for (int d = 1; d <= 16; ++d) {
            none = none && octaveLaneAt(99, slot, d, 0) == 0;
            for (int j = 0; j < 16; ++j)
                if (octaveLaneAt(99, slot, d, j) != 0) nested = nested && octaveLaneAt(99, slot, d, j + 1) == octaveLaneAt(99, slot, d, j);
            nested = nested && octaveLaneAt(99, slot, d, 16) == octaveLaneAt(99, slot, d);
        }
    CHECK(none && nested);

    // A pedal voice measures from the key's tonic: at Range 1 it plays only the tonic, whatever the chord.
    Song p = defaultSong(5);
    Voice& bassVoice = p.groups[1].voices[0];
    bassVoice.notesGen.use = "pedal";
    p.groups[1].overrides.range = 1;
    bool tonic = true;
    for (const Note& n : notes(p, 0, barTicks(p) * 16))
        if (n.channel == bassVoice.channel) tonic = tonic && n.note % 12 == p.root;
    CHECK(tonic);
}

// §9.1: the Matrix screen's "Next: bar N" is the replay's own answer. After a Generate the matrix has its own seed
// (§7.3), so a row at a chance below 100 % fires on that seed's bars, not the master seed's; the bar the label names
// is the next bar the row's layer is on, and never bar 0 (the song starts as written).
static void nextFirings() {
    Song s = defaultSong(21);
    generate(s);
    CHECK(s.matrixSeed != s.masterSeed);
    Row& r = s.matrix[0];  // Leads · Vary Pitch · reverts after 1 bar: its letter is on exactly the bars it fires
    r.every = 1, r.inPhrases = false, r.offset = 0, r.chance = 50, r.lasts = 1;
    View view(s);
    const auto form = view.form(0, 64);
    int fired = 0;
    for (std::int64_t b = 0; b < 64; ++b) {
        const bool on = form[0][static_cast<std::size_t>(b)] != "A";
        // §7.3 written out: the roll is hash(matrixSeed, "matrix", rowID, bar), then "chance".
        const bool spec = b > 0 && roll(hash(hash(s.matrixSeed, {"matrix", r.id, b}), {"chance"})) < r.chance;
        CHECK(on == spec && on == fires(s, r, b));
        fired += on;
    }
    CHECK(fired > 16 && fired < 48);  // the chance roll is doing something
    for (std::int64_t b = 0; b < 60; ++b) {
        const Firing f = nextFiring(s, r, b);
        CHECK(f.bar >= std::max<std::int64_t>(1, b) && f.bar < 64);
        if (f.bar < 0 || f.bar >= 64) continue;
        CHECK(form[0][static_cast<std::size_t>(f.bar)] != "A");
        int quiet = 0;
        for (std::int64_t k = std::max<std::int64_t>(1, b); k < f.bar; ++k) quiet += form[0][static_cast<std::size_t>(k)] == "A";
        CHECK(quiet == f.skipped && quiet == f.bar - std::max<std::int64_t>(1, b));
    }
    // A row whose turn is bar 0 next fires on its next turn; one that's off never does.
    r.chance = 100, r.every = 4;
    CHECK(nextFiring(s, r, 0).bar == 4 && nextFiring(s, r, 0).skipped == 0);
    r.on = false;
    CHECK(nextFiring(s, r, 0).bar == -1 && !fires(s, r, 4));
}

// §5.1: Koenig's selection methods as deal modes. One group of four voices on channels 1–4, a hit on every step:
// the channels in time order are who played each hit.
static void dealing() {
    auto who = [](DealMode mode, int span, int bars) {
        Song s = defaultSong(11);
        s.matrix.clear();
        s.groups.resize(1);
        Group& g = s.groups[0];
        g.voices.resize(1);
        g.voices[0].channel = 1;
        for (int c = 2; c <= 4; ++c) addVoice(s, g, VoiceType::Lead, c);
        g.density = 16, g.steps = 16, g.deal = mode, g.dealSpan = span, g.swing = 0, g.jitter = 0;
        std::vector<int> out;
        for (const Note& n : notes(s, 0, barTicks(s) * bars)) out.push_back(n.channel);
        return out;
    };
    // Alea: a roll per hit, by weight; about a quarter each over 64 bars, and not in a fixed order.
    const auto alea = who(DealMode::Alea, 2, 64);
    int count[5] = {};
    for (int c : alea) ++count[c];
    for (int c = 1; c <= 4; ++c) CHECK(count[c] > static_cast<int>(alea.size()) / 5 && count[c] < static_cast<int>(alea.size()) / 3);
    CHECK(alea == who(DealMode::Alea, 2, 64));  // and the same every time
    // Series: each bag of four hits has every voice once, and no voice plays twice in a row.
    const auto series = who(DealMode::Series, 2, 16);
    bool bags = true, noRepeat = true;
    for (std::size_t b = 0; b + 4 <= series.size(); b += 4) bags = bags && std::set<int>(series.begin() + b, series.begin() + b + 4).size() == 4;
    for (std::size_t k = 1; k < series.size(); ++k) noRepeat = noRepeat && series[k] != series[k - 1];
    CHECK(bags && noRepeat);
    // Group: runs of 3 hits on one voice, and the next run usually another (it looks one run back, so a repeat can
    // slip through, as in Koenig's Group, where each run is an Alea choice).
    const auto runs = who(DealMode::Group, 3, 16);
    bool same = true;
    int changes = 0;
    for (std::size_t k = 0; k < runs.size(); ++k) {
        if (k % 3) same = same && runs[k] == runs[k - 1];
        else if (k) changes += runs[k] != runs[k - 1];
    }
    CHECK(same && changes * 10 >= static_cast<int>(runs.size() / 3) * 8);  // at least 80 % of runs change voice
    // Tendency: a window of 2 voices that moves one voice per pass, back and forth: pass 0 is voices 1–2, pass 1 2–3,
    // pass 2 3–4, pass 3 2–3 again.
    const auto sweep = who(DealMode::Tendency, 2, 4);
    auto within = [&](int pass, int lo, int hi) {
        for (int k = pass * 16; k < pass * 16 + 16; ++k)
            if (sweep[static_cast<std::size_t>(k)] < lo || sweep[static_cast<std::size_t>(k)] > hi) return false;
        return true;
    };
    CHECK(within(0, 1, 2) && within(1, 2, 3) && within(2, 3, 4) && within(3, 2, 3));
}

// §3.7: colour notes, and what Colour does to the three rankings.
static void colour() {
    auto scale = [](const char* name) {
        for (std::size_t i = 0; i < kScales.size(); ++i)
            if (std::string(kScales[i].name) == name) return static_cast<int>(i);
        return -1;
    };
    auto names = [](const std::vector<int>& pcs) {
        std::string s;
        for (int pc : pcs) s += (s.empty() ? "" : " ") + noteName(pc);
        return s;
    };
    const int F = 5;
    CHECK(names(colourNotes(F, scale("Phrygian"))) == "F#");  // G♭
    CHECK(names(colourNotes(F, scale("Dorian"))) == "D");
    CHECK(names(colourNotes(F, scale("Lydian"))) == "B");
    CHECK(names(colourNotes(F, scale("Mixolydian"))) == "D#");
    CHECK(names(colourNotes(F, scale("Locrian"))) == "F# B");
    CHECK(names(colourNotes(F, scale("Harmonic Minor"))) == "E");
    CHECK(names(colourNotes(F, scale("Phrygian Dominant"))) == "F# A");
    for (const char* plain : {"Major", "Minor", "Minor Pentatonic", "Major Pentatonic", "Whole Tone"}) CHECK(colourNotes(F, scale(plain)).empty());

    // At Colour 0 every ranking is ranked(), for every scale and root: songs from before Colour play as they did.
    Song s = defaultSong(7);
    s.colour = 0;
    bool same = true;
    for (int sc = 0; sc < static_cast<int>(kScales.size()); ++sc)
        for (int root = 0; root < 12; ++root) {
            s.scale = sc, s.root = root;
            same = same && harmonyRanked(s) == ranked(root, root, sc);
            for (int r : ranked(root, root, sc)) same = same && lineRanked(s, r) == ranked(r, root, sc) && chordRanked(s, r) == ranked(r, root, sc);
        }
    CHECK(same);

    // At full Colour the progression's first chords are the mode's own: F Phrygian i, ♭vii, ♭II (not C, which has no
    // fifth there); F Dorian i, ii, IV; F Lydian I, vii, II; F Mixolydian I, v, ♭VII.
    s.root = F, s.colour = 16;
    auto firstThree = [&](const char* m) {
        s.scale = scale(m);
        const auto h = harmonyRanked(s);
        return names({h[0], h[1], h[2]});
    };
    CHECK(firstThree("Phrygian") == "F D# F#");
    CHECK(firstThree("Dorian") == "F G A#");
    CHECK(firstThree("Lydian") == "F E G");
    CHECK(firstThree("Mixolydian") == "F C D#");
    s.scale = scale("Phrygian");
    // At the back: C (no fifth there) and D♭, the parent major's tonic (the chord that would make it sound like D♭).
    {
        const auto h = harmonyRanked(s);
        CHECK(h[h.size() - 2] == 0 && h.back() == 1);
    }
    s.scale = scale("Minor");  // plain minor has no parent to avoid: its relative major (A♭) keeps its place
    {
        const auto h = harmonyRanked(s), base = ranked(F, F, scale("Minor"));
        CHECK(std::find(h.begin(), h.end(), 8) - h.begin() == std::find(base.begin(), base.end(), 8) - base.begin());
    }
    s.scale = scale("Phrygian");
    // A line over F reaches the ♭2 right after the chord tones; halfway, it's on its way there.
    CHECK(names(lineRanked(s, F)) == "F C G# F# D# A# C#");
    s.colour = 8;
    CHECK(names(lineRanked(s, F)) == "F C G# D# A# F# C#");
    // And Colour never leaves the scale: every note of a Phrygian song at full Colour is in F Phrygian.
    s.colour = 16;
    bool inside = true;
    for (const Note& n : notes(s, 0, barTicks(s) * 32)) inside = inside && ((kScales[static_cast<std::size_t>(s.scale)].mask >> ((n.note - F + 120) % 12)) & 1);
    CHECK(inside);
}

// §3.8: Mode moves. Off, nothing changes; on (with Colour), the mode's move chord sits by the tonic, the relative
// major's V–I is rare, a chord with no fifth plays (in fourths, or as it is over a tonic pedal), and Phrygian's home
// chord may turn major after ♭II.
static void modeMoves() {
    auto scale = [](const char* name) {
        for (std::size_t i = 0; i < kScales.size(); ++i)
            if (std::string(kScales[i].name) == name) return static_cast<int>(i);
        return -1;
    };
    auto names = [](const std::vector<int>& pcs) {
        std::string s;
        for (int pc : pcs) s += (s.empty() ? "" : " ") + noteName(pc);
        return s;
    };
    const int F = 5;
    Song s = defaultSong(7);
    CHECK(s.modeMoves == 0);  // off for a new song too: the golden render is unchanged
    s.root = F, s.colour = 12;

    // Off, and on at Colour 0, the rankings and the progression are §3.7's exactly, for every scale and root.
    bool same = true;
    for (int sc = 0; sc < static_cast<int>(kScales.size()); ++sc)
        for (int root = 0; root < 12; ++root) {
            Song a = s;
            a.scale = sc, a.root = root, a.colour = 0;
            Song b = a;
            b.modeMoves = 1;
            same = same && harmonyRanked(a) == harmonyRanked(b) && progression(a) == progression(b) && moveChords(b).empty();
            for (int r : ranked(root, root, sc)) same = same && chordRanked(a, r) == chordRanked(b, r);
        }
    CHECK(same);

    // The move chords: a major chord holding a colour note, never the relative major's tonic.
    s.modeMoves = 1;
    auto moves = [&](const char* m) {
        s.scale = scale(m);
        return names(moveChords(s));
    };
    CHECK(moves("Lydian") == "G");        // II
    CHECK(moves("Dorian") == "A#");       // IV
    CHECK(moves("Phrygian") == "F#");     // ♭II
    CHECK(moves("Mixolydian") == "D#");   // ♭VII
    CHECK(moves("Harmonic Minor") == "C");  // V (its major third is the colour)
    CHECK(moves("Minor").empty() && moves("Major").empty() && moves("Minor Pentatonic").empty());
    // ...and they come straight after the tonic, with nothing sent to the back: F Phrygian's C (no fifth) and D♭ (the
    // relative major) are in the ranking at their places, for the weights to judge.
    s.scale = scale("Phrygian");
    {
        const auto h = harmonyRanked(s);
        CHECK(h[0] == F && h[1] == 6 && h.size() == 7);
    }

    // The weights: the relative major's V–I is 1; the move chord next to home outweighs a plain chord.
    s.scale = scale("Lydian");  // F Lydian's relative major is C, its V is G
    CHECK(moveWeight(s, 7, 0, false) == 1);
    CHECK(moveWeight(s, 9, 0, false) > 1);  // A to C is fine
    CHECK(moveWeight(s, F, 7, false) > moveWeight(s, F, 9, false) && moveWeight(s, 2, 7, true) > moveWeight(s, 2, 7, false));

    // Over many seeds at Range 16: G→C (V–I in C) far rarer, and G next to F far more common, than with moves off.
    auto count = [&](int on, int& vi, int& lean, int& pairs) {
        vi = lean = pairs = 0;
        for (std::uint64_t seed = 1; seed <= 400; ++seed) {
            Song t = defaultSong(seed * 0x9E3779B97F4A7C15ull);
            t.root = F, t.scale = scale("Lydian"), t.colour = 12, t.modeMoves = on;
            t.harmony.range = 16, t.harmony.length = 8;
            const auto p = progression(t), rk = harmonyRanked(t);
            CHECK(p[0] == 0);  // still home first
            for (std::size_t k = 0; k < p.size(); ++k) {
                const int a = rk[static_cast<std::size_t>(p[k])], b = rk[static_cast<std::size_t>(p[(k + 1) % p.size()])];
                vi += a == 7 && b == 0;
                lean += (a == F && b == 7) || (a == 7 && b == F);
                ++pairs;
            }
        }
    };
    int viOff, leanOff, pairsOff, viOn, leanOn, pairsOn;
    count(0, viOff, leanOff, pairsOff);
    count(1, viOn, leanOn, pairsOn);
    CHECK(viOff >= 6 && viOn * 2 < viOff);
    CHECK(leanOn > leanOff * 3 / 2);

    // A chord with no fifth: in fourths, stacked (not folded), with no tonic pedal; as §3.7 has it over one.
    s.scale = scale("Phrygian");  // C has no fifth in F Phrygian (G♭, not G)
    CHECK(names(chordRanked(s, 0)) == "C F A# D# G# C# F#");  // the whole scale, in fourths
    Voice cv = s.groups[2].voices[0];
    cv.chordSize = 4, cv.voicing = 1;
    {
        const auto n = chordNotes(s, cv, 0);
        CHECK(n.size() == 4 && n[1] - n[0] == 5 && n[2] - n[1] == 5 && n[3] - n[2] == 5);
    }
    s.groups[1].voices[0].notesGen.use = "pedal";
    CHECK(names(chordRanked(s, 0)).substr(0, 7) == "C F# D#");  // the diminished chord, over the tonic
    s.groups[1].voices[0].notesGen.use = "wander";
    s.modeMoves = 0;
    CHECK(names(chordRanked(s, 0)).substr(0, 7) == "C F# D#");  // moves off: §3.7's chord
    s.modeMoves = 1;
    // The tonic is never in fourths: Locrian keeps its diminished home chord (it has seven notes, so its colour).
    s.scale = scale("Locrian");
    CHECK(names(colourNotes(F, s.scale)) == "F# B");
    {
        Song off = s;
        off.modeMoves = 0;
        CHECK(chordRanked(s, F) == chordRanked(off, F) && names(chordRanked(s, F)).substr(0, 6) == "F B G#");
    }

    // Phrygian: a home chord after ♭II may turn major. Every one that does follows ♭II, sounds A (not A♭) in its
    // chord, and no line plays A♭ while it sounds; elsewhere every note is in F Phrygian.
    s.scale = scale("Phrygian");
    int majors = 0, homes = 0;
    bool afterFlat2 = true, inside = true, third = true;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        Song t = defaultSong(seed * 0x9E3779B97F4A7C15ull);
        t.root = F, t.scale = s.scale, t.colour = 16, t.modeMoves = 1, t.harmony.length = 8;
        const auto p = progression(t), rk = harmonyRanked(t);
        const auto maj = homeMajor(t, p);
        for (std::size_t k = 0; k < p.size(); ++k) {
            if (p[k] != 0) continue;
            ++homes;
            if (!maj[k]) continue;
            ++majors;
            afterFlat2 = afterFlat2 && rk[static_cast<std::size_t>(p[(k + p.size() - 1) % p.size()])] == 6;
        }
        if (seed > 12) continue;
        for (const Note& n : notes(t, 0, changeTicks(t) * 8)) {
            const std::size_t k = static_cast<std::size_t>(n.start / changeTicks(t)) % p.size();
            const int pc = n.note % 12;
            if (maj[k]) third = third && pc != 8;  // A♭
            else inside = inside && ((kScales[static_cast<std::size_t>(t.scale)].mask >> ((pc - F + 12) % 12)) & 1);
        }
    }
    CHECK(majors > 0 && majors < homes && afterFlat2 && inside && third);
    {
        // Named as what it is: F7 for the dominant home chord.
        Song t = s;
        t.colour = 16;
        CHECK(chordName(t, F, 4, true) == "F7" && chordName(t, F, 4, false) == "Fm7");
    }

    // A matrix target of the song (pick-one, Off / On).
    Song m = defaultSong(3);
    m.matrix.clear();
    Row& r = addRow(m);
    r.scope = Scope::Global, r.param = Param::ModeMoves, r.every = 2, r.allowed = {1}, r.lasts = 1;
    CHECK(stateAt(m, 1).modeMoves == 0 && stateAt(m, 2).modeMoves == 1 && stateAt(m, 3).modeMoves == 0);
    // Out of range in a song from outside: clamped.
    m.modeMoves = 7;
    sanitize(m);
    CHECK(m.modeMoves == 1);
}

// §3.4a: Voicing "nearest". Each change takes the inversion closest to the chord before; a tie is the seed's; each pass
// starts on the fixed Voicing; the chords are the same chords, only their inversions move.
static void nearestVoicing() {
    // C major to F major: C F A (one voice still, two by a step), not F A C.
    CHECK(nearestInversion({60, 64, 67}, {65, 69, 72}, 1) == v({60, 65, 69}));
    // Nothing before: the chord as it is.
    CHECK(nearestInversion({}, {65, 69, 72}, 1) == v({65, 69, 72}));
    // A tie: C F♯ to E♭ A is 6 either way (A E♭ below or E♭ A above). Drawn from the seed, the same every time.
    std::set<std::vector<int>> seen;
    for (std::uint64_t h = 0; h < 32; ++h) {
        const auto x = nearestInversion({60, 66}, {63, 69}, h);
        CHECK(x == nearestInversion({60, 66}, {63, 69}, h));
        CHECK(x == v({57, 63}) || x == v({63, 69}));
        seen.insert(x);
    }
    CHECK(seen.size() == 2);

    // In a song: the chord sounding at each change (Sustain: common tones tie over, so it's what's on at the change).
    auto chords = [](const Song& s, int bars) {
        const Voice& cv = s.groups[2].voices[0];
        const std::vector<Note> ns = notes(s, 0, barTicks(s) * bars);
        std::vector<std::vector<int>> out;
        for (std::int64_t t = 0; t < barTicks(s) * bars; t += changeTicks(s)) {
            std::vector<int> on;
            for (const Note& n : ns)
                if (n.voice == cv.id && n.start <= t && n.end > t) on.push_back(n.note);
            std::sort(on.begin(), on.end());
            out.push_back(on);
        }
        return out;
    };
    // How far the voices move from change to change, inside a pass (each pass starts afresh, by design).
    auto moved = [](const std::vector<std::vector<int>>& cs) {
        int d = 0;
        for (std::size_t k = 1; k < cs.size(); ++k)
            if (k % 8 != 0 && cs[k].size() == cs[k - 1].size())
                for (std::size_t i = 0; i < cs[k].size(); ++i) d += std::abs(cs[k][i] - cs[k - 1][i]);
        return d;
    };
    int less = 0, total = 0, movedFixed = 0, movedNear = 0;
    bool samePcs = true, passStart = true, register_ = true, slices = true;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        Song fixed = defaultSong(seed * 0x9E3779B97F4A7C15ull);
        fixed.harmony.length = 8, fixed.harmony.range = 12;
        fixed.groups[2].voices[0].chordSize = 3;  // triads: where the fold into one octave (§3.4) jumps most
        Song near = fixed;
        near.groups[2].voices[0].nearest = true;
        const auto a = chords(fixed, 16), b = chords(near, 16);
        for (std::size_t k = 0; k < a.size(); ++k) {
            std::set<int> pa, pb;
            for (int n : a[k]) pa.insert(n % 12);
            for (int n : b[k]) pb.insert(n % 12);
            samePcs = samePcs && pa == pb && a[k].size() == b[k].size();
            if (k % 8 == 0) passStart = passStart && a[k] == b[k];  // each pass starts on the fixed Voicing
            if (!a[k].empty() && !b[k].empty()) register_ = register_ && b[k].front() >= a[k].front() - 12 && b[k].back() <= a[k].back() + 12;
        }
        ++total;
        less += moved(b) < moved(a);
        movedFixed += moved(a), movedNear += moved(b);
        CHECK(moved(b) <= moved(a));  // never further, for any of these songs
        // A jump lands where the song would have been: bars 5–12 on their own are the same notes as in 0–12.
        if (seed <= 8) {
            const std::int64_t bt = barTicks(near);
            std::vector<Note> whole, part = notes(near, bt * 5, bt * 12);
            for (const Note& n : notes(near, 0, bt * 12))
                if (n.start >= bt * 5) whole.push_back(n);
            slices = slices && whole == part;
        }
    }
    CHECK(samePcs && passStart && register_ && slices);
    CHECK(less * 10 >= total * 8);           // it moves less for at least 80 % of seeds
    CHECK(movedNear * 3 < movedFixed * 2);   // and under two thirds as far over all of them
    std::printf("  nearest voicing: voices move %d semitones over 40 songs, against %d in the fixed Voicing; less in %d of %d\n", movedNear,
                movedFixed, less, total);
    // Off: exactly the fixed Voicing (the golden render checks the default song).
    // A matrix target of a Chord voice: pick-one, Fixed / Nearest.
    Song m = defaultSong(3);
    m.matrix.clear();
    Row& r = addRow(m);
    r.scope = Scope::Voice, r.target = m.groups[2].voices[0].id, r.param = Param::VoiceLeading, r.every = 2, r.allowed = {1}, r.lasts = 1;
    CHECK(!stateAt(m, 1).groups[2].voices[0].nearest && stateAt(m, 2).groups[2].voices[0].nearest && !stateAt(m, 3).groups[2].voices[0].nearest);
}

// §3.4b: Stagger and Top. The spec's worked values (tools/spec_arith.py, "§3.4b Stagger" and "§3.4b Top"), the
// rules around them (a held note doesn't move, a leaving note lets go at its partner's start, nothing crosses a
// strike, a Pulse note keeps its length), replay, and off: bit for bit.
static void staggerAndTop() {
    // A song whose progression starts Cmaj7, G7, Am7: C major, the Chord voice at its default base, size 4, Voicing 1,
    // Sustain, changes every bar.
    Song base;
    bool found = false;
    for (std::uint64_t seed = 1; seed <= 4000 && !found; ++seed) {
        Song s = defaultSong(seed * 0x9E3779B97F4A7C15ull);
        s.matrix.clear();
        s.root = 0, s.scale = 0;  // C major
        s.harmony.length = 4, s.harmony.range = 16, s.harmony.wander = 16;
        const std::int64_t bt = barTicks(s);
        if (chordRootAt(s, 0) == 0 && chordRootAt(s, bt) == 7 && chordRootAt(s, 2 * bt) == 9) base = s, found = true;
    }
    CHECK(found);
    if (!found) return;
    const std::int64_t bt = barTicks(base);
    const std::uint64_t cid = base.groups[2].voices[0].id;
    auto voiceNotes = [&](const Song& s, std::int64_t from, std::int64_t to) {
        std::vector<Note> out;
        for (const Note& n : notes(s, from, to))
            if (n.voice == cid) out.push_back(n);
        return out;
    };
    auto startOf = [&](const std::vector<Note>& ns, int note, std::int64_t lo, std::int64_t hi) -> std::int64_t {
        for (const Note& n : ns)
            if (n.note == note && n.start >= lo && n.start < hi) return n.start;
        return -1;
    };
    auto endOf = [&](const std::vector<Note>& ns, int note, std::int64_t lo, std::int64_t hi) -> std::int64_t {
        for (const Note& n : ns)
            if (n.note == note && n.end >= lo && n.end < hi) return n.end;
        return -1;
    };
    auto with = [&](int S, StaggerOrder o, StaggerLean l, int top = 0) {
        Song s = base;
        Voice& cv = s.groups[2].voices[0];
        cv.stagger = S, cv.staggerOrder = o, cv.staggerLean = l, cv.top = top;
        return s;
    };
    const int C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, A3 = 57, B3 = 59;
    {  // the chords themselves, as the worked values have them
        const auto ns = voiceNotes(base, 0, 3 * bt);
        CHECK(startOf(ns, C3, 0, 1) == 0 && startOf(ns, E3, 0, 1) == 0 && startOf(ns, G3, 0, 1) == 0 && startOf(ns, B3, 0, 1) == 0);
        CHECK(startOf(ns, D3, bt, bt + 1) == bt && startOf(ns, F3, bt, bt + 1) == bt);
        CHECK(startOf(ns, C3, 2 * bt, 2 * bt + 1) == 2 * bt && startOf(ns, E3, 2 * bt, 2 * bt + 1) == 2 * bt && startOf(ns, A3, 2 * bt, 2 * bt + 1) == 2 * bt);
    }
    const std::int64_t g = bt, a = 2 * bt;  // the changes to G7 and to Am7
    {  // Cmaj7 → G7, Stagger 8, Up, Late: D3 +0, F3 +120; C3 lets go at +0, E3 at +120; G3 and B3 are held
        const auto ns = voiceNotes(with(8, StaggerOrder::Up, StaggerLean::Late), 0, 3 * bt);
        CHECK(startOf(ns, D3, g - 200, g + 300) == g + 0 && startOf(ns, F3, g - 200, g + 300) == g + 120);
        CHECK(endOf(ns, C3, g - 200, g + 300) == g + 0 && endOf(ns, E3, g - 200, g + 300) == g + 120);
        // G3 and B3 are held through the change: nothing of them starts or ends near it
        CHECK(startOf(ns, G3, g - 200, g + 300) < 0 && endOf(ns, G3, g - 200, g + 300) < 0 && startOf(ns, B3, g - 200, g + 300) < 0);
        // → Am7: C3 +0, E3 +60, A3 +120
        CHECK(startOf(ns, C3, a - 200, a + 300) == a + 0 && startOf(ns, E3, a - 200, a + 300) == a + 60 && startOf(ns, A3, a - 200, a + 300) == a + 120);
        // the fresh strike of Cmaj7 at the song's start: C3 +0 E3 +40 G3 +80 B3 +120
        CHECK(startOf(ns, C3, 0, 200) == 0 && startOf(ns, E3, 0, 200) == 40 && startOf(ns, G3, 0, 200) == 80 && startOf(ns, B3, 0, 200) == 120);
    }
    {  // Around: D3 −60, F3 +60, C3 lets go at −60, E3 at +60; → Am7: C3 −60, E3 +0, A3 +60; at the start, as Late
        const auto ns = voiceNotes(with(8, StaggerOrder::Up, StaggerLean::Around), 0, 3 * bt);
        CHECK(startOf(ns, D3, g - 200, g + 300) == g - 60 && startOf(ns, F3, g - 200, g + 300) == g + 60);
        CHECK(endOf(ns, C3, g - 200, g + 300) == g - 60 && endOf(ns, E3, g - 200, g + 300) == g + 60);
        CHECK(startOf(ns, C3, a - 200, a + 300) == a - 60 && startOf(ns, E3, a - 200, a + 300) == a + 0 && startOf(ns, A3, a - 200, a + 300) == a + 60);
        CHECK(startOf(ns, C3, 0, 200) == 0 && startOf(ns, B3, 0, 200) == 120);
        // a span that ends just before a change still has the notes Around starts before it
        const auto part = voiceNotes(with(8, StaggerOrder::Up, StaggerLean::Around), g - 100, g);
        CHECK(part.size() == 1 && part[0].note == D3 && part[0].start == g - 60);
    }
    {  // Stagger 16: D3 +0, F3 +240. Down at the start: B3 +0 G3 +80 E3 +160 C3 +240
        const auto ns = voiceNotes(with(16, StaggerOrder::Up, StaggerLean::Late), 0, 3 * bt);
        CHECK(startOf(ns, D3, g - 200, g + 300) == g && startOf(ns, F3, g - 200, g + 300) == g + 240);
        const auto dn = voiceNotes(with(8, StaggerOrder::Down, StaggerLean::Late), 0, bt);
        CHECK(startOf(dn, B3, 0, 200) == 0 && startOf(dn, G3, 0, 200) == 40 && startOf(dn, E3, 0, 200) == 80 && startOf(dn, C3, 0, 200) == 120);
    }
    {  // Seeded: the four places of a fresh strike, in the seed's order, the same on every replay, and kept by Generate
        Song s = with(8, StaggerOrder::Seeded, StaggerLean::Late);
        const auto ns = voiceNotes(s, 0, bt);
        std::set<std::int64_t> at;
        for (int n : {C3, E3, G3, B3}) at.insert(startOf(ns, n, 0, 200));
        CHECK(at == (std::set<std::int64_t>{0, 40, 80, 120}));
        CHECK(voiceNotes(s, 0, bt) == ns);
        Song r = s;
        r.groups[2].voices[0].pitchSeed ^= 0x5555;  // a new pitch seed: the same chords here, the same hand
        CHECK(voiceNotes(r, 0, bt) == ns);
    }
    {  // Restrike hits every 1/16 (gap 240), Stagger 16: the spread is held to 120. Nothing crosses a strike.
        Song s = with(16, StaggerOrder::Up, StaggerLean::Late);
        Group& cg = s.groups[2];
        cg.steps = 16, cg.density = 16, cg.probability = 100, cg.swing = 0;
        cg.voices[0].articulation = Articulation::RestrikeHits;
        const auto ns = voiceNotes(s, 0, 2 * bt);
        std::map<std::int64_t, std::vector<int>> byStart;
        for (const Note& n : ns) byStart[n.start].push_back(n.note);
        // a restrike inside the first bar, not on a change: Cmaj7 rolled over 120
        const std::int64_t t = 4 * 240;
        CHECK(startOf(ns, C3, t, t + 1) == t && startOf(ns, E3, t, t + 240) == t + 40 && startOf(ns, G3, t, t + 240) == t + 80 &&
              startOf(ns, B3, t, t + 240) == t + 120);
        bool inside = true;
        for (const Note& n : ns) inside = inside && n.start % 240 <= 120;
        CHECK(inside);
    }
    {  // Top on Cmaj7 at velocity v: Top 8 → C3 v, E3 v−4, G3 v−4, B3 v+8; Top 16 → +16 / −8
        const auto off = voiceNotes(base, 0, 1);
        CHECK(off.size() == 4);
        const int v0 = off.empty() ? 0 : off[0].velocity;
        for (int T : {8, 16}) {
            const auto ns = voiceNotes(with(0, StaggerOrder::Up, StaggerLean::Late, T), 0, 1);
            std::map<int, int> vel;
            for (const Note& n : ns) vel[n.note] = n.velocity;
            CHECK(vel[C3] == v0 && vel[E3] == v0 - T / 2 && vel[G3] == v0 - T / 2 && vel[B3] == std::min(127, v0 + T));
        }
    }
    {  // Pulse: every note keeps its length, moved by its offset, and plays the same notes
        Song p0 = base;
        p0.groups[2].voices[0].articulation = Articulation::Pulse;
        p0.groups[2].density = 8;
        Song p1 = p0;
        p1.groups[2].voices[0].stagger = 12;
        const auto a0 = voiceNotes(p0, 0, 8 * bt), a1 = voiceNotes(p1, 0, 8 * bt);
        std::multiset<std::tuple<int, std::int64_t>> l0, l1;
        for (const Note& n : a0) l0.insert({n.note, n.end - n.start});
        for (const Note& n : a1) l1.insert({n.note, n.end - n.start});
        CHECK(!a0.empty() && l0 == l1);
        bool moved = false;
        for (const Note& n : a1) moved = moved || n.start % 60 != 0;
        CHECK(moved || a1 != a0);
    }
    {  // Replay: a span on its own is the notes of the whole that start in it, Around and Seeded included
        for (StaggerLean l : {StaggerLean::Late, StaggerLean::Around}) {
            Song s = with(11, StaggerOrder::Seeded, l, 5);
            s.groups[2].voices[0].articulation = Articulation::RestrikeAccents;
            std::vector<Note> whole;
            for (const Note& n : voiceNotes(s, 0, 12 * bt))
                if (n.start >= 5 * bt - 7) whole.push_back(n);
            CHECK(whole == voiceNotes(s, 5 * bt - 7, 12 * bt));
        }
    }
    {  // Off: Order, Lean at Stagger 0 and Top 0 change nothing, bit for bit
        Song s = with(0, StaggerOrder::Seeded, StaggerLean::Around, 0);
        CHECK(notes(s, 0, 16 * bt) == notes(base, 0, 16 * bt));
    }
    // The change matrix: Stagger and Top numeric, Order and Lean pick-one, by their keys.
    CHECK(std::string(paramKey(Param::Stagger)) == "stagger" && std::string(paramKey(Param::StaggerOrder)) == "staggerOrder" &&
          std::string(paramKey(Param::StaggerLean)) == "staggerLean" && std::string(paramKey(Param::Top)) == "top");
    CHECK(isNumericParam(Param::Stagger) && isNumericParam(Param::Top) && isPickParam(Param::StaggerOrder) && isPickParam(Param::StaggerLean));
    CHECK(levelMayDrive("stagger") && levelMayDrive("top") && !levelMayDrive("staggerOrder"));
    Song m = base;
    Row& r = addRow(m);
    r.scope = Scope::Voice, r.target = cid, r.param = Param::Stagger, r.every = 2, r.maxShift = 4, r.min = 0, r.max = 16;
    bool walks = false;
    for (std::int64_t b = 2; b <= 24; b += 2) walks = walks || stateAt(m, b).groups[2].voices[0].stagger > 0;
    CHECK(stateAt(m, 1).groups[2].voices[0].stagger == 0 && walks);
    m.groups[2].voices[0].stagger = 99, m.groups[2].voices[0].top = -3;
    m.groups[2].voices[0].staggerOrder = static_cast<StaggerOrder>(7);
    sanitize(m);
    CHECK(m.groups[2].voices[0].stagger == 16 && m.groups[2].voices[0].top == 0 && m.groups[2].voices[0].staggerOrder == StaggerOrder::Seeded);
}

// §4.7: humanize. Off, nothing moves. On, a voice's notes keep their pitches and order, move by up to ±Velocity, and
// sound late inside their own step; a note that ended at the next hit still does, and the other voices don't change.
static void humanize() {
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Song off = defaultSong(seed * 0x9E3779B97F4A7C15ull);
        off.groups[0].swing = seed % 2 ? 0 : 40;  // and with swing: its odd steps have less room
        Song on = off;
        Voice& lead = on.groups[0].voices[0];
        lead.humanVelocity = 16, lead.humanTiming = 100;
        on.groups[2].voices[0].humanVelocity = 10;  // the chord: velocity only
        const std::int64_t bt = barTicks(off), end = bt * 16;
        const std::vector<Note> a = notes(off, 0, end), b = notes(on, 0, end);
        auto of = [](const std::vector<Note>& ns, std::uint64_t id) {
            std::vector<Note> out;
            for (const Note& n : ns)
                if (n.voice == id) out.push_back(n);
            return out;
        };
        const std::uint64_t id = lead.id;
        const auto la = of(a, id), lb = of(b, id);
        CHECK(la.size() == lb.size() && !la.empty());
        if (la.size() != lb.size()) continue;
        int moved = 0, louder = 0, softer = 0;
        bool inside = true, pitches = true, order = true, noOverlap = true, velRange = true;
        for (std::size_t k = 0; k < la.size(); ++k) {
            pitches = pitches && la[k].note == lb[k].note && la[k].channel == lb[k].channel;
            // Late, never early, and before the next grid line (steps are 240 ticks; the grid is at multiples of 240).
            inside = inside && lb[k].start >= la[k].start && lb[k].start < (la[k].start / 240 + 1) * 240;
            moved += lb[k].start != la[k].start;
            velRange = velRange && std::abs(lb[k].velocity - la[k].velocity) <= 16 && lb[k].velocity >= 1 && lb[k].velocity <= 127;
            louder += lb[k].velocity > la[k].velocity, softer += lb[k].velocity < la[k].velocity;
            if (k + 1 < la.size()) {
                order = order && lb[k].start < lb[k + 1].start;
                if (la[k].end <= la[k + 1].start) noOverlap = noOverlap && lb[k].end <= lb[k + 1].start;
            }
            CHECK(lb[k].end > lb[k].start);
        }
        CHECK(pitches);
        CHECK(inside);
        CHECK(order);
        CHECK(noOverlap);
        CHECK(velRange);
        CHECK(moved * 2 > static_cast<int>(la.size()) && louder > 0 && softer > 0);
        // Every other voice plays exactly as before, but the chord's velocities move.
        for (const Group& g : on.groups)
            for (const Voice& v : g.voices) {
                if (v.id == id) continue;
                const auto x = of(a, v.id), y = of(b, v.id);
                CHECK(x.size() == y.size());
                bool same = x.size() == y.size(), timing = x.size() == y.size();
                for (std::size_t k = 0; k < x.size() && k < y.size(); ++k) {
                    same = same && x[k] == y[k];
                    timing = timing && x[k].start == y[k].start && x[k].end == y[k].end && x[k].note == y[k].note;
                }
                if (v.type == VoiceType::Chord) CHECK(timing && !same);
                else CHECK(same);
            }
        // Replayable: a slice from bar 5 is the same notes as bars 5–12 of the whole.
        std::vector<Note> whole, part = notes(on, bt * 5, bt * 12);
        for (const Note& n : notes(on, 0, bt * 12))
            if (n.start >= bt * 5 && n.start < bt * 12) whole.push_back(n);
        std::vector<Note> partIn;
        for (const Note& n : part)
            if (n.start < bt * 12) partIn.push_back(n);
        CHECK(whole == partIn);
        // Its own stream: Generate re-rolls the notes, not the feel. Where the voice plays the same grid position
        // before and after a Generate, its note sounds the same number of ticks late.
        auto lateBy = [&](const Song& x) {
            Song plain = x;
            plain.groups[0].voices[0].humanTiming = 0, plain.groups[0].voices[0].humanVelocity = 0;
            std::map<std::int64_t, std::int64_t> late;
            const auto p0 = of(notes(plain, 0, end), id), p1 = of(notes(x, 0, end), id);
            for (std::size_t k = 0; k < p0.size() && k < p1.size(); ++k) late[p0[k].start] = p1[k].start - p0[k].start;
            return late;
        };
        Song g1 = on;
        g1.groups[0].lockRhythm = g1.groups[0].lockDeal = true;  // the same hits, the same owners; new notes
        generate(g1);
        const auto before = lateBy(on), after = lateBy(g1);
        int shared = 0;
        bool sameFeel = true;
        for (const auto& [t, d] : before)
            if (after.count(t)) ++shared, sameFeel = sameFeel && after.at(t) == d;
        CHECK(shared > 4 && sameFeel);
    }
    // Inside its step, at full Timing, over many songs and every line voice: never at or past the next grid line.
    bool inStep = true;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        Song x = defaultSong(seed * 0x2545F4914F6CDD1Dull);
        Song plain = x;
        for (Group& g : x.groups)
            for (Voice& v : g.voices) v.humanTiming = 100;
        const auto a = notes(plain, 0, barTicks(x) * 32), b = notes(x, 0, barTicks(x) * 32);
        std::map<std::uint64_t, std::vector<std::int64_t>> sa, sb;
        for (const Note& n : a) sa[n.voice].push_back(n.start);
        for (const Note& n : b) sb[n.voice].push_back(n.start);
        for (auto& [vid, starts] : sa) {
            std::sort(starts.begin(), starts.end());
            std::sort(sb[vid].begin(), sb[vid].end());
            for (std::size_t k = 0; k < starts.size() && k < sb[vid].size(); ++k)
                inStep = inStep && sb[vid][k] >= starts[k] && sb[vid][k] < (starts[k] / 240 + 1) * 240;
        }
    }
    CHECK(inStep);
    // A matrix target of a voice (numeric).
    Song m = defaultSong(3);
    m.matrix.clear();
    Row& r = addRow(m);
    r.scope = Scope::Voice, r.target = m.groups[0].voices[0].id, r.param = Param::HumanTiming, r.every = 2, r.maxShift = 30, r.min = 0, r.max = 100;
    CHECK(stateAt(m, 1).groups[0].voices[0].humanTiming == 0 && stateAt(m, 2).groups[0].voices[0].humanTiming > 0);
    m.groups[0].voices[0].humanVelocity = 99, m.groups[0].voices[0].humanTiming = -4;
    sanitize(m);
    CHECK(m.groups[0].voices[0].humanVelocity == 32 && m.groups[0].voices[0].humanTiming == 0);
}


// §16: generator slots. The defaults are today's behaviour word for word, so every song plays as it did; a slot's
// choice is a name; and a destination can be named by key (§17.2).
static void generatorSlots() {
    const std::string path = std::string(HOCKET_GOLDEN_DIR) + "/default-0123456789abcdef.txt";
    std::ifstream f(path, std::ios::binary);
    std::stringstream want;
    want << f.rdbuf();
    // §16.1: the default song with every slot set explicitly to its default (its `use` the default's name, no
    // parameters) renders the golden file.
    Song explicitDefaults = defaultSong(0x0123456789ABCDEFull);
    for (Group& g : explicitDefaults.groups) {
        g.rhythmGen.use = "random";
        for (Voice& v : g.voices)
            if (v.type != VoiceType::Chord) v.notesGen.use = "wander";
    }
    CHECK(golden(explicitDefaults, 64) == want.str());
    // A name this build doesn't know, or another slot's, plays the default (§16.9); so do parameters it doesn't know.
    Song unknown = defaultSong(0x0123456789ABCDEFull);
    unknown.groups[0].rhythmGen.use = "from-the-future";
    unknown.groups[1].rhythmGen.use = "wander";  // a Notes generator in a Rhythm slot
    unknown.groups[0].voices[0].notesGen.use = "random";
    unknown.groups[0].voices[1].notesGen.use = "kotekan";
    unknown.groups[0].rhythmGen.params["euclid"]["pulses"] = 5;
    unknown.groups[1].voices[0].notesGen.params["wander"]["colour"] = 3;
    CHECK(golden(unknown, 64) == want.str());
    // Every slot plays its default when it names nothing.
    const Song one = defaultSong(1);
    for (const Group& g : one.groups) {
        CHECK(std::string(playing(SlotKind::Rhythm, g.rhythmGen).name) == "random");
        for (const Voice& v : g.voices) CHECK(std::string(playing(SlotKind::Notes, v.notesGen).name) == "wander");
    }
    // The defaults are §4.1 and §3.3 themselves.
    const Song nine = defaultSong(9);
    const Group& leads = nine.groups[0];
    CHECK(rhythmHits(leads, 10) == hits(leads.rhythmSeed, 16, 10));
    CHECK(notePositions(leads.voices[0], 16, 4, 8) == noteIndices(hash(leads.voices[0].pitchSeed, {"notes"}), 16, 4, 8));
    CHECK(findGenerator(SlotKind::Rhythm, "wander") == nullptr && findGenerator(SlotKind::Notes, "random") == nullptr);

    // §17.2: every param has its key, and the key names it back.
    for (int i = 0; i <= static_cast<int>(kLastParam); ++i) {
        const Param p = static_cast<Param>(i);
        CHECK(std::string(paramKey(p)).size() > 0 && paramFromKey(paramKey(p)) == p);
    }
    CHECK(std::string(paramKey(Param::DealMode)) == "deal" && std::string(paramKey(Param::VaryRhythm)) == "vary.rhythm" &&
          std::string(paramKey(Param::HumanTiming)) == "humanTiming" && std::string(paramKey(Param::VoiceLeading)) == "nearest");
    CHECK(!paramFromKey("rhythm.euclid.pulses") && !paramFromKey("mute"));
    // A row by key is the row by its param.
    Song byParam = defaultSong(0x0123456789ABCDEFull), byKey = byParam;
    Row d = row(Scope::Group, byParam.groups[0].id, Param::Density);
    d.every = 1, d.maxShift = 3;
    byParam.matrix.push_back(d);
    d.param = Param::Range, d.key = "density";
    byKey.matrix.push_back(d);
    sanitize(byKey);
    CHECK(byKey.matrix.back().param == Param::Density && byKey.matrix.back().key.empty());
    CHECK(notes(byKey, 0, barTicks(byKey) * 16) == notes(byParam, 0, barTicks(byParam) * 16));
    // A key this build doesn't know does nothing, and stays as it was.
    Song plain = defaultSong(0x0123456789ABCDEFull), odd = plain;
    for (const char* key : {"rhythm.euclid.nope", "rhythm.random.pulses", "notes.wander.dice", "sideways", "harmony.grammar.depth"}) {
        Row u = row(Scope::Group, plain.groups[0].id, Param::Density);
        u.key = key, u.every = 1, u.maxShift = 4;
        odd.matrix.push_back(u);
    }
    sanitize(odd);
    CHECK(odd.matrix.back().key == "harmony.grammar.depth");
    CHECK(notes(odd, 0, barTicks(odd) * 16) == notes(plain, 0, barTicks(plain) * 16));

    // §16.3's t: a group's steps from bar 0, run on through a re-anchor, each stretch at its own Beat division. A
    // 3/16 step (720 ticks) doesn't divide a 4/4 bar: six steps start in bar 0, so the step at the re-anchored bar 1 is
    // t 6 and the one after it 7, where without the re-anchor the grid would have put step 6 at 4320.
    Song clock = defaultSong(3);
    clock.matrix.clear();
    clock.groups[0].beatDivision = 8;  // 3/16
    CHECK(stepCount(clock, 0, 0) == 0 && stepCount(clock, 0, 3841) == 6 && stepCount(clock, 0, 4321) == 7);
    Row o = row(Scope::Group, clock.groups[0].id, Param::Offset);
    o.every = 1, o.offset = 1, o.maxShift = 1, o.min = 1, o.max = 1;  // bar 1 moves Offset: a re-anchor there
    clock.matrix.push_back(o);
    CHECK(stateAt(clock, 1).groups[0].offset == 1);
    CHECK(stepCount(clock, 0, 3840) == 6 && stepCount(clock, 0, 3841) == 7 && stepCount(clock, 0, 4561) == 8);
    // Bar 2 doesn't re-anchor (Offset stays 1), so its steps run on from bar 1's anchor: t 12 at 8160, not the 11 of an
    // unbroken grid from 0.
    CHECK(stepCount(clock, 0, 2 * 3840 + 1) == 12);
}

static std::string pattern(const std::vector<bool>& h) {
    std::string out;
    for (bool x : h) out += x ? 'x' : '.';
    return out;
}

// The slots (0 … L−1) a one-group song's notes start on in bar `bar` (step length 240).
static std::set<int> slotsPlayed(const Song& s, std::int64_t bar) {
    std::set<int> out;
    const std::int64_t bt = barTicks(s);
    for (const Note& n : notes(s, bar * bt, (bar + 1) * bt)) out.insert(static_cast<int>(((n.start - bar * bt) / 240) % s.groups[0].steps));
    return out;
}

// §16.4, Euclidean: Pulses hits spread as evenly as the steps allow, turned by Rotation.
static void euclidean() {
    // The spec's worked values (tools/spec_arith.py, "§16.4 Euclidean").
    CHECK(pattern(euclidHits(8, 3, 0)) == "x..x..x.");
    CHECK(pattern(euclidHits(16, 5, 0)) == "x...x..x..x..x..");
    CHECK(pattern(euclidHits(16, 5, 2)) == "..x...x..x..x..x");
    // Maximally even for every L and Pulses: exactly k hits, gaps (round the cycle) differing by at most one; Rotation
    // turns the pattern a step a notch; Pulses past Steps is every step; Rotation is taken mod Steps.
    for (int L = 1; L <= 64; ++L)
        for (int k = 1; k <= L; ++k) {
            const std::vector<bool> h = euclidHits(L, k, 0);
            CHECK(std::count(h.begin(), h.end(), true) == k);
            std::vector<int> at;
            for (int i = 0; i < L; ++i)
                if (h[static_cast<std::size_t>(i)]) at.push_back(i);
            int lo = L, hi = 0;
            for (std::size_t i = 0; i < at.size(); ++i) {
                const int gap = ((at[(i + 1) % at.size()] - at[i]) % L + L) % L + (at.size() == 1 ? L : 0);
                lo = std::min(lo, gap), hi = std::max(hi, gap);
            }
            CHECK(hi - lo <= 1);
            const std::vector<bool> r = euclidHits(L, k, 3);
            for (int i = 0; i < L; ++i) CHECK(r[static_cast<std::size_t>((i + 3) % L)] == h[static_cast<std::size_t>(i)]);
        }
    CHECK(pattern(euclidHits(8, 40, 0)) == "xxxxxxxx" && euclidHits(16, 5, 18) == euclidHits(16, 5, 2));

    // In a song: the group plays the even pattern, with its frame (§16.2): accents and gates are the slot's, Density
    // isn't read, a fill adds no hits (only ratchets), Probability still thins it, and a swap away and back returns.
    Song s = oneLine(0xE7C11D);
    s.groups[0].rhythmGen.use = "euclid";
    s.groups[0].rhythmGen.params["euclid"] = {{"pulses", 5}, {"rotation", 2}};
    std::set<int> want;
    for (int i = 0; i < 16; ++i)
        if (euclidHits(16, 5, 2)[static_cast<std::size_t>(i)]) want.insert(i);
    for (std::int64_t bar = 0; bar < 4; ++bar) CHECK(slotsPlayed(s, bar) == want);
    const std::int64_t bt = barTicks(s);
    for (const Note& n : notes(s, 0, bt)) {
        const int slot = static_cast<int>(n.start / 240);
        CHECK(n.velocity == (accentAt(s.groups[0].rhythmSeed, slot) ? 113 : 70));
    }
    Song sparse = s;  // Density doesn't move a hit (it still sets the frame's gate classes, §16.2)
    sparse.groups[0].density = 3;
    for (std::int64_t bar = 0; bar < 4; ++bar) CHECK(slotsPlayed(sparse, bar) == want);
    Song fill = s;
    fill.groups[0].fillNow = true;
    CHECK(slotsPlayed(fill, 1) == want && notes(fill, 0, 4 * bt).size() > notes(s, 0, 4 * bt).size());  // ratchets only
    Song chance = s;
    chance.groups[0].probability = 50;
    std::size_t fewer = 0;
    for (std::int64_t bar = 0; bar < 8; ++bar) {
        const std::set<int> p = slotsPlayed(chance, bar);
        CHECK(std::includes(want.begin(), want.end(), p.begin(), p.end()));
        fewer += p.size() < want.size();
    }
    CHECK(fewer > 0);
    Song away = s;
    away.groups[0].rhythmGen.use = "random";
    CHECK(notes(away, 0, 4 * bt) == notes(oneLine(0xE7C11D), 0, 4 * bt));  // Random as if it had never been swapped
    away.groups[0].rhythmGen.use = "euclid";
    CHECK(notes(away, 0, 4 * bt) == notes(s, 0, 4 * bt));  // and back: the rotation is still there (§16.9)

    // §17.2: its knobs are destinations at its group. A row nudging Pulses moves them inside the row's bounds; at a
    // voice it does nothing; on a group playing Random it moves the stored value, which isn't heard until a swap.
    Song moved = s;
    Row r = row(Scope::Group, s.groups[0].id, Param::Range);
    r.key = "rhythm.euclid.pulses", r.every = 1, r.maxShift = 2, r.min = 3, r.max = 9;
    moved.matrix.push_back(r);
    std::set<int> seen;
    for (std::int64_t bar = 0; bar < 24; ++bar) {
        const int k = paramValue(SlotKind::Rhythm, stateAt(moved, bar).groups[0].rhythmGen, "euclid", "pulses");
        CHECK(k >= 3 && k <= 9);
        seen.insert(k);
        CHECK(static_cast<int>(slotsPlayed(moved, bar).size()) == k);
    }
    CHECK(seen.size() >= 3 && paramValue(SlotKind::Rhythm, stateAt(moved, 0).groups[0].rhythmGen, "euclid", "pulses") == 5);
    Song atVoice = s;
    r.scope = Scope::Voice, r.target = s.groups[0].voices[0].id;
    atVoice.matrix.push_back(r);
    CHECK(notes(atVoice, 0, 8 * bt) == notes(s, 0, 8 * bt));
    Song idle = moved;
    idle.groups[0].rhythmGen.use = "random";
    Song idlePlain = s;
    idlePlain.groups[0].rhythmGen.use = "random";
    CHECK(notes(idle, 0, 8 * bt) == notes(idlePlain, 0, 8 * bt));
    CHECK(paramValue(SlotKind::Rhythm, stateAt(idle, 6).groups[0].rhythmGen, "euclid", "pulses") !=
              paramValue(SlotKind::Rhythm, stateAt(idle, 0).groups[0].rhythmGen, "euclid", "pulses") ||
          paramValue(SlotKind::Rhythm, stateAt(idle, 7).groups[0].rhythmGen, "euclid", "pulses") != 5);
    // Rotation is a destination too; Generate never touches either knob, or the choice (§16.3 rule 5).
    Song gen = s;
    generate(gen);
    CHECK(gen.groups[0].rhythmGen == s.groups[0].rhythmGen && slotsPlayed(gen, 0) == want);
    // Vary Rhythm works on its output, through the mutation map (§16.2): a temporary vary changes the bar, then A.
    Song varied = s;
    Row v = row(Scope::Group, s.groups[0].id, Param::VaryRhythm);
    v.every = 4, v.offset = 3, v.maxShift = 2, v.lasts = 1;
    varied.matrix.push_back(v);
    CHECK(slotsPlayed(varied, 3) != want && slotsPlayed(varied, 3).size() == want.size() && slotsPlayed(varied, 4) == want);
}

// §16.4, Metric: Barlow's indispensability blended with the seeded order, on one knob.
static void metric() {
    // The spec's tables (tools/spec_arith.py, "§16.4 Metric"), which equal hmt's Barlow (1987) examples.
    CHECK(barlowUpper(2) == v({1, 0}) && barlowUpper(3) == v({2, 0, 1}) && barlowUpper(5) == v({4, 0, 3, 1, 2}));
    CHECK(barlowUpper(7) == v({6, 0, 4, 2, 5, 1, 3}));
    CHECK(barlowUpper(11) == v({10, 0, 6, 4, 9, 1, 7, 3, 8, 2, 5}));
    CHECK(barlowUpper(13) == v({12, 0, 7, 4, 10, 1, 8, 5, 11, 2, 9, 3, 6}));
    CHECK(indispensability({2, 2, 2, 2}) == v({15, 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7}));
    CHECK(indispensability({3, 2, 2}) == v({11, 0, 6, 3, 9, 1, 7, 4, 10, 2, 8, 5}));
    CHECK(indispensability({2, 3, 2}) == v({11, 0, 6, 2, 8, 4, 10, 1, 7, 3, 9, 5}));
    CHECK(indispensability({2, 2, 3}) == v({11, 0, 4, 8, 2, 6, 10, 1, 5, 9, 3, 7}));
    CHECK(indispensability({3, 5}) == v({14, 0, 9, 3, 6, 12, 1, 10, 4, 7, 13, 2, 11, 5, 8}));
    // The strata: the bar's beats first (ascending), then the beat's subdivisions (descending).
    CHECK(metricStrata(16, 4) == v({2, 2, 2, 2}) && metricStrata(12, 4) == v({3, 2, 2}) && metricStrata(12, 6) == v({2, 3, 2}));
    CHECK(metricStrata(14, 2) == v({7, 2}) && metricStrata(1, 4).empty() && metricStrata(12, 12) == v({3, 2, 2}));
    // A permutation for every L 1–64 and Beat 1–16.
    for (int L = 1; L <= 64; ++L)
        for (int B = 1; B <= 16; ++B) {
            std::vector<int> p = indispensability(metricStrata(L, B));
            std::sort(p.begin(), p.end());
            std::vector<int> want(static_cast<std::size_t>(L));
            std::iota(want.begin(), want.end(), 0);
            CHECK(p == want);
        }
    // Wild 0 is the meter: Density 4 the four beats, 8 adds the off-beats. Wild 16 is Random exactly. Density is nested
    // at any Wild; Steps not divisible by Beat still makes a meter (7/16 in 1/16s).
    const std::uint64_t seed = 0x0123456789ABCDEFull;
    CHECK(pattern(metricHits(seed, 16, 4, 0, 4)) == "x...x...x...x...");
    CHECK(pattern(metricHits(seed, 16, 8, 0, 4)) == "x.x.x.x.x.x.x.x.");
    CHECK(pattern(metricHits(seed, 12, 4, 0, 4)) == "x...x...x...");   // 3/4 in 1/16s: the three beats, then the next
    for (std::uint64_t sd : {seed, std::uint64_t{0x77}, std::uint64_t{0xC0FFEE}})
        for (int L : {5, 12, 16, 33, 64})
            for (int d = 1; d <= 16; ++d) {
                CHECK(metricHits(sd, L, d, 16, 4) == hits(sd, L, d));
                for (int w : {0, 6, 12}) {
                    const std::vector<bool> a = metricHits(sd, L, d, w, 4);
                    if (d < 16) {
                        const std::vector<bool> b = metricHits(sd, L, d + 1, w, 4);
                        for (int i = 0; i < L; ++i) CHECK(!a[static_cast<std::size_t>(i)] || b[static_cast<std::size_t>(i)]);
                    }
                }
            }
    // In between, the seed pulls hits off the grid: at Wild 8 some seeds leave a downbeat for an off-beat.
    int offGrid = 0;
    for (std::uint64_t sd = 1; sd <= 40; ++sd) {
        const std::vector<bool> h = metricHits(sd, 16, 4, 8, 4);
        offGrid += !(h[0] && h[4] && h[8] && h[12]);
    }
    CHECK(offGrid > 0 && offGrid < 40);

    // In a song, through its slot, with Wild and Beat as matrix destinations.
    Song s = oneLine(0x3E7A1C);
    s.groups[0].density = 4;
    s.groups[0].rhythmGen.use = "metric";
    s.groups[0].rhythmGen.params["metric"] = {{"wild", 0}};
    CHECK(slotsPlayed(s, 0) == std::set<int>({0, 4, 8, 12}) && slotsPlayed(s, 5) == std::set<int>({0, 4, 8, 12}));
    s.groups[0].rhythmGen.params["metric"]["wild"] = 16;
    Song random = oneLine(0x3E7A1C);
    random.groups[0].density = 4;
    CHECK(notes(s, 0, 4 * barTicks(s)) == notes(random, 0, 4 * barTicks(s)));
    Song moved = s;
    Row r = row(Scope::Group, s.groups[0].id, Param::Range);
    r.key = "rhythm.metric.wild", r.every = 1, r.maxShift = 4, r.min = 0, r.max = 8;
    moved.matrix.push_back(r);
    std::set<int> wilds;
    for (std::int64_t bar = 1; bar < 24; ++bar) {
        const int w = paramValue(SlotKind::Rhythm, stateAt(moved, bar).groups[0].rhythmGen, "metric", "wild");
        CHECK(w >= 0 && w <= 8);
        wilds.insert(w);
    }
    CHECK(wilds.size() >= 3);
}

// (start in the bar, note) of a one-group song's notes in a bar.
static std::vector<std::pair<std::int64_t, int>> heardNotes(const Song& s, std::int64_t bar) {
    std::vector<std::pair<std::int64_t, int>> out;
    const std::int64_t bt = barTicks(s);
    for (const Note& n : notes(s, bar * bt, (bar + 1) * bt)) out.push_back({n.start - bar * bt, n.note});
    return out;
}

static std::string bitsOf(const Register& r, std::int64_t a, std::int64_t z) {
    std::string out;
    for (std::int64_t t = a; t < z; ++t) out += (t > a && (t - a) % 8 == 0 ? " " : "") + std::to_string(r.bit(t));
    return out;
}
static Register stepped(std::uint64_t seed, int T, int n, int lock) {
    Register r(seed);
    while (r.next() < T) r.step(n, lock);
    return r;
}
// The start ticks of a song's notes in [from, to).
static std::vector<std::int64_t> starts(const Song& s, std::int64_t from, std::int64_t to) {
    std::vector<std::int64_t> out;
    for (const Note& n : notes(s, from, to)) out.push_back(n.start);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
// Every 1/16 step t in [0, T) a register with these settings plays a hit on (w bits, Density d).
static std::vector<std::int64_t> registerStarts(Register r, int T, int n, int lock, int w, int d) {
    std::vector<std::int64_t> out;
    while (r.next() < T) r.step(n, lock);
    for (std::int64_t t = 0; t < T; ++t)
        if (registerHit(r.value(t, w), w, d)) out.push_back(t * 240);
    return out;
}

// §16.7, the Register: one seed's shift register, from slipping to locked to inverted, for rhythm and notes.
static void registerGenerator() {
    const std::uint64_t seed = 0x0123456789ABCDEFull;
    // The spec's worked example (tools/spec_arith.py, "§16.7 Register"), n 8.
    CHECK(hash(seed, {"register", "init"}) == 0xf3a2d8a824f2c6abull);
    CHECK(bitsOf(Register(seed), -16, 0) == "11000110 10101011");  // b(−16 … −1); the spec lists b(−1 … −16)
    CHECK(bitsOf(stepped(seed, 32, 8, 16), 0, 32) == "10101011 10101011 10101011 10101011");
    CHECK(bitsOf(stepped(seed, 80, 8, 12), 0, 80) ==
          "10101011 10101011 10101011 10101011 10101011 10101011 10101011 10101011 00101011 00101011");
    CHECK(bitsOf(stepped(seed, 48, 8, 8), 0, 48) == "10101011 10101011 10101011 10101011 11101011 11111011");
    CHECK(bitsOf(stepped(seed, 32, 8, 0), 0, 32) == "00101110 10110101 11100110 10100100");
    CHECK(bitsOf(stepped(seed, 32, 8, 17), 0, 32) == "01010100 10101011 01010100 10101011");
    {
        const Register r = stepped(seed, 32, 8, 16);
        std::string h, p8, p3;
        for (int t = 0; t < 32; ++t) h += registerHit(r.value(t, 8), 8, 8) ? 'x' : '.';
        for (int t = 0; t < 16; ++t) p8 += std::to_string(registerPosition(r.value(t, 8), 8, 4)), p3 += std::to_string(registerPosition(r.value(t, 3), 3, 4));
        CHECK(h == ".x.x.x...x.x.x...x.x.x...x.x.x..");
        CHECK(p8 == "3121212331212123" && p3 == p8);
    }
    const Register any(seed);
    for (const auto& [lock, want] : std::vector<std::pair<int, int>>{{16, 0}, {12, 129}, {8, 531}, {0, 2054}, {17, 4096}}) {
        int n = 0;
        for (int t = 0; t < 4096; ++t) n += any.flips(t, lock);
        CHECK(n == want);
    }
    {
        Register r = stepped(seed, 16, 8, 16);
        const std::vector<int> back = registerVary(hash(seed, {"example vary"}), 0x42, 2, 8);
        CHECK(back == v({7, 4}));  // b(8) and b(11)
        r.flip(back);
        while (r.next() < 32) r.step(8, 16);
        CHECK(bitsOf(r, 0, 32) == "10101011 00111011 00111011 00111011");
    }

    // In a song: the group plays its register's hits, one per step from bar 0, whatever the Direction (a clocked
    // generator runs forward through t; Direction moves only the frame, §16.3).
    Song s = oneLine(0xBE7101);
    s.groups[0].density = 8;
    s.groups[0].rhythmGen.use = "register";
    s.groups[0].rhythmGen.params["register"] = {{"length", 8}, {"lock", 16}};
    const std::int64_t bt = barTicks(s);
    const Register fresh(s.groups[0].rhythmSeed);
    CHECK(starts(s, 0, 8 * bt) == registerStarts(fresh, 128, 8, 16, 8, 8));
    Song back = s;
    back.groups[0].direction = Direction::Backward;
    CHECK(starts(back, 0, 8 * bt) == starts(s, 0, 8 * bt));
    // A fill reads the fill's Density: the bar with more on top (nested).
    Song filled = s;
    filled.groups[0].fillNow = true;
    {
        const std::vector<std::int64_t> a = starts(s, 0, 4 * bt), b = starts(filled, 0, 4 * bt);
        CHECK(b.size() > a.size() && std::includes(b.begin(), b.end(), a.begin(), a.end()));
    }
    // Slipping at Lock 12 it drifts; Lock moved to 16 by a row at bar 4 locks whatever it holds: from then on every bar
    // is the bar before it (n 16 is a bar of 1/16s). "Evolve it, then lock it."
    Song evolve = s;
    evolve.groups[0].rhythmGen.params["register"] = {{"length", 16}, {"lock", 12}};
    Row lockIt = row(Scope::Group, s.groups[0].id, Param::Range);
    lockIt.key = "rhythm.register.lock", lockIt.every = 64, lockIt.offset = 4, lockIt.maxShift = 4, lockIt.min = 16, lockIt.max = 16;
    evolve.matrix.push_back(lockIt);
    CHECK(paramValue(SlotKind::Rhythm, stateAt(evolve, 3).groups[0].rhythmGen, "register", "lock") == 12);
    CHECK(paramValue(SlotKind::Rhythm, stateAt(evolve, 4).groups[0].rhythmGen, "register", "lock") == 16);
    auto bar = [&](const Song& song, std::int64_t b) {
        std::vector<std::int64_t> out;
        for (std::int64_t x : starts(song, b * bt, (b + 1) * bt)) out.push_back(x - b * bt);
        return out;
    };
    for (std::int64_t b = 5; b < 16; ++b) CHECK(bar(evolve, b) == bar(evolve, 4));
    Song slipping = evolve;
    slipping.matrix.clear();
    int changed = 0;
    for (std::int64_t b = 5; b < 40; ++b) changed += bar(slipping, b) != bar(slipping, b - 1);
    CHECK(changed > 0);

    // One register per seed: a temporary reroll plays another seed's register for its bars, then the first comes back
    // exactly where it would have been, even while slipping.
    Song rerolled = slipping;
    Row rr = row(Scope::Group, s.groups[0].id, Param::RerollRhythm);
    rr.every = 64, rr.offset = 3, rr.lasts = 2, rr.fixedSeed = false;
    rerolled.matrix.push_back(rr);
    CHECK(starts(rerolled, 3 * bt, 5 * bt) != starts(slipping, 3 * bt, 5 * bt));
    CHECK(starts(rerolled, 0, 3 * bt) == starts(slipping, 0, 3 * bt) && starts(rerolled, 5 * bt, 24 * bt) == starts(slipping, 5 * bt, 24 * bt));

    // Vary flips bits (§16.7): a permanent Vary at Lock 16 locks the varied riff from t0 on; a temporary one plays the
    // flipped copy for its bars, then A comes back. Neither touches the mutation map.
    Song varied = s;
    varied.groups[0].rhythmGen.params["register"] = {{"length", 16}, {"lock", 16}};
    Row vr = row(Scope::Group, s.groups[0].id, Param::VaryRhythm);
    vr.every = 64, vr.offset = 4, vr.maxShift = 3;
    varied.matrix.push_back(vr);
    {
        Register want(s.groups[0].rhythmSeed);
        while (want.next() < 64) want.step(16, 16);
        want.flip(registerVary(hash(varied.matrixSeed, {"matrix", vr.id}), s.groups[0].id, 3, 16));
        // From t0 on (bar 4): the flipped bits. Before it, the bars played the register as it was.
        std::vector<std::int64_t> from4;
        for (std::int64_t x : registerStarts(want, 256, 16, 16, 8, 8))
            if (x >= 4 * bt) from4.push_back(x);
        CHECK(starts(varied, 4 * bt, 16 * bt) == from4);
    }
    Song plain = varied;
    plain.matrix.clear();
    CHECK(bar(varied, 4) != bar(plain, 4) && bar(varied, 9) == bar(varied, 4) && bar(varied, 3) == bar(plain, 3));
    Song layer = varied;
    layer.matrix[0].lasts = 2;
    CHECK(bar(layer, 4) == bar(varied, 4) && bar(layer, 5) == bar(varied, 5) && bar(layer, 6) == bar(plain, 6) && bar(layer, 12) == bar(plain, 12));
    CHECK(View(layer).form(4, 3)[0] == std::vector<std::string>({"B", "B", "A"}));

    // Round robin counts the hits as they played, bar after bar: two voices strictly take turns.
    Song robin = s;
    robin.groups[0].voices = defaultSong(0xBE7101).groups[0].voices;
    robin.groups[0].deal = DealMode::RoundRobin;
    robin.groups[0].rhythmGen.params["register"] = {{"length", 16}, {"lock", 4}};
    {
        int prev = 0, turns = 0, n = 0;
        for (const Note& note : notes(robin, 0, 12 * bt)) {
            if (n++ > 0) turns += note.channel != prev;
            prev = note.channel;
        }
        CHECK(n > 40 && turns == n - 1);
    }

    // Notes: Height order (§16.3), measured upwards from the centre. With centre A and the pool A, C#, E (A major,
    // Range 6 keeps three notes), positions 0, 1, 2 are MIDI 45, 49, 52 at transpose 0, Octave Low and High 0.
    Song tune = oneLine(0x70E);
    tune.root = 9, tune.scale = 0, tune.colour = 0, tune.harmony.range = 1, tune.octaveLow = 0, tune.octaveHigh = 0, tune.octaveJumps = 0;
    Voice& lead = tune.groups[0].voices[0];
    lead.octaveTranspose = 0, lead.overrides.range = 6;
    lead.notesGen.use = "register";
    lead.notesGen.params["register"] = {{"lock", 0}};
    std::set<int> heard;
    for (const Note& n : notes(tune, 0, 16 * bt)) heard.insert(n.note);
    CHECK(heard == std::set<int>({45, 49, 52}));
    // A locked register is a riff: its notes repeat every n steps (n 8 at Lock 16: every half bar).
    lead.notesGen.params["register"] = {{"lock", 16}, {"length", 8}};
    {
        std::vector<int> ns;
        for (const Note& n : notes(tune, 0, 8 * bt)) ns.push_back(n.note);
        CHECK(ns.size() == 128);
        bool riff = ns.size() == 128;
        for (std::size_t i = 8; i < ns.size(); ++i) riff = riff && ns[i] == ns[i - 8];
        CHECK(riff);
    }
    // Vary Pitch flips the notes register's bits.
    Song vp = tune;
    Row pr = row(Scope::Voice, lead.id, Param::VaryPitch);
    pr.every = 64, pr.offset = 2, pr.maxShift = 2;
    vp.matrix.push_back(pr);
    CHECK(heardNotes(vp, 1) == heardNotes(tune, 1) && heardNotes(vp, 3) != heardNotes(tune, 3));

    // Jump safety with registers, a lock row and a vary: any window is the slice of the whole.
    Song all = evolve;
    all.matrix.push_back(vr);
    all.groups[0].voices[0].notesGen.use = "register";
    const std::int64_t end = 24 * bt;
    const std::vector<Note> whole = notes(all, 0, end);
    for (std::int64_t cut : {std::int64_t{960}, std::int64_t{3840}, std::int64_t{1234}}) {
        std::vector<Note> pieces;
        for (std::int64_t a = 0; a < end; a += cut) {
            const std::vector<Note> part = notes(all, a, std::min(end, a + cut));
            pieces.insert(pieces.end(), part.begin(), part.end());
        }
        std::stable_sort(pieces.begin(), pieces.end(), [](const Note& a, const Note& b) {
            return std::tie(a.start, a.channel, a.note, a.end) < std::tie(b.start, b.channel, b.note, b.end);
        });
        CHECK(pieces == whole);
    }
}

// §16.5: 1/f Wander and Pedal.
static void pinkAndPedal() {
    const std::uint64_t seed = 0x0123456789ABCDEFull;
    // The spec's worked example (tools/spec_arith.py, "§16.5 1/f Wander"): the same 9 roots as Wander, the rest dice.
    Voice line;
    line.pitchSeed = seed;
    CHECK(notePositions(line, 16, 4, 8) == v({0, 0, 3, 0, 0, 0, 0, 3, 0, 2, 0, 1, 1, 2, 0, 1}));
    line.notesGen.use = "pink";
    CHECK(notePositions(line, 16, 4, 8) == v({0, 0, 2, 0, 0, 0, 0, 2, 0, 2, 0, 2, 2, 2, 0, 2}));
    CHECK(kindOf(SlotKind::Notes, line.notesGen) == GenKind::Static);
    line.notesGen.params["pink"]["loop"] = 1;
    CHECK(kindOf(SlotKind::Notes, line.notesGen) == GenKind::Clocked);
    // Loop is set by hand only: never a destination (§16.3).
    CHECK(!generatorTarget("notes.pink.loop") && generatorTarget("notes.pink.dice"));

    // Riff repeats each pass; Run never does, on the same roots.
    Song riff = oneLine(0x91F);
    riff.harmony.length = 1, riff.octaveJumps = 0;
    riff.groups[0].voices[0].notesGen.use = "pink";
    Song run = riff;
    run.groups[0].voices[0].notesGen.params["pink"]["loop"] = 1;
    CHECK(heardNotes(riff, 0) == heardNotes(riff, 1) && heardNotes(riff, 0) == heardNotes(riff, 5));
    CHECK(heardNotes(run, 0) == heardNotes(riff, 0) && heardNotes(run, 1) != heardNotes(run, 0));
    // A Vary Pitch on a Run does nothing, and pushes no layer: no letter on the form strip.
    Song rv = run;
    Row r = row(Scope::Voice, run.groups[0].voices[0].id, Param::VaryPitch);
    r.every = 4, r.offset = 3, r.maxShift = 4, r.lasts = 1;
    rv.matrix.push_back(r);
    CHECK(notes(rv, 0, 8 * barTicks(rv)) == notes(run, 0, 8 * barTicks(run)));
    CHECK(View(rv).form(3, 1)[0][0] == "A");
    Song rf = riff;  // on a Riff (static) it varies as ever
    r.target = riff.groups[0].voices[0].id;
    rf.matrix.push_back(r);
    CHECK(heardNotes(rf, 3) != heardNotes(riff, 3) && View(rf).form(3, 1)[0][0] == "B");

    // Pedal is Wander measured from the tonic: at Range 1 it holds the tonic under every chord; and it is a choice
    // in the Notes row, so Wander → Pedal → Wander gives the song back.
    Song p = oneLine(0x9ED);
    p.groups[0].voices[0].overrides.range = 1;
    p.groups[0].voices[0].notesGen.use = "pedal";
    for (const Note& n : notes(p, 0, 8 * barTicks(p))) CHECK(((n.note - p.root) % 12 + 12) % 12 == 0);
    p.groups[0].voices[0].notesGen.use = "wander";
    Song w = oneLine(0x9ED);
    w.groups[0].voices[0].overrides.range = 1;
    CHECK(notes(p, 0, 8 * barTicks(p)) == notes(w, 0, 8 * barTicks(w)));
}

// ---- §16.6 derived generators --------------------------------------------------------------------------------

// A note's (start, pitch) on every channel: what's heard, whoever plays it.
static std::map<std::int64_t, int> byStart(const std::vector<Note>& ns, std::uint64_t voice = 0) {
    std::map<std::int64_t, int> out;
    for (const Note& n : ns)
        if (voice == 0 || n.voice == voice) out[n.start] = n.note;
    return out;
}

static void derived() {
    // §16.6's worked values (tools/spec_arith.py, "§16.6 Kotekan"): A minor, chord root A, pool A E C G.
    const std::vector<int> pool = {9, 4, 0, 7}, A = {57, 60, 62, 64, 67, 69, 72, 71};
    const std::vector<std::vector<int>> want = {{57, 60, 60, 64, 67, 69, 72, 69},
                                                {64, 60, 60, 69, 67, 76, 72, 76},
                                                {67, 64, 64, 72, 69, 79, 76, 79},
                                                {60, 64, 64, 67, 69, 72, 74, 74}};
    for (int m = 0; m < 4; ++m)
        for (std::size_t i = 0; i < A.size(); ++i) CHECK(kotekanNote(m, pool, 9, A[i], m == 3 && i >= 6, 2) == want[static_cast<std::size_t>(m)][i]);
    CHECK(kotekanNote(1, {9}, 9, 57, false, 9) == 69 && kotekanNote(2, {9}, 9, 57, false, 9) == 69 && kotekanNote(3, {9}, 9, 57, false, 9) == 69);
    const std::vector<int> retarget = {60, 67, 67, 64, 72, 72, 79, 71};  // the chord moves to C: pool C G E B
    for (std::size_t i = 0; i < A.size(); ++i) CHECK(kotekanNote(1, {0, 7, 4, 11}, 0, A[i], false, 0) == retarget[i]);
    CHECK(generatorTarget("notes.kotekan.mode") && generatorTarget("notes.kotekan.lead") && !generatorTarget("notes.kotekan.source") &&
          !generatorTarget("rhythm.interlock.source"));

    // Interlock (tools/spec_arith.py, "§16.6 Interlock"): the source on Euclidean 5 of 16, this group the complement.
    Song il = defaultSong(0x1A7E);
    il.matrix.clear();
    il.groups.resize(2);
    il.groups[0].voices.resize(1);
    il.groups[0].rhythmGen.use = "euclid";
    il.groups[0].rhythmGen.params["euclid"]["pulses"] = 5;
    Group& bass = il.groups[1];
    bass.rhythmGen.use = "interlock";
    bass.rhythmGen.sources["interlock"] = il.groups[0].id;
    bass.density = 16;
    auto slotsOf = [](const Song& s, std::uint64_t voice, std::int64_t bar, std::int64_t st) {
        std::string out(static_cast<std::size_t>(barTicks(s) / st), '.');
        for (const Note& n : notes(s, bar * barTicks(s), (bar + 1) * barTicks(s)))
            if (n.voice == voice) out[static_cast<std::size_t>((n.start - bar * barTicks(s)) / st)] = 'x';
        return out;
    };
    const std::uint64_t lead = il.groups[0].voices[0].id, low = il.groups[1].voices[0].id;
    CHECK(kindIn(il, SlotKind::Rhythm, 1, -1) == GenKind::Clocked && interlockSource(il, 1) == std::optional<std::size_t>(0));
    for (std::int64_t bar : {0, 3, 9}) {
        CHECK(slotsOf(il, lead, bar, 240) == "x...x..x..x..x..");
        CHECK(slotsOf(il, low, bar, 240) == ".xxx.xx.xx.xx.xx");
    }
    Song il8 = il;
    il8.groups[1].beatDivision = 6, il8.groups[1].steps = 8;  // 1/8
    CHECK(slotsOf(il8, low, 2, 480) == ".x..x..x");
    // The source's Probability doesn't reach it: the parts keep their places when the source thins out.
    Song thin = il;
    thin.groups[0].probability = 40;
    CHECK(slotsOf(thin, low, 5, 240) == ".xxx.xx.xx.xx.xx" && slotsOf(thin, lead, 5, 240) != "x...x..x..x..x..");
    // Lower Density takes the complement in Random's order: nested.
    Song half = il;
    half.groups[1].density = 8;
    const std::vector<bool> r8 = hits(half.groups[1].rhythmSeed, 16, 8);
    std::string w8 = ".xxx.xx.xx.xx.xx";
    for (int i = 0; i < 16; ++i)
        if (!r8[static_cast<std::size_t>(i)]) w8[static_cast<std::size_t>(i)] = '.';
    CHECK(slotsOf(half, low, 1, 240) == w8);
    // No source, or a deleted one, is Random; a loop is broken at its last group in song order.
    Song none = il;
    none.groups[1].rhythmGen.sources.clear();
    Song rnd = il;
    rnd.groups[1].rhythmGen.use = "random";
    CHECK(notes(none, 0, 8 * barTicks(none)) == notes(rnd, 0, 8 * barTicks(rnd)) && kindIn(none, SlotKind::Rhythm, 1, -1) == GenKind::Static);
    Song loop = il;
    loop.groups[0].rhythmGen.use = "interlock";
    loop.groups[0].rhythmGen.sources["interlock"] = loop.groups[1].id;
    CHECK(interlockSource(loop, 0) == std::optional<std::size_t>(1) && !interlockSource(loop, 1));
    CHECK(std::string(playingIn(loop, SlotKind::Rhythm, 1, -1).name) == "random");
    Song self = il;
    self.groups[1].rhythmGen.sources["interlock"] = self.groups[1].id;
    CHECK(!interlockSource(self, 1));

    // Kotekan in a song: two Leads in one group on Round robin, B following A. A's note at each step is its line,
    // played or not, so the song with A playing every hit (Solo) shows it.
    for (int mode = 0; mode < 4; ++mode) {
        Song k = defaultSong(0xC07E + static_cast<std::uint64_t>(mode));
        k.matrix.clear();
        k.groups[0].deal = DealMode::RoundRobin;
        Voice& b = k.groups[0].voices[1];
        b.notesGen.use = "kotekan";
        b.notesGen.sources["kotekan"] = k.groups[0].voices[0].id;
        b.notesGen.params["kotekan"]["mode"] = mode;
        CHECK(kotekanSource(k, 0, 1) == (std::optional<std::pair<std::size_t, int>>(std::make_pair<std::size_t, int>(0, 0))));
        Song solo = k;
        solo.groups[0].deal = DealMode::Solo, solo.groups[0].soloMember = 0;
        const std::int64_t end = 16 * barTicks(k);
        const std::map<std::int64_t, int> a = byStart(notes(solo, 0, end), solo.groups[0].voices[0].id);
        const std::map<std::int64_t, int> got = byStart(notes(k, 0, end), b.id);
        CHECK(got.size() > 40);
        const int n = static_cast<int>(ranked(k.root, k.root, k.scale).size());
        int checked = 0, unisons = 0;
        for (const auto& [t, note] : got) {
            const int root = chordRootAt(k, t);
            const std::vector<int> rk = lineRanked(k, root);
            const std::vector<int> pl(rk.begin(), rk.begin() + std::min<int>(poolSize(n, k.range), static_cast<int>(rk.size())));
            const std::int64_t ct = changeTicks(k), next = (t / ct + 1) * ct;
            const bool ant = next - t <= 2 * 240;
            CHECK(a.count(t) && kotekanNote(mode, pl, root, a.at(t), ant, chordRootAt(k, next)) == note);
            checked += a.count(t) ? 1 : 0;
            unisons += a.count(t) && a.at(t) == note;
        }
        CHECK(checked == static_cast<int>(got.size()));
        // nyog cag: the two parts are one line split, the composite exactly A's line.
        if (mode == 0) CHECK(byStart(notes(k, 0, end)) == byStart(notes(solo, 0, end)));
        if (mode == 1) CHECK(unisons > 0 && unisons < checked);  // telu: the middle tone in unison, not the rest
        // An octave up: B's transpose over A's moves the figure.
        Song up = k;
        up.groups[0].voices[1].octaveTranspose = 2;
        const std::map<std::int64_t, int> got2 = byStart(notes(up, 0, end), b.id);
        bool octave = got2.size() == got.size();
        for (const auto& [t, note] : got) octave = octave && got2.count(t) && got2.at(t) == std::min(127, note + 12);
        CHECK(octave);
        // Jump safety: slices of the render are the render.
        std::vector<Note> parts;
        for (std::int64_t f = 0; f < end; f += 1234) {
            const std::vector<Note> x = notes(k, f, std::min(end, f + 1234));
            parts.insert(parts.end(), x.begin(), x.end());
        }
        std::vector<Note> whole = notes(k, 0, end);
        auto key = [](const Note& x) { return std::make_tuple(x.start, x.channel, x.note, x.end); };
        std::sort(parts.begin(), parts.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
        std::sort(whole.begin(), whole.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
        CHECK(parts == whole);
    }
    // The Play screen's lane shows a Kotekan's notes as it plays them (its source read at the step, not at tick 0).
    {
        Song k = defaultSong(0xC07E);
        k.matrix.clear();
        k.groups[0].deal = DealMode::RoundRobin;
        k.groups[0].voices[1].notesGen.use = "kotekan";
        k.groups[0].voices[1].notesGen.sources["kotekan"] = k.groups[0].voices[0].id;
        const std::int64_t at = 5 * barTicks(k) + 7;  // inside bar 5: the lane shows this bar's pass
        const std::map<std::int64_t, int> heardB = byStart(notes(k, 5 * barTicks(k), 6 * barTicks(k)), k.groups[0].voices[1].id);
        const Lane lane = View(k).lanes(at)[0];
        int matched = 0;
        for (int slot = 0; slot < lane.steps; ++slot) {
            const LaneCell& c = lane.cells[static_cast<std::size_t>(slot)];
            const std::int64_t t = 5 * barTicks(k) + slot * 240;
            if (c.hit && c.owner == 1 && heardB.count(t)) matched += c.note == heardB.at(t);
        }
        CHECK(matched == static_cast<int>(heardB.size()) && matched > 2);
    }
    // In another group, through Interlock: the Bass reads the Lead's line on its own steps.
    Song cross = il;
    Voice& bv = cross.groups[1].voices[0];
    bv.notesGen.use = "kotekan";
    bv.notesGen.sources["kotekan"] = cross.groups[0].voices[0].id;
    bv.notesGen.params["kotekan"]["mode"] = 0;
    CHECK(kotekanSource(cross, 1, 0).has_value());
    {
        Song every = cross;
        every.groups[0].rhythmGen.params["euclid"]["pulses"] = 16;  // A plays every step (its Density, which its octave lanes read, kept)
        every.groups[1].rhythmGen.sources.clear(), every.groups[1].density = 1;
        const std::map<std::int64_t, int> line = byStart(notes(every, 0, 8 * barTicks(every)), every.groups[0].voices[0].id);
        const int shift = 12 * (bv.octaveTranspose - cross.groups[0].voices[0].octaveTranspose);
        int seen = 0;
        for (const auto& [t, note] : byStart(notes(cross, 0, 8 * barTicks(cross)), bv.id)) {
            const int root = chordRootAt(cross, t);
            const std::vector<int> rk = lineRanked(cross, root);
            const int ps = std::min<int>(poolSize(static_cast<int>(ranked(cross.root, cross.root, cross.scale).size()), *cross.groups[1].overrides.range), static_cast<int>(rk.size()));
            CHECK(line.count(t) && kotekanNote(0, std::vector<int>(rk.begin(), rk.begin() + ps), root, line.at(t), false, 0) + shift == note);
            ++seen;
        }
        CHECK(seen > 30);
    }
    // No source plays Wander; so does a loop's last voice in song order, the other following it.
    Song kw = defaultSong(0xC0DE);
    kw.groups[0].voices[1].notesGen.use = "kotekan";
    Song ww = defaultSong(0xC0DE);
    CHECK(notes(kw, 0, 8 * barTicks(kw)) == notes(ww, 0, 8 * barTicks(ww)));
    Song kl = kw;
    kl.groups[0].voices[1].notesGen.sources["kotekan"] = kl.groups[0].voices[0].id;
    kl.groups[0].voices[0].notesGen.use = "kotekan";
    kl.groups[0].voices[0].notesGen.sources["kotekan"] = kl.groups[0].voices[1].id;
    CHECK(kotekanSource(kl, 0, 0).has_value() && !kotekanSource(kl, 0, 1) && kindIn(kl, SlotKind::Notes, 0, 1) == GenKind::Static);
    Song kb = kw;  // a Chord voice isn't a source
    kb.groups[0].voices[1].notesGen.sources["kotekan"] = kb.groups[2].voices[0].id;
    CHECK(!kotekanSource(kb, 0, 1));
    // Vary Pitch on a Kotekan does nothing and pushes no layer (§16.2); on a Kotekan with no source it varies Wander.
    Song kv = defaultSong(0xC07E);
    kv.matrix.clear();
    kv.groups[0].voices[1].notesGen.use = "kotekan";
    kv.groups[0].voices[1].notesGen.sources["kotekan"] = kv.groups[0].voices[0].id;
    Song kvr = kv;
    Row r = row(Scope::Voice, kv.groups[0].voices[1].id, Param::VaryPitch);
    r.every = 4, r.offset = 3, r.maxShift = 4, r.lasts = 1;
    kvr.matrix.push_back(r);
    CHECK(notes(kvr, 0, 8 * barTicks(kv)) == notes(kv, 0, 8 * barTicks(kv)) && View(kvr).form(3, 1)[0][0] == "A");
    // Mode is a destination: a row turning it changes the figure from its bar on.
    Song km = kv;
    Row mr = row(Scope::Voice, kv.groups[0].voices[1].id, Param::Range);
    mr.key = "notes.kotekan.mode", mr.every = 4, mr.maxShift = 3;
    km.matrix.push_back(mr);
    CHECK(notes(km, 0, 4 * barTicks(km)) == notes(kv, 0, 4 * barTicks(kv)) && notes(km, 4 * barTicks(km), 8 * barTicks(km)) != notes(kv, 4 * barTicks(kv), 8 * barTicks(kv)));
}

// §16.6, Resultant: the source group's composite in a band, doubled on this group's lines.
static void resultant() {
    // The worked example (tools/spec_arith.py, "§16.6 Resultant"): the Kotekan example's composite, two bands.
    const std::vector<int> comp = {57, 60, 62, 69, 67, 76, 72, 76};
    std::string hi, lo;
    for (int n : comp) {
        hi += resultantNote(n, 64, 72, 0) < 0 ? std::string(".") : std::to_string(n), hi += ' ';
        lo += resultantNote(n, 63, 57, 0) < 0 ? std::string(".") : std::to_string(n), lo += ' ';  // either order
    }
    CHECK(hi == ". . . 69 67 . 72 . " && lo == "57 60 62 . . . . . ");
    CHECK(resultantNote(70, 60, 72, 1) == 82 && resultantNote(73, 60, 72, 0) == -1 && resultantNote(120, 0, 127, 2) == 127);

    Song s = defaultSong(0x2E5);
    s.matrix.clear();
    Group& res = addGroup(s, "Resultant");
    Voice& r = addVoice(s, res, VoiceType::Lead, 5);
    r.octaveTranspose = 0;
    res.rhythmGen.use = "resultant";
    res.rhythmGen.sources["resultant"] = s.groups[0].id;
    res.rhythmGen.params["resultant"] = {{"low", 50}, {"high", 60}};  // these Leads sit in 36…81, most at 48…59
    const std::uint64_t rid = r.id;
    const std::int64_t end = 16 * barTicks(s);
    CHECK(resultantSource(s, 3) == std::optional<std::size_t>(0) && kindIn(s, SlotKind::Rhythm, 3, -1) == GenKind::Clocked);
    auto leadsNotes = [&](const Song& x) {
        std::vector<std::tuple<std::int64_t, std::int64_t, int, int>> out;
        for (const Note& n : notes(x, 0, end))
            if (n.channel == 1 || n.channel == 2) out.emplace_back(n.start, n.end, n.note, n.velocity);
        std::sort(out.begin(), out.end());
        return out;
    };
    auto resNotes = [&](const Song& x, int shift) {
        std::vector<std::tuple<std::int64_t, std::int64_t, int, int>> out;
        for (const Note& n : notes(x, 0, end))
            if (n.voice == rid) out.emplace_back(n.start, n.end, n.note - shift, n.velocity);
        std::sort(out.begin(), out.end());
        return out;
    };
    std::vector<std::tuple<std::int64_t, std::int64_t, int, int>> band;
    for (const auto& n : leadsNotes(s))
        if (std::get<2>(n) >= 50 && std::get<2>(n) <= 60) band.push_back(n);
    CHECK(band.size() > 20 && band.size() < leadsNotes(s).size());
    CHECK(resNotes(s, 0) == band);  // exactly the Leads' notes in the band, as played
    // An octave up at transpose +1; the source's other groups untouched.
    Song up = s;
    up.groups[3].voices[0].octaveTranspose = 1;
    CHECK(resNotes(up, 12) == band);
    // Muting the source leaves the inherent pattern alone.
    Song alone = s;
    alone.groups[0].mute = true;
    CHECK(leadsNotes(alone).empty() && resNotes(alone, 0) == band);
    // Accented: the band at this group's accented velocity.
    Song acc = s;
    acc.groups[3].rhythmGen.params["resultant"]["velocity"] = 1;
    bool loud = resNotes(acc, 0).size() == band.size();
    for (const auto& n : resNotes(acc, 0)) loud = loud && std::get<3>(n) == 113;
    CHECK(loud);
    // Its own mute, and a chain: a Resultant of the Resultant, at a wide band, plays what it plays.
    Song chain = s;
    chain.groups[3].mute = true;
    Group& r2 = addGroup(chain, "Again");
    Voice& v2 = addVoice(chain, r2, VoiceType::Bass, 6);
    v2.octaveTranspose = 0;
    r2.rhythmGen.use = "resultant";
    r2.rhythmGen.sources["resultant"] = chain.groups[3].id;
    r2.rhythmGen.params["resultant"] = {{"low", 0}, {"high", 127}};
    CHECK(resNotes(chain, 0).empty());
    std::vector<std::tuple<std::int64_t, std::int64_t, int, int>> again;
    for (const Note& n : notes(chain, 0, end))
        if (n.voice == v2.id) again.emplace_back(n.start, n.end, n.note, n.velocity);
    std::sort(again.begin(), again.end());
    CHECK(again == band);
    // Not live: a group on Resultant with no source is Random.
    Song none = s, rnd = s;
    none.groups[3].rhythmGen.sources.clear();
    rnd.groups[3].rhythmGen.use = "random";
    CHECK(notes(none, 0, end) == notes(rnd, 0, end));
    Song loop = s;  // the Leads on Resultant of it: a loop, broken at the later group (this one)
    loop.groups[0].rhythmGen.use = "resultant";
    loop.groups[0].rhythmGen.sources["resultant"] = loop.groups[3].id;
    CHECK(resultantSource(loop, 0).has_value() && !resultantSource(loop, 3));
    // Low and High are destinations: a row widening the band changes it from its bar on.
    Song moved = s;
    Row w = row(Scope::Group, s.groups[3].id, Param::Range);
    w.key = "rhythm.resultant.high", w.every = 4, w.maxShift = 24, w.min = 84, w.max = 84;
    moved.matrix.push_back(w);
    CHECK(generatorTarget("rhythm.resultant.low") && generatorTarget("rhythm.resultant.velocity") && !generatorTarget("rhythm.resultant.source"));
    auto heardIn = [&](const Song& x, std::int64_t a, std::int64_t b) {
        std::vector<std::pair<std::int64_t, int>> out;
        for (const Note& n : notes(x, a * barTicks(x), b * barTicks(x)))
            if (n.voice == rid) out.emplace_back(n.start, n.note);
        return out;
    };
    CHECK(heardIn(moved, 0, 4) == heardIn(s, 0, 4) && heardIn(moved, 4, 8) != heardIn(s, 4, 8));
    for (const auto& [t, n] : heardIn(moved, 4, 8)) CHECK(n >= 50 && n <= 84);
    // Jump safety.
    std::vector<Note> parts;
    for (std::int64_t f = 0; f < end; f += 1000) {
        const std::vector<Note> x = notes(s, f, std::min(end, f + 1000));
        parts.insert(parts.end(), x.begin(), x.end());
    }
    std::vector<Note> whole = notes(s, 0, end);
    auto key = [](const Note& x) { return std::make_tuple(x.start, x.channel, x.note, x.end); };
    std::sort(parts.begin(), parts.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
    std::sort(whole.begin(), whole.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
    CHECK(parts == whole);
}

// §5.5 and §5.6: the pitch decides who plays (Alternate, Bells), and everyone plays (Tutti).
static void partitions() {
    // The worked example (tools/spec_arith.py, "§5.5 Alternate and Bells"): A minor, Root A.
    const std::vector<int> tune = {57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 61}, places = {28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 30};
    for (std::size_t i = 0; i < tune.size(); ++i) CHECK(ladderPlace(tune[i], 9, 1) == places[i]);
    CHECK(ladderPlace(9, 9, 1) == 0 && ladderPlace(8, 9, 1) == -1 && ladderPlace(0, 0, 11) == 0 && ladderPlace(12, 0, 11) == 5);
    CHECK(std::string(paramKey(Param::Handoff)) == "handoff" && paramFromKey("handoff") == Param::Handoff && isNumericParam(Param::Handoff));

    const std::int64_t end = 16 * barTicks(defaultSong(1));
    auto line = [&](const Song& x, std::initializer_list<int> channels) {
        std::map<std::int64_t, std::pair<int, int>> out;  // start → (channel, note)
        for (const Note& n : notes(x, 0, end))
            for (int c : channels)
                if (n.channel == c) out[n.start] = {n.channel, n.note};
        return out;
    };
    // Alternate, two Leads: the tune is the first Lead's line, and each note goes to the member its scale place says.
    Song alt = defaultSong(0x51C0);
    alt.matrix.clear();
    alt.groups[0].deal = DealMode::Alternate;
    alt.groups[0].handoff = 0;
    Song solo = alt;
    solo.groups[0].deal = DealMode::Solo, solo.groups[0].soloMember = 0;
    const auto heard = line(alt, {1, 2}), tuneLine = line(solo, {1});
    CHECK(heard.size() == tuneLine.size() && heard.size() > 60);
    int both[2] = {0, 0};
    for (const auto& [t, cn] : heard) {
        CHECK(tuneLine.count(t) && tuneLine.at(t).second == cn.second);
        CHECK(cn.first == 1 + ladderPlace(cn.second, alt.root, alt.scale) % 2);
        both[cn.first - 1]++;
    }
    CHECK(both[0] > 10 && both[1] > 10);
    // Bells, three Leads, Span 2: pairs of neighbouring scale notes per member.
    Song bells = alt;
    addVoice(bells, bells.groups[0], VoiceType::Lead, 5);
    bells.groups[0].deal = DealMode::Bells;
    for (const auto& [t, cn] : line(bells, {1, 2, 5})) {
        const int want = ladderPlace(cn.second, bells.root, bells.scale) / 2 % 3;
        CHECK(cn.first == (want == 2 ? 5 : want + 1));
    }
    // A muted member isn't one: the pitches it held go round the others.
    Song muted = bells;
    muted.groups[0].voices[1].mute = true;
    for (const auto& [t, cn] : line(muted, {1, 2, 5})) CHECK(cn.first != 2 && cn.first == (ladderPlace(cn.second, muted.root, muted.scale) / 2 % 2 == 0 ? 1 : 5));
    // Handoff: a note handed on lasts into the next member's by Handoff % of a step; 0 leaves it alone; Cut doesn't
    // shorten it.
    Song ho = alt;
    ho.groups[0].handoff = 20;
    Song cut = ho;
    cut.groups[0].overlap = Overlap::Cut;
    for (const Song* x : {&ho, &cut}) {
        const std::vector<Note> ns = notes(*x, 0, end);
        int handoffs = 0;
        for (std::size_t i = 0; i + 1 < ns.size(); ++i) {
            if (ns[i].channel > 2) continue;
            std::size_t j = i + 1;
            while (j < ns.size() && ns[j].channel > 2) ++j;
            if (j == ns.size() || ns[j].channel == ns[i].channel || ns[j].start - ns[i].start != 240) continue;
            CHECK(ns[i].end >= ns[j].start + 48);
            ++handoffs;
        }
        CHECK(handoffs > 10);
    }
    std::vector<Note> plain = notes(alt, 0, end);
    bool overlapsNone = true;
    for (std::size_t i = 0; i + 1 < plain.size(); ++i)
        if (plain[i].channel <= 2 && plain[i].end > plain[i].start + 240 * 4) overlapsNone = false;
    CHECK(overlapsNone);
    // Jump safety.
    std::vector<Note> parts;
    for (std::int64_t f = 0; f < end; f += 999) {
        const std::vector<Note> x = notes(ho, f, std::min(end, f + 999));
        parts.insert(parts.end(), x.begin(), x.end());
    }
    std::vector<Note> whole = notes(ho, 0, end);
    auto key = [](const Note& x) { return std::make_tuple(x.start, x.channel, x.note, x.end); };
    std::sort(parts.begin(), parts.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
    std::sort(whole.begin(), whole.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
    CHECK(parts == whole);
    // Deal mode as a pick-one target reaches the new modes.
    Song dm = alt;
    dm.groups[0].deal = DealMode::OnGenerate;
    Row d = row(Scope::Group, dm.groups[0].id, Param::DealMode);
    d.every = 2, d.allowed = {static_cast<int>(DealMode::Bells)}, d.maxShift = 16;
    dm.matrix.push_back(d);
    CHECK(stateAt(dm, 2).groups[0].deal == DealMode::Bells && stateAt(dm, 1).groups[0].deal == DealMode::OnGenerate);
    Row h = row(Scope::Group, dm.groups[0].id, Param::Handoff);
    h.every = 1, h.maxShift = 30, h.min = 40, h.max = 50;
    dm.matrix.push_back(h);
    CHECK(stateAt(dm, 3).groups[0].handoff >= 40);

    // Tutti: every member plays every hit, each its own line's note; a Pulse chord on every hit too.
    Song tutti = defaultSong(0x7077);
    tutti.matrix.clear();
    tutti.groups[0].deal = DealMode::Tutti;
    std::map<std::int64_t, std::set<int>> chans;
    for (const Note& n : notes(tutti, 0, end))
        if (n.channel <= 2) chans[n.start].insert(n.channel);
    CHECK(chans.size() > 60);
    for (const auto& [t, c] : chans) CHECK(c.size() == 2);
    const std::vector<Lane> tl = View(tutti).lanes(barTicks(tutti) + 1);  // the lane shows a Tutti step as its first member
    for (const LaneCell& c : tl[0].cells) CHECK(!c.hit || (c.owner == 0 && c.note >= 0));
    Song tw = tutti;  // a member with weight 0 isn't one
    tw.groups[0].voices[1].weight = 0;
    for (const Note& n : notes(tw, 0, end)) CHECK(n.channel != 2);
    // Each member's note is its own line's: the same as Solo on it.
    for (int m = 0; m < 2; ++m) {
        Song one = tutti;
        one.groups[0].deal = DealMode::Solo, one.groups[0].soloMember = m;
        CHECK(line(one, {m + 1}) == line(tutti, {m + 1}));
    }
    // Hoketus: the Leads on Tutti, the Chords on Pulse and Tutti, on Interlock from the Leads: blocks that never meet.
    Song hk = tutti;
    Group& ch = hk.groups[2];
    ch.voices[0].articulation = Articulation::Pulse;
    ch.deal = DealMode::Tutti, ch.density = 16;
    ch.rhythmGen.use = "interlock", ch.rhythmGen.sources["interlock"] = hk.groups[0].id;
    std::set<std::int64_t> leadOn, chordOn;
    for (const Note& n : notes(hk, 0, end)) (n.channel <= 2 ? leadOn : n.channel == 3 ? chordOn : leadOn).insert(n.channel == 4 ? -1 : n.start);
    leadOn.erase(-1);
    std::vector<std::int64_t> both2;
    std::set_intersection(leadOn.begin(), leadOn.end(), chordOn.begin(), chordOn.end(), std::back_inserter(both2));
    CHECK(both2.empty() && chordOn.size() > 40 && leadOn.size() + chordOn.size() == static_cast<std::size_t>(end / 240));
}

// §16.6 with §5.5: the derived generators' graph never recurses without end. A song file is shared, so a crafted one
// must not crash the host: a loop through a tune is a loop, broken at a derived slot, and a depth guard backs it up.
static void derivedGraph() {
    // The exact cycle: a group on Alternate whose tune is a Kotekan on another line of the same group. The tune's
    // note is the Kotekan's figure on that line, and that line plays the tune's note: a loop, so the Kotekan (the only
    // derived slot in it) plays Wander, and the group plays as if it had never been a Kotekan.
    Song cyc = defaultSong(0xC1C1E);
    cyc.matrix.clear();
    cyc.groups[0].deal = DealMode::Alternate;
    Voice& t = cyc.groups[0].voices[0];
    t.notesGen.use = "kotekan";
    t.notesGen.sources["kotekan"] = cyc.groups[0].voices[1].id;
    CHECK(!kotekanSource(cyc, 0, 0) && kindIn(cyc, SlotKind::Notes, 0, 0) == GenKind::Static);
    Song plain = cyc;
    plain.groups[0].voices[0].notesGen = {};
    derivedDepthHighWater(true);
    CHECK(notes(cyc, 0, 8 * barTicks(cyc)) == notes(plain, 0, 8 * barTicks(plain)));
    CHECK(derivedDepthHighWater() <= 3);
    // Across two groups: the tune follows the other group's line, which follows this group's second line.
    Song two = cyc;
    two.groups[0].voices[0].notesGen.sources["kotekan"] = two.groups[1].voices[0].id;
    Voice& b = two.groups[1].voices[0];
    b.notesGen.use = "kotekan";
    b.notesGen.sources["kotekan"] = two.groups[0].voices[1].id;
    CHECK(kotekanSource(two, 0, 0).has_value() != kotekanSource(two, 1, 0).has_value());  // one of them breaks it
    CHECK(!kotekanSource(two, 1, 0));  // the later in song order
    (void)notes(two, 0, 8 * barTicks(two));
    // Fuzz: random wiring of every derived generator, deal and source (ids of groups and voices, the slot's own, a
    // stranger's, none) over a few hundred seeds. Every song renders, and no chain of hops gets anywhere near the guard.
    int worst = 0;
    for (std::uint64_t seed = 1; seed <= 300; ++seed) {
        Stream r(hash(0xF022, {seed}));
        Song f = defaultSong(seed);
        f.matrix.clear();
        const int extra = static_cast<int>(r.below(3));
        for (int i = 0; i < extra; ++i) {
            Group& g = addGroup(f, "G");
            for (int k = 0, n = 1 + static_cast<int>(r.below(3)); k < n; ++k) addVoice(f, g, static_cast<VoiceType>(r.below(3)), 5 + k);
        }
        std::vector<std::uint64_t> groupIds, voiceIds;
        for (const Group& g : f.groups) {
            groupIds.push_back(g.id);
            for (const Voice& v : g.voices) voiceIds.push_back(v.id);
        }
        auto pickId = [&](const std::vector<std::uint64_t>& ids) -> std::uint64_t {
            const std::uint64_t k = r.below(ids.size() + 2);
            return k < ids.size() ? ids[k] : k == ids.size() ? 0 : 0xDEADBEEFull;
        };
        const char* rhythms[] = {"random", "euclid", "metric", "register", "interlock", "resultant"};
        const char* lines[] = {"wander", "pink", "register", "pedal", "kotekan", "kotekan", "kotekan"};
        for (Group& g : f.groups) {
            g.rhythmGen.use = rhythms[r.below(6)];
            g.rhythmGen.sources[g.rhythmGen.use] = pickId(groupIds);
            g.deal = static_cast<DealMode>(r.below(11));
            g.cleanHandoffs = (seed & 1) != 0;  // §5.7's runs of handoffs walk the same steps
            g.steps = 1 + static_cast<int>(r.below(16));
            g.beatDivision = static_cast<int>(r.below(8));
            for (Voice& v : g.voices) {
                v.notesGen.use = lines[r.below(7)];
                v.notesGen.sources["kotekan"] = pickId(voiceIds);
                v.notesGen.params["kotekan"]["mode"] = static_cast<int>(r.below(4));
                v.mute = r.below(8) == 0;
            }
        }
        derivedDepthHighWater(true);
        (void)notes(f, 0, 4 * barTicks(f));
        (void)View(f).lanes(barTicks(f) + 5);
        worst = std::max(worst, derivedDepthHighWater());
    }
    std::printf("  derived graph fuzz, 300 songs: deepest chain of hops %d (the guard is at 64)\n", worst);
    CHECK(worst > 1 && worst < 32);
}

// §5.7: Clean handoffs, a note handed on to another member on the pitch class of the one before it redrawn.
static void cleanHandoffs() {
    // The worked example (tools/spec_arith.py, "§5.7 Clean handoffs"): pool A E C G from A3, X and Y alternate.
    const std::uint64_t SX = 0x0123456789ABCDEFull, SY = 0xFEDCBA9876543210ull;
    const std::vector<int> pool = {9, 4, 0, 7}, pos = {0, 0, 1, 1, 0, 0, 2, 3}, want = {57, 67, 64, 60, 57, 64, 60, 67};
    auto at = [&](int c) { return 57 + ((pool[static_cast<std::size_t>(c)] - 9) % 12 + 12) % 12; };
    std::vector<int> got;
    for (std::size_t t = 0; t < pos.size(); ++t) {
        int x = at(pos[t]);
        if (!got.empty() && x % 12 == got.back() % 12)
            x = handoffRedraw(t % 2 == 0 ? SX : SY, static_cast<std::int64_t>(t), 4, got.back() % 12, at).value_or(x);
        got.push_back(x);
    }
    CHECK(got == want);
    CHECK(!handoffRedraw(SY, 1, 1, 9, [](int) { return 57; }));  // a pool of one has nothing else
    CHECK(std::string(paramKey(Param::CleanHandoffs)) == "cleanHandoffs" && isPickParam(Param::CleanHandoffs));

    // In a song: two Leads on Round robin (every note a handoff). Off, many handoffs land on the pitch class before;
    // on, almost none do, and only those notes change.
    auto composite = [](const Song& x, std::int64_t end) {
        std::vector<Note> out;
        for (const Note& n : notes(x, 0, end))
            if (n.channel <= 2) out.push_back(n);
        return out;
    };
    int offSame = 0, onSame = 0, handoffs = 0, changed = 0, unchangedOk = 0;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        Song off = defaultSong(0xC1EA0 + seed);
        off.matrix.clear();
        off.groups[0].deal = DealMode::RoundRobin;
        Song on = off;
        on.groups[0].cleanHandoffs = true;
        const std::int64_t end = 16 * barTicks(off);
        const std::vector<Note> a = composite(off, end), b = composite(on, end);
        CHECK(a.size() == b.size());
        if (a.size() != b.size()) continue;
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].start == b[i].start && a[i].channel == b[i].channel && a[i].velocity == b[i].velocity);  // rhythm, owner, velocity stay
            if (i == 0) continue;
            ++handoffs;
            offSame += a[i].note % 12 == a[i - 1].note % 12;
            onSame += b[i].note % 12 == b[i - 1].note % 12;
            if (a[i].note != b[i].note) ++changed;
            else unchangedOk += a[i].note % 12 != b[i - 1].note % 12 || b[i].note % 12 == b[i - 1].note % 12;
        }
        // Replayable: slices of the render are the render (each slice works out its runs afresh), and so is the
        // Player, a block at a time.
        std::vector<Note> parts;
        for (std::int64_t f = 0; f < end; f += 1111) {
            const std::vector<Note> x = notes(on, f, std::min(end, f + 1111));
            parts.insert(parts.end(), x.begin(), x.end());
        }
        std::vector<Note> whole = notes(on, 0, end);
        auto key = [](const Note& x) { return std::make_tuple(x.start, x.channel, x.note, x.end); };
        std::sort(parts.begin(), parts.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
        std::sort(whole.begin(), whole.end(), [&](const Note& x, const Note& y) { return key(x) < key(y); });
        CHECK(parts == whole);
        if (seed == 1) {
            std::vector<Event> want;
            for (const Event& e : events(notes(on, 0, end)))
                if (e.tick < end) want.push_back(e);
            Player pl(on);
            std::vector<Event> heardEvents;
            for (std::int64_t f = 0; f < end; f += 333)
                for (const Event& e : pl.process(f, std::min(end, f + 333), true)) heardEvents.push_back(e);
            std::stable_sort(want.begin(), want.end(), eventOrder);
            std::stable_sort(heardEvents.begin(), heardEvents.end(), eventOrder);
            CHECK(heardEvents.size() == want.size());
        }
    }
    {  // the Play lane shows the notes as heard
        Song on = defaultSong(0xC1EA1);
        on.matrix.clear();
        on.groups[0].deal = DealMode::RoundRobin, on.groups[0].cleanHandoffs = true;
        const std::int64_t b5 = 5 * barTicks(on);
        std::map<std::int64_t, int> heardNow;
        for (const Note& n : notes(on, b5, b5 + barTicks(on)))
            if (n.channel <= 2) heardNow[n.start] = n.note;
        const std::vector<Lane> lanes = View(on).lanes(b5 + 1);
        int seen = 0;
        for (int slot = 0; slot < lanes[0].steps; ++slot) {
            const LaneCell& c = lanes[0].cells[static_cast<std::size_t>(slot)];
            if (c.hit && heardNow.count(b5 + slot * 240)) {
                CHECK(c.note == heardNow.at(b5 + slot * 240));
                ++seen;
            }
        }
        CHECK(seen > 4);
    }
    std::printf("  clean handoffs, 12 songs on Round robin: same pitch class at a handoff %d → %d of %d; %d notes redrawn\n", offSame, onSame,
                handoffs, changed);
    CHECK(offSame > 100 && onSame * 10 < offSame && changed > 0 && changed <= offSame + onSame);
    // Off is the song as it was; a Kotekan, Alternate and Tutti are left alone.
    Song k = defaultSong(0xC1EA0);
    k.matrix.clear();
    k.groups[0].deal = DealMode::RoundRobin;
    k.groups[0].voices[1].notesGen.use = "kotekan";
    k.groups[0].voices[1].notesGen.sources["kotekan"] = k.groups[0].voices[0].id;
    for (DealMode d : {DealMode::RoundRobin, DealMode::Alternate, DealMode::Tutti}) {
        Song x = k;
        x.groups[0].deal = d;
        if (d != DealMode::RoundRobin) x.groups[0].voices[1].notesGen = {};
        Song y = x;
        y.groups[0].cleanHandoffs = true;
        CHECK(notes(x, 0, 8 * barTicks(x)) == notes(y, 0, 8 * barTicks(y)));
    }
    // A pick-one target: a row turning it on from bar 4.
    Song m = defaultSong(0xC1EA1);
    m.matrix.clear();
    m.groups[0].deal = DealMode::RoundRobin;
    Song mr = m;
    Row c = row(Scope::Group, m.groups[0].id, Param::CleanHandoffs);
    c.every = 4, c.maxShift = 1;
    mr.matrix.push_back(c);
    CHECK(stateAt(mr, 4).groups[0].cleanHandoffs && !stateAt(mr, 3).groups[0].cleanHandoffs);
    CHECK(notes(mr, 0, 4 * barTicks(m)) == notes(m, 0, 4 * barTicks(m)));
}

// ---- §17 modulation ------------------------------------------------------------------------------------------

// §17.10's setting: matrixSeed 0123456789abcdef, source id 00000000000000aa, phrase length 4. The default song on
// that seed has them all (its matrixSeed is its master seed until a Generate).
static Song modSong() {
    Song s = defaultSong(0x0123456789ABCDEFull);
    CHECK(s.matrixSeed == 0x0123456789ABCDEFull && s.phraseLength == 4);
    return s;
}
static ModSource& lfoSource(Song& s, std::vector<std::map<std::string, int>> lfos, std::uint64_t id = 0xAA) {
    ModSource& src = addSource(s, "lfo");
    src.id = id;
    src.params["count"] = static_cast<int>(lfos.size());
    src.lists["lfos"] = std::move(lfos);
    return src;
}
static Cord& cordTo(Song& s, const ModSource& src, const char* output, Scope scope, std::uint64_t target, const char* key) {
    Cord& c = addCord(s, src.id, output);
    c.dest = {scope, target, key};
    return c;
}

static void modWorkedValues() {
    // §17.10, LFOs: python3 tools/spec_arith.py, "§17.10 sources".
    const std::vector<std::pair<int, int>> tri = {{0, -1000}, {1, -764}, {4, -58}, {8, 882}, {9, 882}, {13, -58}, {16, -764}, {17, -1000}};
    for (const auto& [b, want] : tri) CHECK(lfoLevel(b, 17, 0, 0) == want);
    const std::vector<std::pair<int, int>> saw = {{0, -1000}, {1, -895}, {9, -53}, {10, 53}, {18, 895}, {19, -1000}};
    for (const auto& [b, want] : saw) CHECK(lfoLevel(b, 19, 1, 0) == want);
    // Saw down and Square, by §17.9's formulas.
    CHECK(lfoLevel(0, 4, 2, 0) == 1000 && lfoLevel(1, 4, 2, 0) == 500 && lfoLevel(3, 4, 2, 0) == -500);
    CHECK(lfoLevel(0, 4, 3, 0) == 1000 && lfoLevel(1, 4, 3, 0) == 1000 && lfoLevel(2, 4, 3, 0) == -1000 && lfoLevel(3, 4, 3, 0) == -1000);

    Song s = modSong();
    // Seeded phase, LFO i = 0, P 17: 6.
    ModSource& seeded = lfoSource(s, {{{"period", 17}, {"phase", -1}}});
    CHECK(lfoPhase(s, seeded, 0) == 6);
    CHECK(levelAt(s, seeded, "lfo1", 0) == lfoLevel(6, 17, 0, 0));
    // A phase above P − 1 is taken mod P.
    seeded.lists["lfos"][0]["phase"] = 20;
    CHECK(lfoPhase(s, seeded, 0) == 3);

    // Converge, 17 and 19 bars, phases 0: first at b = 323.
    Song c = modSong();
    ModSource& tides = lfoSource(c, {{{"period", 17}}, {{"period", 19}}});
    const std::vector<std::int64_t> conv = triggerBars(c, tides, "converge", 0, 2);
    CHECK(conv == (std::vector<std::int64_t>{323, 646}));
    const auto f = triggerAt(c, tides, "converge", 323);
    CHECK(f && f->amount == 1 && f->u == hash(0x0123456789ABCDEFull, {"mod", std::uint64_t{0xAA}, "converge", 323}));
    CHECK(!triggerAt(c, tides, "converge", 0));  // never at bar 0, though every x is 0 there
    // 5 and 7 phrases: bar 140 (35 phrases). 9 and 3 share a factor: every 9.
    Song p = modSong();
    ModSource& phr = lfoSource(p, {{{"period", 5}, {"unit", 1}}, {{"period", 7}, {"unit", 1}}});
    CHECK(triggerBars(p, phr, "converge", 0, 1) == (std::vector<std::int64_t>{140}));
    ModSource& nine = lfoSource(p, {{{"period", 9}}, {{"period", 3}}}, 0xAB);
    CHECK(triggerBars(p, nine, "converge", 0, 3) == (std::vector<std::int64_t>{9, 18, 27}));
    // The Converge set: only LFO 1 (period 3) converges every 3 bars; members beyond Count are ignored; a set with no
    // member left never fires.
    nine.sets["converge"] = {1};
    CHECK(triggerBars(p, nine, "converge", 0, 2) == (std::vector<std::int64_t>{3, 6}));
    nine.sets["converge"] = {1, 3};
    CHECK(triggerBars(p, nine, "converge", 0, 1) == (std::vector<std::int64_t>{3}));
    nine.sets["converge"] = {2, 3};
    CHECK(triggerBars(p, nine, "converge", 0, 1).empty());

    // Outputs: lfo1 … lfoN and converge; an lfo beyond Count does nothing.
    const auto outs = outputsOf(tides);
    CHECK(outs.size() == 3 && outs[0].name == "lfo1" && outs[1].name == "lfo2" && outs[2].name == "converge");
    CHECK(outs[0].kind == OutputKind::Level && outs[0].bipolar && outs[2].kind == OutputKind::Trigger);
    CHECK(!levelAt(c, tides, "lfo3", 5) && levelAt(c, tides, "lfo2", 5) == lfoLevel(5, 19, 0, 0));
    // Defaults (§17.9): 2 LFOs, periods 17, 19, 23, 29, bars, Triangle, phase 0.
    ModSource bare;
    bare.kind = "lfo";
    CHECK(sourceValue(bare, "count") == 2 && itemValue(bare, 0, "period") == 17 && itemValue(bare, 1, "period") == 19 &&
          itemValue(bare, 2, "period") == 23 && itemValue(bare, 3, "period") == 29 && itemValue(bare, 0, "shape") == 0);
    CHECK(sourceSet(bare) == (std::vector<int>{0, 1}));

    // Depth 4 from the Triangle above: b 4 → 0, b 8 → 4 (rsdiv(−58·4, 1000) = 0; rsdiv(882·4, 1000) = 4). On the
    // Leads' Density (10), and at bar 0 too: the LFO is at −1000 there, so −4.
    Song d = modSong();
    ModSource& tri17 = lfoSource(d, {{{"period", 17}}});
    const std::uint64_t leads = d.groups[0].id;
    cordTo(d, tri17, "lfo1", Scope::Group, leads, "density");
    CHECK(d.groups[0].density == 10);
    CHECK(stateAt(d, 4).groups[0].density == 10 && stateAt(d, 8).groups[0].density == 14 && stateAt(d, 0).groups[0].density == 6);
}

static void modFramework() {
    const std::uint64_t ms = 0x0123456789ABCDEFull;
    // Rules: a song with an empty mod is the song as it was, bar for bar.
    {
        Song s = modSong();
        Row r = row(Scope::Group, s.groups[1].id, Param::Density);
        r.every = 1, r.maxShift = 3;
        s.matrix.push_back(r);
        Song with = s;
        ModSource& lfo = lfoSource(with, {{{"period", 7}}});
        (void)lfo;  // a source with no cords does nothing
        CHECK(notes(s, 0, barTicks(s) * 24) == notes(with, 0, barTicks(s) * 24));
    }
    // §17.4: a trigger never sees the levels. A random walk on the Bass's Density, and an LFO on it: the walk is the
    // walk without the LFO, and the LFO's offset is clamped on top.
    {
        Song s = modSong();
        const std::uint64_t bass = s.groups[1].id;
        Row r = row(Scope::Group, bass, Param::Density);
        r.every = 1, r.maxShift = 3, r.min = 2, r.max = 12;
        s.matrix.push_back(r);
        Song with = s;
        ModSource& lfo = lfoSource(with, {{{"period", 6}, {"shape", 3}}});  // Square: +1000 for 3 bars, −1000 for 3
        cordTo(with, lfo, "lfo1", Scope::Group, bass, "density").depth = 5;
        Song deep = s;
        ModSource& lfo2 = lfoSource(deep, {{{"period", 6}, {"shape", 3}}});
        cordTo(deep, lfo2, "lfo1", Scope::Group, bass, "density").depth = 64;  // far past the edges: clamped, not reflected
        bool moved = false;
        for (std::int64_t b = 0; b < 40; ++b) {
            const int walk = stateAt(s, b).groups[1].density;
            const int lvl = lfoLevel(b, 6, 3, 0);
            CHECK(stateAt(with, b).groups[1].density == std::max(1, std::min(16, walk + (lvl > 0 ? 5 : -5))));
            CHECK(stateAt(deep, b).groups[1].density == (lvl > 0 ? 16 : 1));
            moved = moved || walk != stateAt(s, 0).groups[1].density;
        }
        CHECK(moved);
    }
    // Levels on two scopes: v is the value after the triggers for each. A Global Range level and a Leads Range level.
    {
        Song s = modSong();
        ModSource& lfo = lfoSource(s, {{{"period", 4}, {"shape", 3}}});
        cordTo(s, lfo, "lfo1", Scope::Global, 0, "range").depth = 2;
        cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "range").depth = -3;
        cordTo(s, lfo, "lfo1", Scope::Global, 0, "range").depth = 1;  // two on one destination add up
        const Song at0 = stateAt(s, 0), at2 = stateAt(s, 2);
        CHECK(at0.range == 8 + 3 && at0.groups[0].overrides.range == 8 - 3);
        CHECK(at2.range == 8 - 3 && at2.groups[0].overrides.range == 8 + 3);
    }
    // Mute: a voice muted while the level is below Threshold; Invert flips it. Square of 4 bars on the second Lead.
    {
        Song s = modSong();
        const Voice lead2 = s.groups[0].voices[1];
        ModSource& lfo = lfoSource(s, {{{"period", 4}, {"shape", 3}}});
        Cord& m = cordTo(s, lfo, "lfo1", Scope::Voice, lead2.id, "mute");
        m.threshold = 0;
        for (std::int64_t b = 0; b < 8; ++b) CHECK(stateAt(s, b).groups[0].voices[1].mute == (b % 4 >= 2));
        int heard[8] = {};
        for (const Note& n : notes(s, 0, barTicks(s) * 8))
            if (n.voice == lead2.id) ++heard[n.start / barTicks(s)];
        for (int b = 0; b < 8; ++b) CHECK((heard[b] == 0) == (b % 4 >= 2));
        m.invert = true;
        for (std::int64_t b = 0; b < 8; ++b) CHECK(stateAt(s, b).groups[0].voices[1].mute == (b % 4 < 2));
        // A group's Mute; and Mute at Global does nothing.
        m.dest = {Scope::Group, s.groups[1].id, "mute"};
        CHECK(stateAt(s, 0).groups[1].mute && !stateAt(s, 2).groups[1].mute);
        m.dest = {Scope::Global, 0, "mute"};
        CHECK(stateAt(s, 0).groups[1].mute == false && !stateAt(s, 0).groups[0].voices[1].mute);
    }
    // What a level may not drive: a clock (Steps, Offset, the harmony's Length), Root, Scale, a pick-one. A level
    // cord there, a trigger cord on Mute, a cord to an lfo beyond Count or to a source that's gone: nothing.
    {
        CHECK(levelMayDrive("density") && levelMayDrive("mute") && levelMayDrive("rhythm.euclid.pulses") && levelMayDrive("humanTiming"));
        CHECK(!levelMayDrive("steps") && !levelMayDrive("offset") && !levelMayDrive("length") && !levelMayDrive("root") &&
              !levelMayDrive("scale") && !levelMayDrive("deal") && !levelMayDrive("direction") && !levelMayDrive("changesEvery") &&
              !levelMayDrive("reroll.rhythm") && !levelMayDrive("nonsense") && !levelMayDrive("notes.pink.loop"));
        CHECK(levelMayDrive("rhythm.metric.beat") && levelMayDrive("notes.register.lock"));
        Song s = modSong();
        const Song plain = s;
        ModSource& lfo = lfoSource(s, {{{"period", 5}}, {{"period", 3}}});
        cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "steps").depth = 9;
        cordTo(s, lfo, "lfo2", Scope::Global, 0, "root").depth = 9;
        cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "deal").depth = 9;
        cordTo(s, lfo, "lfo3", Scope::Group, s.groups[0].id, "density").depth = 9;
        cordTo(s, lfo, "converge", Scope::Group, s.groups[0].id, "mute");
        cordTo(s, lfo, "lfo1", Scope::Global, 0, "density").depth = 9;  // an address the target doesn't have
        cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "a.key.from.later").depth = 9;
        Cord& gone = cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "density");
        gone.source = 0x1234;
        gone.depth = 9;
        CHECK(notes(s, 0, barTicks(s) * 32) == notes(plain, 0, barTicks(s) * 32));
    }
    // A generator's parameter, a level's destination, in its own range: the Leads on Euclidean, Pulses moved by an LFO
    // whether or not... it's playing; clamped to 1 … 64.
    {
        Song s = modSong();
        s.groups[0].rhythmGen.use = "euclid";
        ModSource& lfo = lfoSource(s, {{{"period", 4}, {"shape", 3}}});
        cordTo(s, lfo, "lfo1", Scope::Group, s.groups[0].id, "rhythm.euclid.pulses").depth = 64;
        CHECK(paramValue(SlotKind::Rhythm, stateAt(s, 0).groups[0].rhythmGen, "euclid", "pulses") ==
              std::min(64, paramValue(SlotKind::Rhythm, s.groups[0].rhythmGen, "euclid", "pulses") + 64));
        CHECK(paramValue(SlotKind::Rhythm, stateAt(s, 2).groups[0].rhythmGen, "euclid", "pulses") == 1);
        // At a Voice, a rhythm key does nothing.
        Cord& wrong = cordTo(s, lfo, "lfo1", Scope::Voice, s.groups[0].voices[0].id, "rhythm.euclid.pulses");
        wrong.depth = 64;
        CHECK(stateAt(s, 2).groups[0].voices[0].notesGen == s.groups[0].voices[0].notesGen);
    }
    // §17.3, a trigger cord: converge every 6 bars (2 and 3), Reroll the Bass rhythm for 2 bars, Fresh.
    {
        Song s = modSong();
        const std::uint64_t bass = s.groups[1].id, base = s.groups[1].rhythmSeed;
        ModSource& lfo = lfoSource(s, {{{"period", 2}}, {{"period", 3}}});
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, bass, "reroll.rhythm");
        c.lasts = 2, c.fixedSeed = false;
        View view(s);
        const auto form = view.form(0, 14);
        for (std::int64_t b = 0; b < 14; ++b) {
            const bool on = b >= 6 && (b % 6) < 2;
            const std::uint64_t seed = stateAt(s, b).groups[1].rhythmSeed;
            if (!on) {
                CHECK(seed == base && form[1][static_cast<std::size_t>(b)] == "A");
                continue;
            }
            const std::int64_t fired = b - b % 6;
            const std::uint64_t hc = hash(hash(ms, {"mod", std::uint64_t{0xAA}, "converge", fired}), {"cord", c.id});
            CHECK(seed == hash(hc, {"rhythm", bass}));        // Fresh: s = hc
            CHECK(form[1][static_cast<std::size_t>(b)] == "C*");  // the cord's letter: after the rows' (the turnaround is B)
            CHECK(form[0][static_cast<std::size_t>(b)] == "A" || b % 4 == 3);  // the Leads: only their own turnaround
        }
        CHECK(formLetters(s).at(c.id) == "C*" && formLetters(s).at(s.matrix[0].id) == "B");
        // Fixed: hash(matrixSeed, "mod", cordID).
        c.fixedSeed = true;
        CHECK(stateAt(s, 6).groups[1].rhythmSeed == hash(hash(ms, {"mod", c.id}), {"rhythm", bass}));
        CHECK(stateAt(s, 12).groups[1].rhythmSeed == stateAt(s, 6).groups[1].rhythmSeed);
    }
    // As given: amount 1 · Scale 2 = +2 a convergence, Max shift 3, Stays; reflected into Bounds. Scale 0 (amount 0)
    // does nothing at all, and pushes no layer. Random: §7.3's uniform delta from hash(hc, "amount").
    {
        Song s = modSong();
        const std::uint64_t leads = s.groups[0].id;
        ModSource& lfo = lfoSource(s, {{{"period", 4}}});
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, leads, "density");
        c.scale = 2, c.maxShift = 3, c.max = 15;
        CHECK(stateAt(s, 3).groups[0].density == 10 && stateAt(s, 4).groups[0].density == 12 && stateAt(s, 8).groups[0].density == 14);
        CHECK(stateAt(s, 12).groups[0].density == 14);  // 16 reflected off 15
        c.scale = 7;  // clamped to Max shift 3
        CHECK(stateAt(s, 4).groups[0].density == 13);
        c.scale = -1;
        CHECK(stateAt(s, 4).groups[0].density == 9);
        c.scale = 0, c.lasts = 2;
        CHECK(stateAt(s, 4).groups[0].density == 10 && View(s).form(4, 1)[0][0] == "A");
        c.scale = 1, c.move = 0, c.maxShift = 4, c.lasts = 0;
        const std::uint64_t hc = hash(hash(ms, {"mod", std::uint64_t{0xAA}, "converge", 4}), {"cord", c.id});
        const int x = static_cast<int>(Stream(hash(hc, {"amount"})).below(8));
        const int delta = x < 4 ? x - 4 : x - 3;
        int want = 10 + delta;
        if (want > 15) want = 30 - want;
        CHECK(stateAt(s, 4).groups[0].density == want);
        // Two cords on one trigger roll apart.
        Cord& twin = cordTo(s, lfo, "converge", Scope::Group, s.groups[1].id, "reroll.rhythm");
        twin.fixedSeed = false;
        Cord& twin2 = cordTo(s, lfo, "converge", Scope::Group, s.groups[2].id, "reroll.rhythm");
        twin2.fixedSeed = false;
        CHECK(stateAt(s, 4).groups[1].rhythmSeed != stateAt(s, 4).groups[2].rhythmSeed);
        // Root moves in fifths, As given.
        Cord& key = cordTo(s, lfo, "converge", Scope::Global, 0, "root");
        key.scale = 1;
        CHECK(stateAt(s, 4).root == (9 + 7) % 12 && stateAt(s, 8).root == (9 + 14) % 12);
    }
    // Chance: roll(hash(hc, "chance")) < Chance, and nextCordFiring agrees with the replay, skips and all.
    {
        Song s = modSong();
        ModSource& lfo = lfoSource(s, {{{"period", 1}}});  // converges every bar
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, s.groups[1].id, "density");
        c.chance = 40, c.lasts = 1, c.scale = 1;
        View view(s);
        const auto form = view.form(0, 64);
        int acted = 0;
        for (std::int64_t b = 1; b < 64; ++b) {
            const std::uint64_t hc = hash(hash(ms, {"mod", std::uint64_t{0xAA}, "converge", b}), {"cord", c.id});
            const bool spec = roll(hash(hc, {"chance"})) < 40;
            CHECK((form[1][static_cast<std::size_t>(b)] != "A") == spec);
            acted += spec;
        }
        CHECK(acted > 12 && acted < 40);
        for (std::int64_t b = 0; b < 50; ++b) {
            const Firing f = nextCordFiring(s, c, b);
            CHECK(f.bar >= std::max<std::int64_t>(1, b) && f.bar < 64 && form[1][static_cast<std::size_t>(f.bar)] != "A");
            for (std::int64_t k = std::max<std::int64_t>(1, b); k < f.bar; ++k) CHECK(form[1][static_cast<std::size_t>(k)] == "A");
        }
        c.on = false;
        CHECK(nextCordFiring(s, c, 0).bar == -1 && View(s).form(0, 64)[1] == std::vector<std::string>(64, "A"));
        c.on = true, c.dest.key = "mute";
        CHECK(nextCordFiring(s, c, 0).bar == -1);
    }
    // §17.4, whose Bounds: a row moves the Leads' Density inside 8 … 10, then a cord moves it with its own (the full
    // span). Between firings the value is reflect(base + offset, the last mover's Bounds).
    {
        Song s = modSong();
        s.matrix.clear();
        const std::uint64_t leads = s.groups[0].id;
        Row r = row(Scope::Group, leads, Param::Density);
        r.every = 4, r.offset = 1, r.maxShift = 1, r.min = 8, r.max = 10;
        s.matrix.push_back(r);
        ModSource& lfo = lfoSource(s, {{{"period", 4}, {"phase", 2}}});  // converges at bars 2, 6, 10, …
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, leads, "density");
        c.scale = 5, c.maxShift = 5;
        const int at1 = stateAt(s, 1).groups[0].density;
        CHECK(at1 == 9 || at1 == 10 || at1 == 8);
        CHECK(stateAt(s, 2).groups[0].density == at1 + 5);
        CHECK(stateAt(s, 3).groups[0].density == at1 + 5);  // the cord's Bounds hold it there, not the row's 8 … 10
        const int at5 = stateAt(s, 5).groups[0].density;     // the row moves it again: back into its own Bounds
        CHECK(at5 >= 8 && at5 <= 10);
    }
    // §17.4, layer order: a row's temporary layer, then a cord's, on one pick-one: the cord's (later) wins while both
    // are on; when the cord's expires the row's shows again.
    {
        Song s = modSong();
        s.matrix.clear();
        const std::uint64_t leads = s.groups[0].id;
        Row r = row(Scope::Group, leads, Param::DealMode);
        r.every = 8, r.offset = 2, r.allowed = {2}, r.lasts = 4, r.maxShift = 8;
        s.matrix.push_back(r);
        ModSource& lfo = lfoSource(s, {{{"period", 8}, {"phase", 6}}});  // converges at 2, 10, …
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, leads, "deal");
        c.allowed = {5}, c.lasts = 2, c.maxShift = 8;
        CHECK(static_cast<int>(stateAt(s, 2).groups[0].deal) == 5 && static_cast<int>(stateAt(s, 3).groups[0].deal) == 5);
        CHECK(static_cast<int>(stateAt(s, 4).groups[0].deal) == 2 && static_cast<int>(stateAt(s, 6).groups[0].deal) == 0);
        // The strip shows the first active layer in that order (the row's, B), as it did for two rows.
        View view(s);
        CHECK(view.form(1, 6)[0] == (std::vector<std::string>{"A", "B", "B", "B", "B", "A"}));
        CHECK(formLetters(s).at(c.id) == "C");
    }
    // Jump safety with sources: any window is the slice of a longer render.
    {
        Song s = modSong();
        const std::uint64_t leads = s.groups[0].id, bass = s.groups[1].id;
        ModSource& lfo = lfoSource(s, {{{"period", 5}}, {{"period", 3}, {"shape", 1}}, {{"period", 2}, {"unit", 1}, {"phase", -1}}});
        cordTo(s, lfo, "lfo1", Scope::Group, leads, "density").depth = 6;
        cordTo(s, lfo, "lfo2", Scope::Voice, s.groups[0].voices[1].id, "mute");
        cordTo(s, lfo, "lfo3", Scope::Global, 0, "accent").depth = 30;
        Cord& c = cordTo(s, lfo, "converge", Scope::Group, bass, "vary.rhythm");
        c.maxShift = 3, c.lasts = 2, c.fixedSeed = false;
        Cord& k = cordTo(s, lfo, "converge", Scope::Global, 0, "root");
        k.move = 0, k.lasts = 4;
        const std::int64_t end = barTicks(s) * 64;
        const std::vector<Note> whole = notes(s, 0, end);
        for (std::int64_t cut : {std::int64_t{3840}, std::int64_t{2500}, std::int64_t{11111}}) {
            std::vector<Note> pieces;
            for (std::int64_t at = 0; at < end; at += cut) {
                const std::vector<Note> part = notes(s, at, std::min(end, at + cut));
                pieces.insert(pieces.end(), part.begin(), part.end());
            }
            std::stable_sort(pieces.begin(), pieces.end(), [](const Note& x, const Note& y) {
                return std::tie(x.start, x.channel, x.note, x.end) < std::tie(y.start, y.channel, y.note, y.end);
            });
            CHECK(pieces == whole);
        }
    }
    // A kind this build doesn't know has no outputs, and its cords do nothing.
    {
        Song s = modSong();
        const Song plain = s;
        ModSource& later = addSource(s, "ca");
        later.params["rule"] = 90;
        CHECK(outputsOf(later).empty());
        cordTo(s, later, "population", Scope::Group, s.groups[0].id, "density").depth = 9;
        cordTo(s, later, "births", Scope::Group, s.groups[0].id, "reroll.rhythm");
        CHECK(notes(s, 0, barTicks(s) * 16) == notes(plain, 0, barTicks(s) * 16));
    }
}

// The editor's "next firing" can't be made slow by a song file: a convergence is worked out in closed form (the
// Chinese remainder theorem), whatever its distance; one whose phases never line up answers "never" at once; and an
// output that has to be tried bar by bar stops after kScanBars.
static void modNextIsBounded() {
    // crt against brute force, every pair of small periods and phases.
    for (int p1 = 1; p1 <= 12; ++p1)
        for (int p2 = 1; p2 <= 12; ++p2)
            for (int a = 0; a < p1; ++a)
                for (int b = 0; b < p2; b += 3) {
                    const auto sol = crt({{a, p1}, {b, p2}});
                    std::int64_t first = -1;
                    for (std::int64_t x = 0; x < p1 * p2 && first < 0; ++x)
                        if (x % p1 == a && x % p2 == b) first = x;
                    CHECK(sol ? (sol->first == first && sol->second == std::lcm(p1, p2)) : first < 0);
                }
    // The engine's own converge, against trying every bar, with phases.
    Song s = modSong();
    ModSource& lfo = lfoSource(s, {{{"period", 6}, {"phase", 1}}, {{"period", 10}, {"phase", 3}}, {{"period", 4}, {"phase", 1}}});
    std::vector<std::int64_t> brute;
    for (std::int64_t b = 1; b < 400 && brute.size() < 5; ++b)
        if (triggerAt(s, lfo, "converge", b)) brute.push_back(b);
    CHECK(!brute.empty() && triggerBars(s, lfo, "converge", 0, 5, 400) == brute);
    // Phases that never line up (4 and 6 bars, phases 0 and 1: b even and b odd at once): never, at once.
    lfo.lists["lfos"] = {{{"period", 4}}, {{"period", 6}, {"phase", 1}}};
    lfo.params["count"] = 2;
    CHECK(triggerBars(s, lfo, "converge", 0, 1, std::int64_t{1} << 40).empty());
    Cord& c = cordTo(s, lfo, "converge", Scope::Group, s.groups[1].id, "reroll.rhythm");
    CHECK(nextCordFiring(s, c, 0).bar == -1);
    // Four large coprime periods in phrases of 16 bars: they converge every 16·61·59·53·47 = 143,441,744 bars. The
    // first is that far away, the next two after it, and a cord with Chance 1 % looks through all 64 of its turns.
    Song big = modSong();
    big.phraseLength = 16;
    ModSource& far = lfoSource(big, {{{"period", 61}, {"unit", 1}}, {{"period", 59}, {"unit", 1}}, {{"period", 53}, {"unit", 1}},
                                     {{"period", 47}, {"unit", 1}}});
    const std::int64_t L = std::int64_t{16} * 61 * 59 * 53 * 47;
    Cord& rare = cordTo(big, far, "converge", Scope::Group, big.groups[1].id, "reroll.rhythm");
    rare.chance = 1;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(triggerBars(big, far, "converge", 0, 3, std::int64_t{1} << 40) == (std::vector<std::int64_t>{L, 2 * L, 3 * L}));
    const Firing f = nextCordFiring(big, rare, 0);
    rare.chance = 0;
    const Firing never = nextCordFiring(big, rare, 0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(f.bar < 0 || (f.bar % L == 0 && f.bar / L - 1 == f.skipped));
    CHECK(never.bar == -1);
    CHECK(ms < 250.0);  // microseconds in practice; a scan through 64 · 143 million bars would take minutes
    std::printf("  next firing, 4 coprime LFOs (period %lld bars), 64 turns: %.3f ms\n", static_cast<long long>(L), ms);
}

// §17.9's other v1 sources, against §17.10 (python3 tools/spec_arith.py, "§17.10 sources").
static void modSources() {
    const std::uint64_t ms = 0x0123456789ABCDEFull;
    Song s = modSong();
    auto source = [&](const char* kind, std::map<std::string, int> params) -> ModSource& {
        ModSource& m = addSource(s, kind);
        m.id = 0xAA;
        m.params = std::move(params);
        return m;
    };
    auto at = [&](const ModSource& m, const char* out, std::initializer_list<std::int64_t> bars) {
        std::vector<int> v;
        for (std::int64_t b : bars) v.push_back(levelAt(s, m, out, b).value_or(-9999));
        return v;
    };
    // Accumulator, every 1 phrase (4 bars), offset 0, Step 3, S 8, Start 0, at b 0, 4, … 28.
    const auto bars = {std::int64_t{0}, std::int64_t{4}, std::int64_t{8}, std::int64_t{12}, std::int64_t{16}, std::int64_t{20},
                       std::int64_t{24}, std::int64_t{28}};
    auto positions = [&](int edge, int step) {
        std::vector<int> v;
        for (std::int64_t b : bars) v.push_back(accumulatorPosition(accumulatorTicks(b, 4, 0), step, 8, 0, edge, 0));
        return v;
    };
    CHECK(positions(0, 3) == v({0, 3, 6, 1, 4, 7, 2, 5}));   // Wrap
    CHECK(positions(1, 3) == v({0, 3, 6, 5, 2, 1, 4, 7}));   // Pendulum
    CHECK(positions(2, 3) == v({0, 3, 6, 7, 7, 7, 7, 7}));   // Hold
    CHECK(positions(0, -3) == v({0, 5, 2, 7, 4, 1, 6, 3}));  // Step −3, Wrap
    ModSource& acc = source("accumulator", {{"step", 3}});  // the rest are the defaults: 1 phrase, S 8, Pendulum
    CHECK(at(acc, "value", bars) == v({0, 429, 857, 714, 286, 143, 571, 1000}));
    // The step trigger: at each tick where p changed, amount p − p_before; between ticks, nothing.
    CHECK(triggerAt(s, acc, "step", 8)->amount == 3 && triggerAt(s, acc, "step", 12)->amount == -1 &&
          triggerAt(s, acc, "step", 16)->amount == -3 && !triggerAt(s, acc, "step", 13));
    CHECK(triggerAt(s, acc, "step", 4)->u == hash(ms, {"mod", std::uint64_t{0xAA}, "step", 4}));
    // Hold stops firing once it's held; Reset after 3 ticks starts again (p back to Start: a step down).
    acc.params["edge"] = 2;
    CHECK(triggerBars(s, acc, "step", 0, 5) == (std::vector<std::int64_t>{4, 8, 12}));
    acc.params["edge"] = 0, acc.params["reset"] = 3;
    CHECK(triggerAt(s, acc, "step", 12)->amount == -6 && levelAt(s, acc, "value", 12) == 0);
    // Offset counts back from the end, as a row's: every 1 phrase, offset −1 ticks at bars 3, 7, 11, …
    CHECK(accumulatorTicks(2, 4, -1) == 0 && accumulatorTicks(3, 4, -1) == 1 && accumulatorTicks(10, 4, -1) == 2);
    // A Step of 0 isn't one: it plays as the default, 1.
    acc.params = {{"step", 0}};
    CHECK(levelAt(s, acc, "value", 4) == rdiv(1000, 7));
    // With Depth S − 1 a cord moves its destination exactly p notches, for every S ≤ 64.
    for (int S = 2; S <= 64; ++S)
        for (int p = 0; p < S; ++p) CHECK(static_cast<int>(rdiv(rdiv(1000 * p, S - 1) * (S - 1), 1000)) == p);

    // 1/f dice, D 5, bars: the sums at b 0 … 15 and the levels at b 0 … 7.
    ModSource& dice = source("dice", {});
    std::vector<int> sums;
    for (int c = 0; c < 16; ++c) sums.push_back(diceSum(ms, 0xAA, c, 5));
    CHECK(sums == v({36, 44, 45, 43, 27, 23, 21, 27, 34, 32, 45, 40, 41, 38, 41, 48}));
    CHECK(at(dice, "level", {0, 1, 2, 3, 4, 5, 6, 7}) == v({-40, 173, 200, 147, -280, -387, -440, -280}));
    // change: +1 or −1, the way it moved, at a clock where the sum moved.
    CHECK(triggerAt(s, dice, "change", 1)->amount == 1 && triggerAt(s, dice, "change", 3)->amount == -1);
    for (std::int64_t b = 1; b < 16; ++b) CHECK(triggerAt(s, dice, "change", b).has_value() == (sums[b] != sums[b - 1]));
    // On phrases: a clock every 4 bars, so the level holds for a phrase and only a phrase's first bar can fire.
    dice.params["unit"] = 1;
    CHECK(levelAt(s, dice, "level", 5) == levelAt(s, dice, "level", 7) && levelAt(s, dice, "level", 4) == at(dice, "level", {4})[0]);
    CHECK(levelAt(s, dice, "level", 4) == static_cast<int>(rdiv(2000 * 44, 75) - 1000));
    CHECK(!triggerAt(s, dice, "change", 5) && triggerAt(s, dice, "change", 4)->amount == 1);
    // The slowest of five dice changes every 16 bars: die 4 is the same from bar 0 to 15.
    for (int c = 1; c < 16; ++c) CHECK(Stream(hash(ms, {"mod", std::uint64_t{0xAA}, "die", 4, c >> 4})).below(16) == Stream(hash(ms, {"mod", std::uint64_t{0xAA}, "die", 4, 0})).below(16));

    // Energy arc, Span 64 phrases (N 256), Loop.
    const std::vector<std::int64_t> ab = {0, 32, 64, 128, 153, 160, 166, 200, 230, 245, 255, 256};
    std::vector<int> pos, lvl;
    for (std::int64_t b : ab) pos.push_back(arcAt(b, 256, true).pos), lvl.push_back(arcAt(b, 256, true).level);
    CHECK(pos == v({0, 125, 250, 500, 598, 625, 648, 781, 898, 957, 996, 0}));
    CHECK(lvl == v({0, 75, 150, 471, 597, 425, 264, 643, 994, 1000, 100, 0}));
    ModSource& arc = source("arc", {});
    CHECK(levelAt(s, arc, "level", 200) == 643);
    std::vector<std::int64_t> fires;
    std::vector<int> amounts;
    for (std::int64_t b : triggerBars(s, arc, "section", 0, 6)) fires.push_back(b), amounts.push_back(triggerAt(s, arc, "section", b)->amount);
    CHECK(fires == (std::vector<std::int64_t>{64, 154, 167, 231, 246, 256}));
    CHECK(amounts == v({2, 3, 4, 5, 6, 1}));
    // Hold: it stays at the end (level 0, segment 6) and fires no more.
    arc.params["end"] = 1;
    CHECK(levelAt(s, arc, "level", 300) == 0 && triggerBars(s, arc, "section", 0, 8) == (std::vector<std::int64_t>{64, 154, 167, 231, 246}));
    // The arc's level in a song: Density +6 at the peak, from bar 0 (pos 0, level 0) on.
    Song song = modSong();
    ModSource& a2 = addSource(song, "arc");
    a2.params["span"] = 8;  // 32 bars: the peak (‰ 900–960) at bars 29–30
    Cord& c = addCord(song, a2.id, "level");
    c.dest = {Scope::Group, song.groups[0].id, "density"}, c.depth = 6;
    CHECK(stateAt(song, 0).groups[0].density == 10 && stateAt(song, 29).groups[0].density == 16 && stateAt(song, 32).groups[0].density == 10);
}

// §17.5: conditions. What each item did at each bar, as the replay has it (View::nextAct reads its own record).
static std::vector<std::int64_t> actsOf(const Song& s, std::uint64_t id, int bars) {
    View view(s);
    std::vector<std::int64_t> out;
    for (std::int64_t b = 1; b < bars; ++b)
        if (view.nextAct(id, b, 1).bar == b) out.push_back(b);
    return out;
}
static Condition cond(Condition::Kind k, bool negate = false) {
    Condition c;
    c.kind = k, c.negate = negate;
    return c;
}

static void modConditions() {
    using K = Condition::Kind;
    using Bars = std::vector<std::int64_t>;
    const std::uint64_t ms = 0x0123456789ABCDEFull;
    Song s = modSong();
    s.matrix.clear();
    const std::uint64_t leads = s.groups[0].id, bass = s.groups[1].id;
    auto addRowEvery = [&](std::uint64_t id, int every, int offset = 0) -> Row& {
        Row r = row(Scope::Group, leads, Param::Density);
        r.id = id, r.every = every, r.offset = offset;
        s.matrix.push_back(r);
        return s.matrix.back();
    };
    // §17.10: A:B 1:4 on a row every 1 bar passes at b = 1, 5, 9, 13, …
    Row& r = addRowEvery(0x51, 1);
    r.cond = cond(K::AB), r.cond.a = 1, r.cond.b = 4;
    CHECK(actsOf(s, 0x51, 14) == (Bars{1, 5, 9, 13}));
    r.cond.a = 3;
    CHECK(actsOf(s, 0x51, 14) == (Bars{3, 7, 11}));
    r.cond.negate = true;  // not 3:4
    CHECK(actsOf(s, 0x51, 9) == (Bars{1, 2, 4, 5, 6, 8}));
    // Opportunities, not bars: a row every 2 bars, 1:2, acts on every other of its turns (2, 6, 10).
    r.every = 2, r.cond.negate = false, r.cond.a = 1, r.cond.b = 2;
    CHECK(actsOf(s, 0x51, 12) == (Bars{2, 6, 10}));
    // 1ST: the first opportunity of the song (not of Play); not 1ST: every other one.
    r.cond = cond(K::First);
    CHECK(actsOf(s, 0x51, 12) == (Bars{2}));
    r.cond.negate = true;
    CHECK(actsOf(s, 0x51, 12) == (Bars{4, 6, 8, 10}));
    // Clock, then condition, then chance: a failed condition still counts as an opportunity, and the chance roll is
    // only asked where the condition passed. 1:2 at 50 %: acts exactly at the odd opportunities whose roll passes.
    r.every = 1, r.chance = 50, r.cond = cond(K::AB), r.cond.a = 1, r.cond.b = 2;
    Bars want;
    for (std::int64_t b = 1; b < 40; b += 2)
        if (roll(hash(hash(ms, {"matrix", std::uint64_t{0x51}, b}), {"chance"})) < 50) want.push_back(b);
    CHECK(actsOf(s, 0x51, 40) == want && !want.empty());

    // PRE: the item just before acted at its own latest opportunity at or before this bar.
    s.matrix.clear();
    Row& first = addRowEvery(0x61, 1);
    first.chance = 50;
    Row& second = addRowEvery(0x62, 2);
    second.cond = cond(K::Pre);
    const Bars firstActs = actsOf(s, 0x61, 40);
    Bars pre, notPre;
    for (std::int64_t b = 2; b < 40; b += 2)
        (std::find(firstActs.begin(), firstActs.end(), b) != firstActs.end() ? pre : notPre).push_back(b);
    CHECK(actsOf(s, 0x62, 40) == pre && !pre.empty() && !notPre.empty());
    s.matrix[1].cond.negate = true;
    CHECK(actsOf(s, 0x62, 40) == notPre);
    // The first item has no item before it: PRE is false, not PRE true.
    s.matrix[0].cond = cond(K::Pre);
    CHECK(actsOf(s, 0x61, 10).empty());
    s.matrix[0].cond = cond(K::Pre, true), s.matrix[0].chance = 100;
    CHECK(actsOf(s, 0x61, 5) == (Bars{1, 2, 3, 4}));
    // Before the item before has had an opportunity: false. It turns first at bar 3.
    s.matrix[0].cond = {}, s.matrix[0].every = 4, s.matrix[0].offset = 3;
    s.matrix[1].cond = cond(K::Pre), s.matrix[1].every = 1;
    CHECK(actsOf(s, 0x62, 9) == (Bars{3, 4, 5, 6, 7, 8}));
    // An item that's off isn't an item: PRE reads the one before it.
    Row zero = row(Scope::Group, leads, Param::Density);
    zero.id = 0x60, zero.every = 1, zero.chance = 0;
    s.matrix.insert(s.matrix.begin(), zero);  // never acts
    s.matrix[1].on = false;                  // 0x61 off: 0x62's item before is 0x60
    CHECK(actsOf(s, 0x62, 6).empty());
    s.matrix[1].on = true;

    // NEI: the item with that id, at its latest opportunity: at or before this bar for one before it, before this bar
    // for one after it (or itself).
    s.matrix.clear();
    Row& a = addRowEvery(0x71, 1);
    a.cond = cond(K::Nei), a.cond.item = 0x72;
    Row& bRow = addRowEvery(0x72, 1);
    bRow.chance = 50;
    const Bars bActs = actsOf(s, 0x72, 40);
    Bars nei;
    for (std::int64_t b : bActs)
        if (b + 1 < 40) nei.push_back(b + 1);  // 0x71 comes first: it reads 0x72's turn the bar before
    CHECK(actsOf(s, 0x71, 40) == nei);
    // Itself: acts when it acted last time; it never has, so never. Not NEI itself: every other opportunity.
    s.matrix[0].cond.item = 0x71;
    CHECK(actsOf(s, 0x71, 10).empty());
    s.matrix[0].cond.negate = true;
    CHECK(actsOf(s, 0x71, 7) == (Bars{1, 3, 5}));
    // A NEI whose item is gone, or isn't an item (a level cord), is false.
    s.matrix[0].cond.negate = false, s.matrix[0].cond.item = 0xDEAD;
    CHECK(actsOf(s, 0x71, 10).empty());
    ModSource& lfo = lfoSource(s, {{{"period", 4}}});
    Cord& lvl = cordTo(s, lfo, "lfo1", Scope::Group, bass, "density");
    s.matrix[0].cond.item = lvl.id;
    CHECK(actsOf(s, 0x71, 10).empty());

    // FILL: the destination's group fills at this bar (§4.6), from the state before this bar's triggers; a Global
    // destination only by the song's Fill.
    s = modSong();
    s.matrix.clear();
    s.groups[1].fillEvery = 1, s.groups[1].fillBars = 1;  // the Bass fills on the last bar of every phrase
    Row f = row(Scope::Group, bass, Param::Density);
    f.id = 0x81, f.every = 1, f.cond = cond(K::Fill);
    s.matrix.push_back(f);
    CHECK(actsOf(s, 0x81, 12) == (Bars{3, 7, 11}));
    s.matrix[0].scope = Scope::Voice, s.matrix[0].target = s.groups[1].voices[0].id;  // a voice in that group
    CHECK(actsOf(s, 0x81, 12) == (Bars{3, 7, 11}));
    s.matrix[0].scope = Scope::Global, s.matrix[0].target = 0, s.matrix[0].param = Param::Range;
    CHECK(actsOf(s, 0x81, 12).empty());
    s.fill = 1;
    CHECK(actsOf(s, 0x81, 5) == (Bars{1, 2, 3, 4}));
    // A row that turns the Bass's Fill on at bars 2 and 6 for a bar: FILL never sees it. It's gone by bar 3, and at
    // bar 2 the FILL row reads the state before bar 2's triggers.
    s.fill = 0;
    Row on = row(Scope::Group, bass, Param::Fill);
    on.id = 0x80, on.every = 4, on.offset = 2, on.allowed = {1}, on.lasts = 1;
    s.groups[1].fillEvery = 0;
    s.matrix.insert(s.matrix.begin(), on);
    s.matrix[1].scope = Scope::Group, s.matrix[1].target = bass, s.matrix[1].param = Param::Density;
    CHECK(actsOf(s, 0x81, 8).empty());
    s.matrix[0].lasts = 2;  // now it's still on at bars 3 and 7, before their triggers
    CHECK(actsOf(s, 0x81, 8) == (Bars{3, 7}));

    // On trigger cords: §17.7's example, converge every other time. 2 and 3 bars converge at 6, 12, 18, 24.
    s = modSong();
    s.matrix.clear();
    ModSource& tides = lfoSource(s, {{{"period", 2}}, {{"period", 3}}});
    Cord& c = cordTo(s, tides, "converge", Scope::Group, bass, "reroll.rhythm");
    c.lasts = 4, c.fixedSeed = false, c.cond = cond(K::AB), c.cond.a = 1, c.cond.b = 2;
    CHECK(actsOf(s, c.id, 30) == (Bars{6, 18}));
    const std::uint64_t base = s.groups[1].rhythmSeed;
    CHECK(stateAt(s, 6).groups[1].rhythmSeed != base && stateAt(s, 12).groups[1].rhythmSeed == base);
    // A cord after a row: PRE reads the last row. Every bar a row at 50 %; the cord acts at a convergence iff it did.
    Row lead = row(Scope::Group, leads, Param::Density);
    lead.id = 0x91, lead.every = 1, lead.chance = 50;
    s.matrix.push_back(lead);
    c.cond = cond(K::Pre);
    const Bars rowActs = actsOf(s, 0x91, 60);
    Bars cordWant;
    for (std::int64_t b = 6; b < 60; b += 6)
        if (std::find(rowActs.begin(), rowActs.end(), b) != rowActs.end()) cordWant.push_back(b);
    CHECK(actsOf(s, c.id, 60) == cordWant);
    // A condition this build doesn't know: the item never acts (and the song file keeps it, PluginDoc test).
    c.cond = {};
    c.cond.kind = K::Unknown, c.cond.extra = "{\"kind\": \"prob\"}";
    CHECK(actsOf(s, c.id, 30).empty() && stateAt(s, 6).groups[1].rhythmSeed == base);
    // Conditions don't touch a song without them: the default song and the setup are what they were (golden, variance).
    CHECK(Condition{} == Row{}.cond && Condition{} == Cord{}.cond);
}

// §5.8, Density from voices: each member's own Density is its place inside the group's; the line and the deal's
// weights are worked out from them. And §4.3: a voice's own Accent and Gate %.
static void densityFromVoices() {
    auto two = [](int D, int dA, int dB) {
        Song s = defaultSong(23);
        s.matrix.clear();
        s.groups.resize(1);
        Group& g = s.groups[0];
        g.voices.resize(1);
        g.voices[0].channel = 1;
        addVoice(s, g, VoiceType::Lead, 2);
        g.density = D, g.steps = 16, g.deal = DealMode::OnGenerate, g.swing = 0, g.jitter = 0;
        g.densityFromVoices = true;
        g.voices[0].density = dA, g.voices[1].density = dB;
        return s;
    };
    // The song it is the same as with the setting off: the group's Density at `line`, the members' Weights at their
    // Densities, and a member at 0 muted.
    auto plain = [](Song s, int line, int wA, int wB) {
        Group& g = s.groups[0];
        g.densityFromVoices = false, g.density = line;
        g.voices[0].weight = wA, g.voices[1].weight = wB;
        g.voices[0].mute = wA == 0, g.voices[1].mute = wB == 0;
        return s;
    };
    auto bar = [](const Song& s, std::int64_t b = 0) { return notes(s, b * barTicks(s), (b + 1) * barTicks(s)); };
    auto on = [](const std::vector<Note>& ns, int channel) {
        int n = 0;
        for (const Note& x : ns) n += x.channel == channel;
        return n;
    };
    auto starts = [](const std::vector<Note>& ns) {
        std::set<std::int64_t> out;
        for (const Note& x : ns) out.insert(x.start);
        return out;
    };

    // §5.8's worked values: (the group's Density, the members' Densities) → the line.
    const int rows[][4] = {{16, 3, 2, 5}, {10, 3, 2, 3}, {10, 8, 8, 10}, {16, 8, 13, 16}, {16, 13, 0, 13}, {10, 13, 0, 8}, {4, 1, 0, 1}};
    for (const auto& r : rows) {
        const Song s = two(r[0], r[1], r[2]);
        CHECK(static_cast<int>(bar(s).size()) == r[3]);           // the line's hits, on 16 steps
        CHECK(bar(s) == bar(plain(s, r[3], r[1], r[2])));         // and who plays them: the deal by their Densities
        const Song at = stateAt(s, 0);                            // the song as it plays shows the line and the weights
        CHECK(at.groups[0].density == r[3] && at.groups[0].voices[0].weight == r[1] && at.groups[0].voices[1].weight == r[2]);
    }
    // A member at 0 sits out; all at 0, the group rests. A muted member counts as 0.
    CHECK(on(bar(two(16, 13, 0)), 2) == 0 && on(bar(two(16, 13, 0)), 1) == 13);
    CHECK(bar(two(16, 0, 0)).empty());
    Song muted = two(16, 8, 8);
    muted.groups[0].voices[1].mute = true;
    CHECK(bar(muted).size() == 8 && on(bar(muted), 2) == 0);
    // In a mode that doesn't read Weight too: Solo on a member at 0 plays nothing.
    Song solo = two(16, 0, 8);
    solo.groups[0].deal = DealMode::Solo, solo.groups[0].soloMember = 0;
    CHECK(bar(solo).empty());
    solo.groups[0].soloMember = 1;
    CHECK(bar(solo).size() == 8 && on(bar(solo), 1) == 0);

    // Nested: a member's Density only adds hits to the line, and under 16 between them each notch adds exactly the
    // line's own next hits. At 16 or over the line is full (the group's Density) and stays where it is.
    for (int D : {16, 9}) {
        std::set<std::int64_t> before;
        for (int d = 0; d <= 16; ++d) {
            const std::set<std::int64_t> now = starts(bar(two(D, d, 4)));
            CHECK(std::includes(now.begin(), now.end(), before.begin(), before.end()));
            const int line = d + 4 >= 16 ? D : std::max(1, static_cast<int>(rdiv(D * (d + 4), 16)));
            CHECK(static_cast<int>(now.size()) == line);
            before = now;
        }
    }
    // Under 16 between them the other member keeps about what it had: over many songs, B at 4 plays close to 4 of 16
    // whether A is at 2 or at 10.
    int low = 0, high = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        Song a = two(16, 2, 4), b = two(16, 10, 4);
        a.masterSeed = b.masterSeed = seed;
        generate(a), generate(b);
        low += on(bar(a), 2), high += on(bar(b), 2);
    }
    std::printf("  density from voices: a member at 4 plays %.2f of 16 beside a member at 2, %.2f beside one at 10\n", low / 200.0, high / 200.0);
    CHECK(low > 200 * 3 && low < 200 * 5 && high > 200 * 3 && high < 200 * 5);

    // The group's Density stays the song's: a row walks it and the members keep their places. 13 of 16 is 13 hits
    // at 16 and 8 at 10 (§5.8's last-but-one rows).
    Song walked = two(16, 13, 0);
    Row down = row(Scope::Group, walked.groups[0].id, Param::Density);
    down.every = 2, down.offset = 1, down.maxShift = 6, down.min = 10, down.max = 10, down.lasts = 1;
    walked.matrix.push_back(down);
    CHECK(bar(walked, 0).size() == 13 && bar(walked, 1).size() == 8 && bar(walked, 2).size() == 13);
    // A member's Density is a destination (§17.2), 0–16: a row takes it to 0 for a bar, and the member sits out.
    Song dest = two(16, 8, 8);
    Row out = row(Scope::Voice, dest.groups[0].voices[1].id, Param::Density);
    out.every = 2, out.offset = 1, out.maxShift = 16, out.min = 0, out.max = 0, out.lasts = 1;
    dest.matrix.push_back(out);
    CHECK(on(bar(dest, 0), 2) > 0 && on(bar(dest, 1), 2) == 0 && bar(dest, 1).size() == 8 && on(bar(dest, 2), 2) > 0);

    // Off (the default): a member's Density is kept and unused, so nothing a song had changes.
    Song off = two(10, 8, 8);
    off.groups[0].densityFromVoices = false;
    Song moved = off;
    moved.groups[0].voices[0].density = 0, moved.groups[0].voices[1].density = 16;
    CHECK(bar(off) == bar(moved) && !Group().densityFromVoices && Voice().density == 8);

    // §4.3: a voice's own Accent and Gate % are its own notes'. Which hits are accented, and where they fall, stay
    // the group's.
    Song base = two(16, 8, 8);
    base.groups[0].densityFromVoices = false;
    Song own = base;
    own.groups[0].voices[1].overrides.accent = 100, own.groups[0].voices[1].overrides.gatePercent = 50;
    const std::vector<Note> nb = bar(base), no = bar(own);
    CHECK(nb.size() == no.size());
    bool firstSame = true, starts2 = true, loud = true, shorter = true, some = false;
    for (std::size_t i = 0; i < nb.size() && i < no.size(); ++i) {
        starts2 = starts2 && nb[i].start == no[i].start && nb[i].channel == no[i].channel && nb[i].note == no[i].note;
        if (nb[i].channel == 1) firstSame = firstSame && nb[i] == no[i];
        else {
            some = true;
            loud = loud && (no[i].velocity == 40 || no[i].velocity == 127) && (nb[i].velocity == 70 || nb[i].velocity == 113) &&
                   (no[i].velocity == 127) == (nb[i].velocity == 113);
            shorter = shorter && no[i].end - no[i].start <= nb[i].end - nb[i].start;
        }
    }
    CHECK(firstSame && starts2 && some && loud && shorter);
}

// §9.3, live changes: a ready song taken mid-bar plays from the next step, and a step that has begun plays as it was
// decided.
static void liveChanges() {
    using Key = std::tuple<std::int64_t, int, int, int>;  // an on: tick, channel, note, velocity
    auto ons = [](const std::vector<Event>& es) {
        std::multiset<Key> out;
        for (const Event& e : es)
            if (e.kind == Event::On) out.insert({e.tick, e.channel, e.note, e.velocity});
        return out;
    };
    auto onsOf = [](const std::vector<Note>& ns, std::int64_t from, std::int64_t to) {
        std::multiset<Key> out;
        for (const Note& n : ns)
            if (n.start >= from && n.start < to) out.insert({n.start, n.channel, n.note, n.velocity});
        return out;
    };
    Song a = defaultSong(0x1234);
    a.matrix.clear();
    for (Group& g : a.groups) g.swing = 0, g.jitter = 0;
    Song b = a;  // the same song, busier and louder: live values only
    b.groups[0].density = 16, b.groups[1].density = 14, b.accent = 100, b.groups[0].voices[0].mute = true;
    const std::int64_t bt = barTicks(a), step = bt / 16;
    const std::int64_t mid = 2 * bt + 6 * step, end = 4 * bt;  // taken six steps into bar 2

    // Taken live: the old song's notes up to the change, the new song's from the next step, on the same grid.
    Player live(a);
    std::vector<Event> heard = live.process(0, mid, true);
    Player::ReadyPtr old = live.take(Player::ready(b, 2), true);
    CHECK(old != nullptr);
    for (const Event& e : live.process(mid, end, true)) heard.push_back(e);
    std::multiset<Key> want = onsOf(notes(a, 0, end), 0, mid);
    for (const Key& k : onsOf(notes(b, 0, end), mid, end)) want.insert(k);
    CHECK(ons(heard) == want);
    CHECK(onsOf(notes(a, 0, end), mid, end) != onsOf(notes(b, 0, end), mid, end));  // and the change is one you'd hear
    // Block size doesn't matter: the same in blocks of a third of a step.
    Player small(a);
    std::vector<Event> pieces;
    for (std::int64_t t = 0; t < end; t += step / 3) {
        if (t == mid) small.take(Player::ready(b, 2), true);
        for (const Event& e : small.process(t, std::min(end, t + step / 3), true)) pieces.push_back(e);
    }
    CHECK(ons(pieces) == want);
    // Every note that starts is ended: nothing is left sounding by the swap.
    int on = 0, off = 0;
    for (const Event& e : heard) (e.kind == Event::On ? on : off)++;
    for (const Event& e : live.allOff(end)) off += e.kind == Event::Off;
    CHECK(on == off);

    // A step that has begun plays what it had left. With Swing, an odd step's note sounds after the step begins: a
    // live change in between leaves it, and a change on a bar line's terms (not live) drops it, as setSong does.
    Song sa = a, sb = b;
    for (Song* x : {&sa, &sb})
        for (Group& g : x->groups) g.swing = 80, g.density = 16;
    sb.groups[0].probability = 100, sb.accent = 20;  // still live values only
    const std::int64_t odd = 2 * bt + 5 * step, between = odd + step / 8;  // step 5 has begun; its swung note hasn't sounded
    const std::multiset<Key> late = onsOf(notes(sa, 0, end), between, odd + step);
    CHECK(!late.empty());
    auto after = [&](bool isLive) {
        Player p(sa);
        p.process(0, between, true);
        p.take(Player::ready(sb, 2), isLive);
        return ons(p.process(between, odd + step, true));
    };
    CHECK(after(true) == late);
    CHECK(after(false).empty());

    // neutralLive: songs that differ only in live values come out with every live value the same; a clock doesn't.
    Song na = a, nb = b;
    nb.groups[2].voices[0].overrides.gatePercent = 40, nb.groups[1].probability = 60, nb.fill = 1, nb.octaveLow = 0, nb.groups[0].soloMember = 1;
    nb.groups[0].voices[1].density = 3, nb.groups[0].voices[1].weight = 2, nb.groups[1].overrides.range = 12, nb.groups[1].fillNow = true;
    neutralLive(na), neutralLive(nb);
    CHECK(notes(na, 0, end) == notes(nb, 0, end));
    Song nc = a;
    nc.groups[0].steps = 12;
    neutralLive(nc);
    CHECK(notes(nc, 0, end) != notes(na, 0, end));

    // A ready song's state at its bar is worked out before it's taken: the first step after taking it costs no replay.
    Song lf = defaultSong(0x0123456789ABCDEFull);
    addLongForm(lf, true);
    const std::int64_t far = 3000;
    Player cold(lf), warm(lf);
    const auto t0 = std::chrono::steady_clock::now();
    cold.setSong(lf);
    cold.process(far * bt, far * bt + step, true);
    const auto t1 = std::chrono::steady_clock::now();
    Player::ReadyPtr r = Player::ready(lf, far);
    const auto t2 = std::chrono::steady_clock::now();
    warm.take(std::move(r), false);
    const std::vector<Event> first = warm.process(far * bt, far * bt + step, true);
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](auto x, auto y) { return std::chrono::duration<double, std::milli>(y - x).count(); };
    std::printf("  live changes, bar %lld of a long-form song: taking it cold %.2f ms; made ready %.2f ms off the audio thread, then %.3f ms to take and play\n",
                static_cast<long long>(far), ms(t0, t1), ms(t1, t2), ms(t2, t3));
    CHECK(ms(t2, t3) * 4 < ms(t0, t1));  // the audio thread's share is a fraction of the cold replay
    Player again(lf);
    CHECK(ons(first) == ons(again.process(far * bt, far * bt + step, true)));
}

int main() {
    arithmetic();
    priority();
    pitch();
    rhythm();
    jumps();
    midiRules();
    harmony();
    matrix();
    matrixJumps();
    player();
    editorViews();
    colour();
    modeMoves();
    nearestVoicing();
    staggerAndTop();
    humanize();
    evolve();
    nextFirings();
    dealing();
    densityFromVoices();
    liveChanges();
    songChanges();
    chanceRatchetsFills();
    documents();
    hostile();
    goldens();
    generatorSlots();
    euclidean();
    metric();
    registerGenerator();
    pinkAndPedal();
    derived();
    resultant();
    partitions();
    derivedGraph();
    cleanHandoffs();
    modWorkedValues();
    modFramework();
    modNextIsBounded();
    modSources();
    modConditions();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all checks pass\n");
    return failures ? 1 : 0;
}
