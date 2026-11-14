#include "Processor.h"

#include <cmath>
#include <iterator>
#include <optional>
#include <random>

#include "Editor.h"
#include "Library.h"
#include "SongJson.h"

namespace {

// A block owns the ticks from its first sample on (SPEC §4.4): the first whole tick at or after its start, with a
// small bias so 15.9999999 counts as 16. Ceiling, not floor: no event can fall before the block's first sample.
std::int64_t firstTick(double ticks) { return static_cast<std::int64_t>(std::ceil(ticks - 1.0 / 1024.0)); }

// Hosts report each block's position on its own, and it drifts by fractions of a tick. A gap this small is the
// same playback, not a jump (a jump ends every note and re-strikes the chords).
constexpr std::int64_t kContinuity = 2;

}  // namespace

// A meter as one number (0: none), its parts in the ranges a song takes, so the host's 7/8 and the song's compare.
int HocketProcessor::packMeter(int beats, int unit) {
    beats = juce::jlimit(1, 16, beats);
    unit = unit <= 2 ? 2 : unit <= 4 ? 4 : unit <= 8 ? 8 : 16;
    return beats * 64 + unit;
}

namespace {

std::uint64_t randomSeed() {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

// §12.2: the automation slots. A plugin's parameter list is fixed and a song's groups and voices aren't, so the host
// sees 8 group slots and 16 voice slots, and each group and voice says which one it answers to (its `slot`).
enum class FieldKind { Value, Toggle, Trigger };
struct SlotField {
    const char* key;
    const char* name;
    int lo, hi, def;
    FieldKind kind;
};
constexpr SlotField kGroupFields[] = {
    {"density", "Density", 1, 16, 8, FieldKind::Value},      {"steps", "Steps", 1, 64, 16, FieldKind::Value},
    {"range", "Range", 1, 16, 8, FieldKind::Value},          {"wander", "Wander", 1, 16, 8, FieldKind::Value},
    {"swing", "Swing", 0, 100, 0, FieldKind::Value},         {"accent", "Accent", 0, 100, 50, FieldKind::Value},
    {"gate", "Gate %", 1, 200, 100, FieldKind::Value},       {"probability", "Probability", 0, 100, 100, FieldKind::Value},
    {"solo", "Solo member", 1, 16, 1, FieldKind::Value},     {"mute", "Mute", 0, 1, 0, FieldKind::Toggle},
    {"reroll", "Reroll", 0, 1, 0, FieldKind::Trigger},
};
constexpr SlotField kVoiceFields[] = {
    {"density", "Density", 0, 16, 8, FieldKind::Value},      {"weight", "Weight", 0, 16, 8, FieldKind::Value},
    {"range", "Range", 1, 16, 8, FieldKind::Value},          {"wander", "Wander", 1, 16, 8, FieldKind::Value},
    {"accent", "Accent", 0, 100, 50, FieldKind::Value},      {"gate", "Gate %", 1, 200, 100, FieldKind::Value},
    {"octaveLow", "Octave low", -1, 2, -1, FieldKind::Value}, {"octaveHigh", "Octave high", -1, 2, 2, FieldKind::Value},
    {"chordSize", "Chord size", 1, 6, 4, FieldKind::Value},  {"voicing", "Voicing", 1, 16, 1, FieldKind::Value},
    {"mute", "Mute", 0, 1, 0, FieldKind::Toggle},            {"reroll", "Reroll", 0, 1, 0, FieldKind::Trigger},
};
constexpr int kGroupFieldCount = static_cast<int>(std::size(kGroupFields)), kVoiceFieldCount = static_cast<int>(std::size(kVoiceFields));
constexpr int kSlotParams = hocket::kGroupSlots * kGroupFieldCount + hocket::kVoiceSlots * kVoiceFieldCount;

// A slot parameter by its place in the list: the group slots' fields, then the voice slots'.
struct SlotRef {
    bool voice;
    int slot;   // 1-based
    int field;  // its place in kGroupFields or kVoiceFields
    const SlotField& info() const { return voice ? kVoiceFields[field] : kGroupFields[field]; }
};
SlotRef slotRef(int i) {
    const int groups = hocket::kGroupSlots * kGroupFieldCount;
    if (i < groups) return {false, i / kGroupFieldCount + 1, i % kGroupFieldCount};
    return {true, (i - groups) / kVoiceFieldCount + 1, (i - groups) % kVoiceFieldCount};
}
juce::String slotId(const SlotRef& r) { return juce::String(r.voice ? "v" : "g") + juce::String(r.slot) + "_" + r.info().key; }

hocket::Group* groupInSlot(hocket::Song& s, int slot) {
    for (hocket::Group& g : s.groups)
        if (g.slot == slot) return &g;
    return nullptr;
}
hocket::Voice* voiceInSlot(hocket::Song& s, int slot, hocket::Group** owner = nullptr) {
    for (hocket::Group& g : s.groups)
        for (hocket::Voice& v : g.voices)
            if (v.slot == slot) {
                if (owner) *owner = &g;
                return &v;
            }
    return nullptr;
}

// What a slot's field is in a song: the value that plays, so one that inherits (§1.2) reads the level above. Nothing
// when no group or voice answers to the slot, or the field is a button.
std::optional<int> slotValue(hocket::Song& s, const SlotRef& r) {
    auto or3 = [](const std::optional<int>& a, const std::optional<int>& b, int c) { return a ? *a : b ? *b : c; };
    const std::string key = r.info().key;
    if (r.info().kind == FieldKind::Trigger) return std::nullopt;
    if (!r.voice) {
        hocket::Group* g = groupInSlot(s, r.slot);
        if (!g) return std::nullopt;
        if (key == "density") return g->density;
        if (key == "steps") return g->steps;
        if (key == "range") return or3(g->overrides.range, std::nullopt, s.range);
        if (key == "wander") return or3(g->overrides.wander, std::nullopt, s.wander);
        if (key == "swing") return g->swing;
        if (key == "accent") return or3(g->overrides.accent, std::nullopt, s.accent);
        if (key == "gate") return or3(g->overrides.gatePercent, std::nullopt, s.gatePercent);
        if (key == "probability") return g->probability;
        if (key == "solo") return g->soloMember + 1;
        if (key == "mute") return g->mute ? 1 : 0;
        return std::nullopt;
    }
    hocket::Group* g = nullptr;
    hocket::Voice* v = voiceInSlot(s, r.slot, &g);
    if (!v) return std::nullopt;
    if (key == "density") return v->density;
    if (key == "weight") return v->weight;
    if (key == "range") return or3(v->overrides.range, g->overrides.range, s.range);
    if (key == "wander") return or3(v->overrides.wander, g->overrides.wander, s.wander);
    if (key == "accent") return or3(v->overrides.accent, g->overrides.accent, s.accent);
    if (key == "gate") return or3(v->overrides.gatePercent, g->overrides.gatePercent, s.gatePercent);
    if (key == "octaveLow") return or3(v->overrides.octaveLow, std::nullopt, s.octaveLow);
    if (key == "octaveHigh") return or3(v->overrides.octaveHigh, std::nullopt, s.octaveHigh);
    if (key == "chordSize") return v->chordSize;
    if (key == "voicing") return v->voicing;
    if (key == "mute") return v->mute ? 1 : 0;
    return std::nullopt;
}

// The host moved a slot's parameter: the value goes into the song (§12.2: automation sets base values). One that
// inherits becomes the group's or the voice's own (an override, §1.2).
bool setSlotValue(hocket::Song& s, const SlotRef& r, int x) {
    const std::string key = r.info().key;
    if (!r.voice) {
        hocket::Group* g = groupInSlot(s, r.slot);
        if (!g) return false;
        if (key == "density") g->density = x;
        else if (key == "steps") g->steps = x;
        else if (key == "range") g->overrides.range = x;
        else if (key == "wander") g->overrides.wander = x;
        else if (key == "swing") g->swing = x;
        else if (key == "accent") g->overrides.accent = x;
        else if (key == "gate") g->overrides.gatePercent = x;
        else if (key == "probability") g->probability = x;
        else if (key == "solo") g->soloMember = std::max(0, std::min(x - 1, static_cast<int>(g->voices.size()) - 1));
        else if (key == "mute") g->mute = x != 0;
        else return false;
        return true;
    }
    hocket::Voice* v = voiceInSlot(s, r.slot);
    if (!v) return false;
    if (key == "density") v->density = x;
    else if (key == "weight") v->weight = x;
    else if (key == "range") v->overrides.range = x;
    else if (key == "wander") v->overrides.wander = x;
    else if (key == "accent") v->overrides.accent = x;
    else if (key == "gate") v->overrides.gatePercent = x;
    else if (key == "octaveLow") v->overrides.octaveLow = x;
    else if (key == "octaveHigh") v->overrides.octaveHigh = x;
    else if (key == "chordSize") v->chordSize = x;
    else if (key == "voicing") v->voicing = x;
    else if (key == "mute") v->mute = x != 0;
    else return false;
    return true;
}

// The parameters a host sees (SPEC §10's ✓ column): the globals, then §12.2's group and voice slots.
// Their defaults are the default song's, so an untouched instance plays exactly defaultSong(seed).
juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    using namespace juce;
    StringArray roots, scales;
    for (int pc = 0; pc < 12; ++pc) roots.add(hocket::noteName(pc));
    for (const auto& s : hocket::kScales) scales.add(s.name);
    const hocket::Song d = hocket::defaultSong(0);
    auto choice = [](const char* id, const char* name, StringArray options, int def) {
        return std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, options, def);
    };
    auto number = [](const char* id, const char* name, int lo, int hi, int def) {
        return std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name, lo, hi, def);
    };
    AudioProcessorValueTreeState::ParameterLayout out{
        choice("root", "Root", roots, d.root),
        choice("scale", "Scale", scales, d.scale),
        choice("chordEvery", "Chords change every", {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars"}, d.harmony.changesEvery),
        number("chordRange", "Chord range", 1, 16, d.harmony.range),
        number("chordWander", "Chord wander", 1, 16, d.harmony.wander),
        number("chordLength", "Chords in the progression", 1, 16, d.harmony.length),
        number("range", "Range", 1, 16, d.range),
        number("wander", "Wander", 1, 16, d.wander),
        number("accent", "Accent", 0, 100, d.accent),
        number("gate", "Gate %", 1, 200, d.gatePercent),
        number("octaveLow", "Octave low", -1, 2, d.octaveLow),
        number("octaveHigh", "Octave high", -1, 2, d.octaveHigh),
        number("colour", "Colour", 0, 16, d.colour),
        number("jumps", "Octave jumps", 0, 16, d.octaveJumps),
        // §4.6: every group fills while it's on. A button on a controller, MIDI-learned in the host, plays a fill.
        choice("fill", "Fill", {"Off", "On"}, d.fill),
        // §3.8: the harmony leans on the mode's own move and keeps out of the relative major (with Colour above 0).
        choice("moves", "Mode moves", {"Off", "On"}, d.modeMoves),
        // §9.2: Freeze holds what you hear (a live input, never part of the song). A jump switches it off.
        choice("freeze", "Freeze", {"Off", "On"}, 0),
    };
    // §12.2: the group and voice slots. "Group 2 Density", "Voice 5 Weight": a fixed list can't take a song's names.
    hocket::Song defaults = d;
    for (int i = 0; i < kSlotParams; ++i) {
        const SlotRef r = slotRef(i);
        const SlotField& f = r.info();
        const String name = String(r.voice ? "Voice " : "Group ") + String(r.slot) + " " + f.name;
        // The default song's own value where it has a group or voice in the slot, so an untouched instance needs no
        // parameter moved to play it.
        const int def = std::max(f.lo, std::min(f.hi, slotValue(defaults, r).value_or(f.def)));
        if (f.kind == FieldKind::Value) out.add(std::make_unique<AudioParameterInt>(ParameterID{slotId(r), 1}, name, f.lo, f.hi, def));
        else out.add(std::make_unique<AudioParameterChoice>(ParameterID{slotId(r), 1}, name, StringArray{"Off", "On"}, def));
    }
    return out;
}

const char* const kParamIds[] = {"root", "scale", "chordEvery", "chordRange", "chordWander", "chordLength", "range",
                                  "wander", "accent", "gate", "octaveLow", "octaveHigh", "colour", "jumps", "fill", "moves"};

}  // namespace

