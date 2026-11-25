// The editor: an app, not a plugin window (the iPad design). Full screen in AUM or Loopy Pro, and at whatever size
// a desktop host allows: a tab bar (Play, Groups, Changes, Song, Form) over one screen at a time.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "Processor.h"
#include "ui/ClipButton.h"
#include "ui/Controls.h"
#include "ui/Pages.h"

class HocketEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit HocketEditor(HocketProcessor&);
    ~HocketEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;  // ⌘Z / Ctrl+Z undo; with Shift, or Ctrl+Y, redo

    // Which screen shows: 0 Play, 1 Groups, 2 Changes, 3 Song, 4 Form.
    void showPage(int i);
    // What the timer does, now: tests call it so they don't hang on the clock (a slow CI box can miss a tick).
    void tick() { timerCallback(); }

private:
    void timerCallback() override;

    HocketProcessor& proc_;
    std::unique_ptr<juce::LookAndFeel_V4> look_;
    hocket::ui::Segmented tabs_{{"Play", "Groups", "Changes", "Song", "Form"}};
    hocket::ui::Pill generate_{"Generate", hocket::ui::Pill::Kind::Primary};
    hocket::ui::Pill undo_{"Undo"}, redo_{"Redo"};
    hocket::ui::ClipButton clip_;
    // A finger lifting anywhere ends one undo step (Processor::endGesture).
    struct GestureEnd : juce::MouseListener {
        explicit GestureEnd(HocketProcessor& p) : proc(p) {}
        void mouseUp(const juce::MouseEvent&) override { proc.endGesture(); }
        HocketProcessor& proc;
    } gestureEnd_;
    juce::TooltipWindow tooltips_{this, 700};
    std::unique_ptr<hocket::ui::Page> pages_[5];
    int current_ = 0;

    std::unique_ptr<hocket::View> view_;
    int viewVersion_ = -1;
    std::int64_t viewHold_ = -1;
    std::int64_t shownStep_ = -2;
    juce::Rectangle<int> header_, seedArea_;
};
