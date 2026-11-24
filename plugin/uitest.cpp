// planchette_uitest: the real processor and editor, driven the way a finger would. Each screen's buttons are found by
// their words and pressed; the song must change the way the button says. Proof the screens are wired, not just drawn.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <cstddef>
#include <functional>
#include <vector>

#include "Editor.h"
#include "Processor.h"
#include "SongJson.h"
#include "hocket/generators.hpp"
#include "ui/Controls.h"

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

// The message loop for `ms`, then every live processor's and editor's timer, then the loop again for what that posted.
// The timers are ticked by hand: a slow machine (a CI Mac) can let 150 ms pass without a 20 Hz tick, and a test that
// moves a value and moves it back inside one wait would never see it move.
std::vector<std::function<void()>> tickers;
void settle(int ms) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
    for (auto& t : tickers) t();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}
// Ticks `x` in every settle() for as long as it lives.
struct Ticking {
    template <typename T>
    explicit Ticking(T& x) : at(tickers.size()) { tickers.push_back([&x] { x.tick(); }); }
    ~Ticking() { tickers.erase(tickers.begin() + static_cast<std::ptrdiff_t>(at)); }
    std::size_t at;
};

// The first visible button anywhere under `c` whose words are `text` (visible down the tree: the editor isn't on a
// desktop here, so nothing is "showing").
juce::Button* find(juce::Component& c, const juce::String& text) {
    for (auto* child : c.getChildren()) {
        if (!child->isVisible()) continue;
        if (auto* b = dynamic_cast<juce::Button*>(child))
            if (b->getButtonText() == text) return b;
        if (auto* b = find(*child, text)) return b;
    }
    return nullptr;
}

// A tap: the click, the finger lifting (it ends an undo step), then the editor's refresh.
void press(HocketEditor& e, HocketProcessor& p, const char* text) {
    if (auto* b = find(e, text)) b->triggerClick();
    else std::printf("     (no button \"%s\" showing)\n", text);
    p.endGesture();
    settle(150);
}

