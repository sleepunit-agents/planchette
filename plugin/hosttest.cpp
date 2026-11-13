// planchette_hosttest <plugin path>: loads the built plugin the way a DAW does, presses Play at 120 BPM for 16 bars, and
// checks that the MIDI it sends is exactly the engine's render of the same song. The proof that the plugin layer
// adds nothing and loses nothing between the engine and a host.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "hocket/engine.hpp"

namespace {

// A host's playhead: position and tempo, and when it says so a meter (with its bar lines) and a loop.
struct Head : juce::AudioPlayHead {
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    int beats = 0, unit = 4;  // 0: no meter reported
    double firstBar = 0.0;    // where the host's bars start counting from (a pickup or an earlier meter before it)
    double loopStart = 0.0, loopEnd = 0.0;  // a loop when loopEnd > loopStart
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setIsPlaying(playing);
        if (beats > 0) {
            p.setTimeSignature(TimeSignature{beats, unit});
            const double bar = beats * 4.0 / unit;
            p.setPpqPositionOfLastBarStart(firstBar + std::floor((ppq - firstBar) / bar) * bar);
        }
        if (loopEnd > loopStart) {
            p.setIsLooping(true);
            p.setLoopPoints(LoopPoints{loopStart, loopEnd});
        }
        return p;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: planchette_hosttest <Planchette.vst3>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : formats.getFormats()) f->findAllTypesForFile(found, juce::String(argv[1]));
    if (found.isEmpty()) {
        std::fprintf(stderr, "no plugin found in %s\n", argv[1]);
        return 1;
    }
    juce::String err;
    auto plugin = formats.createPluginInstance(*found[0], 48000.0, 512, err);
    if (!plugin) {
        std::fprintf(stderr, "couldn't load: %s\n", err.toRawUTF8());
        return 1;
    }

