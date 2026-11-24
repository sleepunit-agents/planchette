// The form (SPEC §18.1): the form string, its sections, and the song each letter plays.
#include "hocket/form.hpp"

#include <algorithm>

#include "hocket/generators.hpp"
#include "hocket/rng.hpp"
#include "internal.hpp"

namespace hocket {

using detail::floorDiv;
using detail::floorMod;

std::vector<FormEntry> parseForm(const std::string& text) {
    std::vector<FormEntry> out;
    for (std::size_t i = 0; i < text.size() && out.size() < 64; ++i) {
        const char c = text[i];
        const char up = c >= 'a' && c <= 'h' ? static_cast<char>(c - 'a' + 'A') : c;
        if (up >= 'A' && up <= 'H') {
            out.push_back({up, false});
            continue;
        }
        // A prime after a letter: ' or ′ (U+2032, E2 80 B2 in UTF-8).
        if (out.empty()) continue;
        if (c == '\'') out.back().primed = true;
        else if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                 static_cast<unsigned char>(text[i + 2]) == 0xB2)
            out.back().primed = true, i += 2;
    }
    return out;
}

std::string entryName(const FormEntry& e) { return std::string(1, e.letter) + (e.primed ? "\xE2\x80\xB2" : ""); }

std::string formText(const std::vector<FormEntry>& entries) {
    std::string s;
    for (const FormEntry& e : entries) s += (s.empty() ? "" : " ") + entryName(e);
    return s;
}

bool hasForm(const Song& song) { return song.form.on && !parseForm(song.form.text).empty(); }

std::int64_t sectionBars(const Song& song) {
    return static_cast<std::int64_t>(detail::clampi(song.form.section, 1, 16)) * std::max(1, song.phraseLength);
}

Section sectionAt(const Song& song, std::int64_t bar) {
    Section s;
    const std::vector<FormEntry> entries = parseForm(song.form.text);
    if (entries.empty()) return s;
    const std::int64_t S = sectionBars(song), n = static_cast<std::int64_t>(entries.size());
    const std::int64_t k = floorDiv(std::max<std::int64_t>(0, bar), S);
    s.index = k;
    if (song.form.end == 1 && k >= n - 1) {  // Hold: the last letter plays on from its start
        s.index = n - 1, s.position = n - 1, s.start = (n - 1) * S, s.bars = -1;
    } else {
        s.position = floorMod(k, n), s.start = k * S, s.bars = S;
    }
    s.entry = entries[static_cast<std::size_t>(s.position)];
    return s;
}

const FormLetter* letterOf(const Song& song, char name) {
    for (const FormLetter& l : song.form.letters)
        if (l.name == name) return &l;
    return nullptr;
}

Song letterSong(const Song& song, const FormEntry& entry, std::int64_t position) {
    Song s = song;
    s.form = Form();
    if (const FormLetter* l = entry.letter == 'A' ? nullptr : letterOf(song, entry.letter)) {
        // Each rolled seed is A's own, rolled with the letter's salt: a Generate moves A, and the letter with it.
        auto roll = [&](std::uint64_t seed) { return hash(seed, {"form", l->salt}); };
        if (l->harmony) s.harmony.seed = roll(s.harmony.seed);
        if (l->matrix) s.matrixSeed = roll(s.matrixSeed);
        for (Group& g : s.groups) {
            if (l->rhythm) g.rhythmSeed = roll(g.rhythmSeed);
            if (l->deal) g.dealSeed = roll(g.dealSeed);
            for (Voice& v : g.voices)
                if (l->notes) v.pitchSeed = roll(v.pitchSeed);
        }
        for (const auto& [dest, value] : l->params) {
            const auto k = detail::addressOf(dest);
            if (!k || !(detail::numericKey(*k) || detail::pickKey(*k) || std::get<2>(*k) == static_cast<int>(Param::Root) ||
                        std::get<2>(*k) == static_cast<int>(Param::Scale)))
                continue;  // an action, Mute, or a key this build doesn't know: a letter sets values only
            if (!detail::valueAt(s, *k)) continue;  // a target that's gone
            const auto [lo, hi] = detail::spanAt(s, *k);
            detail::setValue(s, *k, detail::clampi(value, lo, hi));
        }
    }
    if (entry.primed) {
        // A′: A with a twist (§7.5), from the section's bar 1, on every Lead and Bass. Its place in the string names it.
        const std::int64_t S = sectionBars(song);
        for (const Group& g : song.groups) {
            bool lines = false;
            for (const Voice& v : g.voices) lines = lines || v.type != VoiceType::Chord;
            if (!lines) continue;
            Row r;
            r.id = hash(s.matrixSeed, {"form", "prime", position, g.id});
            r.scope = Scope::Group, r.target = g.id, r.param = Param::VaryPitch;
            r.every = static_cast<int>(std::min<std::int64_t>(S, 1 << 20)), r.offset = 1;
            r.maxShift = detail::clampi(song.form.prime, 1, 16), r.lasts = 0, r.fixedSeed = true;
            s.matrix.push_back(r);
        }
    }
    return s;
}

