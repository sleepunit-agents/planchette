// planchette_render [seed-hex] [bars] [--mid out.mid]
//
// The default song's MIDI events, one per line (tick, on/off, channel, note, velocity): how the golden files are
// made, and a quick way to look at what a seed plays. With --mid, also a Standard MIDI File (type 1, 960 PPQ,
// 120 BPM, one track per channel) to drop into a DAW and listen to.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "hocket/engine.hpp"
#include "hocket/golden.hpp"

namespace {

void be32(std::string& s, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
void be16(std::string& s, std::uint16_t v) {
    s += static_cast<char>(v >> 8);
    s += static_cast<char>(v & 0xFF);
}
void vlq(std::string& s, std::uint32_t v) {
    char buf[5];
    int n = 0;
    buf[n++] = static_cast<char>(v & 0x7F);
    while (v >>= 7) buf[n++] = static_cast<char>(0x80 | (v & 0x7F));
    while (n) s += buf[--n];
}

void writeMid(const std::string& path, const std::vector<hocket::Event>& evs) {
    std::map<int, std::vector<hocket::Event>> byChannel;
    for (const auto& e : evs) byChannel[e.channel].push_back(e);
    std::string out = "MThd";
    be32(out, 6);
    be16(out, 1);
    be16(out, static_cast<std::uint16_t>(byChannel.size() + 1));
    be16(out, hocket::kTicksPerBeat);
    // Track 0: tempo.
    std::string t0;
    vlq(t0, 0);
    t0 += "\xFF\x51\x03\x07\xA1\x20";  // 500000 µs per beat: 120 BPM
    vlq(t0, 0);
    t0 += std::string("\xFF\x2F\x00", 3);
    out += "MTrk";
    be32(out, static_cast<std::uint32_t>(t0.size()));
    out += t0;
    for (const auto& [ch, list] : byChannel) {
        std::string t;
        const std::string name = "Planchette ch " + std::to_string(ch);
        vlq(t, 0);
        t += "\xFF\x03";
        vlq(t, static_cast<std::uint32_t>(name.size()));
        t += name;
        std::int64_t last = 0;
        for (const auto& e : list) {
            vlq(t, static_cast<std::uint32_t>(e.tick - last));
            last = e.tick;
            t += static_cast<char>((e.kind == hocket::Event::On ? 0x90 : 0x80) | ((ch - 1) & 0x0F));
            t += static_cast<char>(e.note);
            t += static_cast<char>(e.kind == hocket::Event::On ? e.velocity : 0);
        }
        vlq(t, 0);
        t += std::string("\xFF\x2F\x00", 3);
        out += "MTrk";
        be32(out, static_cast<std::uint32_t>(t.size()));
        out += t;
    }
    std::ofstream(path, std::ios::binary) << out;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 0x0123456789ABCDEFull;
    int bars = 64;
    std::string mid;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mid") && i + 1 < argc) mid = argv[++i];
        else if (positional++ == 0) seed = std::strtoull(argv[i], nullptr, 16);
        else bars = std::atoi(argv[i]);
    }
    const hocket::Song song = hocket::defaultSong(seed);
    if (!mid.empty()) {
        writeMid(mid, hocket::events(hocket::notes(song, 0, hocket::barTicks(song) * bars)));
        std::fprintf(stderr, "wrote %s\n", mid.c_str());
        return 0;
    }
    std::fputs(hocket::golden(song, bars).c_str(), stdout);
    return 0;
}
