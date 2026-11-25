// planchette_snapshot out.png [seed-hex | song.planchette] [bar] [page 0-4: Play, Groups, Changes, Song, Form]: the editor as it looks while playing, drawn to a PNG. It links the
// plugin's own processor and editor (no host, no wrapper), plays to the bar, and draws the window. For seeing UI
// changes without a DAW.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <cstdlib>

#include "Editor.h"
#include "Processor.h"
#include "SongJson.h"

namespace {
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
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: planchette_snapshot out.png [seed-hex | song.planchette] [bar] [page 0-4]\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    HocketProcessor p;
    // A song file (a .planchette's JSON) to draw, or a seed for the default song.
    const juce::File songFile = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
    if (argc > 2 && songFile.existsAsFile()) p.loadSong(hocket::json::fromString(songFile.loadFileAsString()), songFile.getFileNameWithoutExtension());
    else if (argc > 2) p.setSeed(std::strtoull(argv[2], nullptr, 16));
    const double bar = argc > 3 ? std::atof(argv[3]) : 4.3;
    Head head;
    p.setPlayHead(&head);
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    const double ticksPerSample = 2.0 * hocket::kTicksPerBeat / 48000.0;
    const double end = bar * static_cast<double>(hocket::barTicks(p.song()));
    for (std::int64_t s = 0; static_cast<double>(s) * ticksPerSample < end; s += 512) {
        head.ppq = static_cast<double>(s) * ticksPerSample / hocket::kTicksPerBeat;
        juce::MidiBuffer midi;
        p.processBlock(audio, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    if (argc > 4)
        if (auto* he = dynamic_cast<HocketEditor*>(editor.get())) he->showPage(std::atoi(argv[4]));
    editor->setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    const juce::Image shot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat().writeImageToStream(shot, stream);
    p.editorBeingDeleted(editor.get());
    std::printf("snapshot: %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
