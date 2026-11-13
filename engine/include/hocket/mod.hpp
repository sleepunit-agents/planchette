// Modulation (SPEC §17): sources make events over time, and cords patch a source's outputs to destinations, the
// song's parameters. The change matrix (§7) is the first source, "rules"; it stays in Song::matrix. Everything here
// is as deterministic as the matrix: a source's events at bar b are a pure function of b, its parameters and the
// matrix's seed.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hocket/matrix.hpp"

namespace hocket {

struct Song;

// §17.2: a destination is an address, (scope, target, key). `key` is §17.2's frozen list: a param's key ("density"),
// a generator's parameter ("rhythm.euclid.pulses"), or "mute". A key this build doesn't know does nothing.
struct Dest {
    Scope scope = Scope::Global;
    std::uint64_t target = 0;  // a group's or voice's id; 0 for Global and Harmony
    std::string key;
    bool operator==(const Dest& o) const { return scope == o.scope && target == o.target && key == o.key; }
};

// §17.1: a source. Its parameters by §17.9's keys: a number or a choice in `params`, a set of numbers in `sets`
// (the LFOs' Converge set), a list of per-item settings in `lists` (the LFOs' `lfos`). A missing one is its default.
// `extra` is what this build couldn't read of the source (a newer build's), as JSON text, written back as read.
struct ModSource {
    std::uint64_t id = 0;
    std::string kind;
    std::string name;
    std::map<std::string, int> params;
    std::map<std::string, std::vector<int>> sets;
    std::map<std::string, std::vector<std::map<std::string, int>>> lists;
    std::string extra;
};

// §17.3: a cord patches one source output to one destination. Trigger fields, then level fields; a missing field is
// its default. A cord to a source, an output or a target that's gone stays, and does nothing.
struct Cord {
    std::uint64_t id = 0;
    std::uint64_t source = 0;
    std::string output;
    Dest dest;
    // from a trigger: a §7 row without its own clock
    int move = 1;      // 0 Random (a uniform delta in ±Max shift), 1 As given (amount · Scale, clamped to ±Max shift)
    int scale = 1;     // As given: notches per unit of amount, −16 … 16
    int maxShift = 1;  // §7.1; for a Vary, the depth
    std::optional<int> min, max;
    int lasts = 0;     // 0 Stays; N ≥ 1 reverts after N bars
    std::vector<int> allowed;
    bool fixedSeed = true;
    int chance = 100;
    Condition cond;  // §17.5
    // from a level: an offset that follows it, recomputed every bar
    int depth = 4;         // −64 … 64 notches at level 1000
    int threshold = 500;   // Mute: muted while the level is below it
    bool invert = false;   // Mute: muted while the level is at or above it instead
    bool on = true;
    std::string extra;  // fields this build doesn't know, as JSON text, written back as read
};

struct Mod {
    std::vector<ModSource> sources;
    std::vector<Cord> cords;  // §17.4: trigger cords act in this order, after the matrix's rows
    std::string extra;
};

// ---- the kinds of source (§17.9) ------------------------------------------------------------------------------

struct SourceParam {
    const char* key;
    const char* label;  // the editor's
    int lo, hi, def;
    bool choice = false;       // a choice, its options numbered from 0
    const char* options = "";  // a choice's, for the editor: "bars|phrases"
    const int* defs = nullptr;  // a per-item parameter whose default differs by item (the LFOs' periods)
};

struct SourceKind {
    const char* name;   // frozen once released (Appendix B): what the song stores
    const char* label;  // the editor's
    std::vector<SourceParam> params;
    // A list of per-item settings (the LFOs): the list's key, how many items at most, and each item's parameters.
    const char* listKey = "";
    int listMax = 0;
    std::vector<SourceParam> per;
    // A set of item numbers (the LFOs' Converge set): its key; missing, every item.
    const char* setKey = "";
};

// Every kind this build has, in Appendix B's order (not "rules": the matrix is never listed in mod.sources).
const std::vector<SourceKind>& sourceKinds();
const SourceKind* findSourceKind(const std::string& name);

// A source's parameter as the song has it: its stored value in range, or its default.
int sourceValue(const ModSource& s, const char* key);
// Item i's parameter (the LFOs' lfos[i]).
int itemValue(const ModSource& s, int i, const char* key);
// The set (Converge): its members, or every item 0 … count − 1 when it's missing.
std::vector<int> sourceSet(const ModSource& s);

enum class OutputKind { Trigger, Level };
struct SourceOutput {
    std::string name;
    OutputKind kind;
    bool bipolar = false;  // a level: −1000 … 1000, else 0 … 1000
};
// §17.1: a source's outputs, as its kind and parameters say (lfo1 … lfoN for N = Count). None for a kind this build
// doesn't know.
std::vector<SourceOutput> outputsOf(const ModSource& s);
std::optional<SourceOutput> outputOf(const ModSource& s, const std::string& name);

// §17.1, a trigger at bar b: nothing, or its amount and u. Never at b ≤ 0.
struct Fire {
    int amount = 0;
    std::uint64_t u = 0;
};
std::optional<Fire> triggerAt(const Song& song, const ModSource& s, const std::string& output, std::int64_t bar);
// §17.1, a level at bar b ≥ 0: its value in its range. None for an output that isn't a level.
std::optional<int> levelAt(const Song& song, const ModSource& s, const std::string& output, std::int64_t bar);
// An output worked out once from its source's parameters, for asking bar after bar (the replay's way): `fire` for a
// trigger, `level` for a level. Not valid for an output the source doesn't have (or a kind this build doesn't know).
// How far ahead the editor looks for a trigger it can only find by trying bar after bar: past this, "not within
// 65536 bars". A song file sets the periods, so no answer may cost more than this many bars of work.
constexpr std::int64_t kScanBars = 65536;

struct CompiledOutput {
    bool valid = false;
    OutputKind kind = OutputKind::Level;
    bool bipolar = false;
    std::function<std::optional<Fire>(std::int64_t)> fire;  // nothing at b ≤ 0
    std::function<int(std::int64_t)> level;
    // A trigger's first firing at or after `from` (and ≥ 1) and before `limit`, or none. `exact`: worked out in closed
    // form, whatever the distance (the LFOs' convergence, by the Chinese remainder theorem); otherwise tried bar by
    // bar, at most kScanBars of them a call.
    std::function<std::optional<std::int64_t>(std::int64_t from, std::int64_t limit)> next;
    bool exact = false;
};
CompiledOutput compileOutput(const Song& song, const ModSource& s, const std::string& output);

// The first bars at or after `bar` at which a trigger fires, up to `count` of them, looking no further than
// `horizon` bars ahead (a scanned output never looks past kScanBars, whatever the horizon).
std::vector<std::int64_t> triggerBars(const Song& song, const ModSource& s, const std::string& output, std::int64_t bar, int count,
                                      std::int64_t horizon = kScanBars);
// The solutions of b ≡ r_i (mod m_i), every m_i ≥ 1: the least x ≥ 0 and the period L = lcm(m_i). None when they
// have no common solution.
std::optional<std::pair<std::int64_t, std::int64_t>> crt(const std::vector<std::pair<std::int64_t, std::int64_t>>& congruences);

// §17.9, LFOs: the level of an LFO of `periodBars`, a shape (0 Triangle, 1 Saw up, 2 Saw down, 3 Square) and a phase
// in bars (≥ 0), at bar b.
int lfoLevel(std::int64_t b, std::int64_t periodBars, int shape, std::int64_t phase);
// The phase an LFO plays: its own, mod P; Seeded (−1), stream(hash(matrixSeed, "mod", id, "phase", i)).below(P).
std::int64_t lfoPhase(const Song& song, const ModSource& s, int i);
std::int64_t lfoPeriodBars(const Song& song, const ModSource& s, int i);

// §17.9, Accumulator: c, the ticks in bars 1 … b (b' mod period == offset mod period), and the position p (0 … S − 1)
// after c ticks: Start + c · Step, wrapped (0), bounced (1, Pendulum) or held (2) at its ends, c taken mod Reset first.
std::int64_t accumulatorTicks(std::int64_t b, std::int64_t period, std::int64_t offset);
int accumulatorPosition(std::int64_t c, int step, int S, int start, int edge, int reset);
// §17.9, 1/f dice: Σ of D dice at clock c, die i re-rolled every 2^i clocks.
int diceSum(std::uint64_t matrixSeed, std::uint64_t id, std::int64_t c, int dice);
// §17.9, Energy arc: at bar b of a span of N bars, the position (‰), the segment (1–6) and the level (‰).
struct ArcPoint {
    int pos = 0, segment = 1, level = 0;
};
ArcPoint arcAt(std::int64_t b, std::int64_t N, bool loop);

// ---- cords ----------------------------------------------------------------------------------------------------

const ModSource* sourceById(const Song& song, std::uint64_t id);
// What a cord is, from its source's output: a trigger, a level, or nothing (a source or output that's gone).
std::optional<OutputKind> cordKind(const Song& song, const Cord& c);
// §17.3: whether a level may drive a destination key: a numeric one that moves no clock, or Mute.
bool levelMayDrive(const std::string& key);
// §7.6: the form strip's letter for each item (a row or a trigger cord, by id) whose changes are temporary: B, C, …
// in §17.4's order, with "*" when it's Fresh.
std::map<std::uint64_t, std::string> formLetters(const Song& song);

// Building: a new source of a kind (its parameters at their defaults, its name the kind's), a new cord.
ModSource& addSource(Song& song, const std::string& kind);
Cord& addCord(Song& song, std::uint64_t source, const std::string& output);
bool removeSource(Song& song, std::uint64_t id);  // its cords stay, and do nothing (§17.7)
bool removeCord(Song& song, std::uint64_t id);

}  // namespace hocket
