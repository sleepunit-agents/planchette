// Long-form structure's tests (SPEC §9 Freeze, and the form features that follow it). Their own executable, so they
// sit beside tests/tests.cpp without sharing its main. No framework: each CHECK prints where it failed, and the exit
// code is the failure count. The worked values are SPEC's, as `python3 tools/spec_arith.py` prints them.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "hocket/engine.hpp"
#include "hocket/form.hpp"
#include "hocket/matrix.hpp"
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

namespace {

using Key = std::tuple<std::int64_t, int, int, int>;  // start, channel, note, velocity
std::vector<Key> keys(const std::vector<Note>& ns, std::int64_t from, std::int64_t to, bool lines = false) {
    std::vector<Key> out;
    for (const Note& n : ns)
        if (n.start >= from && n.start < to && (!lines || n.channel != 3)) out.push_back({n.start, n.channel, n.note, n.velocity});
    std::sort(out.begin(), out.end());
    return out;
}
std::vector<Key> ons(const std::vector<Event>& es, std::int64_t from, std::int64_t to) {
    std::vector<Key> out;
    for (const Event& e : es)
        if (e.kind == Event::On && e.tick >= from && e.tick < to) out.push_back({e.tick, e.channel, e.note, e.velocity});
    std::sort(out.begin(), out.end());
    return out;
}

// The default song with the §9 worked example's modulation: an LFO (17 bars, Triangle) on the Leads' Density at Depth
// 4, and a Density walk on the Bass every bar.
Song freezeSong() {
    Song s = defaultSong(0x0123456789ABCDEFull);
    s.matrixSeed = 0x0123456789ABCDEFull;
    ModSource& src = addSource(s, "lfo");
    const std::uint64_t sid = src.id;
    Cord& c = addCord(s, sid, "lfo1");
    c.dest = {Scope::Group, s.groups[0].id, "density"};
    c.depth = 4;
    Row& r = addRow(s);
    r.scope = Scope::Group, r.target = s.groups[1].id, r.param = Param::Density, r.every = 1, r.maxShift = 2, r.min = 2, r.max = 10;
    return s;
}

// Plays a song a block at a time over [0, end), Freeze pressed at tick `on` and let go at tick `off` (−1: never).
std::vector<Event> play(const Song& s, std::int64_t end, std::int64_t on, std::int64_t off, Player* keep = nullptr) {
    Player local(s);
    Player& p = keep ? *keep : local;
    std::vector<Event> out;
    for (std::int64_t t = 0; t < end; t += 500) {
        if (on >= 0 && t >= on) p.setFreeze(off < 0 || t < off);
        for (const Event& e : p.process(t, std::min(end, t + 500), true)) out.push_back(e);
    }
    return out;
}

}  // namespace

