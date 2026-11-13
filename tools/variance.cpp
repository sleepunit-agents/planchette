// planchette_variance [count] [bars] [setup|plain|moves|nearest|humanize|walk|back|knobs|euclid|metric|register|pink|run|lines|tides|drift|conds|
//                  nyogcag|telu|empat|nyokcok|roundrobin|interlock|resultant|resultantalone|
//                  alternate|bells|tutti|hoketus|reference|freeze|clean|cleanrr|form|gong|irama|tihai]: how different are the songs one setup can make?
//
// One song's settings and matrix rows stay fixed; only Generate changes (new seeds for the harmony, each group's rhythm
// and deal, each voice's notes). It renders `count` Generates (default 2000) for `bars` bars (default 64) and
// measures how far apart they land: how many distinct progressions, rhythms and melodies come out, how the
// note-level features spread, how alike two songs are, how near each song's nearest neighbour is, and how much the
// matrix moves a song within itself. `setup` (the default) is a five-row test song; `plain` the same with no rows,
// to see what Generate alone varies; `moves` the setup with Mode moves on (§3.8);
// `nearest` with its chords on Voicing "nearest" (§3.4a); `euclid` with the Leads on Euclidean (Pulses 7) and the
// Bass on Euclidean (Pulses 5, Rotation 2) (§16.4); `metric` with the Leads on Metric at Wild 4 and the Bass at Wild 0;
// `register` with the Leads' rhythm and the Bass's notes on Registers (Lock 12, §16.7); `pink` and `run` with the
// Leads' notes on 1/f Wander, Riff and Run (§16.5); `lines` asks how each Notes generator moves: one Lead alone (Range
// and Wander 12, Density 16, 16 bars), how often it moves by a step and how far, with Octave jumps off and on; `humanize` with every voice humanized (§4.7); `walk` and `back` the long-form
// starters (Walk around the planet, There and back).
// `knobs` asks another question: how much does each of §4.6's knobs (probability, ratchets, fills) change a song? It
// renders `plain` with one knob turned on the Leads and Bass, and compares each Generate with itself, knob off.
// Modulation (§17): `tides` the setup with an LFO source (5 and 7 bars) on the Leads' Density (±4) and the second
// Lead's Mute (half the time), its convergence (bar 35) rerolling the Bass's rhythm for 8 bars; `drift` the setup with
// an accumulator (every bar, Step 3) on the global Accent, 1/f dice on the Leads' Density whose changes vary 2 of the
// Bass's hits, and an energy arc over 16 phrases on the Leads' Density whose sections turn a chord for 4 bars; `conds`
// the setup's rows with conditions (§17.5): the turnaround every other time (1:2), the Bass's new rhythm on 1:4, and
// the Leads' density walk only while the key move before it hasn't acted (not PRE), so it stops once the key has moved.
// Derived generators (§16.6): `nyogcag`, `telu`, `empat` and `nyokcok` put the second Lead on Kotekan following the
// first, the Leads on Round robin; `roundrobin` is that deal alone, both Leads on Wander; `interlock` the Bass on
// Interlock with the Leads at Density 16 (it plays every step they leave); `resultant` adds a group doubling the Leads'
// composite between 55 and 67 an octave up, Accented, and `resultantalone` mutes the Leads so only that band is heard.
// `freeze` asks what Freeze (§9.2) holds: the setup held for 16 bars, against the same bars playing on. `form` how
// exactly a letter returns and how far the others go (§18.1); `gong` where the colotomic clock puts the changes (§18.2);
// `irama` the Leads' notes per chord as a row moves their level on every gong (§18.3); `tihai` the Leads' cadences
// into every phrase and every gong (§18.4).
// Deal by pitch and all together (§5.5, §5.6): `alternate` and `bells` deal the Leads' tune by its scale place; `tutti`
// puts both Leads on every hit; `hoketus` adds the chords on Pulse and Tutti, on Interlock in the Leads' gaps.
// Every mode but `setup` also reports the Leads' handoffs (how often the next note is the other Lead's, and how often
// such a handoff is on the same pitch class); `reference` is setup itself with those rows.
// `clean` turns on the Leads' Clean handoffs (§5.7), and `cleanrr` does on Round robin (`roundrobin` without it).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "hocket/engine.hpp"
#include "hocket/form.hpp"
#include "hocket/generators.hpp"
#include "hocket/mod.hpp"

using namespace hocket;

namespace {

int scaleNamed(const char* name) {
    for (std::size_t i = 0; i < kScales.size(); ++i)
        if (std::string(kScales[i].name) == name) return static_cast<int>(i);
    return 1;
}

// The setup: the default song in F Phrygian, Colour 12, and five rows that move it around over 64 bars.
Song setup() {
    Song s = defaultSong(0x5EEDF00DCAFEull);
    s.root = 5, s.scale = scaleNamed("Phrygian"), s.colour = 12;
    const std::uint64_t leads = s.groups[0].id, bass = s.groups[1].id;
    // 1. (the default) Leads · Vary Pitch · depth 4 · every phrase, last bar · reverts after 1 bar
    // 2. Bass · Reroll Rhythm · every 2 phrases · fresh · stays
    Row& r2 = addRow(s);
    r2.scope = Scope::Group, r2.target = bass, r2.param = Param::RerollRhythm, r2.every = 2, r2.inPhrases = true, r2.fixedSeed = false;
    // 3. Harmony · Vary Pitch · depth 2 · every 4 phrases · reverts after 4 bars
    Row& r3 = addRow(s);
    r3.scope = Scope::Harmony, r3.param = Param::VaryPitch, r3.every = 4, r3.inPhrases = true, r3.maxShift = 2, r3.lasts = 4;
    // 4. Global · Root · a fifth either way · every 8 phrases · reverts after 8 bars
    Row& r4 = addRow(s);
    r4.scope = Scope::Global, r4.param = Param::Root, r4.every = 8, r4.inPhrases = true, r4.maxShift = 1, r4.lasts = 8;
    // 5. Leads · Density · ±3 · every 2 bars · 50 % · 6–14 · stays (a random walk)
    Row& r5 = addRow(s);
    r5.scope = Scope::Group, r5.target = leads, r5.param = Param::Density, r5.every = 2, r5.chance = 50, r5.maxShift = 3, r5.min = 6,
    r5.max = 14;
    return s;
}

struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double at(double q) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0 : v[static_cast<std::size_t>(std::min<double>(v.size() - 1, q * (v.size() - 1) + 0.5))];
    }
    double mean() const { return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
    double sd() const {
        const double m = mean();
        double s = 0;
        for (double x : v) s += (x - m) * (x - m);
        return v.size() > 1 ? std::sqrt(s / (v.size() - 1)) : 0;
    }
};

void row(const char* name, Stats s, const char* unit = "") {
    std::printf("  %-34s mean %7.2f  sd %6.2f  | p5 %7.2f  median %7.2f  p95 %7.2f  | min %7.2f  max %7.2f %s\n", name, s.mean(), s.sd(),
                s.at(0.05), s.at(0.5), s.at(0.95), s.at(0), s.at(1), unit);
}

// Distinct values and how evenly they're used: the count, and the entropy as an "effective count" (2^H): 2000 songs
// spread evenly over 50 patterns gives 50; the same 50 with one taking half the songs gives far fewer.
void spread(const char* name, const std::vector<std::string>& xs) {
    std::map<std::string, int> n;
    for (const auto& x : xs) ++n[x];
    double h = 0;
    int top = 0;
    for (const auto& [k, c] : n) {
        const double p = static_cast<double>(c) / xs.size();
        h -= p * std::log2(p);
        top = std::max(top, c);
    }
    std::printf("  %-34s %5zu distinct of %zu   effective %7.1f   most common: %4.1f %% of songs\n", name, n.size(), xs.size(), std::pow(2.0, h),
                100.0 * top / xs.size());
}