std::vector<std::pair<Dest, int>> letterValues(const Song& a, const Song& x) {
    std::vector<std::pair<Dest, int>> out;
    auto put = [&](Scope scope, std::uint64_t target, const std::string& key) {
        const Dest d{scope, target, key};
        const auto k = detail::addressOf(d);
        if (!k) return;
        const auto va = detail::valueAt(a, *k), vx = detail::valueAt(x, *k);
        if (vx && va != vx) out.push_back({d, *vx});
    };
    for (const char* key : {"range", "wander", "accent", "gatePercent", "octaveLow", "octaveHigh", "root", "scale", "modeMoves"})
        put(Scope::Global, 0, key);
    for (const char* key : {"range", "wander", "length", "changesEvery", "pushAmount", "pushEarly", "pushLate", "pushHits"}) put(Scope::Harmony, 0, key);
    auto overrides = [&](Scope scope, std::uint64_t id, const Overrides& oa, const Overrides& ox) {
        // A level's own value only where the letter sets one (§1.2): one it inherits follows its level above.
        const std::pair<const char*, std::pair<std::optional<int>, std::optional<int>>> all[] = {
            {"range", {oa.range, ox.range}}, {"wander", {oa.wander, ox.wander}}, {"accent", {oa.accent, ox.accent}},
            {"gatePercent", {oa.gatePercent, ox.gatePercent}}, {"octaveLow", {oa.octaveLow, ox.octaveLow}},
            {"octaveHigh", {oa.octaveHigh, ox.octaveHigh}}};
        for (const auto& [key, v] : all)
            if (v.second && v.first != v.second) out.push_back({Dest{scope, id, key}, *v.second});
    };
    auto generators = [&](Scope scope, std::uint64_t id, SlotKind slot, const GenSlot& ga, const GenSlot& gx) {
        for (const auto& [gen, values] : gx.params)
            for (const auto& [key, value] : values) {
                const std::string k = std::string(slot == SlotKind::Rhythm ? "rhythm." : "notes.") + gen + "." + key;
                if (!generatorTarget(k)) continue;
                const Dest d{scope, id, k};
                const auto dk = detail::addressOf(d);
                if (dk && detail::valueAt(a, *dk) != std::optional<int>(value)) out.push_back({d, value});
            }
        (void)ga;
    };
    for (const Group& gx : x.groups) {
        const Group* ga = nullptr;
        for (const Group& g : a.groups)
            if (g.id == gx.id) ga = &g;
        if (!ga) continue;
        for (const char* key : {"density", "steps", "swing", "offset", "soloMember", "deal", "direction", "probability", "ratchet", "repeats", "irama", "tihai"})
            put(Scope::Group, gx.id, key);
        overrides(Scope::Group, gx.id, ga->overrides, gx.overrides);
        generators(Scope::Group, gx.id, SlotKind::Rhythm, ga->rhythmGen, gx.rhythmGen);
        for (const Voice& vx : gx.voices) {
            const Voice* va = nullptr;
            for (const Voice& v : ga->voices)
                if (v.id == vx.id) va = &v;
            if (!va) continue;
            for (const char* key : {"weight", "density", "voicing", "chordSize", "articulation", "nearest", "humanVelocity", "humanTiming", "stagger", "staggerOrder", "staggerLean", "top", "onGrid", "strikeWith"})
                put(Scope::Voice, vx.id, key);
            overrides(Scope::Voice, vx.id, va->overrides, vx.overrides);
            if (vx.type != VoiceType::Chord) generators(Scope::Voice, vx.id, SlotKind::Notes, va->notesGen, vx.notesGen);
        }
    }
    return out;
}