// §9, §17.6: Freeze holds what you hear; the replay runs on underneath; a jump lets go.
static void freeze() {
    const Song s = freezeSong();
    const std::int64_t bt = barTicks(s);

    // The worked values: the LFO's level at bars 8 and 13 is 882 and −58, so Depth 4 moves the Leads' Density by +4
    // and 0 (§17.10). Pressed during bar 8 and let go during bar 12, bars 9–12 hold bar 8's +4; bar 13 has 0.
    const int moved[] = {3, 4, 4, 1, 0, -1};  // the replay's, at bars 7, 8, 9, 12, 13, 14
    const int at[] = {7, 8, 9, 12, 13, 14};
    for (int i = 0; i < 6; ++i) CHECK(stateAt(s, at[i]).groups[0].density == 10 + moved[i]);

    // No Freeze is the plain render.
    CHECK(keys(notes(s, 0, 24 * bt, -1), 0, 24 * bt) == keys(notes(s, 0, 24 * bt), 0, 24 * bt));
    CHECK(keys(notes(s, 0, 24 * bt, 0), 0, 24 * bt) == keys(notes(s, 0, 24 * bt), 0, 24 * bt));

    // Held from bar 9: the bars before it are the song's own.
    const std::int64_t F = 9;
    CHECK(keys(notes(s, 0, 24 * bt, F), 0, F * bt) == keys(notes(s, 0, 24 * bt), 0, F * bt));
    // From bar 9 on, every bar plays bar 8's state: exactly the song as bar 8 had it, its modulation taken away. (Lines
    // from bar 10: a line's first note can tie or legato back into the bar before.)
    Song held = stateAt(s, F - 1);
    held.matrix.clear();
    held.mod = Mod();
    CHECK(held.groups[0].density == 14);
    const auto frozen = keys(notes(s, 0, 30 * bt, F), (F + 1) * bt, 30 * bt, true);
    CHECK(!frozen.empty());
    CHECK(frozen == keys(notes(held, 0, 30 * bt), (F + 1) * bt, 30 * bt, true));
    // ...and that is not what the song plays there unfrozen (the LFO and the walk move it).
    CHECK(frozen != keys(notes(s, 0, 30 * bt), (F + 1) * bt, 30 * bt, true));

    // A Player: Freeze pressed mid-bar 8 holds from the bar line of bar 9; let go mid-bar 12, bar 13 is the replay's.
    {
        Player p(s);
        std::vector<Event> got;
        for (std::int64_t t = 0; t < 20 * bt; t += 500) {
            if (t >= 8 * bt + 1000) p.setFreeze(t < 12 * bt + 1000);
            for (const Event& e : p.process(t, t + 500, true)) got.push_back(e);
            if (t + 500 <= 9 * bt) CHECK(!p.frozen());
            if (t >= 9 * bt && t + 500 <= 13 * bt) CHECK(p.frozen() && p.holdFrom() == 9);
            if (t >= 13 * bt) CHECK(!p.frozen());
        }
        CHECK(!p.takeReleased());  // let go by hand: nothing to report
        // Frozen bars: the held render's note-ons (lines from bar 10, as above).
        const auto heldOns = keys(notes(s, 0, 14 * bt, 9), 10 * bt, 13 * bt, true);
        auto gotLines = ons(got, 10 * bt, 13 * bt);
        gotLines.erase(std::remove_if(gotLines.begin(), gotLines.end(), [](const Key& k) { return std::get<1>(k) == 3; }), gotLines.end());
        CHECK(!heldOns.empty() && gotLines == heldOns);
        // After it lets go: the song's own, as if Freeze had never been (the replay ran on underneath).
        CHECK(ons(got, 14 * bt, 20 * bt) == keys(notes(s, 0, 21 * bt), 14 * bt, 20 * bt));
    }

    // At bar 0 it holds from bar 1 (bar 0 has no bar before it).
    {
        Player p(s);
        p.process(0, 500, true);
        p.setFreeze(true);
        p.process(500, 1000, true);
        CHECK(!p.frozen());
        for (std::int64_t t = 1000; t < bt + 500; t += 500) p.process(t, t + 500, true);
        CHECK(p.frozen() && p.holdFrom() == 1);
    }

    // A jump lets go and switches Freeze off, and says so once; a Stop does too. An edit keeps the hold.
    {
        Player p(s);
        play(s, 6 * bt, 2 * bt, -1, &p);
        CHECK(p.frozen() && p.holdFrom() == 3);
        Song edited = s;
        edited.groups[1].density = 3;
        p.setSong(edited);
        CHECK(p.frozen() && p.holdFrom() == 3);
        p.process(20 * bt, 20 * bt + 500, true);  // a jump
        CHECK(!p.frozen());
        CHECK(p.takeReleased());
        CHECK(!p.takeReleased());
        p.setFreeze(true);
        for (std::int64_t t = 20 * bt + 500; t < 22 * bt; t += 500) p.process(t, t + 500, true);
        CHECK(p.frozen());
        p.process(22 * bt, 22 * bt + 500, false);  // Stop
        CHECK(!p.frozen() && p.takeReleased());
        // Pressed while stopped, the next Play is a jump: it never takes hold.
        p.setFreeze(true);
        p.process(0, 500, false);
        CHECK(!p.frozen() && p.takeReleased());
    }

    // No temporary layer expires under Freeze: the default song's turnaround (bar 3 of each phrase) held from bar 4
    // keeps its B (§7.6), and the form strip shows it; "Next: bar N" still reads the replay.
    {
        const Song d = defaultSong(0x0123456789ABCDEFull);
        View plain(d), frozenView(d, 4);
        const auto a = plain.form(2, 6), b = frozenView.form(2, 6);
        CHECK(a[0] == (std::vector<std::string>{"A", "B", "A", "A", "A", "B"}));
        CHECK(b[0] == (std::vector<std::string>{"A", "B", "B", "B", "B", "B"}));
        const std::uint64_t row = d.matrix[0].id;
        CHECK(plain.nextAct(row, 4, 64).bar == 7 && frozenView.nextAct(row, 4, 64).bar == 7);
        // The held B is the turnaround itself: bar 5 frozen sounds as bar 3 does, a phrase earlier... in the held
        // state's notes (the varied slots), so it differs from the unfrozen bar 5.
        CHECK(keys(notes(d, 0, 8 * bt, 4), 5 * bt, 6 * bt, true) != keys(notes(d, 0, 8 * bt), 5 * bt, 6 * bt, true));
    }
}

