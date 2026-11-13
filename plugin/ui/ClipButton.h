// "Drag 8 bars": the music as a MIDI clip. Drag it out onto a track (Live, any desktop host that takes files); tap
// it for the length, where it starts, saving it as a file, or (on the iPad, where nothing drags out of a plugin)
// sharing it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../Processor.h"

namespace hocket::ui {

// Legacy directories stay in place. The callback opens one for browsing/sharing without moving its files.
juce::PopupMenu legacyClipMenu(const juce::File& documents, const juce::File& applicationData,
                               const std::function<void(const juce::File&)>& browse);

class ClipButton : public juce::Component {
public:
    explicit ClipButton(HocketProcessor& p);
    ~ClipButton() override;

    int bars() const { return bars_; }
    void setBars(int b);
    // The bar a clip starts on: the bar playing now, or bar 1 when stopped or when asked to.
    std::int64_t firstBar() const;
    // The clip, written to the clips folder (or `dir`). Empty on failure.
    juce::File write(const juce::File& dir) const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void showMenu();
    void saveAs();
    void share();
    void browseLegacy(const juce::File& dir);

    HocketProcessor& proc_;
    int bars_ = 8;
    bool fromTop_ = false;  // always from bar 1
    bool dragging_ = false, menu_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ScopedMessageBox sharer_;
};

}  // namespace hocket::ui