// Two sorted sets of events: shared / all (Jaccard).
double jaccard(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
    std::size_t i = 0, j = 0, both = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) ++both, ++i, ++j;
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    const std::size_t all = a.size() + b.size() - both;
    return all ? static_cast<double>(both) / all : 1.0;
}

// ---- Yang & Lerch (2018), "On the evaluation of generative models in music" -----------------------------------
// Nine features of a stretch of music; distances between stretches, per feature; and how much two sets of distances
// overlap (as smoothed distributions): their overlapped area (1: the same, 0: apart) and KL divergence.
struct Features {
    double pitchCount = 0, pitchRange = 0, interval = 0, noteCount = 0, ioi = 0;
    std::vector<double> pc = std::vector<double>(12), pcTrans = std::vector<double>(144), len = std::vector<double>(5),
                        lenTrans = std::vector<double>(25);
};
const char* const kFeatureNames[] = {"pitch count",  "pitch-class histogram", "pitch-class transitions", "pitch range",
                                     "average interval", "note count", "average inter-onset", "note-length histogram",
                                     "note-length transitions"};

int lengthBin(std::int64_t ticks) { return ticks <= 120 ? 0 : ticks <= 240 ? 1 : ticks <= 480 ? 2 : ticks <= 960 ? 3 : 4; }

Features features(std::vector<Note> ns) {
    Features f;
    std::sort(ns.begin(), ns.end(), [](const Note& a, const Note& b) { return std::tie(a.start, a.note) < std::tie(b.start, b.note); });
    if (ns.empty()) return f;
    std::set<int> pitches;
    int lo = 127, hi = 0;
    double interval = 0, ioi = 0;
    for (std::size_t k = 0; k < ns.size(); ++k) {
        const Note& n = ns[k];
        pitches.insert(n.note);
        lo = std::min(lo, n.note), hi = std::max(hi, n.note);
        f.pc[static_cast<std::size_t>(n.note % 12)] += 1;
        f.len[static_cast<std::size_t>(lengthBin(n.end - n.start))] += 1;
        if (k) {
            const Note& m = ns[k - 1];
            interval += std::abs(n.note - m.note);
            ioi += static_cast<double>(n.start - m.start);
            f.pcTrans[static_cast<std::size_t>((m.note % 12) * 12 + n.note % 12)] += 1;
            f.lenTrans[static_cast<std::size_t>(lengthBin(m.end - m.start) * 5 + lengthBin(n.end - n.start))] += 1;
        }
    }
    const double n = static_cast<double>(ns.size());
    f.pitchCount = static_cast<double>(pitches.size()), f.pitchRange = hi - lo, f.noteCount = n;
    f.interval = n > 1 ? interval / (n - 1) : 0, f.ioi = n > 1 ? ioi / (n - 1) : 0;
    for (auto* v : {&f.pc, &f.pcTrans, &f.len, &f.lenTrans}) {
        double t = 0;
        for (double x : *v) t += x;
        if (t > 0)
            for (double& x : *v) x /= t;
    }
    return f;
}

double euclid(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0;
    for (std::size_t i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s);
}

std::vector<double> distance(const Features& a, const Features& b) {
    return {std::abs(a.pitchCount - b.pitchCount), euclid(a.pc, b.pc), euclid(a.pcTrans, b.pcTrans), std::abs(a.pitchRange - b.pitchRange),
            std::abs(a.interval - b.interval), std::abs(a.noteCount - b.noteCount), std::abs(a.ioi - b.ioi), euclid(a.len, b.len),
            euclid(a.lenTrans, b.lenTrans)};
}

// Two samples smoothed into densities (Gaussian kernels, Silverman's bandwidth) on one grid: overlapped area and
// KL(p || q).
std::pair<double, double> compare(const std::vector<double>& p, const std::vector<double>& q) {
    if (p.size() < 2 || q.size() < 2) return {0, 0};
    double lo = 1e300, hi = -1e300;
    for (const auto* v : {&p, &q})
        for (double x : *v) lo = std::min(lo, x), hi = std::max(hi, x);
    auto bandwidth = [](const std::vector<double>& v) {
        double m = std::accumulate(v.begin(), v.end(), 0.0) / v.size(), s = 0;
        for (double x : v) s += (x - m) * (x - m);
        s = std::sqrt(s / (v.size() - 1));
        return std::max(1e-6, 1.06 * s * std::pow(static_cast<double>(v.size()), -0.2));
    };
    const double hp = bandwidth(p), hq = bandwidth(q);
    lo -= 3 * std::max(hp, hq), hi += 3 * std::max(hp, hq);
    const int grid = 256;
    const double dx = (hi - lo) / grid;
    double overlap = 0, kl = 0, sp = 0, sq = 0;
    std::vector<double> dp(grid), dq(grid);
    for (int g = 0; g < grid; ++g) {
        const double x = lo + (g + 0.5) * dx;
        for (double v : p) dp[g] += std::exp(-0.5 * (x - v) * (x - v) / (hp * hp));
        for (double v : q) dq[g] += std::exp(-0.5 * (x - v) * (x - v) / (hq * hq));
        sp += dp[g], sq += dq[g];
    }
    for (int g = 0; g < grid; ++g) {
        const double a = dp[g] / sp, b = dq[g] / sq;
        overlap += std::min(a, b);
        if (a > 0) kl += a * std::log(a / std::max(b, 1e-12));
    }
    return {overlap, kl};
}

// Toussaint's swap distance between two rhythms on the same grid: with as many onsets each, the least number of
// adjacent swaps that turns one into the other (the sum of the sorted onsets' differences); otherwise the Hamming
// distance.
int swapDistance(std::vector<int> a, std::vector<int> b) {
    std::sort(a.begin(), a.end()), std::sort(b.begin(), b.end());
    a.erase(std::unique(a.begin(), a.end()), a.end()), b.erase(std::unique(b.begin(), b.end()), b.end());
    if (a.size() == b.size()) {
        int d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
        return d;
    }
    std::vector<int> both;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(both));
    return static_cast<int>(both.size());
}

std::uint64_t key(std::int64_t tick, int a, int b) { return (static_cast<std::uint64_t>(tick) << 16) | (static_cast<std::uint64_t>(a) << 8) | b; }

// The lines' notes (not the sustained chords) as sorted keys, per bar.
std::vector<std::vector<std::uint64_t>> lineBars(const std::vector<Note>& ns, std::int64_t bar, int bars) {
    std::vector<std::vector<std::uint64_t>> out(static_cast<std::size_t>(bars));
    for (const Note& n : ns)
        if (n.channel != 3 && n.start / bar < bars) out[static_cast<std::size_t>(n.start / bar)].push_back(key(n.start % bar, n.channel, n.note));
    for (auto& b : out) std::sort(b.begin(), b.end());
    return out;
}