namespace {

// The form's test song: the default song (its turnaround row and all), an LFO on the Leads' Density, and the form
// A B A′ C over 8-bar sections. B rolls new notes and rhythm and sets the Leads' Density to 14; C new notes and
// chords, in E.
Song formSong(const char* text = "A B A' C") {
    Song s = freezeSong();
    s.form.on = true;
    s.form.text = text;
    s.form.section = 2;
    FormLetter* b = addLetter(s);
    b->params.push_back({Dest{Scope::Group, s.groups[0].id, "density"}, 14});
    FormLetter* c = addLetter(s);
    c->rhythm = false, c->harmony = true;
    c->params.push_back({Dest{Scope::Global, 0, "root"}, 4});
    return s;
}

std::vector<Key> shifted(std::vector<Key> ks, std::int64_t by) {
    for (Key& k : ks) std::get<0>(k) += by;
    return ks;
}

}  // namespace

// §18.1: the form string, its sections, letters that return exactly.
static void form() {
    // The string, read and written (the worked values: tools/spec_arith.py, "§18.1 the form").
    const std::vector<FormEntry> e = parseForm("A B A' C");
    CHECK(e.size() == 4 && e[2].letter == 'A' && e[2].primed && !e[0].primed && e[3].letter == 'C');
    CHECK(formText(e) == "A B A\xE2\x80\xB2 C");
    CHECK(parseForm(formText(e)) == e);
    CHECK(parseForm("ab'x c z9") == (std::vector<FormEntry>{{'A', false}, {'B', true}, {'C', false}}));
    CHECK(parseForm(std::string(100, 'A')).size() == 64);

    Song s = formSong();
    const std::int64_t bt = barTicks(s), S = 8;
    CHECK(sectionBars(s) == S);
    struct W {
        std::int64_t bar, index, position, start, bars;
    };
    for (const W& w : {W{0, 0, 0, 0, 8}, W{7, 0, 0, 0, 8}, W{8, 1, 1, 8, 8}, W{16, 2, 2, 16, 8}, W{24, 3, 3, 24, 8}, W{32, 4, 0, 32, 8},
                       W{40, 5, 1, 40, 8}, W{100, 12, 0, 96, 8}}) {
        const Section sec = sectionAt(s, w.bar);
        CHECK(sec.index == w.index && sec.position == w.position && sec.start == w.start && sec.bars == w.bars);
    }
    {
        Song h = s;
        h.form.end = 1;  // Hold: C from bar 24 on, for good
        for (std::int64_t bar : {24, 31, 32, 40, 100}) {
            const Section sec = sectionAt(h, bar);
            CHECK(sec.position == 3 && sec.start == 24 && sec.bars == -1 && sec.entry.letter == 'C');
        }
    }
    // A letter's seeds: A's own, rolled with its salt.
    {
        Song t = s;
        t.form.letters[0].salt = 0xBB;
        t.groups[0].rhythmSeed = 0x0123456789ABCDEFull;
        CHECK(letterSong(t, {'B', false}, 1).groups[0].rhythmSeed == 0x4a3c0b788fbf5ca1ull);
        CHECK(hash(0x0123456789ABCDEFull, {"form", "prime", 2, 1}) == 0x4ac60f0f8b3a8863ull);
    }

    // A is the song as written, cut at the section's end; every A is that A again, note for note.
    Song plain = s;
    plain.form = Form();
    const auto all = notes(s, 0, 48 * bt);
    const auto a0 = keys(all, 0, S * bt);
    CHECK(!a0.empty());
    CHECK(a0 == keys(notes(plain, 0, S * bt), 0, S * bt));
    CHECK(shifted(a0, 4 * S * bt) == keys(all, 4 * S * bt, 5 * S * bt));  // bar 32: the string again, A
    for (const Note& n : all) CHECK(n.end <= (n.start / (S * bt) + 1) * S * bt);  // no note rings past its section
    // B is not A, and B returns as B; C is in E.
    const auto b1 = keys(all, S * bt, 2 * S * bt);
    CHECK(shifted(b1, -S * bt) != a0);
    CHECK(shifted(b1, 4 * S * bt) == keys(all, 5 * S * bt, 6 * S * bt));
    CHECK(stateAt(s, 8).groups[0].density == 14 + (stateAt(plain, 0).groups[0].density - 10) && stateAt(s, 24).root == 4);
    CHECK(stateAt(s, 0).root == 9 && stateAt(s, 16).root == 9);
    // A′: A for its first bar, then A with a twist; not A.
    const auto a2 = shifted(keys(all, 2 * S * bt, 3 * S * bt), -2 * S * bt);
    CHECK(a2 != a0);
    CHECK(keys(all, 2 * S * bt, (2 * S + 1) * bt) == shifted(keys(all, 0, bt), 2 * S * bt));
    int same = 0;
    for (const Key& k : a2) same += std::binary_search(a0.begin(), a0.end(), k);
    CHECK(same * 10 > static_cast<int>(a2.size()) * 7);  // most of it is A's

    // Played a block at a time, a form is exactly its render; a jump into B plays B's own notes.
    {
        Player p(s);
        std::vector<Event> got;
        for (std::int64_t t = 0; t < 40 * bt; t += 700)
            for (const Event& ev : p.process(t, std::min(40 * bt, t + 700), true)) got.push_back(ev);
        CHECK(ons(got, 0, 40 * bt) == keys(all, 0, 40 * bt));
        Player j(s);
        std::vector<Event> jumped;
        for (std::int64_t t = 11 * bt + 300; t < 20 * bt; t += 500)
            for (const Event& ev : j.process(t, t + 500, true)) jumped.push_back(ev);
        CHECK(ons(jumped, 12 * bt, 20 * bt) == keys(all, 12 * bt, 20 * bt));
    }
    // The form strip names the section's letter.
    {
        View v(s);
        const auto f = v.form(6, 12);
        CHECK(f[1][0] == "A" && f[1][2] == "B" && f[1][10] == "A\xE2\x80\xB2");
        // The turnaround row's layer (bar 3 of each phrase) shows inside the letter.
        CHECK(f[0][1] == "A\xC2\xB7" "B" && f[0][5] == "B\xC2\xB7" "B");
    }
    // Generate moves A, and B with it: B stays B relative to A.
    {
        Song g = s;
        generate(g);
        CHECK(letterSong(g, {'B', false}, 1).groups[1].rhythmSeed == hash(g.groups[1].rhythmSeed, {"form", g.form.letters[0].salt}));
        CHECK(keys(notes(g, 0, 16 * bt), 0, S * bt) != a0);
    }
    // Freeze in a section holds it: past the section's end it plays on, the next letter waiting.
    {
        Song held = letterSong(s, {'A', false}, 0);
        CHECK(keys(notes(s, 0, 16 * bt, 7), 9 * bt, 16 * bt, true) == keys(notes(held, 0, 16 * bt, 7), 9 * bt, 16 * bt, true));
    }
    // A letter's values: what makes A into it.
    {
        Song x = plain;
        x.groups[0].density = 3;
        x.root = 2;
        x.groups[1].overrides.range = 5;
        x.groups[0].rhythmGen.params["euclid"]["pulses"] = 9;
        const auto vals = letterValues(plain, x);
        CHECK(vals.size() == 4);
        Song back = s;
        back.form.letters[0].params = vals;
        back.form.letters[0].notes = back.form.letters[0].rhythm = false;
        const Song y = letterSong(back, {'B', false}, 1);
        CHECK(y.groups[0].density == 3 && y.root == 2 && y.groups[1].overrides.range == 5 && y.groups[0].rhythmGen.params.at("euclid").at("pulses") == 9);
        CHECK(letterValues(plain, plain).empty());
    }
    // Off, or an empty string: the song as written.
    {
        Song off = s;
        off.form.on = false;
        CHECK(keys(notes(off, 0, 24 * bt), 0, 24 * bt) == keys(notes(plain, 0, 24 * bt), 0, 24 * bt));
        off.form.on = true, off.form.text = " x ";
        CHECK(keys(notes(off, 0, 24 * bt), 0, 24 * bt) == keys(notes(plain, 0, 24 * bt), 0, 24 * bt));
    }
}

