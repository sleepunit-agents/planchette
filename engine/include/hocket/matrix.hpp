// The change matrix (SPEC §7): rows that change the song on bar lines, and the replay that works out the state at
// any bar from bar 0. Pure: the state at bar N depends only on the song.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hocket {

struct Song;

// What a row's target is: a scope and which one of it (a group's or voice's id; unused for Global and Harmony).
enum class Scope { Global, Harmony, Group, Voice };

enum class Param {
    // numeric (§7.2)
    Range, Wander, Accent, GatePercent, OctaveLow, OctaveHigh, Density, Steps, Swing, Offset, Weight, Voicing,
    ChordSize, Length,
    // moves on its own circle / by its own distance
    Root, Scale,
    // pick-one
    SoloMember, DealMode, Direction, Articulation, ChangesEvery,
    // actions
    RerollRhythm, RerollPitch, RerollDeal, RerollAll, VaryPitch, VaryRhythm,
    // §4.6, appended (a saved row stores its target as this number): numeric, numeric, numeric, pick-one (Off / On)
    Probability, Ratchet, Repeats, Fill,
    // §3.8, appended: pick-one (Off / On), of the song
    ModeMoves,
    // §3.4a, appended: pick-one (Fixed / Nearest), of a Chord voice
    VoiceLeading,
    // §4.7, appended: numeric, numeric, of a voice
    HumanVelocity, HumanTiming,
    // §5.5, appended: numeric, of a group
    Handoff,
    // §5.7, appended: pick-one (Off / On), of a group
    CleanHandoffs,
    // §3.4b, appended: numeric, pick-one (Up / Down / Seeded), pick-one (Late / Around), numeric, of a Chord voice
    Stagger, StaggerOrder, StaggerLean, Top,
    // §4.8, appended: numeric ×3 and pick-one (All / Accents) of the harmony; pick-one (Off / On) of a voice, pick-one
    // (Own hits / Source) of a Chord voice
    PushAmount, PushEarly, PushLate, PushHits, OnGrid, StrikeWith,
};
// The last param there is: new ones go after it, and this moves with them.
constexpr Param kLastParam = Param::StrikeWith;

// What kind of target a row's param is (§7.2).
inline bool isNumericParam(Param p) {
    return p <= Param::Length || (p >= Param::Probability && p <= Param::Repeats) || p == Param::HumanVelocity || p == Param::HumanTiming ||
           p == Param::Handoff || p == Param::Stagger || p == Param::Top ||
           (p >= Param::PushAmount && p <= Param::PushLate);
}
inline bool isPickParam(Param p) { return (p >= Param::SoloMember && p <= Param::ChangesEvery) || p == Param::Fill || p == Param::ModeMoves ||
                                     p == Param::VoiceLeading || p == Param::CleanHandoffs ||
                                     p == Param::StaggerOrder || p == Param::StaggerLean ||
                                     (p >= Param::PushHits && p <= Param::StrikeWith); }

// §17.2: every destination's key. The params above by their number; a generator's parameter as slot.generator.key
// ("rhythm.euclid.pulses", "notes.register.lock"), which has no number.
const char* paramKey(Param p);
std::optional<Param> paramFromKey(const std::string& key);

// §17.5: a trigger's condition (named after the trig conditions on Elektron's sequencers). It gates a row or a trigger cord
// before its chance is rolled. Unknown: a kind this build doesn't know, which never passes; `extra` keeps it, as JSON
// text, to be written back as it was read.
struct Condition {
    // Gong, Kenong (§18.2): the bar is a gong boundary, a kenong boundary (the gong's included). Appended before Unknown.
    enum class Kind { None, AB, First, Pre, Nei, Fill, Gong, Kenong, Unknown };
    Kind kind = Kind::None;
    int a = 1, b = 1;          // A:B, 1 ≤ A ≤ B ≤ 8: passes on the A-th of every B opportunities
    std::uint64_t item = 0;    // NEI: the row's or cord's id
    bool negate = false;       // "not"
    std::string extra;
    bool operator==(const Condition& o) const {
        return kind == o.kind && a == o.a && b == o.b && item == o.item && negate == o.negate && extra == o.extra;
    }
};

struct Row {
    std::uint64_t id = 0;
    Scope scope = Scope::Global;
    std::uint64_t target = 0;  // group or voice id
    Param param = Param::Range;
    int every = 4;           // the period, in bars, or in phrases when inPhrases
    bool inPhrases = false;
    // §18.2: 0 the period is bars or phrases (inPhrases); 2 gong cycles (Offset in bars, as phrases'); 3 kenong
    // strokes (Every and Offset count strokes from bar 0).
    int clock = 0;
    int offset = 0;          // the bar in the period it fires on; negative counts back from the end (−1: last bar)
    int chance = 100;        // 0–100 %
    int maxShift = 1;        // notches; for Vary, the depth
    std::optional<int> min, max;  // Bounds (numeric)
    int lasts = 0;           // 0: Stays; N ≥ 1: Reverts after N bars
    bool fixedSeed = true;   // Reroll, Vary: Fixed or Fresh
    bool on = true;
    std::vector<int> allowed;  // pick-one and Scale: the options it may choose (empty: all)
    Condition cond;  // §17.5
    // §17.2: a destination with no param number, by its key (a generator's parameter). Empty: the target is `param`.
    // A key this build doesn't know does nothing, and is written back as it was read.
    std::string key;
};

// A rhythm slot rewritten by Vary Rhythm: hit or not, and its rolls when it was added.
struct RhythmCell {
    bool hit = false;
    bool accent = false;
    int gate = 1;
};

// §16.7: a Vary on a Register flips bits of one seed's register before step t0, `back` steps back from it. Kind 0 is
// a group's rhythm register (target: the group's id), 1 a voice's notes register (the voice's id).
struct RegisterFlip {
    int kind = 0;
    std::uint64_t target = 0, seed = 0;
    std::int64_t t0 = 0;
    std::vector<int> back;
    bool operator<(const RegisterFlip& o) const {
        if (t0 != o.t0) return t0 < o.t0;
        if (kind != o.kind) return kind < o.kind;
        if (target != o.target) return target < o.target;
        if (seed != o.seed) return seed < o.seed;
        return back < o.back;
    }
    bool operator==(const RegisterFlip& o) const { return kind == o.kind && target == o.target && seed == o.seed && t0 == o.t0 && back == o.back; }
};

// What the matrix has done to the patterns by a bar: slot → its replacement (§7.5).
struct Mutations {
    std::map<std::uint64_t, std::map<int, std::uint64_t>> pitch;  // voice id → slot → u
    std::map<std::uint64_t, std::map<int, RhythmCell>> rhythm;    // group id → slot → cell
    std::map<int, std::uint64_t> harmony;                         // change index → u
    // §16.7: the permanent flips in the order they were made, then each temporary layer's, in layer order. Never part
    // of the mutation map: a Vary on a Register writes nothing there.
    std::vector<RegisterFlip> registers;
};

}  // namespace hocket
