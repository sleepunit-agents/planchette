#include "hocket/song.hpp"

#include "hocket/rng.hpp"

#include <algorithm>

namespace hocket {

namespace {
constexpr std::uint16_t mask(std::initializer_list<int> intervals) {
    std::uint16_t m = 0;
    for (int i : intervals) m = static_cast<std::uint16_t>(m | (1u << i));
    return m;
}
}  // namespace

const std::array<Scale, 35> kScales = {{
    {"Major", mask({0, 2, 4, 5, 7, 9, 11})},
    {"Minor", mask({0, 2, 3, 5, 7, 8, 10})},
    {"Dorian", mask({0, 2, 3, 5, 7, 9, 10})},
    {"Mixolydian", mask({0, 2, 4, 5, 7, 9, 10})},
    {"Lydian", mask({0, 2, 4, 6, 7, 9, 11})},
    {"Phrygian", mask({0, 1, 3, 5, 7, 8, 10})},
    {"Locrian", mask({0, 1, 3, 5, 6, 8, 10})},
    {"Whole Tone", mask({0, 2, 4, 6, 8, 10})},
    {"Half-whole Dim.", mask({0, 1, 3, 4, 6, 7, 9, 10})},
    {"Whole-half Dim.", mask({0, 2, 3, 5, 6, 8, 9, 11})},
    {"Minor Blues", mask({0, 3, 5, 6, 7, 10})},
    {"Minor Pentatonic", mask({0, 3, 5, 7, 10})},
    {"Major Pentatonic", mask({0, 2, 4, 7, 9})},
    {"Harmonic Minor", mask({0, 2, 3, 5, 7, 8, 11})},
    {"Harmonic Major", mask({0, 2, 4, 5, 7, 8, 11})},
    {"Dorian #4", mask({0, 2, 3, 6, 7, 9, 10})},
    {"Phrygian Dominant", mask({0, 1, 4, 5, 7, 8, 10})},
    {"Melodic Minor", mask({0, 2, 3, 5, 7, 9, 11})},
    {"Lydian Augmented", mask({0, 2, 4, 6, 8, 9, 11})},
    {"Lydian Dominant", mask({0, 2, 4, 6, 7, 9, 10})},
    {"Super Locrian", mask({0, 1, 3, 4, 6, 8, 10})},
    {"8-Tone Spanish", mask({0, 1, 3, 4, 5, 6, 8, 10})},
    {"Bhairav", mask({0, 1, 4, 5, 7, 8, 11})},
    {"Hungarian Minor", mask({0, 2, 3, 6, 7, 8, 11})},
    {"Hirajoshi", mask({0, 2, 3, 7, 8})},
    {"In-Sen", mask({0, 1, 5, 7, 10})},
    {"Iwato", mask({0, 1, 5, 6, 10})},
    {"Kumoi", mask({0, 2, 3, 7, 9})},
    {"Pelog Selisir", mask({0, 1, 3, 7, 8})},
    {"Pelog Tembung", mask({0, 1, 5, 7, 8})},
    {"Messiaen 3", mask({0, 2, 3, 4, 6, 7, 8, 10, 11})},
    {"Messiaen 4", mask({0, 1, 2, 5, 6, 7, 8, 11})},
    {"Messiaen 5", mask({0, 1, 5, 6, 7, 11})},
    {"Messiaen 6", mask({0, 2, 4, 5, 6, 8, 10, 11})},
    {"Messiaen 7", mask({0, 1, 2, 3, 5, 6, 7, 8, 9, 11})},
}};

const std::array<int, 24> kDivision = {60,   80,   120,  160,   240,   320,   480,   640,
                                       720,  960,  1200, 1280,  1440,  1920,  2880,  3840,
                                       5760, 7680, 11520, 15360, 23040, 30720, 61440, 122880};

std::uint64_t newId(Song& song) { return hash(song.masterSeed, {"id", song.idCounter++}); }

std::uint64_t seedFor(const Song& song, std::uint64_t generateCount, const char* kind, std::uint64_t id, const char* what) {
    return hash(song.masterSeed, {"generate", generateCount, kind, id, what});
}

Group& addGroup(Song& s, const std::string& name) {
    Group g;
    g.id = newId(s);
    g.name = name;
    g.rhythmSeed = seedFor(s, s.generateCount, "group", g.id, "rhythm");
    g.dealSeed = seedFor(s, s.generateCount, "group", g.id, "deal");
    s.groups.push_back(g);
    assignSlots(s);
    return s.groups.back();
}

Voice& addVoice(Song& s, Group& g, VoiceType type, int channel) {
    Voice v;
    v.id = newId(s);
    v.type = type;
    v.channel = channel;
    v.octaveTranspose = type == VoiceType::Bass ? -1 : 1;  // §10: Lead +1, Bass −1, Chord +1
    v.pitchSeed = seedFor(s, s.generateCount, "voice", v.id, "pitch");
    g.voices.push_back(v);
    assignSlots(s);
    return g.voices.back();
}

void neutralLive(Song& s) {
    s.range = s.wander = 8;
    s.accent = 50, s.gatePercent = 100;
    s.octaveLow = -1, s.octaveHigh = 2;
    s.fill = 0;
    for (Group& g : s.groups) {
        g.density = 8, g.probability = 100, g.ratchet = 0;
        g.fillNow = false, g.mute = false;
        g.soloMember = 0;
        g.overrides.range = g.overrides.wander = g.overrides.accent = g.overrides.gatePercent = std::nullopt;
        for (Voice& v : g.voices) {
            v.density = v.weight = 8;
            v.mute = false;
            v.overrides = Overrides();
        }
    }
}

void assignSlots(Song& s) {
    bool group[kGroupSlots + 1] = {}, voice[kVoiceSlots + 1] = {};
    for (const Group& g : s.groups) {
        if (g.slot >= 1 && g.slot <= kGroupSlots) group[g.slot] = true;
        for (const Voice& v : g.voices)
            if (v.slot >= 1 && v.slot <= kVoiceSlots) voice[v.slot] = true;
    }
    auto take = [](bool* used, int n) {
        for (int k = 1; k <= n; ++k)
            if (!used[k]) return used[k] = true, k;
        return 0;
    };
    for (Group& g : s.groups) {
        if (g.slot < 0) g.slot = take(group, kGroupSlots);
        for (Voice& v : g.voices)
            if (v.slot < 0) v.slot = take(voice, kVoiceSlots);
    }
}

Row& addRow(Song& s) {
    Row r;
    r.id = newId(s);
    s.matrix.push_back(r);
    return s.matrix.back();
}

bool removeGroup(Song& s, std::uint64_t id) {
    const auto n = s.groups.size();
    s.groups.erase(std::remove_if(s.groups.begin(), s.groups.end(), [&](const Group& g) { return g.id == id; }), s.groups.end());
    return s.groups.size() != n;
}

bool removeVoice(Song& s, std::uint64_t id) {
    for (Group& g : s.groups) {
        const auto n = g.voices.size();
        g.voices.erase(std::remove_if(g.voices.begin(), g.voices.end(), [&](const Voice& v) { return v.id == id; }), g.voices.end());
        if (g.voices.size() != n) return true;
    }
    return false;
}

bool removeRow(Song& s, std::uint64_t id) {
    const auto n = s.matrix.size();
    s.matrix.erase(std::remove_if(s.matrix.begin(), s.matrix.end(), [&](const Row& r) { return r.id == id; }), s.matrix.end());
    return s.matrix.size() != n;
}

namespace {
int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
template <typename E>
E clampEnum(E e, int hi) { return static_cast<E>(clamp(static_cast<int>(e), 0, hi)); }
void clampOverrides(Overrides& o) {
    auto c = [](std::optional<int>& x, int lo, int hi) {
        if (x) x = clamp(*x, lo, hi);
    };
    c(o.range, 1, 16);
    c(o.wander, 1, 16);
    c(o.accent, 0, 100);
    c(o.gatePercent, 1, 200);
    c(o.octaveLow, -1, 2);
    c(o.octaveHigh, -1, 2);
}
// §17.5: A:B with 1 ≤ A ≤ B ≤ 8.
void sanitizeCondition(Condition& c) {
    c.b = clamp(c.b, 1, 8);
    c.a = clamp(c.a, 1, c.b);
}
}  // namespace

void sanitize(Song& s) {
    s.barLength = clamp(s.barLength, 1, 16);
    s.beatUnit = s.beatUnit <= 2 ? 2 : s.beatUnit <= 4 ? 4 : s.beatUnit <= 8 ? 8 : 16;
    s.phraseLength = clamp(s.phraseLength, 1, 16);
    s.root = clamp(s.root, 0, 11);
    s.scale = clamp(s.scale, 0, static_cast<int>(kScales.size()) - 1);
    s.range = clamp(s.range, 1, 16);
    s.wander = clamp(s.wander, 1, 16);
    s.octaveLow = clamp(s.octaveLow, -1, 2);
    s.octaveHigh = clamp(s.octaveHigh, -1, 2);
    s.accent = clamp(s.accent, 0, 100);
    s.colour = clamp(s.colour, 0, 16);
    s.octaveJumps = clamp(s.octaveJumps, 0, 16);
    s.fill = clamp(s.fill, 0, 1);
    s.modeMoves = clamp(s.modeMoves, 0, 1);
    s.gatePercent = clamp(s.gatePercent, 1, 200);
    s.harmony.changesEvery = clamp(s.harmony.changesEvery, 0, 4);
    s.harmony.length = clamp(s.harmony.length, 1, 16);
    s.harmony.range = clamp(s.harmony.range, 1, 16);
    s.harmony.wander = clamp(s.harmony.wander, 1, 16);
    s.harmony.push.amount = clamp(s.harmony.push.amount, 0, 16);
    s.harmony.push.early = clamp(s.harmony.push.early, 0, 4);
    s.harmony.push.late = clamp(s.harmony.push.late, 0, 4);
    s.harmony.push.hits = clampEnum(s.harmony.push.hits, 1);
    if (s.groups.size() > 64) s.groups.resize(64);
    bool groupSlot[kGroupSlots + 1] = {}, voiceSlot[kVoiceSlots + 1] = {};
    for (Group& g : s.groups) {
        g.density = clamp(g.density, 1, 16);
        g.steps = clamp(g.steps, 1, 64);
        g.beatDivision = clamp(g.beatDivision, 0, static_cast<int>(kDivision.size()) - 1);
        g.offset = clamp(g.offset, 0, 63);
        g.direction = clampEnum(g.direction, 2);
        g.swing = clamp(g.swing, 0, 100);
        g.jitter = clamp(g.jitter, 0, 100);
        g.deal = clampEnum(g.deal, 10);
        g.handoff = clamp(g.handoff, 0, 50);
        g.dealSpan = clamp(g.dealSpan, 1, 16);
        g.probability = clamp(g.probability, 0, 100);
        g.ratchet = clamp(g.ratchet, 0, 100);
        g.repeats = clamp(g.repeats, 2, 4);
        g.ramp = clamp(g.ramp, -100, 100);
        g.fillEvery = clamp(g.fillEvery, 0, 16);
        g.fillBars = clamp(g.fillBars, 1, 16);
        g.fillAmount = clamp(g.fillAmount, 1, 16);
        g.irama = clamp(g.irama, 0, 5);
        g.tihai = clamp(g.tihai, 0, 16);
        g.tihaiUnit = clamp(g.tihaiUnit, 0, 1);
        g.tihaiLength = clamp(g.tihaiLength, 1, 16);
        g.tihaiGap = clamp(g.tihaiGap, 0, 8);
        g.tihaiNow = clamp(g.tihaiNow, 0, 1);
        g.overlap = clampEnum(g.overlap, 1);
        clampOverrides(g.overrides);
        g.slot = clamp(g.slot, -1, kGroupSlots);
        if (g.slot > 0 && groupSlot[g.slot]) g.slot = 0;
        if (g.slot > 0) groupSlot[g.slot] = true;
        if (g.voices.size() > 64) g.voices.resize(64);
        g.soloMember = clamp(g.soloMember, 0, std::max(0, static_cast<int>(g.voices.size()) - 1));
        for (Voice& v : g.voices) {
            v.type = clampEnum(v.type, 2);
            v.channel = clamp(v.channel, 1, 16);
            v.octaveTranspose = clamp(v.octaveTranspose, -3, 3);
            v.weight = clamp(v.weight, 0, 16);
            v.density = clamp(v.density, 0, 16);
            v.slot = clamp(v.slot, -1, kVoiceSlots);
            if (v.slot > 0 && voiceSlot[v.slot]) v.slot = 0;  // §12.2: a slot answers to one voice; a later claim has none
            if (v.slot > 0) voiceSlot[v.slot] = true;
            v.chordSize = clamp(v.chordSize, 1, 6);
            v.voicing = clamp(v.voicing, 1, 16);
            v.articulation = clampEnum(v.articulation, 3);
            v.humanVelocity = clamp(v.humanVelocity, 0, 32);
            v.humanTiming = clamp(v.humanTiming, 0, 100);
            v.stagger = clamp(v.stagger, 0, 16);
            v.staggerOrder = clampEnum(v.staggerOrder, 2);
            v.staggerLean = clampEnum(v.staggerLean, 1);
            v.top = clamp(v.top, 0, 16);
            v.strikeWith = clampEnum(v.strikeWith, 1);
            clampOverrides(v.overrides);
        }
    }
    if (s.matrix.size() > 256) s.matrix.resize(256);
    for (Row& r : s.matrix) {
        r.scope = clampEnum(r.scope, 3);
        r.param = clampEnum(r.param, static_cast<int>(kLastParam));
        r.every = clamp(r.every, 1, 256);
        r.offset = clamp(r.offset, -256, 256);
        r.chance = clamp(r.chance, 0, 100);
        r.maxShift = clamp(r.maxShift, 1, 64);
        r.lasts = clamp(r.lasts, 0, 64);
        if (r.min) r.min = clamp(*r.min, -128, 128);
        if (r.max) r.max = clamp(*r.max, -128, 128);
        if (r.allowed.size() > 64) r.allowed.resize(64);
        sanitizeCondition(r.cond);
        // §17.2: a row may name its target by key. One of the numbered params' keys is that param.
        if (!r.key.empty())
            if (const auto p = paramFromKey(r.key)) r.param = *p, r.key.clear();
    }
    // §17: at most 64 sources and 256 cords; every field into its range (§17.3, §17.9). A source's parameters are
    // clamped where they're read (sourceValue), so a newer build's values are written back as they were.
    if (s.mod.sources.size() > 64) s.mod.sources.resize(64);
    for (ModSource& src : s.mod.sources) {
        for (auto& [key, list] : src.lists)
            if (list.size() > 64) list.resize(64);
        for (auto& [key, set] : src.sets)
            if (set.size() > 64) set.resize(64);
    }
    if (s.mod.cords.size() > 256) s.mod.cords.resize(256);
    for (Cord& c : s.mod.cords) {
        c.dest.scope = clampEnum(c.dest.scope, 3);
        c.move = clamp(c.move, 0, 1);
        c.scale = clamp(c.scale, -16, 16);
        c.maxShift = clamp(c.maxShift, 1, 64);
        if (c.min) c.min = clamp(*c.min, -128, 128);
        if (c.max) c.max = clamp(*c.max, -128, 128);
        c.lasts = clamp(c.lasts, 0, 64);
        if (c.allowed.size() > 64) c.allowed.resize(64);
        c.chance = clamp(c.chance, 0, 100);
        c.depth = clamp(c.depth, -64, 64);
        sanitizeCondition(c.cond);
        c.threshold = clamp(c.threshold, -1000, 1000);
    }
    // §18.2: the colotomic clock.
    s.gongCycle = clamp(s.gongCycle, 1, 16);
    s.kenongs = clamp(s.kenongs, 1, 16);
    for (Row& r : s.matrix) {
        if (r.clock == 1) r.inPhrases = true;  // phrases are inPhrases' own
        r.clock = r.clock == 2 || r.clock == 3 ? r.clock : 0;
    }
    // §18.1: the form. Its string is read where it's played (at most 64 entries); letters B…H, once each.
    s.form.section = clamp(s.form.section, 1, 16);
    s.form.end = clamp(s.form.end, 0, 1);
    s.form.prime = clamp(s.form.prime, 1, 16);
    if (s.form.text.size() > 1024) s.form.text.resize(1024);
    std::vector<FormLetter> letters;
    for (const FormLetter& l : s.form.letters) {
        bool dup = false;
        for (const FormLetter& k : letters) dup = dup || k.name == l.name;
        if (l.name < 'B' || l.name > 'H' || dup) continue;
        letters.push_back(l);
        if (letters.back().params.size() > 256) letters.back().params.resize(256);
        for (auto& [dest, value] : letters.back().params) dest.scope = clampEnum(dest.scope, 3), value = clamp(value, -128, 128);
    }
    s.form.letters = std::move(letters);
}

void generate(Song& s) {
    const std::uint64_t n = ++s.generateCount;
    if (!s.harmony.locked) s.harmony.seed = hash(s.masterSeed, {"generate", n, "harmony"});
    if (!s.matrixLocked) s.matrixSeed = hash(s.masterSeed, {"generate", n, "matrix"});
    for (Group& g : s.groups) {
        if (!g.lockRhythm) g.rhythmSeed = seedFor(s, n, "group", g.id, "rhythm");
        if (!g.lockDeal) g.dealSeed = seedFor(s, n, "group", g.id, "deal");
        for (Voice& v : g.voices)
            if (!v.lockNotes) v.pitchSeed = seedFor(s, n, "voice", v.id, "pitch");
    }
}

void rerollHarmony(Song& s) { s.harmony.seed = hash(s.masterSeed, {"generate", ++s.generateCount, "harmony"}); }
void rerollRhythm(Song& s, Group& g) { g.rhythmSeed = seedFor(s, ++s.generateCount, "group", g.id, "rhythm"); }
void rerollNotes(Song& s, Voice& v) { v.pitchSeed = seedFor(s, ++s.generateCount, "voice", v.id, "pitch"); }

// The long-form songs: rows on phrase clocks that never line up (3, 5, 6, 7, 9, 11, 13 phrases), so the texture keeps
// moving without quite repeating; the big moves (new notes, new rhythm, new chords) only on even phrase counts.
// `home`: everything that moves comes back, and the big moves are trips away from the song as written (A B A C A D…).
// Otherwise nothing comes back: it wanders on and on.
void addLongForm(Song& s, bool home) {
    if (s.groups.size() < 3 || s.groups[2].voices.empty()) return;
    const int phrase = s.phraseLength;  // bars
    const std::uint64_t leads = s.groups[0].id, bass = s.groups[1].id, chord = s.groups[2].voices[0].id;
    auto add = [&](Scope scope, std::uint64_t target, Param p, int everyPhrases, int offsetPhrases, int shift, int lo, int hi, int lastsPhrases) {
        Row& r = addRow(s);
        r.scope = scope, r.target = target, r.param = p;
        r.every = everyPhrases, r.inPhrases = true, r.offset = offsetPhrases * phrase;
        r.maxShift = shift;
        if (lo <= hi) r.min = lo, r.max = hi;
        r.lasts = lastsPhrases * phrase;
        r.fixedSeed = false;  // a fresh roll each time it fires
        return &r;
    };
    s.harmony.range = 8;  // four chords to choose from
    // The texture: small, constant motion. Coming home, each change lasts a few phrases and snaps back.
    add(Scope::Group, leads, Param::Density, 3, 0, 2, 6, 14, home ? 2 : 0);
    add(Scope::Group, bass, Param::Density, 5, 0, 1, 4, 9, home ? 3 : 0);
    add(Scope::Voice, chord, Param::ChordSize, 6, 0, 1, 3, 5, home ? 2 : 0);
    add(Scope::Group, leads, Param::Range, 7, 0, 2, 5, 14, home ? 2 : 0);
    add(Scope::Global, 0, Param::Wander, 9, 0, 2, 4, 13, home ? 4 : 0);
    add(Scope::Group, leads, Param::Swing, 11, 0, 10, 0, 40, home ? 3 : 0);
    add(Scope::Group, bass, Param::Range, 13, 0, 1, 2, 6, home ? 5 : 0);
    // A chord of the progression changes every 4 phrases.
    add(Scope::Harmony, 0, Param::VaryPitch, 4, 0, 1, 1, 0, home ? 2 : 0);
    if (home) {
        // Away for 8 phrases, home for 8: new notes every trip; new rhythm on every other trip, new chords on the rest.
        add(Scope::Group, leads, Param::RerollPitch, 16, 8, 1, 1, 0, 8);
        add(Scope::Group, bass, Param::RerollPitch, 16, 8, 1, 1, 0, 8);
        add(Scope::Group, leads, Param::RerollRhythm, 32, 8, 1, 1, 0, 8);
        add(Scope::Group, bass, Param::RerollRhythm, 32, 8, 1, 1, 0, 8);
        add(Scope::Harmony, 0, Param::RerollAll, 32, 24, 1, 1, 0, 8);
    } else {
        // New notes every 16 phrases, new rhythm every 32, a new progression every 32: staggered, so one lands every
        // 8 phrases and the song is always somewhere new.
        add(Scope::Group, bass, Param::RerollPitch, 16, 8, 1, 1, 0, 0);
        add(Scope::Group, leads, Param::RerollPitch, 16, 0, 1, 1, 0, 0);
        add(Scope::Group, leads, Param::RerollRhythm, 32, 0, 1, 1, 0, 0);
        add(Scope::Group, bass, Param::RerollRhythm, 32, 16, 1, 1, 0, 0);
        add(Scope::Harmony, 0, Param::RerollAll, 32, 24, 1, 1, 0, 0);
    }
}

Song defaultSong(std::uint64_t masterSeed) {
    Song s;
    s.masterSeed = masterSeed;
    s.harmony.seed = hash(masterSeed, {"generate", std::uint64_t{0}, "harmony"});
    s.harmony.length = s.phraseLength;
    s.colour = 12;  // §3.7: a new song leans into its mode
    s.harmony.range = 8;  // a pool of four chords in a seven-note scale (Reach 5 gave three, the same three every seed)
    s.matrixSeed = masterSeed;  // until the first Generate: the rolls a song on this seed always had

    auto group = [&](const char* name, int density) {
        Group g;
        g.id = newId(s);
        g.name = name;
        g.density = density;
        g.rhythmSeed = seedFor(s, 0, "group", g.id, "rhythm");
        g.dealSeed = seedFor(s, 0, "group", g.id, "deal");
        return g;
    };
    auto voice = [&](VoiceType type, int channel) {
        Voice v;
        v.id = newId(s);
        v.type = type;
        v.channel = channel;
        v.octaveTranspose = type == VoiceType::Bass ? -1 : 1;  // §10: Lead +1, Bass −1, Chord +1
        v.pitchSeed = seedFor(s, 0, "voice", v.id, "pitch");
        return v;
    };

    Group leads = group("Leads", 10);
    leads.voices.push_back(voice(VoiceType::Lead, 1));
    leads.voices.push_back(voice(VoiceType::Lead, 2));
    s.groups.push_back(leads);

    Group bass = group("Bass", 6);
    bass.overrides.range = 3;
    bass.overrides.wander = 4;
    bass.voices.push_back(voice(VoiceType::Bass, 4));
    s.groups.push_back(bass);

    Group chords = group("Chords", 2);
    chords.steps = 32;
    chords.voices.push_back(voice(VoiceType::Chord, 3));
    s.groups.push_back(chords);

    // The AAAB turnaround: Leads · Vary Pitch · depth 4 · every phrase, last bar · fixed · reverts after 1 bar.
    Row turn;
    turn.id = newId(s);
    turn.scope = Scope::Group;
    turn.target = s.groups[0].id;
    turn.param = Param::VaryPitch;
    turn.every = 1;
    turn.inPhrases = true;
    turn.offset = -1;
    turn.maxShift = 4;
    turn.lasts = 1;
    turn.fixedSeed = true;
    s.matrix.push_back(turn);
    assignSlots(s);  // §12.2
    return s;
}

}  // namespace hocket
