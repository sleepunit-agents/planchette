// planchette_porttest: MIDI out to a port that comes and goes. A virtual device stands in for a synth being plugged in:
// Planchette must connect when it appears, let go when it's unplugged, reconnect when it's back, and deliver notes to it.
// Where virtual devices can't be made (Windows; a machine without a MIDI system) it says so and skips.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdio>

#include "Processor.h"

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

struct Counter : juce::MidiInputCallback {
    std::atomic<int> notes{0};
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (m.isNoteOn()) ++notes;
    }
};

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

// Wait (pumping the message loop, where device changes arrive) until `done`, or give up.
template <typename F>
bool waitFor(F done, int ms = 4000) {
    for (int t = 0; t < ms; t += 50) {
        if (done()) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
    return done();
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if !(JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS)
    std::printf("skipped: this system can't make a virtual MIDI device\n");
    return 0;
#else
    const juce::ScopedJuceInitialiser_GUI juce;
    const juce::String name = "Planchette Port Test";
    Counter counter;
    auto synth = juce::MidiInput::createNewDevice(name, &counter);
    if (!synth) {
        std::printf("skipped: this system can't make a virtual MIDI device\n");
        return 0;
    }
    synth.reset();  // start unplugged

    HocketProcessor p;
    p.setSeed(0x0123456789ABCDEFull);
    p.setOutput(HocketProcessor::Output::Port);
    p.setPort(name);
    check(!p.portConnected(), "chosen but not plugged in: not connected");

    synth = juce::MidiInput::createNewDevice(name, &counter);
    synth->start();
    check(waitFor([&] { return p.portConnected(); }), "plugged in: connects by itself");

    // Play two bars into it.
    Head head;
    p.setPlayHead(&head);
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    for (std::int64_t s = 0; s < 48000 * 4; s += 512) {
        head.ppq = static_cast<double>(s) / 48000.0 * 2.0;
        juce::MidiBuffer midi;
        p.processBlock(audio, midi);
    }
    check(waitFor([&] { return counter.notes.load() > 10; }), "and the notes arrive");

    synth.reset();
    check(waitFor([&] { return !p.portConnected(); }), "unplugged: lets go");

    synth = juce::MidiInput::createNewDevice(name, &counter);
    synth->start();
    check(waitFor([&] { return p.portConnected(); }), "plugged back in: reconnects by itself");

    synth.reset();
    std::printf("%s\n", failures ? "port test failed" : "port test passed");
    return failures ? 1 : 0;
#endif
}
