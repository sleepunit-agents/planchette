// The song (SPEC §1): what's saved. Nothing generated lives here, only seeds and settings (§2.2).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hocket/form.hpp"
#include "hocket/matrix.hpp"
#include "hocket/mod.hpp"

namespace hocket {

constexpr int kEngineVersion = 2;
constexpr int kTicksPerBeat = 960;  // §4.4

// Appendix A, in parameter order. Frozen: new scales are only ever appended.
struct Scale {
    const char* name;
    std::uint16_t mask;  // bit i set: i semitones above the root is in the scale
};
extern const std::array<Scale, 35> kScales;

// §4.4: step lengths in ticks, index = Beat division.
extern const std::array<int, 24> kDivision;
constexpr int kDivision16th = 4;

enum class VoiceType { Lead, Bass, Chord };
// §5.1. After Koenig's Project 2 selection methods: OnGenerate and EveryCycle are Ratio (weighted), RoundRobin is
// Sequence, Alea a fresh weighted roll per hit, Series a shuffle bag, Group runs of `span` hits, Tendency a window of
// `span` voices that sweeps across the group.
// Appended (§5.5, §5.6): Alternate and Bells give a hit to the member that owns the tune's pitch (siku, handbells);
// Tutti gives it to every member at once.
enum class DealMode { OnGenerate, EveryCycle, RoundRobin, Solo, Alea, Series, Group, Tendency, Alternate, Bells, Tutti };
enum class Overlap { Ring, Cut };
enum class Direction { Forward, Backward, Pendulum };
enum class Articulation { Sustain, RestrikeAccents, RestrikeHits, Pulse };
enum class StaggerOrder { Up, Down, Seeded };  // §3.4b: lowest first, highest first, the master seed's
enum class StaggerLean { Late, Around };       // §3.4b: from the strike on, or half before it and half after
enum class PushHits { All, Accents };          // §4.8: which of the source's steps a change may push to
enum class StrikeWith { OwnHits, Source };     // §4.8: a Chord voice's restrikes and pulses, from its own hits or the source's

// §16: a slot holds one generator, by name, and the parameters of every generator it has had (so swapping away and
// back brings a generator's knobs back). An empty or unknown `use` plays the slot's default. `extra` is what this
// build couldn't read of the song file's field (a newer build's), as JSON text, written back as it was read (§16.9).
struct GenSlot {
    std::string use;
    std::map<std::string, std::map<std::string, int>> params;  // generator name → parameter key → value
    std::string extra;
    // §16.6: a derived generator's source, by generator name: the id of the group or voice it reads (0: none).
    std::map<std::string, std::uint64_t> sources;
    bool operator==(const GenSlot& o) const { return use == o.use && params == o.params && extra == o.extra && sources == o.sources; }
};

// The levels of §1.2: a set value at a lower level wins.
struct Overrides {
    std::optional<int> range, wander, accent, gatePercent, octaveLow, octaveHigh;
};

struct Voice {
    std::uint64_t id = 0;
    VoiceType type = VoiceType::Lead;
    int channel = 1;          // 1–16
    int octaveTranspose = 0;  // −3…+3
    int weight = 8;           // 0–16
    // §5.8: its place inside the group's Density, 0–16 (0 sits out). Read only under the group's Density from voices.
    int density = 8;
    // §12.2: the automation slot it answers to (1–16), 0 for none; −1 until it is given one (assignSlots).
    int slot = -1;
    bool mute = false;
    std::uint64_t pitchSeed = 0;
    // Chord only (§3.4)
    int chordSize = 4;  // 1–6
    int voicing = 1;    // 1–16
    Articulation articulation = Articulation::Sustain;
    // §3.4a, Voicing "nearest": each change takes the inversion of its chord (in this Voicing) closest to the chord
    // before, from the start of each pass through the progression. Off: every chord in the fixed Voicing.
    bool nearest = false;
    // §4.7, humanize, both off at 0: each note's velocity moves by up to ±humanVelocity (0–32), and each line note
    // sounds late by up to humanTiming % (0–100) of the room left in its step. Its own stream, so Generate keeps it.
    int humanVelocity = 0;
    int humanTiming = 0;
    // §3.4b, Chord only, both off at 0: Stagger (0–16) spreads the notes a chord strikes over up to a 1/16 note, in
    // Order, from the strike on or Around it; Top (0–16) puts the chord's top note +Top and its inner notes −Top/2.
    int stagger = 0;
    StaggerOrder staggerOrder = StaggerOrder::Up;
    StaggerLean staggerLean = StaggerLean::Late;
    int top = 0;
    // §4.8: a voice on the grid reads the harmony as §4.5 has it, not where Push moves it; a Chord voice's restrikes
    // and pulses can come from the Push source's hits.
    bool onGrid = false;
    StrikeWith strikeWith = StrikeWith::OwnHits;
    // Lead, Bass (§4.3)
    bool tie = false;
    bool lockNotes = false;  // Generate leaves its pitch seed alone (§2.2)
    GenSlot notesGen;        // Lead and Bass: the Notes slot (§16.1); default Wander. v0.5's Pedal switch is its Pedal (§16.5)
    Overrides overrides;  // range, wander, accent, gatePercent (§4.3: its own notes'), octaveLow/High
};

struct Group {
    std::uint64_t id = 0;
    std::string name;
    std::uint64_t rhythmSeed = 0, dealSeed = 0;
    int density = 8;                    // 1–16
    int steps = 16;                     // 1–64
    int beatDivision = kDivision16th;   // index into kDivision
    int offset = 0;                     // 0–63
    Direction direction = Direction::Forward;
    int swing = 0;   // 0–100
    int jitter = 0;  // 0–100 %
    DealMode deal = DealMode::OnGenerate;
    // §5.8: the line's density and the deal's weights are worked out from the members' own Densities. Off: the
    // group's Density and the members' Weights, as ever.
    bool densityFromVoices = false;
    int slot = -1;  // §12.2: its automation slot (1–8), 0 for none; −1 until it is given one (assignSlots)
    Overlap overlap = Overlap::Ring;
    int soloMember = 0;  // 0-based
    int dealSpan = 2;    // 1–16: Group's run length in hits, Tendency's window in voices (§5.1), Bells' bells each (§5.5)
    int handoff = 10;    // 0–50 % of a step: under Alternate and Bells, how far a note overlaps the next member's (§5.5)
    bool cleanHandoffs = false;  // §5.7: a note handed on, on the pitch class of the one before it, is redrawn
    // §18.3, irama: 0 Off (its own Beat division); 1–5, 1 2 4 8 16 notes per core note (a change of the harmony).
    // Moved by the matrix or a cord, a new level waits for the gong.
    int irama = 0;
    // §18.4, tihai: a phrase of tihaiLength steps played three times with tihaiGap steps between, landing on the sam
    // (every `tihai` phrases, or gongs with tihaiUnit 1; 0 off). tihaiNow (0/1, a destination): on every phrase's sam.
    int tihai = 0;
    int tihaiUnit = 0;
    int tihaiLength = 5;
    int tihaiGap = 1;
    int tihaiNow = 0;
    // §4.6, all off by default: a hit plays with Probability %; one that plays ratchets (2…repeats times inside its
    // step) with Ratchet %, its repeats' velocity ramped by Ramp %; a fill (the last fillBars bars of every fillEvery
    // phrases, or while fillNow is on) adds fillAmount to Density and amount · 50 / 16 % to Ratchet.
    int probability = 100;  // 0–100 %
    int ratchet = 0;        // 0–100 %
    int repeats = 2;        // 2–4
    int ramp = 0;           // −100…+100 %
    int fillEvery = 0;      // 0 (off), 1–16 phrases
    int fillBars = 1;       // 1–16
    int fillAmount = 8;     // 1–16
    bool fillNow = false;
    bool mute = false;
    bool lockRhythm = false, lockDeal = false;  // Generate leaves these seeds alone (§2.2)
    GenSlot rhythmGen;  // the Rhythm slot (§16.1); default Random
    Overrides overrides;  // range, wander, accent, gatePercent
    std::vector<Voice> voices;
};

// §4.5: the song's core.
struct Harmony {
    std::uint64_t seed = 0;
    int changesEvery = 2;  // index into {1/4, 1/2, 1, 2, 4} bars
    int length = 4;        // 1–16 changes
    int range = 5;         // 1–16
    int wander = 8;        // 1–16
    bool locked = false;   // Generate keeps the progression
    // §4.8 Push: each change arrives on a source group's hit near it. Off with no source (0), or a source that isn't
    // live; then nothing is drawn.
    struct Push {
        std::uint64_t source = 0;  // a group's id
        int amount = 16;           // 0–16
        int early = 1;             // 0–4 of the source's steps
        int late = 0;              // 0–4
        PushHits hits = PushHits::All;
    } push;
};

struct Song {
    std::uint64_t masterSeed = 0;
    std::uint64_t generateCount = 0;
    std::uint64_t idCounter = 0;
    int barLength = 4;     // beats (the meter's top number)
    int beatUnit = 4;      // the beat: 2, 4, 8 or 16 (the bottom number); 7/8 is barLength 7, beatUnit 8
    int phraseLength = 4;  // bars
    // Global (§1)
    int root = 9;   // A
    int scale = 1;  // Minor
    int range = 8, wander = 8;
    int octaveLow = -1, octaveHigh = 2;
    int accent = 50, gatePercent = 100;
    // §3.7: how strongly the mode's colour notes (Phrygian's ♭2, Dorian's 6, Lydian's ♯4…) lead the harmony and the
    // lines, 0–16. 0 is the plain ranking every scale shares; a song saved before Colour existed has 0.
    int colour = 0;
    // §3.6: how often a line jumps to another octave, 0–16. 16 is the spread a line's octave lanes give (§3.6's
    // table); lower keeps more notes in the line's home octave, so it moves by steps more than leaps.
    int octaveJumps = 16;
    // §4.6: every group fills while it's on (0 or 1; a host parameter, so a controller's button can play a fill).
    int fill = 0;
    // §3.8: Mode moves (0 or 1, a host parameter). With Colour above 0, the progression is chosen by weights that lean
    // on the mode's own move (Lydian I–II, Dorian i–IV, Phrygian i–♭II…) and shy away from the V–I into the relative
    // major, a chord with no fifth is played (in fourths, or as it is over a tonic pedal), and Phrygian's home chord may
    // turn major after ♭II. Off: the harmony is §3.7's, exactly. Off for every song from before it.
    int modeMoves = 0;
    // §7.3: the matrix's own seed, re-rolled by Generate (unless locked), so each Generate also gets new matrix rolls.
    // A song from before it had none: it uses the master seed, as it always did.
    std::uint64_t matrixSeed = 0;
    bool matrixLocked = false;
    Harmony harmony;
    std::vector<Group> groups;
    std::vector<Row> matrix;  // §7, in firing order: the "rules" source (§17.9)
    Mod mod;                  // §17: every other source, and the cords from them. Missing: none
    Form form;                // §18.1: the form string and its letters. Missing or off: the song plays as written
    // §18.2: the colotomic clock. The gong cycle, in phrases (1–16), and its kenong strokes (1–16, at most its bars).
    int gongCycle = 4;
    int kenongs = 4;
};

// The default song of §13, for a master seed.
Song defaultSong(std::uint64_t masterSeed);

// §13.1's long-form rows, on a default song: a texture that keeps moving on phrase clocks that never line up, and big
// moves (new notes, rhythm, chords) on even phrase counts. `home`: they're trips away from the song as written and
// back (A B A C…); otherwise nothing comes back.
void addLongForm(Song& song, bool home);

// Every setting into its range (SPEC §10, §7.1), enums to values that exist, and the counts bounded (64 groups,
// 64 voices a group, 256 rows). For a song from outside (a session, a .planchette file someone sent): nothing in one can
// divide by zero or keep the engine looping.
void sanitize(Song& song);

// §9.3: the live values, each set to one fixed value, so that two songs that differ only in live values come out the
// same. A change between two such songs is a live change: it moves no clock and no seed, and is heard from the next
// step. The song's: Range, Wander, Accent, Gate %, Octave Low/High, Fill. A group's: Density, Probability, Ratchet,
// Fill now, Mute, Solo member, and its own Range, Wander, Accent, Gate %. A voice's: Density, Weight, Mute, and its
// own Range, Wander, Accent, Gate %, Octave Low/High.
void neutralLive(Song& song);

// §2.2: new seeds for everything not locked. Settings, groups, voices and rows stay as they are.
void generate(Song& song);
// One thing re-rolled (the editor's Reroll and New progression), counted like a Generate so it stays reproducible.
void rerollHarmony(Song& song);
void rerollRhythm(Song& song, Group& group);
void rerollNotes(Song& song, Voice& voice);

// Building a song (§8: adding never re-rolls anything already there). Each new thing gets a fresh id and seeds from
// the song's current Generate count, so the same edits on the same seed make the same song.
Group& addGroup(Song& song, const std::string& name);
Voice& addVoice(Song& song, Group& group, VoiceType type, int channel);
Row& addRow(Song& song);
// §12.2: every group and voice that hasn't been given an automation slot takes the first free one (8 for groups, 16
// for voices), in the song's order, or none when they're all taken. One that has a slot, or none, keeps it.
constexpr int kGroupSlots = 8, kVoiceSlots = 16;
void assignSlots(Song& song);
// Removing: a group with its voices, a voice, a row. Rows aimed at what's gone stay, and do nothing.
bool removeGroup(Song& song, std::uint64_t id);
bool removeVoice(Song& song, std::uint64_t id);
bool removeRow(Song& song, std::uint64_t id);

// A fresh id (§12.1): hash(masterSeed, "id", idCounter), never reused.
std::uint64_t newId(Song& song);

// The seed a path gets on Generate number `generateCount` (§2.2).
std::uint64_t seedFor(const Song& song, std::uint64_t generateCount, const char* kind, std::uint64_t id, const char* what);

}  // namespace hocket
