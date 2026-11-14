// The plugin's processor (SPEC §6): the host's clock in, the engine's Player in the middle, MIDI out to the host,
// to a port, or both. It owns no music of its own; everything it plays is the engine's.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "Library.h"
#include "hocket/engine.hpp"
#include "hocket/form.hpp"

// The plugin build says which it is; the test apps that build the processor in are the instrument.
#ifndef JucePlugin_IsMidiEffect
#define JucePlugin_IsMidiEffect 0
#endif

class HocketProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    enum class Output { Host = 0, Port = 1, Both = 2 };

    HocketProcessor();
    ~HocketProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;  // the double-precision one stays JUCE's
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Planchette"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The host's presets are the song library: the starter songs, then yours (as of when the plugin loaded or the
    // last save; some hosts read the list once).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // The host-automatable settings (SPEC §10, §12.2): Root, Scale, the chords, the lines.
    juce::AudioProcessorValueTreeState params;

    // The song (message thread). The document is what's saved: groups, voices, rows, seeds and locks. The song that
    // plays is the document with the host parameters on it. Any change builds it again; the audio thread takes it on
    // the next bar line (SPEC §7.3: changes apply on the bar line), letting what's sounding ring out.
    std::uint64_t seed() const { return doc_.masterSeed; }
    void setSeed(std::uint64_t seed);  // a new song: the default song (§13) on this master seed
    void newSong();                    // the same, on a random seed
    void generate();                   // §2.2: new seeds for everything not locked
    void edit(const std::function<void(hocket::Song&)>& change);

    // Undo and Redo for the song: every edit above is a step. A touch, from finger down to finger up, is one step
    // however many values it moves (the editor calls endGesture whenever a finger lifts). The host's parameters
    // aren't in it: a host keeps their undo itself.
    juce::UndoManager& undoManager() { return undo_; }
    void endGesture() { undo_.beginNewTransaction(); }

    // The host's tempo, as of the last block (for a clip's tempo).
    double hostBpm() const { return bpm_.load(); }
    // What the screens show and edit: the document, or while a letter is being edited (§18.1) A with that letter on it.
    const hocket::Song& document() const { return editing_ ? display_ : doc_; }
    // §18.1: which letter the screens edit (0: A, the song itself). Editing one, its values are what you set, the
    // host's knobs show and move it, and it plays on its own; A keeps its own values.
    char editingLetter() const { return editing_; }
    void editLetter(char name);
    // The song as it would be saved: the document with the host settings on it (not the host's meter).
    hocket::Song current() const;

    // Songs with names (Library.h). Loading takes the host settings with it, and undoes as one step.
    void loadSong(const hocket::Song& song, const juce::String& name);
    bool loadSong(const hocket::library::Entry& e);
    bool saveSong(const juce::String& name);  // into the library folder, as <name>.planchette
    bool deleteSong(const hocket::library::Entry& e);
    const std::vector<hocket::library::Entry>& songs() const { return songs_; }
    void refreshSongs();
    juce::String songName() const { return name_; }  // empty: not saved yet
    bool edited() const;                              // changed since it was loaded or saved (or never saved)
    juce::File libraryFolder() const { return library_; }
    void setLibraryFolder(const juce::File& dir);  // for tests
    const hocket::Song& song() const { return song_; }
    int songVersion() const { return version_; }

    Output output() const { return static_cast<Output>(output_.load()); }
    void setOutput(Output o);
    juce::String portName() const { return portName_; }
    void setPort(const juce::String& name);  // a MIDI output device's name (§6.4); empty: none
    // Whether the chosen port is open now (it may be unplugged; it reopens by itself when it's back).
    bool portConnected() const {
        const juce::ScopedLock lock(portLock_);
        return port_ != nullptr;
    }

    // §11's Panic: every sounding note off, and all-notes-off (CC 123) on every channel in use. At the next block.
    void panic() { panic_ = true; }
    // §9.2: the bar line Freeze holds from, or −1 when it isn't holding (the "freeze" parameter is its button).
    std::int64_t frozenFrom() const { return frozenFrom_.load(); }

    // Where the host is, for the editor: the song's tick while playing, −1 when stopped.
    std::int64_t playheadTick() const { return tick_.load(); }
    // The host's meter, when it reports one: {beats, unit}; {0, 0} when it doesn't (the song's own meter plays).
    std::pair<int, int> hostMeter() const {
        const int m = hostMeter_.load();
        return {m / 64, m % 64};
    }
    static int packMeter(int beats, int unit);