// §18.2: the colotomic clock, and the rules, cords and conditions that read it.
static void colotomy() {
    Song s = defaultSong(0x0123456789ABCDEFull);  // gong cycle 4 phrases (16 bars), 4 kenongs
    CHECK(gongBars(s) == 16 && kenongCount(s) == 4);
    std::vector<std::int64_t> ks, nums;
    for (std::int64_t b = 0; b < 16; ++b)
        if (kenongAt(s, b)) ks.push_back(b), nums.push_back(kenongNumber(s, b));
    CHECK(ks == (std::vector<std::int64_t>{0, 4, 8, 12}) && nums == (std::vector<std::int64_t>{4, 1, 2, 3}));
    CHECK(gongAt(s, 0) && gongAt(s, 32) && !gongAt(s, 8) && kenongStrokes(s, 0) == 1 && kenongStrokes(s, 16) == 5 && kenongStrokes(s, 15) == 4);
    {
        Song t = s;
        t.gongCycle = 3, t.kenongs = 8;  // N 12, K 8: not a divisor, so the strokes fall at ceilDiv(j·12, 8)
        ks.clear(), nums.clear();
        for (std::int64_t b = 0; b < 12; ++b)
            if (kenongAt(t, b)) ks.push_back(b), nums.push_back(kenongNumber(t, b));
        CHECK(ks == (std::vector<std::int64_t>{0, 2, 3, 5, 6, 8, 9, 11}) && nums == (std::vector<std::int64_t>{8, 1, 2, 3, 4, 5, 6, 7}));
        t.gongCycle = 1, t.phraseLength = 3, t.kenongs = 4;  // more kenongs than bars: one a bar
        CHECK(kenongCount(t) == 3 && kenongAt(t, 1) && kenongAt(t, 2) && kenongNumber(t, 3) == 3);
    }
    // Rules: every gong, every other kenong (offset 1), and the bar before each gong.
    auto turns = [&](const Row& r) {
        std::vector<std::int64_t> out;
        for (std::int64_t b = 1; b < 70; ++b)
            if (rowTurn(s, r, b)) out.push_back(b);
        return out;
    };
    Row g;
    g.clock = 2, g.every = 1, g.offset = 0;
    CHECK(turns(g) == (std::vector<std::int64_t>{16, 32, 48, 64}));
    g.offset = -1;
    CHECK(turns(g) == (std::vector<std::int64_t>{15, 31, 47, 63}));
    Row k;
    k.clock = 3, k.every = 2, k.offset = 1;
    CHECK(turns(k) == (std::vector<std::int64_t>{4, 12, 20, 28, 36, 44, 52, 60, 68}));
    // Conditions: a row every bar, gated by GONG, by KENONG and by not KENONG, acts where the clock says.
    {
        Song c = s;
        c.matrix.clear();
        for (auto [kind, neg] : {std::pair{Condition::Kind::Gong, false}, std::pair{Condition::Kind::Kenong, false}, std::pair{Condition::Kind::Kenong, true}}) {
            Row& r = addRow(c);
            r.scope = Scope::Group, r.target = c.groups[0].id, r.param = Param::Density, r.every = 1, r.maxShift = 1;
            r.cond.kind = kind, r.cond.negate = neg;
        }
        View v(c);
        CHECK(v.nextAct(c.matrix[0].id, 1, 64).bar == 16);
        CHECK(v.nextAct(c.matrix[1].id, 1, 64).bar == 4 && v.nextAct(c.matrix[1].id, 5, 64).bar == 8);
        CHECK(v.nextAct(c.matrix[2].id, 4, 64).bar == 5 && v.nextAct(c.matrix[2].id, 8, 64).bar == 9);
    }
    // Cords: the gong source's triggers and its level.
    {
        Song c = s;
        ModSource& src = addSource(c, "gong");
        const ModSource copy = src;
        std::vector<std::int64_t> gongs, kenongs;
        std::vector<int> amounts;
        for (std::int64_t b = 0; b < 40; ++b) {
            if (triggerAt(c, copy, "gong", b)) gongs.push_back(b);
            if (const auto f = triggerAt(c, copy, "kenong", b)) kenongs.push_back(b), amounts.push_back(f->amount);
        }
        CHECK(gongs == (std::vector<std::int64_t>{16, 32}));
        CHECK(kenongs == (std::vector<std::int64_t>{4, 8, 12, 16, 20, 24, 28, 32, 36}));
        CHECK(amounts == (std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4, 1}));
        const int want[] = {0, 250, 500, 938, 0};
        const std::int64_t at[] = {0, 4, 8, 15, 16};
        for (int i = 0; i < 5; ++i) CHECK(levelAt(c, copy, "position", at[i]) == want[i]);
        CHECK(triggerBars(c, copy, "gong", 17, 2) == (std::vector<std::int64_t>{32, 48}));
        ModSource own = copy;
        own.params["cycle"] = 1;  // its own cycle: a phrase
        CHECK(triggerAt(c, own, "gong", 4) && !triggerAt(c, own, "gong", 6));
    }
    // A song that doesn't use it plays as it did: the clock is only read where a row, cord or condition asks.
    {
        Song c = s;
        c.gongCycle = 7, c.kenongs = 3;
        CHECK(notes(c, 0, 16 * barTicks(c)) == notes(s, 0, 16 * barTicks(s)));
    }
}