// §4.6's knobs, one at a time, against the same Generate with the knob off.
int knobs(int count, int bars) {
    Song base = setup();
    base.matrix.clear();
    const std::int64_t bar = barTicks(base);
    struct Knob {
        const char* name;
        std::function<void(Group&)> set;
    };
    const std::vector<Knob> all = {
        {"off (all defaults)", [](Group&) {}},
        {"Probability 90 %", [](Group& g) { g.probability = 90; }},
        {"Probability 75 %", [](Group& g) { g.probability = 75; }},
        {"Probability 50 %", [](Group& g) { g.probability = 50; }},
        {"Ratchet 10 %, Repeats 2", [](Group& g) { g.ratchet = 10; }},
        {"Ratchet 25 %, Repeats 4", [](Group& g) { g.ratchet = 25, g.repeats = 4; }},
        {"Ratchet 50 %, Repeats 4, Ramp -50", [](Group& g) { g.ratchet = 50, g.repeats = 4, g.ramp = -50; }},
        {"Fill every phrase, 1 bar, amount 8", [](Group& g) { g.fillEvery = 1; }},
        {"Fill every 2 phrases, 4 bars", [](Group& g) { g.fillEvery = 2, g.fillBars = 4; }},
        {"Fill now (every bar), amount 8", [](Group& g) { g.fillNow = true; }},
    };
    std::printf("Knobs (§4.6) on the Leads and Bass of `plain` (F Phrygian, no matrix rows). %d Generates x %d bars; each against\n"
                "the same Generate with the knob off. Lines only (the sustained chords don't take these knobs).\n\n", count, bars);
    std::printf("  %-36s %10s %13s %13s %13s %11s\n", "knob", "notes/bar", "shared w/ off", "bars changed", "bar vs +1 phr", "distinct");
    for (const Knob& k : all) {
        Stats perBar, shared, changed, repeat, distinct;
        for (int i = 0; i < count; ++i) {
            Song off = base;
            off.generateCount = static_cast<std::uint64_t>(i);
            generate(off);
            Song on = off;
            for (int gi : {0, 1}) k.set(on.groups[static_cast<std::size_t>(gi)]);
            const auto a = lineBars(notes(off, 0, bar * bars), bar, bars), b = lineBars(notes(on, 0, bar * bars), bar, bars);
            std::vector<std::uint64_t> fa, fb;
            int moved = 0;
            std::size_t n = 0;
            for (int x = 0; x < bars; ++x) {
                const auto& A = a[static_cast<std::size_t>(x)];
                const auto& B = b[static_cast<std::size_t>(x)];
                for (std::uint64_t e : A) fa.push_back(e + (static_cast<std::uint64_t>(x) << 48));
                for (std::uint64_t e : B) fb.push_back(e + (static_cast<std::uint64_t>(x) << 48));
                moved += A != B;
                n += B.size();
            }
            Stats rep;
            for (int x = 0; x + base.phraseLength < bars; ++x) rep.add(jaccard(b[static_cast<std::size_t>(x)], b[static_cast<std::size_t>(x + base.phraseLength)]));
            perBar.add(static_cast<double>(n) / bars);
            shared.add(100.0 * jaccard(fa, fb));
            changed.add(100.0 * moved / bars);
            repeat.add(100.0 * rep.mean());
            distinct.add(static_cast<double>(std::set<std::vector<std::uint64_t>>(b.begin(), b.end()).size()));
        }
        std::printf("  %-36s %10.2f %11.1f %% %11.1f %% %11.1f %% %8.1f/%d\n", k.name, perBar.mean(), shared.mean(), changed.mean(), repeat.mean(),
                    distinct.mean(), bars);
    }
    std::printf("\n  notes/bar: line notes per bar (a ratchet's repeats count each). shared w/ off: of all the notes in either\n"
                "  render, the share that are the same note at the same time (Jaccard). bars changed: bars that differ from the\n"
                "  knob-off render at all. bar vs +1 phr: how alike a bar is to the bar a phrase later (the plain song repeats\n"
                "  every phrase). distinct: different bars in the song.\n");
    return 0;
}

}  // namespace

// One Lead alone on each Notes generator: how its line moves (§16.5).
int lines(int count, int bars) {
    std::printf("One Lead alone, Range 12, Wander 12, Density 16, %d songs x %d bars:\n", count, bars);
    for (const char* gen : {"wander", "pink", "register", "pedal"})
        for (int jumps : {0, 16}) {
            double moves = 0, small = 0, total = 0, notesAll = 0;
            for (int i = 0; i < count; ++i) {
                Song s = defaultSong(1000 + static_cast<std::uint64_t>(i));
                s.matrix.clear();
                s.groups.resize(1);
                s.groups[0].voices.resize(1);
                s.groups[0].density = 16;
                s.octaveJumps = jumps;
                s.groups[0].voices[0].notesGen.use = gen;
                s.groups[0].voices[0].overrides.range = 12, s.groups[0].voices[0].overrides.wander = 12;
                const std::vector<Note> ns = notes(s, 0, barTicks(s) * bars);
                notesAll += static_cast<double>(ns.size());
                for (std::size_t k = 1; k < ns.size(); ++k) {
                    const int d = std::abs(ns[k].note - ns[k - 1].note);
                    if (d) moves++, small += d <= 2, total += d;
                }
            }
            std::printf("  %-9s octave jumps %2d: repeats %5.1f %%, stepwise (1-2 st) %5.1f %% of moves, mean move %5.2f st\n", gen, jumps,
                        100.0 * (1.0 - moves / std::max(1.0, notesAll - count)), 100.0 * small / std::max(1.0, moves), total / std::max(1.0, moves));
        }
    return 0;
}

// §9.2: Freeze on the setup, held from bar 17 for 16 bars (pressed during bar 16, let go during bar 32). How much
// the held bars hold still, against the same bars unfrozen, and that the song after it is its own again.
int freeze(int count, int bars) {
    const Song base = setup();
    const std::int64_t bar = barTicks(base), F = 17, R = 33;
    std::printf("Freeze (§9.2) on `setup` (5 rows), held from bar %lld to bar %lld. %d Generates x %d bars; lines only.\n\n",
                static_cast<long long>(F), static_cast<long long>(R), count, bars);
    Stats heldSame, plainSame, heldDistinct, plainDistinct, heldVsLast, plainVsLast, afterSame;
    for (int i = 0; i < count; ++i) {
        Song s = base;
        s.generateCount = static_cast<std::uint64_t>(i);
        generate(s);
        const auto plain = lineBars(notes(s, 0, bar * bars), bar, bars);
        const auto held = lineBars(notes(s, 0, bar * bars, F), bar, bars);
        // the window, frozen and not: a bar against the bar a phrase later; distinct bars; each bar against bar F - 1
        Stats hs, ps, hl, pl;
        std::set<std::vector<std::uint64_t>> hd, pd;
        for (std::int64_t b = F; b < R; ++b) {
            const auto& H = held[static_cast<std::size_t>(b)];
            const auto& P = plain[static_cast<std::size_t>(b)];
            hd.insert(H), pd.insert(P);
            if (b + base.phraseLength < R) {
                hs.add(jaccard(H, held[static_cast<std::size_t>(b + base.phraseLength)]));
                ps.add(jaccard(P, plain[static_cast<std::size_t>(b + base.phraseLength)]));
            }
            hl.add(jaccard(H, held[static_cast<std::size_t>(b - base.phraseLength * ((b - F) / base.phraseLength + 1))]));
            pl.add(jaccard(P, plain[static_cast<std::size_t>(b - base.phraseLength * ((b - F) / base.phraseLength + 1))]));
        }
        heldSame.add(100 * hs.mean()), plainSame.add(100 * ps.mean());
        heldDistinct.add(static_cast<double>(hd.size())), plainDistinct.add(static_cast<double>(pd.size()));
        heldVsLast.add(100 * hl.mean()), plainVsLast.add(100 * pl.mean());
        // After it lets go: the song's own (a frozen render from R on is the replay's).
        Player player(s);
        std::vector<Note> heard;
        for (std::int64_t t = 0; t < bar * bars; t += 960) {
            if (t > 0) player.setFreeze(t >= (F - 1) * bar + 960 && t < (R - 1) * bar + 960);
            for (const Event& e : player.process(t, t + 960, true))
                if (e.kind == Event::On) heard.push_back({e.tick, e.tick + 1, e.channel, e.note, e.velocity, 0});
        }
        const auto released = lineBars(heard, bar, bars);
        Stats a;
        for (int b = static_cast<int>(R) + 1; b < bars; ++b) a.add(jaccard(released[static_cast<std::size_t>(b)], plain[static_cast<std::size_t>(b)]));
        afterSame.add(100 * a.mean());
    }
    std::printf("  %-44s %10s %10s\n", "", "frozen", "playing on");
    std::printf("  %-44s %8.1f %% %8.1f %%\n", "bar vs the bar a phrase later (in the window)", heldSame.mean(), plainSame.mean());
    std::printf("  %-44s %8.1f %% %8.1f %%\n", "bar vs the same phrase bar before the window", heldVsLast.mean(), plainVsLast.mean());
    std::printf("  %-44s %7.1f/16 %7.1f/16\n", "distinct bars in the 16", heldDistinct.mean(), plainDistinct.mean());
    std::printf("  %-44s %8.1f %%\n", "after it lets go: shared with the song's own", afterSame.mean());
    std::printf("\n  Frozen, the matrix's walk, rerolls and key moves stop and the held layers stay; the harmony, the steps and\n"
                "  the deal play on, so the held song comes round every pass of its progression (here a phrase).\n");
    return 0;
}