template <typename T>
T* findOf(juce::Component& c) {
    for (auto* child : c.getChildren()) {
        if (auto* t = dynamic_cast<T*>(child)) return t;
        if (auto* t = findOf<T>(*child)) return t;
    }
    return nullptr;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const juce::ScopedJuceInitialiser_GUI juce;
    HocketProcessor p;
    p.setSeed(0x0123456789ABCDEFull);
    std::unique_ptr<HocketEditor> e(dynamic_cast<HocketEditor*>(p.createEditor()));
    const Ticking tickP(p), tickE(*e);  // the processor's timer before the editor's, as the song moves then the screens
    e->setVisible(true);
    settle(200);

    // Song: Generate re-rolls, New progression changes the harmony seed, Lock chords locks it.
    e->showPage(3);
    const auto harmony = p.document().harmony.seed;
    press(*e, p, "Generate");
    check(p.document().generateCount == 1, "Generate counts a Generate");
    press(*e, p, "New progression");
    check(p.document().harmony.seed != harmony, "New progression gives the chords a new seed");
    press(*e, p, "Lock chords");
    check(p.document().harmony.locked, "Lock chords locks them");

    // Groups: Add voice, New group, and the new group's Delete.
    e->showPage(1);
    settle(150);
    press(*e, p, "Add voice");
    check(p.document().groups[0].voices.size() == 3, "Add voice gives Leads a third voice");
    check(p.document().groups[0].voices.size() == 3 && p.document().groups[0].voices[2].channel == 5, "on the first free channel (5), not the Chords' 3");
    press(*e, p, "New group");
    const auto& gs = p.document().groups;
    check(gs.size() == 4 && gs[3].voices.size() == 1, "New group: a fourth group, with a voice");
    check(gs.size() == 4 && !gs[3].voices.empty() && gs[3].voices[0].channel == 6, "its voice takes the first free channel (6)");
    press(*e, p, "Lock rhythm");
    check(gs.size() == 4 && gs[3].lockRhythm, "Lock rhythm locks the selected (new) group");
    press(*e, p, "Fill now");
    check(gs.size() == 4 && gs[3].fillNow && !gs[0].fillNow, "Fill now switches the selected group's fill on (§4.6)");
    // A chord voice's Voicing has a second option: Nearest (§3.4a).
    p.edit([](hocket::Song& s) { s.groups[3].voices[0].type = hocket::VoiceType::Chord; });
    p.endGesture();
    settle(150);
    press(*e, p, "Nearest");
    check(gs.size() == 4 && gs[3].voices[0].nearest && !gs[2].voices[0].nearest, "Nearest switches the chord voice to the nearest inversion");
    // Humanize (§4.7): the voice's Feel bars set its velocity and timing feel.
    p.edit([](hocket::Song& s) { s.groups[3].voices[0].type = hocket::VoiceType::Lead; });
    p.endGesture();
    settle(150);
    {
        std::vector<hocket::ui::ValueBar*> feel;
        std::function<void(juce::Component&)> look = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* b = dynamic_cast<hocket::ui::ValueBar*>(child))
                    if (b->isVisible() && (b->getTitle() == "Feel: velocity" || b->getTitle() == "Feel: timing")) feel.push_back(b);
                look(*child);
            }
        };
        look(*e);
        for (auto* b : feel) b->setValue(b->getTitle() == "Feel: velocity" ? 12 : 40, true);
        p.endGesture();
        check(feel.size() == 2 && gs.size() == 4 && gs[3].voices[0].humanVelocity == 12 && gs[3].voices[0].humanTiming == 40,
              "the Feel bars set the voice's humanize");
    }
    // §16.10: the group's rhythm generator is a row of buttons, and the chosen one's knobs appear under it.
    {
        auto titled = [&](const juce::String& t) -> juce::Component* {
            juce::Component* found = nullptr;
            std::function<void(juce::Component&)> look = [&](juce::Component& c) {
                for (auto* child : c.getChildren()) {
                    if (child->isVisible() && child->getTitle() == t && !found) found = child;
                    look(*child);
                }
            };
            look(*e);
            return found;
        };
        auto* rhythm = dynamic_cast<hocket::ui::Segmented*>(titled("Rhythm generator"));
        check(rhythm != nullptr && rhythm->options().contains("Even") && rhythm->options().contains("Metric") && titled("Pulses") == nullptr,
              "the rhythm panel offers Even and Metric, and Random has no knobs");
        if (rhythm) rhythm->setSelected(rhythm->options().indexOf("Even"), true);
        p.endGesture();
        settle(150);
        check(gs.size() == 4 && gs[3].rhythmGen.use == "euclid" && gs[0].rhythmGen.use.empty(), "Even swaps the selected group to Euclidean");
        auto* pulses = dynamic_cast<hocket::ui::ValueBar*>(titled("Pulses"));
        if (pulses) pulses->setValue(7, true);
        p.endGesture();
        check(pulses != nullptr && titled("Rotation") != nullptr && gs.size() == 4 && gs[3].rhythmGen.params.at("euclid").at("pulses") == 7,
              "its Pulses and Rotation show, and Pulses sets the group's");
        if (rhythm) rhythm->setSelected(rhythm->options().indexOf("Random"), true);
        p.endGesture();
        check(gs.size() == 4 && gs[3].rhythmGen.use == "random" && gs[3].rhythmGen.params.at("euclid").at("pulses") == 7,
              "back to Random, and Even's Pulses are kept for later");
        // A line's card has a Notes row (§16.10): Wander, 1/f, Register, Pedal. Pedal's button replaces v0.5's switch.
        auto* notesRow = dynamic_cast<hocket::ui::Segmented*>(titled("Notes"));
        check(notesRow != nullptr && notesRow->options().joinIntoString(",") == "Wander,1/f,Register,Pedal,Kotekan" && find(*e, "Pedal") == nullptr,
              "a line's Notes row offers Wander, 1/f, Register, Pedal and Kotekan (no Pedal switch)");
        if (notesRow) notesRow->setSelected(2, true);
        p.endGesture();
        settle(150);
        auto* lock = dynamic_cast<hocket::ui::ValueBar*>(titled("Lock"));
        check(gs.size() == 4 && gs[3].voices[0].notesGen.use == "register" && lock != nullptr && lock->valueText() == "12",
              "Register: the voice's notes on a register, its Lock showing 12");
        if (lock) lock->setValue(17, true);
        p.endGesture();
        check(lock != nullptr && lock->valueText() == "Invert" && gs[3].voices[0].notesGen.params.at("register").at("lock") == 17,
              "Lock's last notch is Invert");
        notesRow = dynamic_cast<hocket::ui::Segmented*>(titled("Notes"));
        if (notesRow) notesRow->setSelected(3, true);
        p.endGesture();
        check(gs.size() == 4 && gs[3].voices[0].notesGen.use == "pedal", "Pedal puts the line on the tonic");
        // §16.6: Kotekan, its Source chosen from the song's other lines, its Mode a knob.
        notesRow = dynamic_cast<hocket::ui::Segmented*>(titled("Notes"));
        if (notesRow) notesRow->setSelected(notesRow->options().indexOf("Kotekan"), true);
        p.endGesture();
        settle(150);
        auto* source = dynamic_cast<hocket::ui::ValueBar*>(titled("Source"));
        auto* mode = dynamic_cast<hocket::ui::ValueBar*>(titled("Mode"));
        check(gs[3].voices[0].notesGen.use == "kotekan" && source != nullptr && source->valueText() == "none" && mode != nullptr &&
                  mode->valueText() == "Telu",
              "Kotekan: its Source shows none, its Mode Telu");
        if (source) source->setValue(1, true);
        p.endGesture();
        settle(150);
        check(gs[3].voices[0].notesGen.sources.count("kotekan") && gs[3].voices[0].notesGen.sources.at("kotekan") == gs[0].voices[0].id &&
                  hocket::kotekanSource(p.document(), 3, 0).has_value(),
              "its Source picks the first other line (Leads · 1), and the Kotekan is live");
        rhythm = dynamic_cast<hocket::ui::Segmented*>(titled("Rhythm generator"));
        if (rhythm) rhythm->setSelected(rhythm->options().indexOf("Interlock"), true);
        p.endGesture();
        settle(150);
        source = dynamic_cast<hocket::ui::ValueBar*>(titled("Source"));
        if (source) source->setValue(1, true);
        p.endGesture();
        check(gs[3].rhythmGen.use == "interlock" && gs[3].rhythmGen.sources.count("interlock") && gs[3].rhythmGen.sources.at("interlock") == gs[0].id,
              "Interlock: the group's rhythm plays where the Leads rest");
        // §16.6, Resultant: its four knobs, the band's Low and High among them.
        if (rhythm) rhythm->setSelected(rhythm->options().indexOf("Resultant"), true);
        p.endGesture();
        settle(150);
        source = dynamic_cast<hocket::ui::ValueBar*>(titled("Source"));
        if (source) source->setValue(1, true);
        p.endGesture();
        auto* high = dynamic_cast<hocket::ui::ValueBar*>(titled("High"));
        if (high) high->setValue(72, true);
        p.endGesture();
        check(gs[3].rhythmGen.use == "resultant" && gs[3].rhythmGen.sources.at("resultant") == gs[0].id && titled("Low") != nullptr &&
                  titled("Velocity") != nullptr && gs[3].rhythmGen.params.at("resultant").at("high") == 72,
              "Resultant: the Leads' composite in a band, its High set to 72");
        // §5.5, §5.6: the deal's new modes, and Bells' Span and Handoff knobs.
        hocket::ui::Segmented* deal = nullptr;
        std::function<void(juce::Component&)> findDeal = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* sg = dynamic_cast<hocket::ui::Segmented*>(child))
                    if (sg->isVisible() && sg->options().contains("Tutti")) deal = sg;
                findDeal(*child);
            }
        };
        findDeal(*e);
        check(deal != nullptr && deal->options().contains("Alternate") && deal->options().contains("Bells"), "the deal offers Alternate, Bells and Tutti");
        if (deal) deal->setSelected(deal->options().indexOf("Bells"), true);
        p.endGesture();
        settle(150);
        auto* handoff = dynamic_cast<hocket::ui::ValueBar*>(titled("Handoff"));
        check(gs[3].deal == hocket::DealMode::Bells && handoff != nullptr && handoff->valueText() == "10 %", "Bells: the pitch deals, Handoff shows 10 %");
        if (handoff) handoff->setValue(25, true);
        p.endGesture();
        check(gs[3].handoff == 25, "Handoff sets the group's overlap at a handoff");
        if (deal) deal->setSelected(deal->options().indexOf("Tutti"), true);
        p.endGesture();
        settle(150);
        check(gs[3].deal == hocket::DealMode::Tutti && titled("Handoff") == nullptr, "Tutti: every member, and no Handoff");
        press(*e, p, "Clean handoffs");  // §5.7
        // §5.8: Density from voices, the group's switch.
        const bool fvWas = p.document().groups[3].densityFromVoices;
        press(*e, p, "Voice density");
        const bool fvOn = p.document().groups[3].densityFromVoices;
        press(*e, p, "Voice density: on");
        check(!fvWas && fvOn && !p.document().groups[3].densityFromVoices, "Voice density switches Density from voices on, and off again");
        check(gs[3].cleanHandoffs && !gs[0].cleanHandoffs && find(*e, "Clean handoffs: on") != nullptr, "Clean handoffs switches the group's filter on");
    }
    press(*e, p, "Delete");
    check(p.document().groups.size() == 3, "Delete removes it");

    // Changes (§17.8): a source, a cord from it, and the cord deleted; then the rows: Add a change, Duplicate, Delete.
    e->showPage(2);
    settle(150);
    {
        const auto& mod = p.document().mod;
        press(*e, p, "+ LFOs");
        check(mod.sources.size() == 1 && mod.sources[0].kind == "lfo" && mod.sources[0].name == "LFOs", "+ LFOs adds a source, named for its kind");
        check(find(*e, "Patch a cord") != nullptr, "the new source's panel shows");
        press(*e, p, "Patch a cord");
        check(mod.cords.size() == 1 && mod.cords[0].source == mod.sources[0].id && mod.cords[0].output == "lfo1" &&
                  mod.cords[0].dest.key == "density" && mod.cords[0].dest.target == p.document().groups[0].id,
              "Patch a cord: from its first LFO to the first group's density");
        check(hocket::stateAt(p.document(), 8).groups[0].density != p.document().groups[0].density, "the cord moves the density");
        press(*e, p, "Delete");
        check(mod.cords.empty() && mod.sources.size() == 1, "Delete (the cord's panel) removes the cord, not its source");
        press(*e, p, "+ Accumulator");
        press(*e, p, "+ 1/f dice");
        press(*e, p, "+ Energy arc");
        check(mod.sources.size() == 4 && mod.sources[1].kind == "accumulator" && mod.sources[2].kind == "dice" && mod.sources[3].kind == "arc",
              "+ Accumulator, + 1/f dice, + Energy arc");
        press(*e, p, "Patch a cord");  // from the arc (selected): its level, to the first group's density
        check(mod.cords.size() == 1 && mod.cords[0].source == mod.sources[3].id && mod.cords[0].output == "level", "a cord from the arc's level");
        press(*e, p, "Delete");
        check(mod.cords.empty(), "and Delete removes it");
    }
    press(*e, p, "Add a change");
    const auto& m = p.document().matrix;
    check(m.size() == 2 && m[1].param == hocket::Param::VaryPitch, "Add a change: a second row, a turnaround");
    {
        // §17.5: the row's condition, A:B: a new one starts as every other time.
        hocket::ui::Segmented* condition = nullptr;
        std::function<void(juce::Component&)> look = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (child->isVisible() && child->getTitle() == "Condition" && !condition) condition = dynamic_cast<hocket::ui::Segmented*>(child);
                look(*child);
            }
        };
        look(*e);
        if (condition) condition->setSelected(1, true);
        p.endGesture();
        settle(150);
        check(condition != nullptr && m[1].cond.kind == hocket::Condition::Kind::AB && m[1].cond.a == 1 && m[1].cond.b == 2,
              "a row's condition: A:B starts as 1:2, every other time");
    }
    press(*e, p, "Duplicate");
    check(m.size() == 3 && m[2].id != m[1].id && m[2].param == m[1].param, "Duplicate copies it, with its own id");
    press(*e, p, "Delete");
    check(p.document().matrix.size() == 2, "Delete removes it");

    // Play: a group's Mute.
    e->showPage(0);
    settle(150);
    press(*e, p, "Mute");
    check(!p.document().groups.empty() && p.document().groups[0].mute, "Mute mutes the first group");
    // §9.2: Freeze is the host's "freeze" parameter, on and off again; it's never part of the song.
    {
        auto* f = p.params.getParameter("freeze");
        const hocket::Song before = p.document();
        press(*e, p, "Freeze");
        check(f && f->getValue() > 0.5f, "Freeze switches the freeze parameter on");
        press(*e, p, "Freeze");
        check(f && f->getValue() < 0.5f, "a second tap switches it off");
        check(hocket::json::toString(p.document()) == hocket::json::toString(before), "Freeze leaves the song alone");
    }

    // Undo and Redo: a step back through Mute and the row's Delete, and forward again.
    settle(150);  // the editor enables Undo
    press(*e, p, "Undo");
    check(!p.document().groups[0].mute, "Undo unmutes");
    press(*e, p, "Undo");
    check(p.document().matrix.size() == 3, "Undo again brings the deleted row back");
    press(*e, p, "Redo");
    press(*e, p, "Redo");
    check(p.document().matrix.size() == 2 && p.document().groups[0].mute, "Redo twice: deleted and muted again");
    // A drag is one step, however many values it passes through.
    const int range = p.document().range;
    p.edit([](hocket::Song& s) { s.range = 3; });
    p.edit([](hocket::Song& s) { s.range = 4; });
    p.edit([](hocket::Song& s) { s.range = 5; });
    p.endGesture();
    p.undoManager().undo();
    check(p.document().range == range, "a gesture's three edits undo as one");

    // The clip: 8 bars from bar 1 (stopped), a MIDI file with the song's notes and its tempo.
    if (auto* clip = findOf<hocket::ui::ClipButton>(*e)) {
        const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hocket-uitest");
        const juce::File f = clip->write(dir);
        juce::MidiFile mf;
        juce::FileInputStream in(f);
        const bool read = in.openedOk() && mf.readFrom(in);
        int ons = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (const auto* ev : *mf.getTrack(t)) ons += ev->message.isNoteOn();
        check(read && f.getFileName().endsWith("bars 1-8.mid"), "the clip is written: bars 1-8");
        check(read && mf.getNumTracks() >= 3 && ons > 50, "with a track per group and their notes");
        dir.deleteRecursively();
    } else {
        check(false, "the clip button is there");
    }

    // The actual legacy menu's actions route both directories without touching the originals.
    {
        const auto bench = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("planchette-clips", "", false);
        const auto docs = bench.getChildFile("Documents"), data = bench.getChildFile("AppData");
        std::vector<juce::File> opened;
        auto browse = [&](const juce::File& dir) { opened.push_back(dir); };
        check(hocket::ui::legacyClipMenu(docs, data, browse).getNumItems() == 0, "no older menu for absent folders");
        const auto clips = docs.getChildFile("Hocket/Clips"), outgoing = data.getChildFile("Hocket/Outgoing");
        clips.createDirectory();
        outgoing.createDirectory();
        const auto oldClip = clips.getChildFile("old.mid"), oldOut = outgoing.getChildFile("old.mid");
        oldClip.replaceWithText("old clip");
        oldOut.replaceWithText("old outgoing");
        auto menu = hocket::ui::legacyClipMenu(docs, data, browse);
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) if (it.getItem().action) it.getItem().action();
        check(opened == std::vector<juce::File>{clips, outgoing}, "older clip actions open both original directories");
        check(oldClip.loadFileAsString() == "old clip" && oldOut.loadFileAsString() == "old outgoing" &&
              !docs.getChildFile("Planchette").exists() && !data.getChildFile("Planchette").exists(),
              "browsing does not move, overwrite or delete old clips");
        bench.deleteRecursively();
    }

    // The song library: starters as the host's presets, save, load (with the host settings), undo a load, delete.
    {
        namespace lib = hocket::library;
        const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hocket-uitest-songs");
        dir.deleteRecursively();
        p.setLibraryFolder(dir);
        auto program = [&](const juce::String& name) {
            for (int i = 0; i < p.getNumPrograms(); ++i)
                if (p.getProgramName(i) == name) return i;
            return -1;
        };
        check(p.getNumPrograms() == lib::numStarters() && program("Slow chords, busy bass") >= 0 && program("There and back") >= 0,
              "the starters are the host's presets");
        p.setCurrentProgram(program("Seven eight"));
        check(p.songName() == "Seven eight" && p.document().barLength == 7 && p.document().beatUnit == 8 && p.current().root == 7,
              "a preset loads its song and its key (Seven eight: 7/8 in G)");
        check(!p.edited(), "and isn't edited");
        p.edit([](hocket::Song& s) { s.groups[0].density = 3; });
        p.endGesture();
        check(p.edited(), "an edit marks it edited");
        check(p.saveSong("Test song") && dir.getChildFile("Test song.planchette").existsAsFile(), "Save as writes Test song.planchette");
        check(p.getNumPrograms() == lib::numStarters() + 1 && p.getProgramName(lib::numStarters()) == "Test song" && !p.edited(),
              "it joins the presets, saved");
        auto* root = p.params.getParameter("root");
        root->setValueNotifyingHost(root->convertTo0to1(2.0f));
        check(p.current().root == 2 && p.edited(), "moving Root marks it edited");
        check(p.loadSong(p.songs().back()) && p.current().root == 7 && p.document().groups[0].density == 3 && !p.edited(),
              "loading it back brings back its Root and its edit");
        p.undoManager().undo();
        check(p.current().root == 2, "Undo of a load puts the settings back");
        p.newSong();
        check(p.songName().isEmpty() && p.current().root == 2, "New song: untitled, and keeps the key");
        check(p.deleteSong(p.songs().back()) && !dir.getChildFile("Test song.planchette").exists() && p.getNumPrograms() == lib::numStarters(),
              "Delete removes the file and the preset");
        bool allPlay = true;
        for (int i = 0; i < lib::numStarters(); ++i) {
            const hocket::Song s = lib::starter(i);
            allPlay = allPlay && hocket::notes(s, 0, hocket::barTicks(s) * 8).size() > 40;
        }
        check(allPlay, "every starter song plays");
        dir.deleteRecursively();
    }

    // And the song it all made still saves and plays.
    juce::MemoryBlock state;
    p.getStateInformation(state);
    check(state.getSize() > 500, "the edited song saves");
    // A session saved and opened again later: a fresh instance given that state is the same Planchette.
    {
        auto* root = p.params.getParameter("root");
        root->setValueNotifyingHost(root->convertTo0to1(5.0f));  // a knob moved after the last save
        p.setOutput(HocketProcessor::Output::Both);
        p.setPort("Some synth");
        juce::MemoryBlock session;
        p.getStateInformation(session);
        HocketProcessor reopened;
        const Ticking tick_reopened(reopened);
        reopened.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
        const hocket::Song a = p.current(), b = reopened.current();
        const std::int64_t end = hocket::barTicks(a) * 32;
        check(hocket::json::toString(a) == hocket::json::toString(b), "reopened: the same song, knobs included");
        check(hocket::notes(a, 0, end) == hocket::notes(b, 0, end), "and it plays the same notes");
        check(reopened.output() == HocketProcessor::Output::Both && reopened.portName() == "Some synth", "the same MIDI out and port");
        check(reopened.songName() == p.songName() && reopened.edited() == p.edited(), "the same name and edited mark");

        // A session saved before Colour existed (no Colour in its settings or its song) opens with Colour off, so it
        // plays as it was made, not with a new song's default.
        auto xml = juce::AudioProcessor::getXmlFromBinary(session.getData(), static_cast<int>(session.getSize()));
        for (auto* e = xml->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->getStringAttribute("id") == "colour") {
                xml->removeChildElement(e, true);
                break;
            }
        auto song = juce::JSON::parse(xml->getStringAttribute("song"));
        song.getDynamicObject()->removeProperty("colour");
        xml->setAttribute("song", juce::JSON::toString(song));
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary(*xml, old);
        HocketProcessor before;
        const Ticking tick_before(before);
        before.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        check(p.current().colour == 12 && before.current().colour == 0, "a session from before Colour opens with it off");

        // And one from before Fill (§4.6), opened in an instance whose Fill is on: it opens with Fill off.
        auto* fillParam = p.params.getParameter("fill");
        fillParam->setValueNotifyingHost(fillParam->convertTo0to1(1.0f));
        juce::MemoryBlock withFill;
        p.getStateInformation(withFill);
        auto noFill = juce::AudioProcessor::getXmlFromBinary(withFill.getData(), static_cast<int>(withFill.getSize()));
        for (auto* e = noFill->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->getStringAttribute("id") == "fill") {
                noFill->removeChildElement(e, true);
                break;
            }
        juce::MemoryBlock oldFill;
        juce::AudioProcessor::copyXmlToBinary(*noFill, oldFill);
        check(p.current().fill == 1, "Fill is a host parameter, on");
        p.setStateInformation(oldFill.getData(), static_cast<int>(oldFill.getSize()));
        check(p.current().fill == 0, "a session from before Fill opens with it off");

        // Mode moves (§3.8): the Song screen's switch is the host parameter; a session from before it opens with it off.
        e->showPage(3);
        settle(150);
        hocket::ui::ParamChoice* moves = nullptr;
        std::function<void(juce::Component&)> look = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* pc = dynamic_cast<hocket::ui::ParamChoice*>(child))
                    if (pc->getTitle() == "Mode moves" && pc->isVisible()) moves = pc;
                look(*child);
            }
        };
        look(*e);
        check(moves != nullptr && p.current().modeMoves == 0, "Mode moves is on the Song screen, off");
        if (moves) moves->setSelected(1, true);
        check(p.current().modeMoves == 1, "its switch turns Mode moves on");
        juce::MemoryBlock withMoves;
        p.getStateInformation(withMoves);
        auto noMoves = juce::AudioProcessor::getXmlFromBinary(withMoves.getData(), static_cast<int>(withMoves.getSize()));
        for (auto* x = noMoves->getFirstChildElement(); x != nullptr; x = x->getNextElement())
            if (x->getStringAttribute("id") == "moves") {
                noMoves->removeChildElement(x, true);
                break;
            }
        juce::MemoryBlock oldMoves;
        juce::AudioProcessor::copyXmlToBinary(*noMoves, oldMoves);
        p.setStateInformation(oldMoves.getData(), static_cast<int>(oldMoves.getSize()));
        check(p.current().modeMoves == 0, "a session from before Mode moves opens with it off");
    }
    // §18.1, the Form screen: a letter added joins the string; editing it sets its values, A keeps its own.
    {
        e->showPage(4);
        settle(150);
        hocket::ui::Segmented* on = nullptr;
        std::function<void(juce::Component&)> look = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* sg = dynamic_cast<hocket::ui::Segmented*>(child))
                    if (sg->getTitle() == "Form" && sg->isVisible()) on = sg;
                look(*child);
            }
        };
        look(*e);
        if (on) on->setSelected(1, true);
        p.endGesture();
        settle(150);
        check(p.document().form.on, "the Form screen's switch plays the form");
        press(*e, p, "+ Letter");
        check(p.document().form.letters.size() == 1 && p.document().form.letters[0].name == 'B' && p.document().form.text == "A B",
              "+ Letter adds B, and B joins the string");
        const int aDensity = p.document().groups[0].density;
        press(*e, p, "Edit B");
        check(p.editingLetter() == 'B', "Edit B: the screens edit B");
        p.edit([](hocket::Song& s) { s.groups[0].density = 3; });  // as the Groups screen's Density bar does
        p.endGesture();
        settle(150);
        const auto& b = p.document();
        check(b.groups[0].density == 3, "editing B, the screens show B's value");
        bool set = false;
        const hocket::Song now = p.current();  // a copy that lives through the loop
        for (const auto& [d, v] : now.form.letters[0].params) set = set || (d.key == "density" && v == 3);
        check(set && now.groups[0].density == aDensity, "B keeps the value; A keeps its own");
        press(*e, p, "Back to A");
        check(p.editingLetter() == 0 && p.document().groups[0].density == aDensity, "Back to A: the screens show A again");
        press(*e, p, "Delete B");
        check(p.document().form.letters.empty(), "Delete B");
        // §18.3: a group's irama on the Form screen.
        hocket::ui::Segmented* ir = nullptr;
        const juce::String want = "Irama " + juce::String(p.document().groups[0].name);
        std::function<void(juce::Component&)> findIrama = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* sg = dynamic_cast<hocket::ui::Segmented*>(child))
                    if (sg->getTitle() == want && sg->isVisible()) ir = sg;
                findIrama(*child);
            }
        };
        findIrama(*e);
        if (ir) ir->setSelected(4, true);
        p.endGesture();
        settle(150);
        check(ir != nullptr && p.document().groups[0].irama == 4, "the Form screen sets a group's irama (8 a chord)");
        // §18.4: a group's tihai, every gong.
        hocket::ui::Segmented* th = nullptr;
        const juce::String wantT = "Tihai " + juce::String(p.document().groups[0].name);
        std::function<void(juce::Component&)> findTihai = [&](juce::Component& c) {
            for (auto* child : c.getChildren()) {
                if (auto* sg = dynamic_cast<hocket::ui::Segmented*>(child))
                    if (sg->getTitle() == wantT && sg->isVisible()) th = sg;
                findTihai(*child);
            }
        };
        findTihai(*e);
        if (th) th->setSelected(4, true);
        p.endGesture();
        settle(150);
        check(th != nullptr && p.document().groups[0].tihai == 1 && p.document().groups[0].tihaiUnit == 1, "the Form screen lands a tihai on every gong");
    }


    // §12.2: the automation slots. A fixed list of parameters for a song's groups and voices: each group and voice
    // answers to one slot, its parameters show what plays, and the song and the host move each other.
    {
        HocketProcessor q;
        const Ticking tick_q(q);
        q.setSeed(0x0123456789ABCDEFull);
        auto pump = [] { settle(150); };
        auto value = [&](const char* id) {
            auto* x = q.params.getParameter(id);
            return x ? static_cast<int>(std::lround(x->convertFrom0to1(x->getValue()))) : -999;
        };
        auto host = [&](const char* id, int v) {  // the host (automation, a mapped controller) moves a parameter
            auto* x = q.params.getParameter(id);
            if (x) x->setValueNotifyingHost(x->convertTo0to1(static_cast<float>(v)));
            pump();
        };
        // Which parameters the plugin says were touched (what a host's learn mode listens for).
        struct Touches : juce::AudioProcessorListener {
            void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
            void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor* a, int index) override {
                ids.add(dynamic_cast<juce::AudioProcessorParameterWithID*>(a->getParameters()[index])->paramID);
            }
            juce::StringArray ids;
        } touches;
        q.addListener(&touches);
        pump();
        {
            const hocket::Song& d = q.document();
            check(d.groups.size() >= 3 && d.groups[0].slot == 1 && d.groups[1].slot == 2 && d.groups[2].slot == 3 && d.groups[0].voices[0].slot == 1 &&
                      d.groups[0].voices[1].slot == 2 && d.groups[1].voices[0].slot == 3,
                  "groups and voices take the first free slots, in order");
            check(value("g1_density") == d.groups[0].density && value("g2_density") == d.groups[1].density && value("g3_steps") == d.groups[2].steps &&
                      value("v1_weight") == d.groups[0].voices[0].weight && value("v4_chordSize") == d.groups[2].voices[0].chordSize,
                  "a slot's parameters show its group's and its voice's values");
            check(touches.ids.isEmpty(), "and an untouched instance has moved none of them");
        }
        host("g1_density", 3);
        check(q.document().groups[0].density == 3 && q.song().groups[0].density == 3, "the host moves Group 1 Density: the song follows");
        q.edit([](hocket::Song& s) { s.groups[0].density = 12; });
        q.endGesture();
        pump();
        check(value("g1_density") == 12, "an edit on the screen moves the parameter");
        check(touches.ids.contains("g1_density") && touches.ids.size() == 1, "and tells the host it was touched (and only it)");
        // A value that inherits (§1.2): the parameter shows what plays, follows the level above, and moved, is the group's own.
        check(!q.document().groups[0].overrides.range && value("g1_range") == q.current().range, "Group 1 Range shows the song's Range it inherits");
        host("range", 5);
        check(value("g1_range") == 5 && value("v1_range") == 5, "and follows it");
        host("g1_range", 11);
        check(q.document().groups[0].overrides.range == std::optional<int>(11) && value("v1_range") == 11, "moved, it is the group's own; its voices follow");
        host("range", 6);
        check(value("g1_range") == 11, "and the song's Range no longer moves it");
        q.edit([](hocket::Song& s) { s.groups[0].overrides.range.reset(); });
        q.endGesture();
        pump();
        check(value("g1_range") == 6, "the override cleared on the screen: the parameter is the song's again");
        // A voice's: its own Accent, and its Density under Density from voices (§5.8).
        host("v2_accent", 100);
        check(q.document().groups[0].voices[1].overrides.accent == std::optional<int>(100), "Voice 2 Accent is the voice's own");
        q.edit([](hocket::Song& s) { s.groups[0].densityFromVoices = true; });
        q.endGesture();
        host("v2_density", 0);
        {
            const std::uint64_t second = q.document().groups[0].voices[1].id;
            int played = 0;
            for (const hocket::Note& n : hocket::notes(q.song(), 0, hocket::barTicks(q.song()) * 4)) played += n.voice == second;
            check(q.document().groups[0].voices[1].density == 0 && played == 0, "Voice 2 Density at 0: the voice sits out");
        }
        host("g1_solo", 9);
        check(q.document().groups[0].soloMember == 1 && value("g1_solo") == 2, "Solo member past the last voice lands on the last, and the parameter says so");
        host("g2_mute", 1);
        check(q.document().groups[1].mute, "Group 2 Mute mutes it");
        // Reroll: a button. Going on is the group's Reroll now, or the voice's Reroll; coming off does nothing.
        const std::uint64_t rhythm = q.document().groups[0].rhythmSeed, pitch = q.document().groups[0].voices[0].pitchSeed;
        host("g1_reroll", 1);
        const std::uint64_t rhythm2 = q.document().groups[0].rhythmSeed;
        host("g1_reroll", 0);
        host("v1_reroll", 1);
        check(rhythm2 != rhythm && q.document().groups[0].rhythmSeed == rhythm2 && q.document().groups[0].voices[0].pitchSeed != pitch,
              "Reroll going on rerolls the group's rhythm, or the voice's notes; coming off does nothing");
        // A slot nothing answers to: moving it changes nothing.
        const juce::String before = hocket::json::toString(q.document());
        host("g8_density", 2);
        host("v16_weight", 0);
        check(hocket::json::toString(q.document()) == before, "a slot no group or voice answers to does nothing");
        // A session: the same values come back, in the song and in the slots.
        juce::MemoryBlock session;
        q.getStateInformation(session);
        HocketProcessor again;
        const Ticking tick_again(again);
        again.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
        pump();
        auto valueOf = [](HocketProcessor& x, const char* id) {
            auto* a = x.params.getParameter(id);
            return static_cast<int>(std::lround(a->convertFrom0to1(a->getValue())));
        };
        check(hocket::json::toString(again.document()) == hocket::json::toString(q.document()) && valueOf(again, "g1_density") == 12 &&
                  valueOf(again, "g1_range") == 6 && valueOf(again, "v2_density") == 0,
              "a session reopens with the song and its slots as they were");
        // A session from before the slots has no values for them: the song's are kept, and the parameters take them.
        auto xml = juce::AudioProcessor::getXmlFromBinary(session.getData(), static_cast<int>(session.getSize()));
        for (auto* x = xml->getFirstChildElement(); x != nullptr;) {
            auto* next = x->getNextElement();
            const juce::String id = x->getStringAttribute("id");
            if ((id.startsWith("g") || id.startsWith("v")) && id.containsChar('_')) xml->removeChildElement(x, true);
            x = next;
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary(*xml, old);
        HocketProcessor older;
        const Ticking tick_older(older);
        older.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        pump();
        check(hocket::json::toString(older.document()) == hocket::json::toString(q.document()) && valueOf(older, "g1_density") == 12 &&
                  valueOf(older, "v2_density") == 0,
              "a session from before the slots keeps its song, and the slots take its values");
        q.removeListener(&touches);
    }


    // §9.3: live changes. Under a running host, a live value the host moves is heard from the next step; a value that
    // moves a clock waits for the bar line.
    {
        struct Head : juce::AudioPlayHead {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override {
                PositionInfo p;
                p.setPpqPosition(ppq);
                p.setBpm(120.0);
                p.setIsPlaying(true);
                return p;
            }
        };
        HocketProcessor q;
        const Ticking tick_q(q);
        q.setSeed(0x0123456789ABCDEFull);
        q.edit([](hocket::Song& x) {
            x.matrix.clear();
            x.mod = hocket::Mod();
            for (hocket::Group& g : x.groups) g.swing = 0, g.jitter = 0, g.density = 3;
        });
        q.endGesture();
        auto pump = [] { settle(150); };
        pump();
        Head head;
        q.setPlayHead(&head);
        q.prepareToPlay(48000.0, 256);
        const double ticksPerSample = 2.0 * hocket::kTicksPerBeat / 48000.0;  // 120 bpm
        const std::int64_t step = hocket::kTicksPerBeat / 4, bar = hocket::barTicks(q.song());
        std::int64_t sample = 0;
        // The leads' note-ons (channels 1 and 2), in order, with the tick each fell on.
        std::vector<std::pair<std::int64_t, std::pair<int, int>>> heard;
        auto play = [&](std::int64_t untilTick) {
            juce::AudioBuffer<float> audio(2, 256);
            while (static_cast<double>(sample) * ticksPerSample < static_cast<double>(untilTick)) {
                head.ppq = static_cast<double>(sample) * ticksPerSample / hocket::kTicksPerBeat;
                juce::MidiBuffer midi;
                q.processBlock(audio, midi);
                for (const auto meta : midi) {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn() && m.getChannel() <= 2)
                        heard.push_back({static_cast<std::int64_t>(std::llround((sample + meta.samplePosition) * ticksPerSample)), {m.getChannel(), m.getNoteNumber()}});
                }
                sample += 256;
            }
        };
        auto heardIn = [&](std::int64_t from, std::int64_t to) {
            std::vector<std::pair<int, int>> out;
            for (const auto& h : heard)
                if (h.first >= from && h.first < to) out.push_back(h.second);
            return out;
        };
        auto songIn = [&](const hocket::Song& x, std::int64_t from, std::int64_t to) {
            std::vector<std::pair<int, int>> out;
            for (const hocket::Note& n : hocket::notes(x, from, to))
                if (n.channel <= 2) out.push_back({n.channel, n.note});
            return out;
        };
        auto host = [&](const char* id, int v) {
            auto* x = q.params.getParameter(id);
            if (x) x->setValueNotifyingHost(x->convertTo0to1(static_cast<float>(v)));
            pump();
        };
        const std::int64_t half = step / 2;
        // Bars 0 and 1 and six steps of bar 2 as the song is; then Group 1 Density goes to 16, mid-bar.
        const hocket::Song sparse = q.song();
        const std::int64_t turn = 2 * bar + 6 * step + half;
        play(turn);
        host("g1_density", 16);
        const hocket::Song busy = q.song();
        play(3 * bar + 6 * step + half);
        check(heardIn(0, turn) == songIn(sparse, 0, turn) && songIn(sparse, 0, turn).size() < 30, "before the turn: the song as it was");
        check(heardIn(turn, 3 * bar - half) == songIn(busy, turn, 3 * bar - half) && songIn(busy, turn, 3 * bar - half).size() == 9,
              "Group 1 Density turned up mid-bar: every step from the next one plays, in the same bar");
        // Then its Steps go to 5, mid-bar 3: a clock, so the rest of the bar is as it was, and bar 4 is the new song.
        const std::int64_t clock = 3 * bar + 6 * step + half;
        host("g1_steps", 5);
        const hocket::Song five = q.song();
        play(5 * bar);
        check(heardIn(clock, 4 * bar - half) == songIn(busy, clock, 4 * bar - half) && songIn(busy, clock, 4 * bar - half) != songIn(five, clock, 4 * bar - half),
              "Group 1 Steps changed mid-bar: the bar plays on as it was");
        check(heardIn(4 * bar - half, 5 * bar - half) == songIn(five, 4 * bar - half, 5 * bar - half), "and the next bar line takes it");
        q.setPlayHead(nullptr);
    }

    p.editorBeingDeleted(e.get());
    e.reset();
    std::printf("%s\n", failures ? "UI test failed" : "UI test passed");
    return failures ? 1 : 0;
}