namespace {
// The Leads' onsets in a bar (both voices), and the gaps between them.
std::vector<std::int64_t> gaps(const Song& s, std::int64_t bar) {
    const std::int64_t bt = barTicks(s);
    std::set<std::int64_t> on;
    for (const Note& n : notes(s, 0, (bar + 2) * bt))
        if ((n.channel == 1 || n.channel == 2) && n.start >= bar * bt && n.start < (bar + 1) * bt) on.insert(n.start);
    std::vector<std::int64_t> out;
    for (auto it = on.begin(); it != on.end() && std::next(it) != on.end(); ++it) out.push_back(*std::next(it) - *it);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
}  // namespace

// §18.3: irama, notes per core note, a level that changes only on the gong.
static void irama() {
    Song s = defaultSong(0x0123456789ABCDEFull);
    s.matrix.clear();
    Group& leads = s.groups[0];
    leads.density = 16, leads.steps = 64;  // every step a hit: the gaps are the step
    // The worked values: chords every bar (3840 ticks), levels 1…5 are steps of 3840 1920 960 480 240.
    const std::int64_t want[] = {3840, 1920, 960, 480, 240};
    for (int level = 2; level <= 5; ++level) {
        Song t = s;
        t.groups[0].irama = level;
        CHECK(gaps(t, 1) == std::vector<std::int64_t>{want[level - 1]});
    }
    {
        Song t = s;
        t.groups[0].irama = 1;  // one note a chord: a whole bar
        std::int64_t count = 0;
        for (const Note& n : notes(t, 0, 4 * barTicks(t))) count += n.channel == 1 || n.channel == 2;
        CHECK(count == 4);
        t.harmony.changesEvery = 0;  // a quarter bar: 960, then 480 240 120 60
        t.groups[0].irama = 5;
        CHECK(gaps(t, 1) == std::vector<std::int64_t>{60});
        t.barLength = 7, t.beatUnit = 8, t.groups[0].irama = 5;  // 7/8, a quarter bar is 840 ticks: 16 doesn't divide, 8 does
        CHECK(gaps(t, 1) == std::vector<std::int64_t>{105});
    }
    // Off is the Beat division, exactly as before.
    CHECK(gaps(s, 1) == std::vector<std::int64_t>{240});
    // Moved at bar 5 (a row, stays), the new level waits for the gong at bar 16, and re-anchors there.
    {
        Song t = s;
        t.groups[0].irama = 2;
        Row& r = addRow(t);
        r.scope = Scope::Group, r.target = t.groups[0].id, r.key = "irama", r.every = 64, r.offset = 5, r.allowed = {4}, r.maxShift = 8;
        CHECK(stateAt(t, 5).groups[0].irama == 4);
        CHECK(gaps(t, 5) == std::vector<std::int64_t>{1920} && gaps(t, 15) == std::vector<std::int64_t>{1920});
        CHECK(gaps(t, 16) == std::vector<std::int64_t>{480} && gaps(t, 33) == std::vector<std::int64_t>{480});
        // A layer that's gone before the gong is never heard.
        t.matrix.back().lasts = 4;
        CHECK(gaps(t, 16) == std::vector<std::int64_t>{1920});
        // On the gong itself it's heard at once.
        t.matrix.back().lasts = 0, t.matrix.back().clock = 2, t.matrix.back().every = 1, t.matrix.back().offset = 0;
        CHECK(gaps(t, 15) == std::vector<std::int64_t>{1920} && gaps(t, 16) == std::vector<std::int64_t>{480});
        // A cord can move it too, and a letter can set it (§18.1).
        Song c = s;
        c.groups[0].irama = 2;
        ModSource& src = addSource(c, "gong");
        Cord& cd = addCord(c, src.id, "gong");
        cd.dest = {Scope::Group, c.groups[0].id, "irama"}, cd.allowed = {3}, cd.maxShift = 8;
        CHECK(gaps(c, 15) == std::vector<std::int64_t>{1920} && gaps(c, 16) == std::vector<std::int64_t>{960});
        CHECK(!levelMayDrive("irama"));
    }
}

namespace {
// The tihai's test song: the Leads alone, every step a hit, chords every 4 bars (so a copied step sounds the same
// note), a tihai of N 5 and g 1 on every phrase.
Song tihaiSong() {
    Song s = defaultSong(0x0123456789ABCDEFull);
    s.matrix.clear();
    s.groups.resize(1);
    s.harmony.changesEvery = 4;
    Group& g = s.groups[0];
    g.density = 16, g.tihai = 1, g.tihaiLength = 5, g.tihaiGap = 1;
    return s;
}
// The Leads' note-ons by step (1/16 from tick 0): channel and note, or nothing.
std::map<std::int64_t, std::pair<int, int>> byStep(const Song& s, std::int64_t bars) {
    std::map<std::int64_t, std::pair<int, int>> out;
    for (const Note& n : notes(s, 0, bars * barTicks(s)))
        if (n.start % 240 == 0) out[n.start / 240] = {n.channel, n.note};
    return out;
}
}  // namespace

std::pair<int, int> get(const std::map<std::int64_t, std::pair<int, int>>& m, std::int64_t a) {
    const auto it = m.find(a);
    return it == m.end() ? std::pair<int, int>{-1, -1} : it->second;
}

// §18.4: tihai, a phrase three times with two gaps, landing on the sam: start + 3N + 2g = sam.
static void tihai() {
    const Song s = tihaiSong();
    Song off = s;
    off.groups[0].tihai = 0;
    const auto on = byStep(s, 10), plain = byStep(off, 10);
    // The worked values: sam step 64 (tick 15360), start 47 = 64 − (3·5 + 2·1): 12345.12345.12345L
    for (std::int64_t a = 0; a < 47; ++a) CHECK(on.count(a) && get(on, a) == get(plain, a));  // before it, the song's own
    for (std::int64_t a : {52, 58}) CHECK(!on.count(a) && plain.count(a));                  // the two gaps
    for (std::int64_t r = 0; r < 5; ++r) {
        CHECK(get(on, 47 + r) == get(plain, 47 + r));  // the first statement is the song's own steps
        CHECK(get(on, 53 + r) == get(on, 47 + r) && get(on, 59 + r) == get(on, 47 + r));  // and the second and third are it again
    }
    CHECK(get(on, 64) == get(plain, 64));
    int landing = -1, before = -1;
    for (const Note& n : notes(s, 0, 6 * barTicks(s))) {
        if (n.start == 64 * 240) landing = n.velocity;
        if (n.start == 47 * 240) before = n.velocity;
    }
    CHECK(landing == 113 && before > 0);  // the landing is accented: 100 + 27·50/100
    // The next sam (bar 8) has one too, from step 111 = 128 − 17.
    CHECK(!on.count(116) && !on.count(122) && on.count(111) && get(on, 117) == get(on, 111));
    // Off, nothing changes; and a song without one is the golden render (tests/golden).
    CHECK(byStep(off, 10) == plain);
    // Every gong instead (tihaiUnit 1): bar 16 only.
    {
        Song g = s;
        g.groups[0].tihaiUnit = 1;
        const auto x = byStep(g, 18), y = byStep(off, 18);
        CHECK(x.count(52) && get(x, 52) == get(y, 52) && !x.count(52 + 192) && y.count(52 + 192));
    }
    // N 3, g 2: 123..123..123L from step 51.
    {
        Song g = s;
        g.groups[0].tihaiLength = 3, g.groups[0].tihaiGap = 2;
        const auto x = byStep(g, 6);
        for (std::int64_t a : {54, 55, 59, 60}) CHECK(!x.count(a));
        CHECK(x.count(51) && x.count(53) && get(x, 56) == get(x, 51) && get(x, 61) == get(x, 51) && get(x, 63) == get(x, 53));
    }
    // Too long for its period (a phrase of 3 bars is 48 steps; N 16, g 8 is 64): no tihai.
    {
        Song g = s;
        g.phraseLength = 3, g.groups[0].tihaiLength = 16, g.groups[0].tihaiGap = 8;
        Song h = g;
        h.groups[0].tihai = 0;
        CHECK(byStep(g, 7) == byStep(h, 7));
    }
    // "Tihai now" by a row (the destination `tihai`): on in bar 7 only, a tihai lands on bar 8 and not on bar 4.
    {
        Song g = s;
        g.groups[0].tihai = 0;
        Row& r = addRow(g);
        r.scope = Scope::Group, r.target = g.groups[0].id, r.key = "tihai", r.every = 8, r.offset = 7, r.allowed = {1}, r.lasts = 1;
        const auto x = byStep(g, 10);
        CHECK(x.count(52) && !x.count(116) && !x.count(122) && x.count(111) && get(x, 117) == get(x, 111));
    }
    // Irama 4 (quarter-note steps, §18.3), N 3, g 0: 123123123L, from step 7.
    {
        Song g = s;
        g.harmony.changesEvery = 2;  // chords every bar: 4 a chord is quarter notes
        g.groups[0].irama = 3, g.groups[0].tihaiLength = 3, g.groups[0].tihaiGap = 0;
        std::map<std::int64_t, int> q;  // the voice (channel) on each quarter: a copy is played by the same voice
        for (const Note& n : notes(g, 0, 5 * barTicks(g)))
            if (n.start % 960 == 0) q[n.start / 960] = n.channel;
        CHECK(q.size() >= 17 && q[10] == q[7] && q[13] == q[7] && q[11] == q[8] && q[15] == q[9] && q.count(16));
    }
}

int main() {
    freeze();
    form();
    colotomy();
    irama();
    tihai();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("all form checks pass\n");
    return failures ? 1 : 0;
}