// §18.1: the form. The setup with the form A B A C A′ B over 8-bar sections: B rolls new notes and rhythm, C new notes
// and chords, A′ is A with 4 notes a line changed. Each section against the opening A: how much of it is A's (the same
// note at the same place in the section), and how far its features are, against the yardstick of one song's opening
// against another's (Yang & Lerch: overlap 1 means as far from A as another song is).
int formReport(int count, int bars) {
    Song base = setup();
    base.form.on = true, base.form.text = "A B A C A' B", base.form.section = 2;
    FormLetter* b = addLetter(base);
    FormLetter* c = addLetter(base);
    c->rhythm = false, c->harmony = true;
    (void)b;
    const std::int64_t bar = barTicks(base), S = sectionBars(base);
    const int sections = static_cast<int>(std::min<std::int64_t>(6, bars / S));
    std::printf("The form (§18.1) on `setup` (5 rows): A B A C A' B, sections of %lld bars. %d Generates x %d sections; lines only.\n\n",
                static_cast<long long>(S), count, sections);
    const char* names[] = {"A (the opening)", "B", "A again", "C", "A' (A primed)", "B again"};
    std::vector<Stats> shared(6), withSame(6);
    std::vector<std::vector<std::vector<double>>> dist(6, std::vector<std::vector<double>>(9));
    std::vector<Features> openings;
    for (int i = 0; i < count; ++i) {
        Song s = base;
        s.generateCount = static_cast<std::uint64_t>(i);
        generate(s);
        const std::vector<Note> ns = notes(s, 0, bar * S * sections);
        std::vector<std::vector<std::uint64_t>> sec(static_cast<std::size_t>(sections));
        std::vector<std::vector<Note>> raw(static_cast<std::size_t>(sections));
        for (const Note& n : ns) {
            if (n.channel == 3) continue;
            const auto k = static_cast<std::size_t>(n.start / (bar * S));
            sec[k].push_back(key(n.start % (bar * S), n.channel, n.note));
            raw[k].push_back(n);
        }
        for (auto& x : sec) std::sort(x.begin(), x.end());
        const Features open = features(raw[0]);
        openings.push_back(open);
        for (int k = 0; k < sections; ++k) {
            shared[static_cast<std::size_t>(k)].add(100 * jaccard(sec[0], sec[static_cast<std::size_t>(k)]));
            const auto d = distance(open, features(raw[static_cast<std::size_t>(k)]));
            for (int f = 0; f < 9; ++f) dist[static_cast<std::size_t>(k)][static_cast<std::size_t>(f)].push_back(d[static_cast<std::size_t>(f)]);
        }
        if (sections > 5) withSame[5].add(100 * jaccard(sec[1], sec[5]));
    }
    std::vector<std::vector<double>> between(9);
    const int pairs = std::min(count, 300);
    for (int a = 0; a < pairs; ++a)
        for (int b2 = a + 1; b2 < pairs; ++b2) {
            const auto d = distance(openings[static_cast<std::size_t>(a)], openings[static_cast<std::size_t>(b2)]);
            for (int f = 0; f < 9; ++f) between[static_cast<std::size_t>(f)].push_back(d[static_cast<std::size_t>(f)]);
        }
    std::printf("  %-18s %16s   %s\n", "section", "shared with A", "Yang & Lerch overlap with song-vs-song (pitch-class hist., pc transitions, IOI)");
    for (int k = 0; k < sections; ++k) {
        // A feature that never moves from A's (every distance 0) is a copy: no spread to smooth.
        auto cell = [&](int f) {
            const auto& d = dist[static_cast<std::size_t>(k)][static_cast<std::size_t>(f)];
            if (*std::max_element(d.begin(), d.end()) < 1e-9) return std::string(" copy");
            char buf[16];
            std::snprintf(buf, sizeof buf, "%5.2f", compare(d, between[static_cast<std::size_t>(f)]).first);
            return std::string(buf);
        };
        std::printf("  %-18s %13.1f %%   %s  %s  %s\n", names[k], shared[static_cast<std::size_t>(k)].mean(), cell(1).c_str(), cell(2).c_str(),
                    cell(6).c_str());
    }
    if (sections > 5) std::printf("\n  B again vs the first B: %.1f %% shared\n", withSame[5].mean());
    std::printf("\n  shared with A: the same note at the same place in the section (Jaccard). A again is A exactly; A' is A's\n"
                "  fidelity; B and C its contrast. Overlap near 0: as near to A as a copy; near 1: as far as another song.\n");
    return 0;
}

// §18.2: the colotomic clock. The setup, and the setup with its big rows on the gong (the Bass's new rhythm every gong,
// the key move every 2) and its Density walk on the kenongs. How often a change lands on gong bars, on kenong bars and
// on the rest (a bar against itself under the bar before's state): the clock should put the changes on the strokes.
int gongReport(int count, int bars) {
    std::printf("The colotomic clock (§18.2): gong cycle 4 phrases (16 bars), 4 kenongs. %d Generates x %d bars; lines only.\n"
                "How often a change lands on a bar: it sounds different from itself played under the bar before's state.\n\n", count, bars);
    std::printf("  %-34s %10s %10s %10s\n", "", "gong bars", "kenongs", "the rest");
    for (int v = 0; v < 2; ++v) {
        Song base = setup();
        if (v == 1) {
            base.matrix[1].clock = 2, base.matrix[1].every = 1, base.matrix[1].inPhrases = false;  // Bass new rhythm: every gong
            base.matrix[3].clock = 2, base.matrix[3].every = 2, base.matrix[3].inPhrases = false;  // the key: every 2 gongs
            base.matrix[4].clock = 3, base.matrix[4].every = 1, base.matrix[4].chance = 100;       // Density walk: on the kenongs
        }
        const std::int64_t bar = barTicks(base);
        Stats onGong, onKenong, rest;
        for (int i = 0; i < count; ++i) {
            Song s = base;
            s.generateCount = static_cast<std::uint64_t>(i);
            generate(s);
            const auto per = lineBars(notes(s, 0, bar * bars), bar, bars);
            // A change lands on bar b when the bar sounds different from itself played under the state of the bar
            // before: Freeze held from b (§9.2) is exactly that.
            for (int b = 1; b < bars; ++b) {
                const auto held = lineBars(notes(s, b * bar, (b + 1) * bar, b), bar, b + 1);
                const double lands = held[static_cast<std::size_t>(b)] != per[static_cast<std::size_t>(b)] ? 100.0 : 0.0;
                (gongAt(s, b) ? onGong : kenongAt(s, b) ? onKenong : rest).add(lands);
            }
        }
        std::printf("  %-34s %8.1f %% %8.1f %% %8.1f %%\n", v == 0 ? "setup (rows on bars and phrases)" : "setup on the gong clock", onGong.mean(),
                    onKenong.mean(), rest.mean());
    }
    return 0;
}

