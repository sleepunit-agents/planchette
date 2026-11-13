// The engine (SPEC §3–§5, §11): a pure function from a song and a span of ticks to notes, and from notes to MIDI
// events. No clock, no host, no state between calls: the same song and span give the same answer on any machine,
// and a span gives exactly the slice of a longer one (so a host jump replays what was played).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hocket/song.hpp"

namespace hocket {

// ---- the pieces, exposed for tests ----------------------------------------------------------------------

// §2.5: a seeded shuffle of the 64 slots, and the same order keeping only slots < L.
std::vector<int> priorityOrder(std::uint64_t h);
std::vector<int> priorityOrder(std::uint64_t h, int L);

// §3.1: the scale's pitch classes (0–11), ranked by PREF from r. `root` and `scale` define the scale.
std::vector<int> ranked(int r, int root, int scale);

// §3.7: a mode's colour notes (pitch classes): what it has that plain major or minor, whichever is nearer, doesn't.
// Phrygian on F: G♭. Dorian: the 6th. Locrian: ♭2 and ♭5. None for major, minor, scales under 7 notes, or scales
// more than two notes from both (the octatonics).
std::vector<int> colourNotes(int root, int scale);
// §3.7: the three rankings, with the song's Colour on them. At Colour 0 each is exactly ranked().
std::vector<int> harmonyRanked(const Song& song);         // chord roots: colour chords forward, fifthless ones back
std::vector<int> lineRanked(const Song& song, int r);     // a line's notes from chord root r: colour notes forward
std::vector<int> chordRanked(const Song& song, int r);    // a chord's notes: a chord with no fifth takes its ♭5 or ♯5
// §3.8, Mode moves (song.modeMoves, with Colour above 0; otherwise they change nothing):
// the mode's move chords: major chords that hold a colour note, not the relative major's tonic (Lydian II, Dorian IV,
// Phrygian ♭II, Mixolydian ♭VII). Empty when Mode moves is off.
std::vector<int> moveChords(const Song& song);
// The weight of `root` for a change that follows `prev` (integers; §3.8's table). 1 for the relative major's V–I.
int moveWeight(const Song& song, int prev, int root, bool nextIsTonic);
// Per change of a progression (indices into harmonyRanked): whether its home chord is major (Phrygian, after ♭II).
std::vector<bool> homeMajor(const Song& song, const std::vector<int>& progression);

// §5.5: a MIDI note's place on the scale's ladder from Root (MIDI `root` = a note whose pitch class is Root, place 0); a
// note off the scale takes the place of the scale note below it.
int ladderPlace(int note, int root, int scale);

// §16.6: the deepest chain of derived hops (a slot into its source) any render on this thread has followed since the
// last reset. Loops are broken in the song's graph; past 64 hops the engine plays plain material instead (a backstop).
int derivedDepthHighWater(bool reset = false);

// §3.2: how many of n ranked notes a Range (1–16) keeps.
int poolSize(int n, int range);

// §3.3: a pool index per slot (0 = the root), from the order named by h.
std::vector<int> noteIndices(std::uint64_t h, int L, int poolSize, int wander);

// §4.1, §4.2, §3.5
std::vector<bool> hits(std::uint64_t rhythmSeed, int L, int density);
bool accentAt(std::uint64_t rhythmSeed, int slot);
int gateAt(std::uint64_t rhythmSeed, int slot, int density);
int octaveLaneAt(std::uint64_t pitchSeed, int slot, int density, int jumps = 16);

// §4.4: which slot plays at a step (before jitter).
int slotAt(std::int64_t absStep, int steps, Direction direction, int offset);

// §4.5: the current chord's root pitch class at a tick (the matrix applied), and the saved progression's indices.
std::vector<int> progression(const Song& song);
int chordRootAt(const Song& song, std::int64_t tick);
std::int64_t changeTicks(const Song& song);
std::int64_t barTicks(const Song& song);

// §16.3: t, the count from bar 0 of a group's step at or after `tick` (its grid position), through every re-anchor.
std::int64_t stepCount(const Song& song, std::size_t group, std::int64_t tick);

// §7: the song as it plays during a bar, the change matrix replayed from bar 0. Under a form (§18.1), its section's
// letter at the section's own bar.
Song stateAt(const Song& song, std::int64_t bar);
// §7.3: whether a bar is a row's turn (on, its period and Offset, not bar 0): an opportunity, in §17.5's words.
bool rowTurn(const Song& song, const Row& row, std::int64_t bar);
// Whether a row acts on a bar by its clock and its chance roll on the matrix seed (§7.3), leaving its condition aside.
// For a row with no condition (§17.5) it's exactly what the replay does, and so is the editor's "Next: bar N"; a row
// or cord with a condition asks View::nextAct, which reads the replay itself.
bool fires(const Song& song, const Row& row, std::int64_t bar);
// §9.1: the first bar at or after `bar` that a row fires on, within `turns` of its turns; bar −1 if none does.
// `skipped`: its turns before that one whose chance roll failed.
struct Firing {
    std::int64_t bar = -1;
    int skipped = 0;
};
Firing nextFiring(const Song& song, const Row& row, std::int64_t bar, int turns = 64);
// §17.8, for a trigger cord: the first bar at or after `bar` it acts on (its source fires and its chance roll passes),
// within `turns` of its source's firings; bar −1 if none does. `skipped`: its turns before that one whose roll failed.
Firing nextCordFiring(const Song& song, const Cord& cord, std::int64_t bar, int turns = 64);

// §3.4: a chord on root r, voiced (MIDI notes, ascending, unique).
std::vector<int> chordNotes(const Song& song, const Voice& voice, int r, bool homeMajor = false);
// §3.4a: of a chord's inversions (ascending MIDI notes; the top note moved down an octave up to n − 1 times, or the
// bottom one up), the one whose voices move least from `prev`. A tie is drawn from h.
std::vector<int> nearestInversion(const std::vector<int>& prev, const std::vector<int>& chord, std::uint64_t h);

// ---- notes and events -----------------------------------------------------------------------------------

struct Note {
    std::int64_t start = 0, end = 0;  // ticks; end > start
    int channel = 1, note = 0, velocity = 0;
    std::uint64_t voice = 0;
    bool operator==(const Note& o) const {
        return start == o.start && end == o.end && channel == o.channel && note == o.note && velocity == o.velocity &&
               voice == o.voice;
    }
};

// Every note that starts in [from, to), sorted by start, channel, note.
std::vector<Note> notes(const Song& song, std::int64_t from, std::int64_t to);
// The same under Freeze (§9, §17.6) held from bar line `hold` (≥ 1): every bar from it on plays the state of bar
// hold − 1, while steps, cycles, the harmony's changes and fills run on. A `hold` below 1 is no Freeze.
std::vector<Note> notes(const Song& song, std::int64_t from, std::int64_t to, std::int64_t hold);

struct Event {
    enum Kind { Off = 0, On = 1 };
    std::int64_t tick = 0;
    Kind kind = On;
    int channel = 1, note = 0, velocity = 0;
};

// §11: notes → note-on/off, tracked per (channel, note): a note-on over a sounding note ends it first, and a
// note-off is only sent for a note that's on.
std::vector<Event> events(const std::vector<Note>& notes);

// The engine as a plugin drives it (§6.3, §11): one audio block at a time, in host ticks. It keeps one timeline
// for the song, so a block costs the same at bar 1 and bar 1000; it remembers what's sounding, so offs land in
// later blocks; and when the host jumps or stops it ends everything, and after a jump it sounds again the chords
// that should already be ringing there. A block's events are in [from, to), ordered by tick.
class Player {
public:
    explicit Player(const Song& song);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    std::vector<Event> process(std::int64_t from, std::int64_t to, bool playing);
    // Ends every sounding note now (a Stop, a bypass, Panic's first half).
    std::vector<Event> allOff(std::int64_t at);
    // A new song (a knob turned, a new seed) from here on: what's sounding rings out as it was, and the next notes
    // are the new song's. The plugin calls it on a bar line.
    void setSong(const Song& song);

