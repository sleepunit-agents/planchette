// Things the screens draw from the engine's View: the progression as chips, a group's steps, the form strip.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

#include "Theme.h"
#include "hocket/engine.hpp"

namespace hocket::ui {

// The progression, one chip per chord, the sounding one lit; up to `perRow` a row.
class ChordChips : public juce::Component {
public:
    void set(const HarmonyView& h, bool playing, bool showNotes = false, const std::vector<std::string>& notes = {});
    void setPerRow(int n) { perRow_ = n; }
    void setChipHeight(float h) { chipH_ = h; }
    int heightFor(int width) const;
    void paint(juce::Graphics&) override;

private:
    HarmonyView h_;
    bool playing_ = false, showNotes_ = false;
    std::vector<std::string> notes_;
    int perRow_ = 4;
    float chipH_ = 74.0f;
};

// A group's steps: a hit in its voice's colour, wide for a long gate, tall for an accent, the note under it, the
// playhead outlined. Patterns over 16 steps wrap onto more lines. A held-chord group is its chord, as a band.
class LaneCells : public juce::Component {
public:
    void set(const Lane& lane, bool playing);
    void paint(juce::Graphics&) override;

private:
    Lane lane_;
    bool playing_ = false;
};

// §7.6: per group, the next bars' letters (A, B, C…), and the chords above them.
class FormStrip : public juce::Component {
public:
    void set(std::vector<std::string> groupNames, std::vector<std::vector<std::string>> letters, std::vector<std::string> chords,
             std::int64_t firstBar);
    void paint(juce::Graphics&) override;

private:
    std::vector<std::string> names_, chords_;
    std::vector<std::vector<std::string>> letters_;
    std::int64_t first_ = 0;
};

}  // namespace hocket::ui
