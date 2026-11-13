// The text form of a render, for golden files: a header naming the song, then one line per MIDI event.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "hocket/engine.hpp"
#include "hocket/rng.hpp"

namespace hocket {

inline std::string golden(const Song& song, int bars) {
    std::string out;
    char line[160];
    std::snprintf(line, sizeof line, "# hocket engine %d, master seed %016llx, %d bars\n", kEngineVersion,
                  static_cast<unsigned long long>(song.masterSeed), bars);
    out += line;
    // The frozen arithmetic, pinned: a change here means every song changed.
    std::snprintf(line, sizeof line, "# hash(seed, \"hits\") %016llx; stream(0) %016llx; below(10) x3 %d %d %d\n",
                  static_cast<unsigned long long>(hash(song.masterSeed, {"hits"})),
                  static_cast<unsigned long long>(Stream(0).next()),
                  static_cast<int>(Stream(song.masterSeed).below(10)), static_cast<int>(Stream(song.masterSeed + 1).below(10)),
                  static_cast<int>(Stream(song.masterSeed + 2).below(10)));
    out += line;
    std::string prog = "# harmony:";
    const std::vector<int> p = progression(song);
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (std::size_t k = 0; k < p.size(); ++k)
        prog += std::string(" ") + names[chordRootAt(song, static_cast<std::int64_t>(k) * changeTicks(song))];
    out += prog + "\n";
    for (const Event& e : events(notes(song, 0, barTicks(song) * bars))) {
        std::snprintf(line, sizeof line, "%lld %s %d %d %d\n", static_cast<long long>(e.tick), e.kind == Event::On ? "on" : "off",
                      e.channel, e.note, e.velocity);
        out += line;
    }
    return out;
}

}  // namespace hocket