HocketProcessor::HocketProcessor()
    // An instrument has a silent stereo out (a host wants one to give it a track); a MIDI effect (the iPad's AUv3)
    // has no audio at all.
    : AudioProcessor(JucePlugin_IsMidiEffect ? BusesProperties() : BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "Hocket", layout()) {
    for (int i = 0; i < kSlotParams; ++i) slotParams_.push_back(params.getParameter(slotId(slotRef(i))));
    slotShadow_.assign(static_cast<std::size_t>(kSlotParams), kUnknown);
    retired_.reserve(kRetired);
    doc_ = hocket::defaultSong(randomSeed());
    song_ = doc_;
    player_ = std::make_unique<hocket::Player>(song_);
    playingBar_ = hocket::barTicks(song_);
    {
        hocket::Song neutral = song_;  // §9.3: what's playing, by its live key
        hocket::neutralLive(neutral);
        played_.key = hocket::json::toString(neutral);
    }
    playingMeter_ = packMeter(song_.barLength, song_.beatUnit);
    rebuild();
    devices_ = juce::MidiDeviceListConnection::make([this] { devicesChanged(); });
    library_ = hocket::library::folder();
    songs_ = hocket::library::list(library_);
    startTimerHz(20);
}

HocketProcessor::~HocketProcessor() { stopTimer(); }