// §18.3: irama. The setup with the Leads' level moved by a row on every gong (Allowed 2, 4, 8 and 16 notes a chord,
// Max shift 1: a level up or down a step), from 4. The Leads' notes per chord in each gong cycle, by level, and how
// often the gong changed the level.
int iramaReport(int count, int bars) {
    Song base = setup();
    base.groups[0].irama = 3;
    Row& r = addRow(base);
    r.scope = Scope::Group, r.target = base.groups[0].id, r.key = "irama", r.clock = 2, r.every = 1, r.allowed = {2, 3, 4, 5}, r.maxShift = 1;
    const std::int64_t bar = barTicks(base), N = gongBars(base);
    std::printf("Irama (§18.3) on `setup`: the Leads from 4 notes a chord, a level up or down on every gong (Allowed 2, 4, 8, 16).\n"
                "%d Generates x %d bars (gong cycles of %lld bars); the Leads' notes per chord change, per cycle.\n\n",
                count, bars, static_cast<long long>(N));
    std::map<int, Stats> perLevel;
    int gongs = 0, moved = 0;
    for (int i = 0; i < count; ++i) {
        Song s = base;
        s.generateCount = static_cast<std::uint64_t>(i);
        generate(s);
        const std::vector<Note> ns = notes(s, 0, bar * bars);
        int last = -1;
        for (std::int64_t c = 0; c * N < bars; ++c) {
            const int level = stateAt(s, c * N).groups[0].irama;
            std::int64_t leads = 0;
            for (const Note& n : ns)
                if ((n.channel == 1 || n.channel == 2) && n.start >= c * N * bar && n.start < (c + 1) * N * bar) ++leads;
            const double changes = static_cast<double>(N * bar) / changeTicks(stateAt(s, c * N));
            perLevel[level].add(leads / changes);
            if (c > 0) ++gongs, moved += level != last;
            last = level;
        }
    }
    static const char* names[] = {"off", "1 a chord", "2 a chord", "4 a chord", "8 a chord", "16 a chord"};
    std::printf("  %-14s %10s %18s\n", "level", "cycles", "Leads notes/chord");
    for (auto& [level, st] : perLevel)
        std::printf("  %-14s %10zu %12.2f (of %d)\n", names[level], st.v.size(), st.mean(), level ? 1 << (level - 1) : 0);
    std::printf("\n  the gong changed the level at %.1f %% of %d gongs (the row's Max shift 1: up or down a level, never the same)\n",
                gongs ? 100.0 * moved / gongs : 0.0, gongs);
    std::printf("  notes/chord is under the level by the Leads' Density (10 of 16 steps); at Density 16 it is the level.\n");
    return 0;
}

// §18.4: tihai. The setup with a tihai on the Leads into every phrase (N 5, g 1: the last 17 sixteenths before the
// sam), and into every gong. What it does to the song: the sam struck (a Leads note on the downbeat, and accented), the
// three statements the same rhythm, and how much of the rest of the song it leaves alone.
int tihaiReport(int count, int bars) {
    std::printf("Tihai (§18.4) on `setup`'s Leads: N 5, g 1. %d Generates x %d bars; Leads only.\n\n", count, bars);
    std::printf("  %-26s %12s %12s %16s %14s\n", "", "sam struck", "accented", "3 statements =", "rest the same");
    for (int v = 0; v < 3; ++v) {
        Song base = setup();
        if (v > 0) base.groups[0].tihai = 1, base.groups[0].tihaiUnit = v == 2 ? 1 : 0;
        const std::int64_t bar = barTicks(base), T = 240;
        Stats struck, accented, same, rest;
        for (int i = 0; i < count; ++i) {
            Song s = base;
            s.generateCount = static_cast<std::uint64_t>(i);
            generate(s);
            Song off = s;
            off.groups[0].tihai = 0;
            std::map<std::int64_t, int> on, plain;  // Leads onsets by step → velocity
            for (const Note& n : notes(s, 0, bar * bars))
                if (n.channel <= 2 && n.start % T == 0) on[n.start / T] = std::max(on[n.start / T], n.velocity);
            for (const Note& n : notes(off, 0, bar * bars))
                if (n.channel <= 2 && n.start % T == 0) plain[n.start / T] = std::max(plain[n.start / T], n.velocity);
            const std::int64_t period = (v == 2 ? gongBars(s) : s.phraseLength) * bar / T;  // steps between sams
            for (std::int64_t sam = period; sam * T < bar * bars; sam += period) {
                struck.add(on.count(sam) ? 100 : 0);
                accented.add(on.count(sam) && on[sam] > 100 ? 100 : 0);
                const std::int64_t a0 = sam - 17;
                bool eq = true;
                for (int r = 0; r < 5; ++r) eq = eq && on.count(a0 + r) == on.count(a0 + 6 + r) && on.count(a0 + r) == on.count(a0 + 12 + r);
                same.add(eq ? 100 : 0);
            }
            for (const auto& [a, vel] : plain) {
                const std::int64_t to = period - a % period;  // steps to the next sam
                if (v > 0 && (to <= 17 || a % period == 0)) continue;
                rest.add(on.count(a) && on[a] == vel ? 100 : 0);
            }
        }
        std::printf("  %-26s %10.1f %% %10.1f %% %14.1f %% %12.1f %%\n", v == 0 ? "no tihai" : v == 1 ? "a tihai into every phrase" : "a tihai into every gong",
                    struck.mean(), accented.mean(), same.mean(), rest.mean());
    }
    std::printf("\n  sam struck: a Leads note on each phrase's (or gong's) downbeat. 3 statements =: the onsets of the three\n"
                "  5-step statements match (no tihai: by chance). rest the same: Leads notes outside the windows, untouched.\n");
    return 0;
}