    // Which song is it playing? Its seed, read back out of the state the plugin saves for a host session (a VST3
    // wraps it in its own format, so look for the seed attribute inside).
    juce::MemoryBlock mb;
    plugin->getStateInformation(mb);
    std::string blob(static_cast<const char*>(mb.getData()), mb.getSize());
    if (const auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), static_cast<int>(mb.getSize())))
        if (const auto* comp = xml->getChildByName("IComponent")) {
            juce::MemoryBlock inner;
            inner.fromBase64Encoding(comp->getAllSubText().trim());
            blob.assign(static_cast<const char*>(inner.getData()), inner.getSize());
        }
    const auto at = blob.find("seed=\"");
    if (at == std::string::npos) {
        std::fprintf(stderr, "no seed in the plugin's state (%d bytes)\n", static_cast<int>(mb.getSize()));
        return 1;
    }
    const std::uint64_t seed = std::strtoull(blob.c_str() + at + 6, nullptr, 16);
    std::printf("the plugin's seed: %016llx\n", static_cast<unsigned long long>(seed));

    // The hosts. 48 kHz in 512s; 44.1 kHz in odd blocks of 333; 96 kHz in 1024s with a little float wobble on each
    // block's position (it must not read as a jump). Then a host in 7/8; one in 3/4 whose bars start a beat in (a
    // pickup bar, or a meter change before); and one looping bars 2–4, wrapping in the middle of a block.
    struct Config {
        const char* name;
        double rate;
        int block;
        double wobbleTicks = 0.0;
        int beats = 0, unit = 4;
        double firstBar = 0.0, loopStart = 0.0, loopEnd = 0.0;
    };
    const Config configs[] = {
        {"48 kHz, blocks of 512", 48000.0, 512},
        {"44.1 kHz, blocks of 333", 44100.0, 333},
        {"96 kHz, blocks of 1024, wobbling host", 96000.0, 1024, 0.4},
        {"host in 7/8", 48000.0, 512, 0.0, 7, 8},
        {"host in 3/4, bars from beat 2", 44100.0, 333, 0.0, 3, 4, 1.0},
        {"host looping bars 2-4", 48000.0, 509, 0.0, 4, 4, 0.0, 4.0, 16.0},
    };
    auto order = [](const hocket::Event& a, const hocket::Event& b) {
        return std::tie(a.tick, a.kind, a.channel, a.note) < std::tie(b.tick, b.kind, b.channel, b.note);
    };
    int failures = 0;
    for (const Config& c : configs) {
        // What the engine says this host should hear, on the host's own timeline (a loop unrolled: each pass after
        // the first is the engine's jump back to the loop start, placed after the passes before it).
        hocket::Song song = hocket::defaultSong(seed);
        if (c.beats > 0) song.barLength = c.beats, song.beatUnit = c.unit;
        const std::int64_t end = hocket::barTicks(song) * 16;
        const auto shift = static_cast<std::int64_t>(c.firstBar * hocket::kTicksPerBeat);
        std::vector<hocket::Event> want;
        {
            hocket::Player player(song);
            const bool loops = c.loopEnd > c.loopStart;
            const std::int64_t ls = static_cast<std::int64_t>(c.loopStart * hocket::kTicksPerBeat);
            const std::int64_t le = static_cast<std::int64_t>(c.loopEnd * hocket::kTicksPerBeat);
            std::int64_t t = 0, host = 0;  // the song's tick, and the host's unrolled tick it plays at
            while (host < end) {
                const std::int64_t stop = loops ? le : t + (end - host);
                for (auto e : player.process(t, stop, true)) {
                    e.tick += host - t + shift;
                    if (e.tick < end + shift) want.push_back(e);
                }
                host += stop - t;
                t = loops ? ls : stop;
            }
        }
        std::stable_sort(want.begin(), want.end(), order);

        Head head;
        head.beats = c.beats, head.unit = c.unit, head.firstBar = c.firstBar;
        head.loopStart = c.loopStart, head.loopEnd = c.loopEnd;
        plugin->setPlayHead(&head);
        plugin->prepareToPlay(c.rate, c.block);
        // Stopped first, so each run starts from the top like pressing Play; and long enough for a host meter to
        // reach the song (the plugin builds it again on its message thread).
        head.playing = false;
        head.ppq = c.firstBar;
        juce::AudioBuffer<float> audio(2, c.block);
        juce::MidiBuffer ignore;
        plugin->processBlock(audio, ignore);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        plugin->processBlock(audio, ignore);
        head.playing = true;
        const double ticksPerSample = head.bpm / 60.0 * hocket::kTicksPerBeat / c.rate;
        const double loopTicks = (c.loopEnd - c.loopStart) * hocket::kTicksPerBeat;
        std::vector<hocket::Event> got;
        for (std::int64_t s = 0, i = 0;; s += c.block, ++i) {
            const double wobble = c.wobbleTicks * ((i % 3) - 1);  // −w, 0, +w
            const double unrolled = static_cast<double>(s) * ticksPerSample;  // the host's time since Play
            if (unrolled >= static_cast<double>(end)) break;
            // Where the host says it is: past the loop's end, it has wrapped (whole passes back).
            double ticks = unrolled + c.firstBar * hocket::kTicksPerBeat;
            int passes = 0;
            while (loopTicks > 0 && ticks >= c.loopEnd * hocket::kTicksPerBeat) ticks -= loopTicks, ++passes;
            head.ppq = std::max(0.0, ticks + wobble) / hocket::kTicksPerBeat;
            juce::MidiBuffer midi;
            plugin->processBlock(audio, midi);
            for (const auto m : midi) {
                const auto msg = m.getMessage();
                if (!msg.isNoteOnOrOff()) continue;
                const double at = head.ppq * hocket::kTicksPerBeat + passes * loopTicks + m.samplePosition * ticksPerSample;
                const auto tick = static_cast<std::int64_t>(std::llround(at));
                if (tick < end + shift) got.push_back({tick, msg.isNoteOn() ? hocket::Event::On : hocket::Event::Off, msg.getChannel(), msg.getNoteNumber(),
                                                       msg.isNoteOn() ? msg.getVelocity() : 0});
            }
        }
        // Stop, so the next host starts clean.
        head.playing = false;
        plugin->processBlock(audio, ignore);
        plugin->releaseResources();
        std::stable_sort(got.begin(), got.end(), order);
        // A wobbling host, or a wrap inside a block, can put an event a tick off; the notes and their order must hold.
        const std::int64_t slack = c.wobbleTicks > 0 || loopTicks > 0 ? 1 : 0;
        std::size_t same = 0;
        while (same < got.size() && same < want.size() && std::llabs(got[same].tick - want[same].tick) <= slack &&
               got[same].kind == want[same].kind && got[same].channel == want[same].channel && got[same].note == want[same].note &&
               got[same].velocity == want[same].velocity)
            ++same;
        const bool ok = same == want.size() && got.size() == want.size();
        std::printf("%s, %s: %zu events, %zu match the engine%s\n", found[0]->name.toRawUTF8(), c.name, got.size(), same,
                    ok ? "" : "  <-- MISMATCH");
        if (!ok && same < got.size() && same < want.size())
            std::printf("  first difference: plugin tick %lld %s ch %d note %d, engine tick %lld %s ch %d note %d\n",
                        static_cast<long long>(got[same].tick), got[same].kind ? "on" : "off", got[same].channel, got[same].note,
                        static_cast<long long>(want[same].tick), want[same].kind ? "on" : "off", want[same].channel, want[same].note);
        failures += !ok;
    }
    const hocket::Song song = hocket::defaultSong(seed);
    // --snapshot out.png: play to bar 5, open the editor as a host would, and save what it shows.
    if (argc > 3 && juce::String(argv[2]) == "--snapshot") {
        Head head;
        plugin->setPlayHead(&head);
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> audio(2, 512);
        const double ticksPerSample = head.bpm / 60.0 * hocket::kTicksPerBeat / 48000.0;
        for (std::int64_t s = 0; static_cast<double>(s) * ticksPerSample < 4.3 * hocket::barTicks(song); s += 512) {
            head.ppq = static_cast<double>(s) * ticksPerSample / hocket::kTicksPerBeat;
            juce::MidiBuffer midi;
            plugin->processBlock(audio, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
        if (editor) {
            editor->setVisible(true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
            const juce::Image shot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));
            out.deleteFile();
            juce::FileOutputStream stream(out);
            juce::PNGImageFormat().writeImageToStream(shot, stream);
            std::printf("snapshot: %s\n", out.getFullPathName().toRawUTF8());
            plugin->editorBeingDeleted(editor.get());
        }
        plugin->releaseResources();
    }
    return failures ? 1 : 0;
}
