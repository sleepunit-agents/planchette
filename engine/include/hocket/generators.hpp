// Generator slots (SPEC §16): a group's Rhythm and a Lead or Bass voice's Notes are each made by one generator,
// chosen by name and swapped, never stacked. This is the list of generators, their parameters, and what each makes.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hocket/song.hpp"

namespace hocket {

enum class SlotKind { Rhythm, Notes };

// §16.3: what a generator's output may depend on. Static: the seed, its parameters and the frame. Cycled: also the
// cycle. Clocked: also the group's step count t, which runs on from bar 0.
enum class GenKind { Static, Cycled, Clocked };

struct GenParam {
    const char* key;        // its name in the song file and in a destination (§17.2)
    const char* label;      // the editor's
    int lo, hi;             // a number's range; a choice's options are 0 … hi
    int rhythmDefault, notesDefault;
    bool choice = false;    // a choice (its options numbered from 0), not a number
    bool destination = true;  // a modulation destination (§17.2): false for a choice that sets the kind (§16.3)
    const char* options = "";  // a choice's options, for the editor: "Riff|Run"
    bool id = false;           // a derived generator's source (§16.6): an id, kept in GenSlot::sources, never a destination
};

struct Generator {
    const char* name;   // frozen once released (Appendix B): what the song stores
    const char* label;  // the editor's button
    bool rhythm, notes;  // the slots it can fill
    std::vector<GenParam> params;
    // The frame knobs it reads (§16.2): one it doesn't is greyed in the editor, with `why`.
    bool readsDensity = true, readsWander = true;
    const char* why = "";
};

// Every generator, in Appendix B's order.
const std::vector<Generator>& generators();
// The generators a slot can hold, in that order.
std::vector<const Generator*> generatorsFor(SlotKind slot);
// A generator of this slot by name, or nullptr (a name this build doesn't know, or another slot's).
const Generator* findGenerator(SlotKind slot, const std::string& name);
const GenParam* findParam(const Generator& gen, const std::string& key);
// The generator a slot plays: the one it names, or the slot's default (§16.9).
const Generator& playing(SlotKind slot, const GenSlot& s);
const char* defaultGenerator(SlotKind slot);
// A generator's parameter as the slot has it: its stored value in range, or its default for this slot.
int paramValue(SlotKind slot, const GenSlot& s, const Generator& gen, const GenParam& p);
int paramValue(SlotKind slot, const GenSlot& s, const char* gen, const char* key);
GenKind kindOf(SlotKind slot, const GenSlot& s);

// §16.6, derived generators. A slot's source id for a generator (0: none).
std::uint64_t sourceOf(const GenSlot& s, const char* gen);
// The source a group's Interlock reads, when it's live (a group of the song, not this one, not closing a loop as the
// last in song order): its index in song.groups.
std::optional<std::size_t> interlockSource(const Song& song, std::size_t gi);
// The same for a group on Resultant (a rhythm slot that names a group, like Interlock; one loop rule for both).
std::optional<std::size_t> resultantSource(const Song& song, std::size_t gi);
// The source a Lead or Bass voice's Kotekan reads, when it's live: (group index, voice index).
std::optional<std::pair<std::size_t, int>> kotekanSource(const Song& song, std::size_t gi, int vi);
// What a slot plays in a song: a derived generator whose source isn't live plays the slot's default (§16.6). vi −1 is
// the group's rhythm slot.
const Generator& playingIn(const Song& song, SlotKind slot, std::size_t gi, int vi);
GenKind kindIn(const Song& song, SlotKind slot, std::size_t gi, int vi);
// §16.6, Kotekan: the sangsih's note (MIDI, before the transpose difference) from the polos's note x, over B's pool
// (pitch classes, any order) around the core tone `centre`. Mode 0 nyog cag, 1 telu, 2 empat, 3 nyok cok;
// `anticipate` and `nextRoot` are Nyok cok's.
// §5.7, Clean handoffs: up to three draws from stream(hash(pitchSeed, "handoff", slot)).below(poolSize), the first whose
// note (pitchAt(position)) isn't on the pitch class `avoid`; none: nothing.
std::optional<int> handoffRedraw(std::uint64_t pitchSeed, std::int64_t slot, int poolSize, int avoid, const std::function<int(int)>& pitchAt);

// §16.6, Resultant: a composite note inside the band [low, high] (either order), moved by a voice's transpose; −1
// outside it.
int resultantNote(int note, int low, int high, int transpose);
int kotekanNote(int mode, const std::vector<int>& pool, int centre, int x, bool anticipate, int nextRoot);

// §16.4: the hits a group's static (or cycled, at a cycle) rhythm generator makes over its slots 0 … Steps − 1, at a
// Density (a fill's, §4.6, or the group's own). Random is §4.1's hits() exactly.
std::vector<bool> rhythmHits(const Group& g, int density, std::int64_t cycle = 0);

// §17.2: a generator's parameter as a destination, by its key: "rhythm.<generator>.<key>", "notes.<generator>.<key>".
// None for a key this build doesn't know, or a parameter that isn't a destination (1/f Wander's Loop, §16.3).
struct GenTarget {
    SlotKind slot;
    const Generator* gen;
    const GenParam* param;
};
std::optional<GenTarget> generatorTarget(const std::string& key);
std::string generatorKey(SlotKind slot, const Generator& gen, const GenParam& p);

// §16.4, Euclidean: hit(slot) = ((((slot − rot) mod L) · k) mod L) < k, k = clamp(Pulses, 1, L), rot = Rotation mod L.
std::vector<bool> euclidHits(int L, int pulses, int rotation);

// §16.4, Metric. The strata of L steps with a Beat of `beat` steps: the prime factors of L / gcd(Beat, L) ascending,
// then those of gcd(Beat, L) descending. Barlow's U(p) for a prime, and ψ over strata (coarsest first): a
// permutation of 0 … L−1, the largest the strongest pulse.
std::vector<int> metricStrata(int L, int beat);
std::vector<int> barlowUpper(int p);
std::vector<int> indispensability(const std::vector<int>& strata);
// score = (16 − Wild) · (L − 1 − ψ(s)) + Wild · (s's place in Random's order); the lowest `hits` scores are hits.
std::vector<bool> metricHits(std::uint64_t rhythmSeed, int L, int density, int wild, int beat);

// §16.7: one seed's shift register, b(t) for t ≥ −16, stepped one step at a time. b(t) = b(t − n) XOR flip(t), with
// flip(t) = Lock 17 ? 1 : stream(hash(seed, "register", t)).below(512) < (16 − Lock)².
class Register {
public:
    explicit Register(std::uint64_t seed);
    std::int64_t next() const { return static_cast<std::int64_t>(bits_.size()) - 16; }  // the t the next step makes
    void step(int n, int lock);
    // Vary (§16.7): flip b(next − 1 − q) for each q, before the next step.
    void flip(const std::vector<int>& back);
    int bit(std::int64_t t) const;
    // v(t): the newest w bits, newest first (0 … 2^w − 1).
    int value(std::int64_t t, int w) const;
    bool flips(std::int64_t t, int lock) const;

private:
    std::uint64_t seed_;
    std::vector<std::uint8_t> bits_;  // b(t) at t + 16
};
// §16.7's Vary: the min(depth, n) bits back from t0 to flip: the first of the priority order from
// hash(s, "register", targetID), keeping slots < n.
std::vector<int> registerVary(std::uint64_t s, std::uint64_t targetID, int depth, int n);
// Rhythm: v < rdiv(2^w · Density, 16). Notes: (v · poolSize) >> w, in Height order.
bool registerHit(int v, int w, int density);
int registerPosition(int v, int w, int poolSize);

// §16.5, 1/f Wander: a non-root step's position from Voss's dice at `time` (the slot for Riff, t for Run).
int pinkAt(std::uint64_t pitchSeed, int poolSize, int dice, std::int64_t time);

// §16.3: how a Notes generator's position is read, and from where.
enum class NoteOrder { Ranked, Height };
NoteOrder orderOf(const GenSlot& notes);
bool onTonic(const GenSlot& notes);  // Pedal: measured from the key's tonic, not the chord root

// §16.5: the pool position a Lead or Bass voice's static notes generator gives each slot (Ranked order; Wander is
// §3.3's noteIndices() exactly).
std::vector<int> notePositions(const Voice& v, int L, int poolSize, int wander);

}  // namespace hocket