void keepValues(const Song& from, Song& to) {
    auto copy = [&](Scope scope, std::uint64_t target, const char* key) {
        const auto k = detail::addressOf(Dest{scope, target, key});
        if (!k) return;
        const auto v = detail::valueAt(from, *k);
        if (v && detail::valueAt(to, *k)) detail::setValue(to, *k, *v);
    };
    for (const char* key : {"range", "wander", "accent", "gatePercent", "octaveLow", "octaveHigh", "root", "scale", "modeMoves"})
        copy(Scope::Global, 0, key);
    for (const char* key : {"range", "wander", "length", "changesEvery", "pushAmount", "pushEarly", "pushLate", "pushHits"}) copy(Scope::Harmony, 0, key);
    for (Group& gt : to.groups)
        for (const Group& gf : from.groups) {
            if (gf.id != gt.id) continue;
            for (const char* key : {"density", "steps", "swing", "offset", "soloMember", "deal", "direction", "probability", "ratchet", "repeats", "irama", "tihai"})
                copy(Scope::Group, gt.id, key);
            gt.overrides = gf.overrides;
            gt.rhythmGen.params = gf.rhythmGen.params;
            for (Voice& vt : gt.voices)
                for (const Voice& vf : gf.voices) {
                    if (vf.id != vt.id) continue;
                    for (const char* key : {"weight", "density", "voicing", "chordSize", "articulation", "nearest", "humanVelocity", "humanTiming", "stagger", "staggerOrder", "staggerLean", "top", "onGrid", "strikeWith"})
                        copy(Scope::Voice, vt.id, key);
                    vt.overrides = vf.overrides;
                    vt.notesGen.params = vf.notesGen.params;
                }
        }
}

FormLetter* addLetter(Song& song) {
    char next = 'B';
    for (const FormLetter& l : song.form.letters) next = std::max<char>(next, static_cast<char>(l.name + 1));
    if (next > 'H' || song.form.letters.size() >= 7) return nullptr;
    FormLetter l;
    l.name = next;
    l.salt = newId(song);
    song.form.letters.push_back(l);
    return &song.form.letters.back();
}

// ---- §18.2 ----------------------------------------------------------------------------------------------------

std::int64_t gongBars(const Song& song) {
    return static_cast<std::int64_t>(detail::clampi(song.gongCycle, 1, 16)) * std::max(1, song.phraseLength);
}
int kenongCount(const Song& song) { return static_cast<int>(std::min<std::int64_t>(detail::clampi(song.kenongs, 1, 16), gongBars(song))); }
bool gongAt(const Song& song, std::int64_t bar) { return floorMod(bar, gongBars(song)) == 0; }
bool kenongAt(const Song& song, std::int64_t bar) {
    const std::int64_t N = gongBars(song), K = kenongCount(song);
    return floorMod(floorMod(bar, N) * K, N) < K;
}
int kenongNumber(const Song& song, std::int64_t bar) {
    if (!kenongAt(song, bar)) return 0;
    const std::int64_t N = gongBars(song), K = kenongCount(song), j = floorMod(bar, N) * K / N;
    return static_cast<int>(j == 0 ? K : j);
}
std::int64_t kenongStrokes(const Song& song, std::int64_t bar) {
    const std::int64_t N = gongBars(song), K = kenongCount(song);
    return floorDiv(bar, N) * K + floorMod(bar, N) * K / N + 1;
}

}  // namespace hocket