int main(int argc, char** argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int bars = argc > 2 ? std::atoi(argv[2]) : 64;
    const std::string mode = argc > 3 ? argv[3] : "setup";
    if (mode == "knobs") return knobs(count, bars);
    if (mode == "lines") return lines(count, bars);
    if (mode == "freeze") return freeze(count, bars);
    if (mode == "form") return formReport(count, bars);
    if (mode == "gong") return gongReport(count, bars);
    if (mode == "irama") return iramaReport(count, bars);
    if (mode == "tihai") return tihaiReport(count, bars);
    Song base = setup();
    if (mode == "plain") base.matrix.clear();
    if (mode == "moves") base.modeMoves = 1;  // §3.8: the same setup, the harmony on Mode moves
    if (mode == "nearest") base.groups[2].voices[0].nearest = true;  // §3.4a: the chords on Voicing "nearest"
    if (mode == "euclid") {  // §16.4: the same setup, the Leads and Bass rhythms made even
        base.groups[0].rhythmGen.use = "euclid", base.groups[0].rhythmGen.params["euclid"] = {{"pulses", 7}, {"rotation", 0}};
        base.groups[1].rhythmGen.use = "euclid", base.groups[1].rhythmGen.params["euclid"] = {{"pulses", 5}, {"rotation", 2}};
    }
    if (mode == "metric") {  // §16.4: the Leads near the meter, the Bass on it (Beat 4: quarter notes)
        base.groups[0].rhythmGen.use = "metric", base.groups[0].rhythmGen.params["metric"] = {{"wild", 4}};
        base.groups[1].rhythmGen.use = "metric", base.groups[1].rhythmGen.params["metric"] = {{"wild", 0}};
    }
    if (mode == "register") {  // §16.7: a slipping register for the Leads' rhythm and the Bass's notes, at the defaults
        base.groups[0].rhythmGen.use = "register";
        base.groups[1].voices[0].notesGen.use = "register";
    }
    if (mode == "pink" || mode == "run")  // §16.5: the Leads' notes on 1/f Wander
        for (Voice& v : base.groups[0].voices) {
            v.notesGen.use = "pink";
            if (mode == "run") v.notesGen.params["pink"]["loop"] = 1;
        }
    // §16.6: the second Lead on Kotekan following the first, the Leads on Round robin (strict alternation)
    const char* kotekanModes[] = {"nyogcag", "telu", "empat", "nyokcok"};
    for (int m = 0; m < 4; ++m)
        if (mode == kotekanModes[m]) {
            base.groups[0].deal = DealMode::RoundRobin;
            Voice& b = base.groups[0].voices[1];
            b.notesGen.use = "kotekan", b.notesGen.sources["kotekan"] = base.groups[0].voices[0].id, b.notesGen.params["kotekan"]["mode"] = m;
        }
    if (mode == "roundrobin") base.groups[0].deal = DealMode::RoundRobin;  // the kotekan modes' deal, with Wander on both
    if (mode == "interlock") {  // §16.6: the Bass plays where the Leads rest, every free step it has
        base.groups[1].rhythmGen.use = "interlock", base.groups[1].rhythmGen.sources["interlock"] = base.groups[0].id;
        base.groups[1].density = 16;
    }
    if (mode == "resultant" || mode == "resultantalone") {  // §16.6: the Leads' composite, 55–67, doubled an octave up
        Group& r = addGroup(base, "Resultant");
        Voice& v = addVoice(base, r, VoiceType::Lead, 5);
        v.octaveTranspose = 1;
        r.rhythmGen.use = "resultant", r.rhythmGen.sources["resultant"] = base.groups[0].id;
        r.rhythmGen.params["resultant"] = {{"low", 55}, {"high", 67}, {"velocity", 1}};
        if (mode == "resultantalone") base.groups[0].mute = true;  // the inherent pattern heard alone
    }
    if (mode == "alternate") base.groups[0].deal = DealMode::Alternate;  // §5.5: the Leads share one tune by pitch (siku)
    if (mode == "bells") base.groups[0].deal = DealMode::Bells;          // §5.5: two bells each
    if (mode == "tutti") base.groups[0].deal = DealMode::Tutti;          // §5.6: both Leads on every hit
    if (mode == "hoketus") {  // §5.6: the Leads on Tutti; the chords on Pulse, Tutti, in the Leads' gaps (Interlock)
        base.groups[0].deal = DealMode::Tutti;
        Group& ch = base.groups[2];
        ch.voices[0].articulation = Articulation::Pulse;
        ch.deal = DealMode::Tutti, ch.density = 16;
        ch.rhythmGen.use = "interlock", ch.rhythmGen.sources["interlock"] = base.groups[0].id;
    }
    if (mode == "clean" || mode == "cleanrr") {  // §5.7: the Leads' handoffs cleaned (cleanrr: on Round robin, every note one)
        base.groups[0].cleanHandoffs = true;
        if (mode == "cleanrr") base.groups[0].deal = DealMode::RoundRobin;
    }
    if (mode == "humanize")  // §4.7: every voice humanized, velocity ±12, timing 50 %
        for (Group& g : base.groups)
            for (Voice& v : g.voices) v.humanVelocity = 12, v.humanTiming = 50;
    if (mode == "tides") {  // §17: an LFO source and three cords on the setup
        ModSource& src = addSource(base, "lfo");
        src.lists["lfos"] = {{{"period", 5}}, {{"period", 7}}};
        Cord& d = addCord(base, src.id, "lfo1");
        d.dest = {Scope::Group, base.groups[0].id, "density"}, d.depth = 4;
        Cord& m = addCord(base, src.id, "lfo2");
        m.dest = {Scope::Voice, base.groups[0].voices[1].id, "mute"}, m.threshold = 0;
        Cord& c = addCord(base, src.id, "converge");
        c.dest = {Scope::Group, base.groups[1].id, "reroll.rhythm"}, c.lasts = 8, c.fixedSeed = false;
    }
    if (mode == "drift") {  // §17.9: an accumulator, 1/f dice and an energy arc on the setup
        ModSource& acc = addSource(base, "accumulator");
        acc.params = {{"every", 1}, {"inPhrases", 0}, {"step", 3}};
        ModSource& dice = addSource(base, "dice");
        ModSource& arc = addSource(base, "arc");
        arc.params["span"] = 16;
        auto cord = [&](const ModSource& src, const char* out, Scope sc, std::uint64_t t, const char* key) -> Cord& {
            Cord& c = addCord(base, src.id, out);
            c.dest = {sc, t, key};
            return c;
        };
        cord(acc, "value", Scope::Global, 0, "accent").depth = 40;
        cord(dice, "level", Scope::Group, base.groups[0].id, "density").depth = 4;
        Cord& v = cord(dice, "change", Scope::Group, base.groups[1].id, "vary.rhythm");
        v.maxShift = 2, v.lasts = 1;
        cord(arc, "level", Scope::Group, base.groups[0].id, "density").depth = 6;
        Cord& t = cord(arc, "section", Scope::Harmony, 0, "vary.pitch");
        t.lasts = 4, t.fixedSeed = false;
    }
    if (mode == "conds") {  // §17.5: the same rows, gated
        base.matrix[0].cond.kind = Condition::Kind::AB, base.matrix[0].cond.a = 1, base.matrix[0].cond.b = 2;
        base.matrix[1].cond.kind = Condition::Kind::AB, base.matrix[1].cond.a = 1, base.matrix[1].cond.b = 4;
        base.matrix[4].cond.kind = Condition::Kind::Pre, base.matrix[4].cond.negate = true;
    }
    if (mode == "walk" || mode == "back") {
        base = defaultSong(0x5EEDF00DCAFEull);
        base.root = mode == "walk" ? 2 : 9, base.scale = scaleNamed(mode == "walk" ? "Dorian" : "Minor");
        addLongForm(base, mode == "back");
    }
    const std::int64_t bar = barTicks(base), step = kTicksPerBeat / 4;
    std::printf("Setup (%s): %s %s, Colour %d%s, %zu groups, %zu matrix rows. %d Generates x %d bars.\n\n", mode.c_str(), noteName(base.root).c_str(),
                kScales[static_cast<std::size_t>(base.scale)].name, base.colour, base.modeMoves ? ", Mode moves" : "", base.groups.size(), base.matrix.size(),
                count, bars);

    std::vector<std::string> progressions, leadRhythm, bassRhythm, leadLine, bassLine, songHash;
    Stats homeSame, awaySame;  // a bar after the first 64, against the same place in the first 64
    Stats notesPerBar, leadNotes, bassNotes, distinctPitches, leadPitch, leadSpan, leadLeap, leadStepwise, colourShare, chordChanges,
        selfDistinctBars, selfRepeat;
    Stats handoffs, handoffSame;  // §5.5: lead notes that pass to the other Lead, and of those, the ones on the same pitch class
    std::vector<std::vector<std::uint64_t>> events, rhythms, pitches;  // first 16 bars: (tick,ch,note), (tick,ch), (ch,note) bag
    const auto colours = colourNotes(base.root, base.scale);
    // Yang & Lerch: each song's 8-bar stretches (the lines, not the sustained chords). The first is the opening.
    const int seg = 8;
    std::vector<Features> openings;
    std::vector<std::vector<double>> homeDist(9), awayDist(9);  // a stretch at home / away against its song's opening
    Stats swapHome, swapAway;                                   // the bar's rhythm against bar 1's (Leads and Bass)

    for (int i = 0; i < count; ++i) {
        Song s = base;
        s.generateCount = static_cast<std::uint64_t>(i);
        generate(s);
        const std::vector<Note> ns = notes(s, 0, bar * bars);

        View view(s);
        std::string prog;
        for (int r : view.harmony(0).roots) prog += noteName(r) + " ";
        progressions.push_back(prog);

        // The first bar's rhythm and the first two bars' line, per part (Leads: ch 1–2 together; Bass: ch 4).
        std::uint32_t lr = 0, br = 0;
        std::string ll, bl;
        std::vector<int> lead;
        std::set<int> pcs;
        int ln = 0, bn = 0, inColour = 0, total = 0;
        for (const Note& n : ns) {
            const bool isLead = n.channel == 1 || n.channel == 2, isBass = n.channel == 4;
            const std::int64_t st = n.start / step;
            if (n.start < bar) {
                if (isLead) lr |= 1u << (st % 16);
                if (isBass) br |= 1u << (st % 16);
            }
            if (n.start < 2 * bar) {
                if (isLead) ll += std::to_string(st) + ":" + std::to_string(n.note) + " ";
                if (isBass) bl += std::to_string(st) + ":" + std::to_string(n.note) + " ";
            }
            if (isLead) ++ln, lead.push_back(n.note);
            if (isBass) ++bn;
            if (n.channel != 3) {
                ++total;
                inColour += std::find(colours.begin(), colours.end(), n.note % 12) != colours.end();
            }
            pcs.insert(n.note);
        }
        leadRhythm.push_back(std::to_string(lr));
        bassRhythm.push_back(std::to_string(br));
        leadLine.push_back(ll);
        bassLine.push_back(bl);
        notesPerBar.add(static_cast<double>(ns.size()) / bars);
        leadNotes.add(static_cast<double>(ln) / bars);
        bassNotes.add(static_cast<double>(bn) / bars);
        distinctPitches.add(static_cast<double>(pcs.size()));
        colourShare.add(total ? 100.0 * inColour / total : 0);
        if (!lead.empty()) {
            leadPitch.add(std::accumulate(lead.begin(), lead.end(), 0.0) / lead.size());
            leadSpan.add(*std::max_element(lead.begin(), lead.end()) - *std::min_element(lead.begin(), lead.end()));
            double leap = 0;
            int stepwise = 0;
            for (std::size_t k = 1; k < lead.size(); ++k) {
                const int d = std::abs(lead[k] - lead[k - 1]);
                leap += d;
                stepwise += d > 0 && d <= 2;
            }
            leadLeap.add(lead.size() > 1 ? leap / (lead.size() - 1) : 0);
            leadStepwise.add(lead.size() > 1 ? 100.0 * stepwise / (lead.size() - 1) : 0);
        }
        {
            // The Leads' composite, onset by onset (a Tutti step counts once): how often the next note is the other
            // Lead's, and how often such a handoff lands on the same pitch class (a unison or an octave).
            std::vector<const Note*> ls;
            for (const Note& n : ns)
                if ((n.channel == 1 || n.channel == 2) && (ls.empty() || ls.back()->start != n.start)) ls.push_back(&n);
            int ho = 0, same = 0;
            for (std::size_t k = 1; k < ls.size(); ++k)
                if (ls[k]->channel != ls[k - 1]->channel) ++ho, same += ls[k]->note % 12 == ls[k - 1]->note % 12;
            handoffs.add(ls.size() > 1 ? 100.0 * ho / (ls.size() - 1) : 0);
            handoffSame.add(ho ? 100.0 * same / ho : 0);
        }
        // Within the song: how many of its bars are different from each other (the matrix at work), and how alike a
        // bar is to the same bar one phrase later.
        std::vector<std::vector<std::uint64_t>> perBar(static_cast<std::size_t>(bars));
        for (const Note& n : ns)
            if (n.channel != 3) perBar[static_cast<std::size_t>(n.start / bar)].push_back(key(n.start % bar, n.channel, n.note));
        std::set<std::vector<std::uint64_t>> distinct;
        for (auto& b : perBar) std::sort(b.begin(), b.end()), distinct.insert(b);
        selfDistinctBars.add(static_cast<double>(distinct.size()));
        Stats rep;
        for (int b = 0; b + 4 < bars; ++b) rep.add(jaccard(perBar[static_cast<std::size_t>(b)], perBar[static_cast<std::size_t>(b + 4)]));
        selfRepeat.add(100.0 * rep.mean());
        {
            const int cycle = 16 * s.phraseLength, half = 8 * s.phraseLength;
            std::vector<std::vector<Note>> stretches(static_cast<std::size_t>((bars + seg - 1) / seg));
            std::vector<std::vector<int>> leadOn(static_cast<std::size_t>(bars)), bassOn(static_cast<std::size_t>(bars));
            for (const Note& n : ns) {
                if (n.channel == 3) continue;
                stretches[static_cast<std::size_t>(n.start / (bar * seg))].push_back(n);
                const auto b = static_cast<std::size_t>(n.start / bar);
                const int at = static_cast<int>((n.start % bar) / step);
                (n.channel == 4 ? bassOn : leadOn)[b].push_back(at);
            }
            const Features open = features(stretches[0]);
            openings.push_back(open);
            for (std::size_t k = 1; k < stretches.size(); ++k) {
                const int b0 = static_cast<int>(k) * seg;
                if (b0 < cycle) continue;  // the first cycle is where it sets out from
                const auto d = distance(open, features(stretches[k]));
                for (int f = 0; f < 9; ++f) (b0 % cycle < half ? homeDist : awayDist)[static_cast<std::size_t>(f)].push_back(d[static_cast<std::size_t>(f)]);
            }
            for (int b = cycle; b < bars; ++b) {
                const double sd = swapDistance(leadOn[static_cast<std::size_t>(b)], leadOn[static_cast<std::size_t>(b % (2 * s.phraseLength))]) +
                                  swapDistance(bassOn[static_cast<std::size_t>(b)], bassOn[static_cast<std::size_t>(b % (2 * s.phraseLength))]);
                (b % cycle < half ? swapHome : swapAway).add(sd);
            }
        }
        // Coming home: bars where the song should be home (the first 8 phrases of every 16) against the same bar in
        // the opening; and the bars away, against it too.
        {
            const int cycle = 16 * s.phraseLength, half = 8 * s.phraseLength;
            Stats home, away;
            for (int b = cycle; b < bars; ++b)
                (b % cycle < half ? home : away).add(jaccard(perBar[static_cast<std::size_t>(b)], perBar[static_cast<std::size_t>(b % cycle)]));
            if (!home.v.empty()) homeSame.add(100.0 * home.mean());
            if (!away.v.empty()) awaySame.add(100.0 * away.mean());
        }
        std::set<std::string> changes;
        for (int b = 0; b < bars; ++b) changes.insert(view.chordsByBar(b, 1)[0]);
        chordChanges.add(static_cast<double>(changes.size()));

        // For comparing songs: the first 16 bars, as exact events, as rhythm only, and as a bag of pitches.
        std::vector<std::uint64_t> ev, rh, pb;
        std::uint64_t h = 1469598103934665603ull;
        for (const Note& n : ns) {
            h = (h ^ key(n.start, n.channel, n.note)) * 1099511628211ull;
            if (n.start >= 16 * bar || n.channel == 3) continue;
            ev.push_back(key(n.start, n.channel, n.note));
            rh.push_back(key(n.start, n.channel, 0));
            pb.push_back((static_cast<std::uint64_t>(n.channel) << 40) | (static_cast<std::uint64_t>(n.note) << 24) | pb.size());
        }
        songHash.push_back(std::to_string(h));
        std::sort(ev.begin(), ev.end());
        std::sort(rh.begin(), rh.end());
        rh.erase(std::unique(rh.begin(), rh.end()), rh.end());
        events.push_back(std::move(ev));
        rhythms.push_back(std::move(rh));
        // A bag of pitches: each (channel, note) numbered by its occurrence, so counts matter and time doesn't.
        std::map<std::uint64_t, int> seen;
        std::vector<std::uint64_t> bag;
        for (std::uint64_t x : pb) {
            const std::uint64_t cn = x >> 24;
            bag.push_back((cn << 24) | static_cast<std::uint64_t>(seen[cn]++));
        }
        std::sort(bag.begin(), bag.end());
        pitches.push_back(std::move(bag));
    }

    std::printf("What comes out (distinct across the songs; \"effective\" weighs how evenly they're used):\n");
    spread("whole songs (every note, all bars)", songHash);
    spread("chord progressions (first pass)", progressions);
    spread("lead rhythm, bar 1", leadRhythm);
    spread("bass rhythm, bar 1", bassRhythm);
    spread("lead line, bars 1-2 (time + pitch)", leadLine);
    spread("bass line, bars 1-2 (time + pitch)", bassLine);

    std::printf("\nHow the songs' features spread:\n");
    row("notes per bar (all parts)", notesPerBar);
    row("lead notes per bar", leadNotes);
    row("bass notes per bar", bassNotes);
    row("distinct MIDI notes used", distinctPitches);
    row("lead: mean pitch (MIDI)", leadPitch);
    row("lead: span, lowest to highest", leadSpan, "semitones");
    row("lead: average jump", leadLeap, "semitones");
    row("lead: stepwise moves (1-2 st)", leadStepwise, "%");
    row("line notes on the colour note", colourShare, "%");
    row("different chords heard", chordChanges);
    if (mode != "setup") {  // setup's report stays byte for byte what it was: the check every change is held to
        row("lead: handoffs (next note, other Lead)", handoffs, "%");
        row("lead: handoffs on the same pitch class", handoffSame, "%");
    }

    // Song against song, first 16 bars. Every pair for up to 600 songs.
    const int m = std::min(count, 600);
    Stats exact, rhythm, pitch, nearestExact, nearestRhythm;
    int nearDupes = 0;
    for (int a = 0; a < m; ++a) {
        double ne = 0, nr = 0;
        for (int b = 0; b < m; ++b) {
            if (a == b) continue;
            const double e = jaccard(events[a], events[b]), r = jaccard(rhythms[a], rhythms[b]);
            if (b > a) exact.add(100 * e), rhythm.add(100 * r), pitch.add(100 * jaccard(pitches[a], pitches[b]));
            ne = std::max(ne, e);
            nr = std::max(nr, r);
        }
        nearestExact.add(100 * ne);
        nearestRhythm.add(100 * nr);
        nearDupes += ne >= 0.5;
    }
    std::printf("\nSong against song (first 16 bars, %d songs, every pair; %% of notes shared):\n", m);
    row("same note, same time", exact, "%");
    row("same onset, any pitch (rhythm)", rhythm, "%");
    row("same pitches, any time", pitch, "%");
    row("nearest neighbour: notes", nearestExact, "%");
    row("nearest neighbour: rhythm", nearestRhythm, "%");
    std::printf("  songs sharing half their notes with another: %d of %d\n", nearDupes, m);

    std::printf("\nInside one song (%d bars; the matrix at work):\n", bars);
    row("distinct bars", selfDistinctBars, ("of " + std::to_string(bars)).c_str());
    row("bar vs same bar a phrase later", selfRepeat, "% shared");
    if (!homeSame.v.empty()) {
        row("home phrases vs the opening", homeSame, "% shared");
        row("away phrases vs the opening", awaySame, "% shared");
        row("rhythm drift at home (swap dist.)", swapHome, "steps");
        row("rhythm drift away (swap dist.)", swapAway, "steps");
    }

    // Yang & Lerch: three sets of distances per feature, from 8-bar stretches.
    //   between songs: one song's opening against another's (how different Generate makes them: the yardstick)
    //   home / away:   a stretch at home / away (after the first 16 phrases) against its own song's opening
    // A home stretch that's as near as a copy has overlap ~0 with "between songs" and a small mean; an away stretch
    // that's as new as another song overlaps "between songs" heavily.
    std::vector<std::vector<double>> between(9);
    const int pairs = std::min(count, 400);
    for (int a = 0; a < pairs; ++a)
        for (int b = a + 1; b < pairs; ++b) {
            const auto d = distance(openings[static_cast<std::size_t>(a)], openings[static_cast<std::size_t>(b)]);
            for (int f = 0; f < 9; ++f) between[static_cast<std::size_t>(f)].push_back(d[static_cast<std::size_t>(f)]);
        }
    if (!homeDist[0].empty()) {
        std::printf("\nYang & Lerch (8-bar stretches; mean distance, and overlap with song-vs-song: 1 = as far as another song):\n");
        std::printf("  %-26s %10s | %10s %9s | %10s %9s\n", "feature", "song/song", "home", "overlap", "away", "overlap");
        for (int f = 0; f < 9; ++f) {
            auto mean = [](const std::vector<double>& v) { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); };
            const auto h = compare(homeDist[static_cast<std::size_t>(f)], between[static_cast<std::size_t>(f)]);
            const auto w = compare(awayDist[static_cast<std::size_t>(f)], between[static_cast<std::size_t>(f)]);
            // A feature every song shares (note count, when density is a setting) has no song-to-song spread to
            // measure against: its overlaps mean nothing.
            const auto& bv = between[static_cast<std::size_t>(f)];
            const bool flat = *std::max_element(bv.begin(), bv.end()) - *std::min_element(bv.begin(), bv.end()) < 1e-9;
            auto cell = [&](double x) { return flat ? std::string("      n/a") : (std::string(x < 0.995 ? " " : "") + std::to_string(x).substr(0, 4)).insert(0, 5 - (x < 0.995 ? 1 : 0), ' '); };
            std::printf("  %-26s %10.3f | %10.3f %s | %10.3f %s\n", kFeatureNames[f], mean(bv), mean(homeDist[static_cast<std::size_t>(f)]),
                        cell(h.first).c_str(), mean(awayDist[static_cast<std::size_t>(f)]), cell(w.first).c_str());
        }
    }
    return 0;
}