private:
    void timerCallback() override;  // rebuilds the song when a parameter moves; frees what the audio thread let go
    void rebuild();
    void openPort();
    void devicesChanged();  // a MIDI device came or went: reopen the chosen port if it's back, let go if it's gone
    void change(hocket::Song next);  // the document becomes `next`, as one undoable step
    void setParams(const hocket::Song& song);  // the host settings from a song
    // §12.2, the automation slots: their parameters follow the song (push), and one the host moves goes into it (pull).
    void pushSlots();
    bool pullSlots();
    hocket::Song letterView() const;
    struct SongEdit;
    struct SongLoad;

    juce::File library_;
    std::vector<hocket::library::Entry> songs_;
    juce::String name_, saved_;  // the song's name, and its JSON as loaded or saved

    juce::UndoManager undo_{0, 200};  // the last 200 steps

    hocket::Song doc_, song_;
    char editing_ = 0;          // §18.1: the letter the screens edit, or 0 for A
    std::vector<int> aHost_;    // A's host values while a letter is edited (eachParam's order)
    hocket::Song display_;      // the letter as the screens show it
    int version_ = 0;
    std::vector<float> lastValues_;
    static constexpr std::size_t kRetired = 16;    // songs the audio thread can let go of between two timer ticks
    static constexpr int kUnknown = -100000;       // a slot's value not yet taken from the song
    std::vector<juce::RangedAudioParameter*> slotParams_;
    std::vector<int> slotShadow_;                  // each slot parameter's value as last agreed with the song
    bool touching_ = false;                        // an edit from the screen is under way (§12.2: it's a touch)

    // The audio thread's player, and a song waiting for the next bar line (handed over under `swap_`).
    juce::SpinLock swap_;
    std::unique_ptr<hocket::Player> player_;
    // The waiting song is made ready on the message thread (§9.3: its state at the playing bar worked out), so the
    // audio thread only swaps it in: at once when the change is live, else on the bar line. What it replaces goes to
    // `retired_`, which the message thread frees.
    hocket::Player::ReadyPtr incoming_;
    std::vector<hocket::Player::ReadyPtr> retired_;
    bool incomingLive_ = false;             // under swap_: the waiting song differs from the playing one in live values only
    int incomingVersion_ = 0;               // under swap_
    std::int64_t incomingBar_ = 3840;       // under swap_: the waiting song's bar, in ticks, and its meter
    int incomingMeter_ = 0;
    std::atomic<int> playingVersion_{0};    // the handover the audio thread last took
    // The message thread's: the songs it handed over, by their live key (the song's JSON with every live value
    // neutral, §9.3): the one known to be playing, and the one waiting.
    struct Handed {
        int version = 0;
        juce::String key;
    };
    Handed played_, pending_;
    int handVersion_ = 0;

    std::atomic<int> output_{static_cast<int>(Output::Host)};
    juce::String portName_;
    juce::CriticalSection portLock_;
    std::unique_ptr<juce::MidiOutput> port_;
    juce::MidiDeviceListConnection devices_;

    double sampleRate_ = 48000.0;
    std::int64_t expected_ = -1;  // where the last block ended, while playing
    std::atomic<std::int64_t> tick_{-1};
    std::atomic<int> hostMeter_{0};
    std::atomic<double> bpm_{120.0};  // packMeter's; set by the audio thread
    int meterApplied_ = 0;           // the host meter the song was last built with (message thread)
    // The audio thread's: the playing song's bar and meter, and where the host's bar lines put its bar 0.
    std::int64_t playingBar_ = 3840, anchor_ = 0;
    int playingMeter_ = 0;
    std::atomic<bool> panic_{false};
    std::atomic<std::int64_t> frozenFrom_{-1};
    std::atomic<bool> freezeReleased_{false};  // a jump let go of Freeze: the message thread switches its button off
    bool freezeHeldOff_ = false;               // the audio thread's: let go, until the button reads off

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HocketProcessor)
};
