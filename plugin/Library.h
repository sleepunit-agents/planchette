// Songs with names (SPEC §12.3): a few starter songs built in, and yours, saved as .planchette files (the song's JSON,
// host settings included) in Documents/Planchette/Songs. The same list is the plugin's presets in a host.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "hocket/song.hpp"

namespace hocket::library {

struct Entry {
    juce::String name;
    juce::File file;  // none for a starter song
    int starter = -1;  // which built-in song, or −1
    bool isStarter() const { return starter >= 0; }
};

int numStarters();
juce::String starterName(int i);
Song starter(int i);

juce::File folder();  // Documents/Planchette/Songs

// Starters first, then both extensions, A to Z. The default library also reads the legacy folder.
std::vector<Entry> list(const juce::File& dir);

// Writes the song as <name>.planchette in `dir` (a name a file system takes; replaces one of that name). Empty on failure.
juce::File save(const juce::File& dir, const juce::String& name, const Song& song);
juce::File fileFor(const juce::File& dir, const juce::String& name);
std::optional<Song> load(const juce::File& file);
std::optional<Song> load(const Entry& e);

}  // namespace hocket::library