bool HocketProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (JucePlugin_IsMidiEffect) return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void HocketProcessor::prepareToPlay(double sampleRate, int) { sampleRate_ = sampleRate; }

// The song's fields that are host parameters, both ways. One list, so reading and writing can't disagree.
namespace {
template <typename F>
void eachParam(hocket::Song& s, F f) {
    f("root", s.root);
    f("scale", s.scale);
    f("chordEvery", s.harmony.changesEvery);
    f("chordRange", s.harmony.range);
    f("chordWander", s.harmony.wander);
    f("chordLength", s.harmony.length);
    f("range", s.range);
    f("wander", s.wander);
    f("accent", s.accent);
    f("gate", s.gatePercent);
    f("octaveLow", s.octaveLow);
    f("octaveHigh", s.octaveHigh);
    f("colour", s.colour);
    f("jumps", s.octaveJumps);
    f("fill", s.fill);
    f("moves", s.modeMoves);
}
}  // namespace

hocket::Song HocketProcessor::current() const {
    hocket::Song s = doc_;
    std::size_t i = 0;
    eachParam(s, [this, &i](const char* id, int& field) {
        auto* p = params.getParameter(id);
        field = static_cast<int>(std::lround(p->convertFrom0to1(p->getValue())));
        // §18.1: while a letter is being edited, the host's values are the letter's; A keeps the ones it had. (Colour
        // and Octave jumps aren't a letter's, nor the live Fill: they stay the host's, for A too.)
        if (editing_ && i < aHost_.size() && std::string(id) != "colour" && std::string(id) != "jumps" && std::string(id) != "fill") field = aHost_[i];
        ++i;
    });
    return s;
}

