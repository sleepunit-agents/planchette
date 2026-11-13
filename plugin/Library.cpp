#include "Library.h"

#include "SongJson.h"
#include "hocket/form.hpp"

namespace hocket::library {

namespace {

int scaleNamed(const char* name) {
    for (std::size_t i = 0; i < kScales.size(); ++i)
        if (juce::String(kScales[i].name) == name) return static_cast<int>(i);
    return 1;
}

// Each starter shows one idea, on a fixed seed so it's the same song everywhere.
struct Starter {
    const char* name;
    void (*make)(Song&);
};

const Starter kStarters[] = {
    {"Walk around the planet", [](Song& s) {
         s.root = 2, s.scale = scaleNamed("Dorian");
         addLongForm(s, false);
     }},
    {"There and back", [](Song& s) {
         s.root = 9, s.scale = scaleNamed("Minor");
         addLongForm(s, true);
     }},
    {"Init: two leads trading", [](Song&) {}},  // the default song (§13): one turnaround, nothing else
    {"Slow chords, busy bass",
     [](Song& s) {
         s.harmony.changesEvery = 3;  // 2 bars
         s.groups[0].density = 5;
         s.groups[1].density = 12;
         s.root = 2, s.scale = scaleNamed("Dorian");
     }},
    {"Polymeter drift",
     [](Song& s) {
         s.groups[0].steps = 13;
         s.groups[1].steps = 7;
         s.groups[1].density = 9;
         s.root = 4, s.scale = scaleNamed("Phrygian");
     }},
    {"Seven eight",
     [](Song& s) {
         s.barLength = 7, s.beatUnit = 8;
         s.groups[0].steps = 14;
         s.groups[1].steps = 14;
         s.root = 7, s.scale = scaleNamed("Mixolydian");
     }},
    {"Wide and sparse",
     [](Song& s) {
         s.range = 14, s.wander = 13;
         s.groups[0].density = 4;
         s.groups[1].density = 3;
         s.harmony.changesEvery = 4;  // 4 bars
         s.root = 0, s.scale = scaleNamed("Lydian");
     }},
    // §18.1: a rondo. Walk around the planet's texture, on a form: each letter 8 phrases, and A comes back exactly.
    // B: new notes and rhythm. C: new notes and chords. A′: A with four notes a line changed. D: new rhythm and chords,
    // busier Leads.
    {"Rondo (A B A C A' D)",
     [](Song& s) {
         s.root = 4, s.scale = scaleNamed("Dorian");
         addLongForm(s, false);
         s.form.on = true, s.form.text = "A B A C A' D", s.form.section = 8;
         addLetter(s);
         FormLetter* c = addLetter(s);
         c->rhythm = false, c->harmony = true;
         FormLetter* d = addLetter(s);
         d->notes = false, d->harmony = true;
         d->params.push_back({Dest{Scope::Group, s.groups[0].id, "density"}, 13});
     }},
};

}  // namespace

int numStarters() { return static_cast<int>(std::size(kStarters)); }

juce::String starterName(int i) { return i >= 0 && i < numStarters() ? juce::String(kStarters[i].name) : juce::String(); }

Song starter(int i) {
    Song s = defaultSong(0x48A0C4E7000000ull + static_cast<std::uint64_t>(i));
    if (i >= 0 && i < numStarters() && s.groups.size() >= 3) kStarters[i].make(s);
    sanitize(s);
    return s;
}

juce::File folder() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Planchette").getChildFile("Songs");
}

std::vector<Entry> list(const juce::File& dir) {
    std::vector<Entry> out;
    for (int i = 0; i < numStarters(); ++i) out.push_back({starterName(i), {}, i});
    juce::Array<juce::File> files;
    const auto scan = [&](const juce::File& root) {
        files.addArray(root.findChildFiles(juce::File::findFiles, false, "*.planchette"));
        files.addArray(root.findChildFiles(juce::File::findFiles, false, "*.hocket"));  // backwards-compatible reader
    };
    scan(dir);
    // The same directory layout works under the user's Documents or an explicitly chosen root.
    // Read both forever; never move, delete or overwrite legacy files during discovery.
    if (dir.getFileName() == "Songs" && dir.getParentDirectory().getFileName() == "Planchette")
        scan(dir.getParentDirectory().getSiblingFile("Hocket").getChildFile("Songs"));
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        const int order = a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension());
        return order != 0 ? order < 0 : a.getFullPathName() < b.getFullPathName();
    });
    for (const auto& f : files) out.push_back({f.getFileNameWithoutExtension(), f, -1});
    return out;
}

juce::File fileFor(const juce::File& dir, const juce::String& name) {
    const juce::String legal = juce::File::createLegalFileName(name.trim());
    return legal.isEmpty() ? juce::File() : dir.getChildFile(legal + ".planchette");
}

juce::File save(const juce::File& dir, const juce::String& name, const Song& song) {
    const juce::File f = fileFor(dir, name);
    if (f == juce::File() || !dir.createDirectory()) return {};
    return f.replaceWithText(json::toString(song)) ? f : juce::File();
}

std::optional<Song> load(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) return std::nullopt;  // a song is a few KB
    const juce::var v = juce::JSON::parse(file.loadFileAsString());
    if (!v.isObject()) return std::nullopt;
    return json::fromVar(v);
}

std::optional<Song> load(const Entry& e) { return e.isStarter() ? std::optional<Song>(starter(e.starter)) : load(e.file); }

}  // namespace hocket::library