    // Freeze (§9, §17.6), a live input. Asked for, it takes hold on the next bar line (bar 1 at the earliest): every
    // bar from there plays the state of the bar before it. Let go, the next bar line plays the replay's state there.
    // A jump or a Stop lets go of it and switches it off; takeReleased() says so once, so the button can follow.
    void setFreeze(bool on);
    bool frozen() const;
    std::int64_t holdFrom() const;  // the bar line it holds from, or −1
    bool takeReleased();

private:
    std::vector<Event> span(std::int64_t from, std::int64_t to, bool jump);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---- what the editor shows (SPEC §9) ----------------------------------------------------------------------

// A chord's name from its root and the scale: "Am7", "Cmaj7", "E7", "Bm7b5", "F#dim" (§3.4's chord, `size` notes).
std::string chordName(const Song& song, int root, int size, bool homeMajor = false);
std::string noteName(int pitchClass);

struct LaneCell {
    bool hit = false;
    int owner = -1;  // index of the voice in the group that plays it
    bool accent = false;
    int gate = 1;
    int note = -1;  // MIDI note (a chord voice: the chord's root, in its octave)
};
struct Lane {
    std::string name;
    int steps = 16;
    int playing = -1;  // the slot at the asked tick
    bool held = false;   // a group of sustained chords: its hits play nothing, the chord holds (§4.5)
    std::string chord;   // the chord sounding, for a chord group
    std::vector<LaneCell> cells;
    std::vector<int> channels;  // per voice
    // §16.10: the group's rhythm generator (its name; empty for Random) and, for a Register, the Lock in force (−1).
    std::string generator;
    int lock = -1;
};
struct HarmonyView {
    std::vector<int> roots;      // the progression's chord roots, one per change
    std::vector<std::string> names;
    int current = 0;             // which change is sounding
    int phraseBar = 0, phraseLength = 4;
};

// Reads the song as it plays at a tick, for display. Keeps its replay between calls, so asking every frame is cheap.
class View {
public:
    explicit View(const Song& song);
    // The song as it plays under a Freeze held from bar line `hold` (§17.6); "Next: bar N" still reads the replay.
    View(const Song& song, std::int64_t hold);
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    std::vector<Lane> lanes(std::int64_t tick);
    HarmonyView harmony(std::int64_t tick);
    // §7.6's form strip: per group, per bar from `bar` on, "A" when no temporary change is on it, else a letter per
    // row (B, C, …, in matrix order; "B*" when the row varies each time it fires).
    std::vector<std::vector<std::string>> form(std::int64_t bar, int bars);
    // §17.5, for the editor: the first bar at or after `bar`, within `bars` of it, at which the row or cord `id` acts
    // as the replay has it (clock, condition and chance), and its opportunities before that one that didn't act.
    Firing nextAct(std::uint64_t id, std::int64_t bar, int bars);
    // The chord sounding at the start of each bar from `bar` on.
    std::vector<std::string> chordsByBar(std::int64_t bar, int bars);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hocket