// The letter being edited, as the screens show it: A with the letter on it, the host's values (which are the
// letter's while it's edited) on top, and the song's form.
hocket::Song HocketProcessor::letterView() const {
    hocket::Song v = hocket::letterSong(current(), {editing_, false}, -1);
    eachParam(v, [this](const char* id, int& field) {
        auto* p = params.getParameter(id);
        field = static_cast<int>(std::lround(p->convertFrom0to1(p->getValue())));
    });
    v.form = doc_.form;
    return v;
}

void HocketProcessor::editLetter(char name) {
    if (name == 'A') name = 0;
    if (name && !hocket::letterOf(doc_, name)) return;
    if (name == editing_) return;
    const hocket::Song a = current();  // A, with its own host values
    editing_ = 0;
    aHost_.clear();
    if (name) {
        hocket::Song s = a;
        eachParam(s, [this](const char*, int& field) { aHost_.push_back(field); });
        editing_ = name;
        setParams(hocket::letterSong(a, {name, false}, -1));  // the host's knobs show the letter's values
    } else {
        setParams(a);  // A's own back
    }
    rebuild();
}

void HocketProcessor::setParams(const hocket::Song& song) {
    hocket::Song s = song;
    eachParam(s, [this](const char* id, int& field) {
        auto* p = params.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(field)));
    });
}

// The song that plays: the document with the host's values on it.
void HocketProcessor::rebuild() {
    pushSlots();  // §12.2: the slots' parameters follow the song
    if (editing_ && !hocket::letterOf(doc_, editing_)) editing_ = 0, aHost_.clear();  // its letter is gone (an undo, a delete)
    hocket::Song s = current();
    if (editing_) {
        // §18.1: the letter's values are what makes A into what the screens show; and the letter plays on its own,
        // round and round, so what you hear is what you edit.
        const hocket::Song view = letterView();
        for (hocket::FormLetter& l : doc_.form.letters)
            if (l.name == editing_) l.params = hocket::letterValues(s, view);
        display_ = view;
        s.form = doc_.form;
        s.form.on = true;
        s.form.text = std::string(1, editing_);
        s.form.end = 0;
    }
    // The host's meter, when it says (§6.3): its bars are the song's. The song's own is for hosts that don't.
    meterApplied_ = hostMeter_.load();
    if (meterApplied_ != 0) {
        s.barLength = meterApplied_ / 64;
        s.beatUnit = meterApplied_ % 64;
    }
    song_ = s;
    ++version_;
    // §9.3: a change of live values only is heard from the next step; anything else waits for the bar line. Which it
    // is depends on the song that is playing: the last one handed over, if the audio thread has taken it.
    hocket::Song neutral = s;
    hocket::neutralLive(neutral);
    const juce::String key = hocket::json::toString(neutral);
    const bool likePlayed = key == played_.key, likePending = key == pending_.key;
    // Made ready here: the replay to the playing bar (§2.1) is this thread's work, not the audio thread's.
    const std::int64_t tick = tick_.load();
    const std::int64_t bar = hocket::barTicks(s);
    hocket::Player::ReadyPtr next = hocket::Player::ready(s, tick >= 0 ? tick / bar : 0, frozenFrom_.load());
    hocket::Player::ReadyPtr old;
    const int version = ++handVersion_;
    bool taken = false;
    {
        const juce::SpinLock::ScopedLockType lock(swap_);
        taken = pending_.version != 0 && playingVersion_.load() == pending_.version;
        old = std::move(incoming_);  // a song still waiting is replaced, not queued
        incoming_ = std::move(next);
        incomingLive_ = taken ? likePending : likePlayed;
        incomingVersion_ = version;
        incomingBar_ = bar;
        incomingMeter_ = packMeter(s.barLength, s.beatUnit);
    }
    if (taken) played_ = pending_;
    pending_ = {version, key};
}

