// What the engine's files share and the API doesn't show: the state at a bar, and the replay that builds it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "hocket/engine.hpp"
#include "hocket/song.hpp"

namespace hocket::detail {

inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline std::int64_t ceilDiv(std::int64_t a, std::int64_t b) { return -floorDiv(-a, b); }
inline std::int64_t floorMod(std::int64_t a, std::int64_t b) { return a - floorDiv(a, b) * b; }

// The song as it plays during one bar: the saved song with the matrix applied (§7.3), what it has done to the
// patterns, and where each clock was last re-anchored (§4.4, §4.5).
struct State {
    Song song;  // its mod is empty: sources and cords are read from the song the replay was made from
    Mutations mut;
    std::map<std::uint64_t, std::int64_t> anchor;  // group id → anchor tick
    std::map<std::uint64_t, std::int64_t> stepsBefore;  // group id → the steps it played before its anchor (§16.3)
    // §18.3: group id → the irama level it plays (as the state at its last gong had it), and its step in ticks.
    std::map<std::uint64_t, int> irama;
    std::map<std::uint64_t, std::int64_t> step;
    std::int64_t harmonyAnchor = 0;
    // The items whose temporary layers are on during this bar (§7.6), in §17.4's order: a row by its place in the
    // matrix, a trigger cord by matrix.size() + its place in mod.cords.
    std::vector<std::size_t> layers;
    // §17.5: what each item did at this bar, by its place in §17.4's order (rows, then cords): 0 no opportunity (or not
    // an item), 1 an opportunity it didn't act on (its condition or its chance failed), 2 it acted.
    std::vector<std::uint8_t> acts;
};

// §17.2's addresses as the replay keys them: (scope, target, param or −1, a generator parameter's key).
using DestKey = std::tuple<int, std::uint64_t, int, std::string>;
std::optional<DestKey> addressOf(const Dest& d);
std::optional<int> valueAt(const Song& s, const DestKey& k);
void setValue(Song& s, const DestKey& k, int x);
std::pair<int, int> spanAt(const Song& s, const DestKey& k);
bool numericKey(const DestKey& k);
bool pickKey(const DestKey& k);

// §18.3: a group's step in ticks at an irama level: its Beat division at 0 (Off); otherwise the harmony's change in
// 2^(level − 1) steps, halved as far as the change's ticks divide.
std::int64_t iramaStep(const Song& s, const Group& g, int level);
// A group's step in ticks under a bar's state (§4.4, §18.3).
inline std::int64_t stepTicksIn(const State& st, const Group& g) {
    const auto it = st.step.find(g.id);
    return it != st.step.end() ? it->second : kDivision[static_cast<std::size_t>(clampi(g.beatDivision, 0, 23))];
}

// The progression's indices for a song, with any harmony mutations (§4.5, §7.5).
std::vector<int> progressionOf(const Song& song, const Mutations& mut);

// A group's hit array with its rhythm mutations (§4.1, §7.5).
std::vector<bool> hitsOf(const Group& g, const Mutations& mut);
// The same at another Density (a fill's, §4.6).
std::vector<bool> hitsOf(const Group& g, const Mutations& mut, int density);

// The matrix replayed from bar 0 (§2.1, §7.3), one state per bar, built on demand and kept.
class Replay {
public:
    explicit Replay(const Song& base);
    const State& at(std::int64_t bar);

private:
    // scope, target, param, and a generator parameter's key (§17.2; its param is −1), empty for a param's
    using Key = std::tuple<int, std::uint64_t, int, std::string>;
    struct Numeric {
        int offset = 0;
        int lo = 0, hi = 0;
    };
    struct Change {
        std::map<Key, int> deltas;     // numeric: added, then reflected into the row's bounds
        std::map<Key, int> choices;    // pick-one, Scale
        std::map<Key, std::uint64_t> seeds;
        Mutations mut;
        int lo = 0, hi = 0;
        bool skip = false;  // a Vary whose every target is clocked, not a Register (§16.2): it writes nothing, pushes no layer
    };
    struct Layer {
        std::size_t item;  // §17.4's order: rows, then trigger cords
        std::int64_t until;
        Change change;
    };

    State build(std::int64_t bar) const;
    void advance();
    // What an item does when it acts (§7.3, §17.3): `row` is a rule row, or a trigger cord as one. `h` is where its
    // rolls come from (a row's h, a cord's hc); `fixed` the seed a Fixed reroll or vary uses; `given` a cord's As
    // given delta, for a numeric destination or Root.
    Change fire(const Row& row, std::int64_t bar, std::uint64_t h, std::uint64_t fixed, std::optional<int> given, const State& pre) const;
    void commit(std::size_t item, std::int64_t bar, int lasts, Change c);
    void levels(State& st, std::int64_t bar) const;  // §17.4, step 3

    // A cord worked out once (§17.3): its source's output, compiled, and its destination as a key.
    struct CordPlan {
        CompiledOutput out;
        std::optional<Key> key;  // none: a key this build doesn't know, or Mute
        bool mute = false;       // a Mute cord (a level's alone)
        bool levelOk = false;    // a level may drive its destination
        Row row;                 // a trigger cord as the row it acts like
    };

    // §17.5: an item's opportunities so far, its latest, and whether it acted then.
    struct ItemRec {
        std::int64_t count = 0;
        std::int64_t last = -1;
        bool acted = false;
    };
    bool passes(const Condition& c, std::size_t item, std::int64_t n, std::int64_t bar, const Scope scope, std::uint64_t target,
                const State* fillState) const;

    const Song& base_;
    std::vector<CordPlan> plans_;
    std::vector<ItemRec> recs_;             // one per row and cord, in §17.4's order
    std::vector<std::size_t> items_;        // the items (rows that are on, trigger cords that are on), in that order
    std::vector<std::size_t> itemPos_;      // each row's or cord's place in items_ (−1: not an item)
    std::map<std::uint64_t, std::size_t> byId_;  // a row's or cord's id → its index in §17.4's order
    bool fillConds_ = false;                // any FILL condition (the state before the triggers is only built then)  // one per cord in mod.cords
    Song plain_;  // the base without its mod: each bar's state copies this (the replay reads mod from base_)
    std::deque<State> states_;  // a deque: a state's address never moves, and the bars' group plays hold on to it
    std::map<Key, Numeric> offsets_;
    std::map<Key, int> choices_;
    std::map<Key, std::uint64_t> seeds_;
    Mutations perm_;
    std::vector<Layer> layers_;
};

}  // namespace hocket::detail
