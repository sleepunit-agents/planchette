// Touch-first controls (the design: big targets, values you drag with a finger, never a tiny knob).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

#include "Theme.h"

namespace hocket::ui {

// The host's own menu for a parameter (automation, MIDI learn, …) at a right-click, where the host offers one (Live,
// Bitwig, Reaper); elsewhere our own, with "Back to default". `c` is the control that was clicked.
void showParameterMenu(juce::Component& c, juce::AudioProcessorParameter& param, const juce::MouseEvent& e,
                       std::function<void()> backToDefault);

// A value you drag sideways (or tap where you want it); a double-tap goes back to its default. When `inheritable`,
// a badge says whether the value is this level's own or the level above's, and tapping it clears or sets it (§1.2).
class ValueBar : public juce::Component {
public:
    enum class Layout { Stacked, Inline };

    ValueBar(juce::String label, int lo, int hi, int value, juce::Colour fill = colour::blue);

    void setValue(int v, bool notify = false);
    int value() const { return value_; }
    void setRange(int lo, int hi);
    void setLabel(const juce::String& l);
    void setDefault(int d) { default_ = d; }
    void setLayout(Layout l) { layout_ = l; }
    void setInheritable(bool on, bool inherited);
    void setInheritedWord(const juce::String& w) { inheritedWord_ = w; }  // what the badge says of an inherited value ("song")
    bool inherited() const { return inherited_; }
    void setTrack(juce::Colour c) {  // the empty bar's colour, for a bar on a raised surface
        track_ = c;
        repaint();
    }

    std::function<juce::String(int)> format;  // how the value reads ("1/16", "100 %", "+1")
    std::function<void(int)> onChange;
    std::function<void(bool inherited)> onInherit;
    // A finger down and up again: one gesture, however many values it passes through (a host's automation, undo).
    std::function<void()> onGestureStart, onGestureEnd;
    // A right-click (or a two-finger tap): by default, a menu with "Back to default".
    std::function<void(const juce::MouseEvent&)> onMenu;

    int low() const { return lo_; }
    int high() const { return hi_; }
    juce::String valueText() const { return format ? format(value_) : juce::String(value_); }
    void resetToDefault() { set(default_); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<int> barArea() const;
    juce::Rectangle<int> badgeArea() const;
    int valueAt(int x) const;
    void set(int v);

    juce::String label_;
    int lo_, hi_, value_, default_;
    juce::Colour fill_, track_ = colour::raised;
    Layout layout_ = Layout::Stacked;
    bool inheritable_ = false, inherited_ = false;
    juce::String inheritedWord_{"song"};
    int dragStartValue_ = 0;
    bool dragged_ = false, menu_ = false, gesture_ = false;
};

// A ValueBar on a host parameter: the host's automation moves it, and dragging it writes the parameter.
class ParamBar : public ValueBar {
public:
    ParamBar(juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String label, juce::Colour fill = colour::blue);

private:
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attach_;
    bool gesture_ = false;
};

// One choice of a few, as a row of buttons; `columns` wraps them into a grid.
class Segmented : public juce::Component {
public:
    explicit Segmented(juce::StringArray options, int selected = 0, int columns = 0);
    void setSelected(int i, bool notify = false);
    int selected() const { return selected_; }
    void setOptions(juce::StringArray options);
    void setOff(juce::Colour c) {  // the unselected buttons' colour, for a control on a raised surface
        off_ = c;
        repaint();
    }
    const juce::StringArray& options() const { return options_; }
    std::function<void(int)> onChange;
    std::function<void(const juce::MouseEvent&)> onMenu;  // a right-click; nothing by default
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<int> cell(int i) const;
    juce::StringArray options_;
    int selected_, columns_;
    juce::Colour off_ = colour::raised;
};

// A Segmented on a host choice parameter.
class ParamChoice : public Segmented {
public:
    ParamChoice(juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::StringArray labels, int columns = 0);

private:
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attach_;
};

// A button with the design's looks: primary (pink), neutral, selected (light), danger, dashed (add something).
class Pill : public juce::Button {
public:
    enum class Kind { Primary, Neutral, Selected, Danger, Dashed, Ghost };
    Pill(const juce::String& text, Kind kind = Kind::Neutral);
    void setKind(Kind k) {
        kind_ = k;
        repaint();
    }
    void setAccent(juce::Colour c) {
        accent_ = c;
        repaint();
    }
    void paintButton(juce::Graphics&, bool over, bool down) override;

private:
    Kind kind_;
    juce::Colour accent_ = juce::Colours::transparentBlack;
};

}  // namespace hocket::ui