// One step of the song's undo: the document before and after. Steps in one gesture fold into one.
struct HocketProcessor::SongEdit : juce::UndoableAction {
    SongEdit(HocketProcessor& p, hocket::Song before, hocket::Song after) : proc(p), before(std::move(before)), after(std::move(after)) {}
    bool perform() override { return put(after); }
    bool undo() override { return put(before); }
    int getSizeInUnits() override { return 1; }
    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override {
        if (auto* n = dynamic_cast<SongEdit*>(next)) return new SongEdit(proc, before, n->after);
        return nullptr;
    }
    bool put(const hocket::Song& s) {
        proc.doc_ = s;
        proc.rebuild();
        return true;
    }
    HocketProcessor& proc;
    hocket::Song before, after;
};

// Loading a song is one step too, and it takes the host settings with it (Root, Scale, the knobs): undo puts both
// back, and the name.
struct HocketProcessor::SongLoad : juce::UndoableAction {
    SongLoad(HocketProcessor& p, hocket::Song before, juce::String beforeName, hocket::Song after, juce::String afterName)
        : proc(p), before(std::move(before)), after(std::move(after)), beforeName(std::move(beforeName)), afterName(std::move(afterName)) {}
    bool perform() override { return put(after, afterName); }
    bool undo() override { return put(before, beforeName); }
    int getSizeInUnits() override { return 1; }
    bool put(const hocket::Song& s, const juce::String& name) {
        proc.editing_ = 0;  // a song loaded: back to A
        proc.aHost_.clear();
        proc.doc_ = s;
        proc.setParams(s);
        proc.name_ = name;
        proc.rebuild();
        return true;
    }
    HocketProcessor& proc;
    hocket::Song before, after;
    juce::String beforeName, afterName;
};

void HocketProcessor::loadSong(const hocket::Song& song, const juce::String& name) {
    undo_.beginNewTransaction();
    undo_.perform(new SongLoad(*this, current(), name_, song, name));
    undo_.beginNewTransaction();
    saved_ = hocket::json::toString(current());
}

bool HocketProcessor::loadSong(const hocket::library::Entry& e) {
    const auto song = hocket::library::load(e);
    if (!song) return false;
    loadSong(*song, e.name);
    return true;
}

bool HocketProcessor::saveSong(const juce::String& name) {
    const hocket::Song s = current();
    const juce::File f = hocket::library::save(library_, name, s);
    if (f == juce::File()) return false;
    name_ = f.getFileNameWithoutExtension();
    saved_ = hocket::json::toString(s);
    refreshSongs();
    ++version_;  // the editor shows the name
    return true;
}

bool HocketProcessor::deleteSong(const hocket::library::Entry& e) {
    if (e.isStarter() || !e.file.deleteFile()) return false;
    if (e.name == name_) name_.clear(), saved_.clear();
    refreshSongs();
    ++version_;
    return true;
}

bool HocketProcessor::edited() const { return name_.isEmpty() || hocket::json::toString(current()) != saved_; }

void HocketProcessor::setLibraryFolder(const juce::File& dir) {
    library_ = dir;
    refreshSongs();
}

void HocketProcessor::refreshSongs() {
    songs_ = hocket::library::list(library_);
    updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
}

int HocketProcessor::getNumPrograms() { return juce::jmax(1, static_cast<int>(songs_.size())); }

int HocketProcessor::getCurrentProgram() {
    for (std::size_t i = 0; i < songs_.size(); ++i)
        if (songs_[i].name == name_) return static_cast<int>(i);
    return 0;
}

const juce::String HocketProcessor::getProgramName(int i) {
    return i >= 0 && i < static_cast<int>(songs_.size()) ? songs_[static_cast<std::size_t>(i)].name : juce::String();
}

void HocketProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= static_cast<int>(songs_.size()) || songs_[static_cast<std::size_t>(i)].name == name_) return;
    const auto entry = songs_[static_cast<std::size_t>(i)];
    // A host may pick a preset from any thread; the song changes on the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread()) loadSong(entry);
    else juce::MessageManager::callAsync([this, entry] { loadSong(entry); });
}

void HocketProcessor::change(hocket::Song next) { undo_.perform(new SongEdit(*this, doc_, std::move(next))); }

void HocketProcessor::setSeed(std::uint64_t seed) {
    // A new song, untitled; it keeps the host settings (your key and knobs) as they are.
    hocket::Song s = hocket::defaultSong(seed), now = current();
    std::vector<int> keep;
    eachParam(now, [&](const char*, int& v) { keep.push_back(v); });
    std::size_t i = 0;
    eachParam(s, [&](const char*, int& v) { v = keep[i++]; });
    loadSong(s, {});
}

void HocketProcessor::newSong() { setSeed(randomSeed()); }

void HocketProcessor::generate() {
    hocket::Song next = doc_;
    hocket::generate(next);
    change(std::move(next));
}

