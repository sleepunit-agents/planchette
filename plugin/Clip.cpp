#include "Clip.h"

#include <map>

namespace hocket::clip {

juce::MidiFile render(const Song& song, std::int64_t fromBar, int bars, double bpm) {
    const std::int64_t bar = barTicks(song);
    const std::int64_t from = fromBar * bar, to = (fromBar + juce::jmax(1, bars)) * bar;
    // Far enough back to catch a chord that started before the clip and is still ringing: a held chord lasts one
    // change (4 bars at most), and a long gate can carry a note on past that.
    const std::int64_t back = juce::jmax<std::int64_t>(0, from - 8 * bar);

    std::map<std::uint64_t, std::size_t> groupOf;  // voice id → group
    for (std::size_t g = 0; g < song.groups.size(); ++g)
        for (const auto& v : song.groups[g].voices) groupOf[v.id] = g;
    std::vector<std::vector<Note>> perGroup(song.groups.size());
    for (Note n : notes(song, back, to)) {
        if (n.end <= from) continue;
        n.start = juce::jmax(n.start, from) - from;
        n.end = juce::jmin(n.end, to) - from;
        if (n.end <= n.start) continue;
        const auto it = groupOf.find(n.voice);
        if (it != groupOf.end()) perGroup[it->second].push_back(n);
    }

    juce::MidiFile file;
    file.setTicksPerQuarterNote(kTicksPerBeat);
    juce::MidiMessageSequence head;
    head.addEvent(juce::MidiMessage::textMetaEvent(3, "Planchette " + juce::String::toHexString(static_cast<juce::int64>(song.masterSeed))), 0);
    head.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(60000000.0 / juce::jmax(1.0, bpm)))), 0);
    head.addEvent(juce::MidiMessage::timeSignatureMetaEvent(song.barLength, song.beatUnit), 0);
    head.addEvent(juce::MidiMessage::endOfTrack(), static_cast<double>(to - from));
    file.addTrack(head);
    for (std::size_t g = 0; g < song.groups.size(); ++g) {
        if (perGroup[g].empty()) continue;
        std::sort(perGroup[g].begin(), perGroup[g].end(), [](const Note& a, const Note& b) {
            return std::tie(a.start, a.channel, a.note) < std::tie(b.start, b.channel, b.note);
        });
        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::textMetaEvent(3, juce::String(song.groups[g].name)), 0);
        for (const Event& e : events(perGroup[g])) {
            const auto msg = e.kind == Event::On ? juce::MidiMessage::noteOn(e.channel, e.note, static_cast<juce::uint8>(e.velocity))
                                                 : juce::MidiMessage::noteOff(e.channel, e.note);
            track.addEvent(msg, static_cast<double>(e.tick));
        }
        track.updateMatchedPairs();
        track.addEvent(juce::MidiMessage::endOfTrack(), static_cast<double>(to - from));
        file.addTrack(track);
    }
    return file;
}

juce::File folder() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Planchette").getChildFile("Clips");
}

juce::File stagingFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Planchette").getChildFile("Outgoing");
}

juce::File write(const Song& song, std::int64_t fromBar, int bars, double bpm, const juce::File& dir) {
    if (!dir.createDirectory()) return {};
    const juce::String seed = juce::String::toHexString(static_cast<juce::int64>(song.masterSeed)).paddedLeft('0', 16).substring(0, 8);
    const juce::File out = dir.getChildFile("Planchette " + seed + " bars " + juce::String(fromBar + 1) + "-" +
                                            juce::String(fromBar + juce::jmax(1, bars)) + ".mid");
    const juce::MidiFile file = render(song, fromBar, bars, bpm);
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (!stream.openedOk() || !file.writeTo(stream)) return {};
    return out;
}

}  // namespace hocket::clip