void HocketProcessor::edit(const std::function<void(hocket::Song&)>& edit) {
    hocket::Song next = doc_;
    edit(next);
    if (editing_) {
        // §18.1: editing a letter, its values go to the letter, and A keeps its own; everything else (groups and voices
        // added, rows, seeds, the form itself) is the song's.
        hocket::Song view = letterView();
        edit(view);
        hocket::keepValues(doc_, next);
        hocket::Song a = next;
        std::size_t i = 0;
        eachParam(a, [this, &i](const char*, int& field) {
            if (i < aHost_.size()) field = aHost_[i];
            ++i;
        });
        for (hocket::FormLetter& l : next.form.letters)
            if (l.name == editing_) l.params = hocket::letterValues(a, view);
    }
    // §12.2: an edit from the screen is a touch: a slot's parameter it moves tells the host so (a host's learn mode
    // listens for that).
    touching_ = true;
    change(std::move(next));
    touching_ = false;
}

// §12.2: the slots' parameters follow the song. Each shows what plays: its group's or voice's value, or the one it
// inherits. A slot nothing answers to keeps whatever the host left in it.
void HocketProcessor::pushSlots() {
    hocket::Song s = current();  // with the host's globals on it, for the values that inherit them
    for (int i = 0; i < kSlotParams; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        juce::RangedAudioParameter* p = slotParams_[k];
        const int held = static_cast<int>(std::lround(p->convertFrom0to1(p->getValue())));
        const std::optional<int> v = slotValue(s, slotRef(i));
        if (!v) {
            if (slotRef(i).info().kind != FieldKind::Trigger || slotShadow_[k] == kUnknown) slotShadow_[k] = held;
            continue;
        }
        if (*v == slotShadow_[k] && slotShadow_[k] != kUnknown) continue;  // the song hasn't moved it; the host's value, if it differs, is pulled
        slotShadow_[k] = *v;
        if (*v == held) continue;
        if (touching_) p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(*v)));
        if (touching_) p->endChangeGesture();
    }
}

// §12.2: a slot's parameter the host moved goes into the song, as its base value (§7.3). A Reroll going on is the
// group's Reroll now or the voice's Reroll. The song is the document itself here, A's values while a letter is edited
// (§18.1). Not an undo step: the host keeps its parameters' undo.
bool HocketProcessor::pullSlots() {
    bool changed = false;
    for (int i = 0; i < kSlotParams; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        juce::RangedAudioParameter* p = slotParams_[k];
        const int held = static_cast<int>(std::lround(p->convertFrom0to1(p->getValue())));
        if (held == slotShadow_[k] || slotShadow_[k] == kUnknown) continue;
        const SlotRef r = slotRef(i);
        const bool on = held != 0 && slotShadow_[k] == 0;
        slotShadow_[k] = held;
        if (r.info().kind == FieldKind::Trigger) {
            if (!on) continue;
            if (r.voice) {
                if (hocket::Voice* v = voiceInSlot(doc_, r.slot)) hocket::rerollNotes(doc_, *v), changed = true;
            } else if (hocket::Group* g = groupInSlot(doc_, r.slot)) {
                hocket::rerollRhythm(doc_, *g), changed = true;
            }
            continue;
        }
        changed = setSlotValue(doc_, r, held) || changed;
    }
    return changed;
}

void HocketProcessor::setOutput(Output o) {
    output_ = static_cast<int>(o);
    openPort();
}

void HocketProcessor::setPort(const juce::String& name) {
    portName_ = name;
    openPort();
}

void HocketProcessor::openPort() {
    std::unique_ptr<juce::MidiOutput> next;
    if (output() != Output::Host && portName_.isNotEmpty())
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == portName_) {
                next = juce::MidiOutput::openDevice(d.identifier);
                break;
            }
    if (next) next->startBackgroundThread();  // sendBlockOfMessages schedules from it, sample-accurately
    const juce::ScopedLock lock(portLock_);
    port_ = std::move(next);
}

void HocketProcessor::devicesChanged() {
    if (output() == Output::Host || portName_.isEmpty()) return;
    juce::String now;  // the chosen device's identifier, if it's plugged in
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == portName_) now = d.identifier;
    bool same = false;
    {
        const juce::ScopedLock lock(portLock_);
        same = port_ != nullptr && now.isNotEmpty() && port_->getIdentifier() == now;
    }
    if (same) return;
    if (now.isEmpty()) {
        // Gone (unplugged): let go of it, so nothing waits on a dead handle.
        std::unique_ptr<juce::MidiOutput> old;
        const juce::ScopedLock lock(portLock_);
        old = std::move(port_);
        return;
    }
    openPort();  // back, or back with a new identifier: open it again
}

void HocketProcessor::timerCallback() {
    // A moved parameter (the editor, host automation, a controller) builds the song again.
    std::vector<float> now;
    for (const char* id : kParamIds) now.push_back(params.getParameter(id)->getValue());
    const bool slots = pullSlots();  // §12.2: a slot's parameter the host moved
    if (slots || now != lastValues_ || hostMeter_.load() != meterApplied_) {
        lastValues_ = now;
        rebuild();
    }
    // §9.2: a jump, Stop or Play let go of Freeze; its button follows.
    if (freezeReleased_.exchange(false))
        if (auto* f = params.getParameter("freeze")) f->setValueNotifyingHost(0.0f);
    std::vector<hocket::Player::ReadyPtr> old;
    old.reserve(kRetired);
    {
        const juce::SpinLock::ScopedLockType lock(swap_);
        old.swap(retired_);  // `retired_` keeps room for kRetired, so the audio thread never allocates to add one
    }
    // `old` is freed here, on the message thread, never on the audio thread.
}

void HocketProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    const int n = buffer.getNumSamples();
    midi.clear();  // MIDI in isn't passed through (§11); Solo select comes later

    // The host's clock (§6.3): where it is, its tempo, its meter and bar lines, and its loop.
    bool playing = false, looping = false;
    double ppq = 0.0, bpm = 120.0, loopStart = 0.0, loopEnd = 0.0;
    juce::Optional<double> barStart;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition()) {
            playing = pos->getIsPlaying();
            if (const auto p = pos->getPpqPosition()) ppq = *p;
            else playing = false;
            if (const auto b = pos->getBpm()) bpm = *b;
            bpm_ = bpm;
            if (const auto t = pos->getTimeSignature()) {
                hostMeter_ = packMeter(t->numerator, t->denominator);
                barStart = pos->getPpqPositionOfLastBarStart();
            } else {
                hostMeter_ = 0;
            }
            if (const auto l = pos->getLoopPoints(); l && pos->getIsLooping() && l->ppqEnd > l->ppqStart) {
                looping = true;
                loopStart = l->ppqStart;
                loopEnd = l->ppqEnd;
            }
        }
    const double ticksPerSample = bpm / 60.0 * hocket::kTicksPerBeat / sampleRate_;
    const double startTick = ppq * hocket::kTicksPerBeat;

    const juce::SpinLock::ScopedTryLockType lock(swap_);
    if (!lock.isLocked() || !player_) return;  // the message thread is handing over a song: skip one block

    // §9.2: Freeze, as its button says. Once a jump has let go of it, the button stays let go until it reads off.
    const bool freezeOn = params.getRawParameterValue("freeze")->load() > 0.5f;
    if (!freezeOn) freezeHeldOff_ = false;
    player_->setFreeze(freezeOn && !freezeHeldOff_);

    // The host's bar lines are the song's (§6.3). With one meter from the top they're the multiples of a bar; after
    // a meter change in the host they're shifted, so the song's bar 0 moves to line up with them (the anchor).
    if (barStart && hostMeter_.load() == playingMeter_) {
        const auto line = static_cast<std::int64_t>(std::llround(*barStart * hocket::kTicksPerBeat));
        anchor_ = ((line % playingBar_) + playingBar_) % playingBar_;
    }
    std::int64_t from = firstTick(startTick) - anchor_;
    std::int64_t to = std::max(from, firstTick(startTick + n * ticksPerSample) - anchor_);
    if (playing && expected_ >= 0 && std::abs(from - expected_) <= kContinuity) from = std::min(expected_, to);

    // Each event's sample: `shift` takes the song's tick back to the host's, along the block's own time.
    std::vector<std::pair<int, hocket::Event>> events;
    auto add = [&](const std::vector<hocket::Event>& es, double shift) {
        for (const auto& e : es) {
            const double at = (static_cast<double>(e.tick) + shift - startTick) / ticksPerSample;
            events.push_back({juce::jlimit(0, juce::jmax(0, n - 1), static_cast<int>(std::lround(at))), e});
        }
    };
    // A span of the song, taking on a waiting song: a live change at once (§9.3: heard from the next step), anything
    // else at the bar line (§7.3; stopped, at once). What's sounding rings out. The song it replaces is the message
    // thread's to free.
    auto retire = [&](hocket::Player::ReadyPtr old) {
        if (old && retired_.size() < retired_.capacity()) retired_.push_back(std::move(old));
    };
    auto run = [&](std::int64_t a, std::int64_t b, double shift) {
        if (incoming_) {
            const bool live = incomingLive_ && playing;
            const std::int64_t bar = incomingBar_;
            const std::int64_t line = live ? a : playing ? (a + bar - 1) / bar * bar : a;
            if (line < b || !playing) {
                if (line > a) add(player_->process(a, line, true), shift);
                retire(player_->take(std::move(incoming_), live));
                playingVersion_ = incomingVersion_;
                playingBar_ = bar;
                playingMeter_ = incomingMeter_;
                add(player_->process(std::max(a, line), b, playing), shift);
                return;
            }
        }
        add(player_->process(a, b, playing), shift);
    };

    // The host's loop (§6.3): a block that runs over the loop's end plays up to it, then on from the loop's start in
    // the same block, so the wrap is on its sample (the jump back ends what's sounding and re-strikes the chords).
    const std::int64_t wrapAt = firstTick(loopEnd * hocket::kTicksPerBeat) - anchor_;
    const std::int64_t back = firstTick(loopStart * hocket::kTicksPerBeat) - anchor_;
    if (playing && looping && from < wrapAt && wrapAt < to && back < wrapAt) {
        run(from, wrapAt, static_cast<double>(anchor_));
        const std::int64_t rest = to - wrapAt;
        run(back, back + rest, static_cast<double>(anchor_ + wrapAt - back));
        from = back;
        to = back + rest;
    } else {
        run(from, to, static_cast<double>(anchor_));
    }
    expected_ = playing ? to : -1;
    tick_ = playing ? from : -1;
    if (player_->takeReleased()) {
        freezeHeldOff_ = true;
        freezeReleased_ = true;
    }
    frozenFrom_ = player_->holdFrom();

    std::vector<juce::MidiMessage> extra;
    if (panic_.exchange(false)) {
        // At the block's last sample, after every note-on in it, so nothing started in this block is left on.
        for (const auto& e : player_->allOff(to)) events.push_back({juce::jmax(0, n - 1), e});
        std::uint32_t channels = 0;
        for (const auto& g : song_.groups)
            for (const auto& v : g.voices) channels |= 1u << (v.channel - 1);
        for (int ch = 1; ch <= 16; ++ch)
            if (channels & (1u << (ch - 1))) extra.push_back(juce::MidiMessage::allNotesOff(ch));
    }

    juce::MidiBuffer forPort;
    const Output out = output();
    for (const auto& m : extra) {
        if (out != Output::Port) midi.addEvent(m, juce::jmax(0, n - 1));
        if (out != Output::Host) forPort.addEvent(m, juce::jmax(0, n - 1));
    }
    for (const auto& [sample, e] : events) {
        const auto msg = e.kind == hocket::Event::On
            ? juce::MidiMessage::noteOn(e.channel, e.note, static_cast<juce::uint8>(e.velocity))
            : juce::MidiMessage::noteOff(e.channel, e.note);
        if (out != Output::Port) midi.addEvent(msg, sample);
        if (out != Output::Host) forPort.addEvent(msg, sample);
    }
    if (!forPort.isEmpty()) {
        const juce::ScopedTryLock portLock(portLock_);
        if (portLock.isLocked() && port_) port_->sendBlockOfMessages(forPort, juce::Time::getMillisecondCounterHiRes(), sampleRate_);
    }
}

void HocketProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.setProperty("engineVersion", hocket::kEngineVersion, nullptr);
    state.setProperty("seed", juce::String::toHexString(static_cast<juce::int64>(doc_.masterSeed)), nullptr);
    state.setProperty("song", hocket::json::toString(doc_), nullptr);
    state.setProperty("output", output_.load(), nullptr);
    state.setProperty("port", portName_, nullptr);
    state.setProperty("name", name_, nullptr);
    state.setProperty("saved", saved_, nullptr);
    if (const auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void HocketProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(params.state.getType())) return;
    // A session from before Colour has no value for it: it plays as it was made, with 0 (not a new song's default).
    // Asked before replaceState, which fills in any parameter the state is missing.
    const bool noColour = !state.getChildWithProperty("id", "colour").isValid();
    // Nor Fill (§4.6): replaceState would keep whatever this instance had; a session from before it had it off.
    const bool noFill = !state.getChildWithProperty("id", "fill").isValid();
    // Nor Mode moves (§3.8): a session from before it plays the harmony it was made with.
    const bool noMoves = !state.getChildWithProperty("id", "moves").isValid();
    params.replaceState(state);
    // §9.2: Freeze is a live input; a session never reloads with it on.
    if (auto* f = params.getParameter("freeze")) f->setValueNotifyingHost(0.0f);
    if (noColour)
        if (auto* c = params.getParameter("colour")) c->setValueNotifyingHost(c->convertTo0to1(0.0f));
    if (noFill)
        if (auto* f = params.getParameter("fill")) f->setValueNotifyingHost(f->convertTo0to1(0.0f));
    if (noMoves)
        if (auto* m = params.getParameter("moves")) m->setValueNotifyingHost(m->convertTo0to1(0.0f));
    // The song document, else (a state from before documents) the default song on the saved seed.
    const juce::String song = state["song"].toString();
    doc_ = song.isNotEmpty() ? hocket::json::fromString(song)
                             : hocket::defaultSong(static_cast<std::uint64_t>(state["seed"].toString().getHexValue64()));
    editing_ = 0;
    aHost_.clear();
    portName_ = state["port"].toString();
    name_ = state["name"].toString();
    saved_ = state["saved"].toString();
    setOutput(static_cast<Output>(static_cast<int>(state["output"])));
    lastValues_.clear();
    // §12.2: the slots' parameters take the document's values (a session from before slots has none of its own).
    slotShadow_.assign(slotShadow_.size(), kUnknown);
    undo_.clearUndoHistory();  // a session loaded: its song is where undo starts
    rebuild();
}

juce::AudioProcessorEditor* HocketProcessor::createEditor() { return new HocketEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HocketProcessor(); }
