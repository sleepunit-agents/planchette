#include "Views.h"

namespace hocket::ui {

namespace {
std::string noteLabel(int midi) { return midi < 0 ? "" : noteName(midi % 12) + std::to_string(midi / 12 - 1); }
}  // namespace

// ---- ChordChips ------------------------------------------------------------------------------------------

void ChordChips::set(const HarmonyView& h, bool playing, bool showNotes, const std::vector<std::string>& notes) {
    if (h.names == h_.names && h.current == h_.current && playing == playing_ && notes == notes_) return;
    h_ = h;
    playing_ = playing;
    showNotes_ = showNotes;
    notes_ = notes;
    // What a screen reader says: the progression, and which chord is sounding.
    juce::StringArray names;
    for (const auto& n : h.names) names.add(n);
    setTitle("Chords");
    setDescription(names.joinIntoString(", ") +
                   (playing && h.current >= 0 && h.current < static_cast<int>(h.names.size()) ? ". Playing " + juce::String(h.names[static_cast<std::size_t>(h.current)]) : juce::String()));
    repaint();
}

int ChordChips::heightFor(int) const {
    const int n = static_cast<int>(h_.names.size());
    const int rows = (n + perRow_ - 1) / juce::jmax(1, perRow_);
    return static_cast<int>(rows * chipH_ + (rows - 1) * 10);
}

void ChordChips::paint(juce::Graphics& g) {
    const int n = static_cast<int>(h_.names.size());
    if (n == 0) return;
    const int per = juce::jmin(n, perRow_);
    const float gap = 10.0f, w = (getWidth() - gap * (per - 1)) / per;
    for (int i = 0; i < n; ++i) {
        const auto r = juce::Rectangle<float>((i % per) * (w + gap), (i / per) * (chipH_ + gap), w, chipH_);
        const bool lit = playing_ && i == h_.current;
        g.setColour(lit ? colour::yellow : colour::raised);
        g.fillRoundedRectangle(r, 14.0f);
        g.setColour(lit ? colour::ground : colour::text);
        if (showNotes_ && i < static_cast<int>(notes_.size())) {
            auto in = r.reduced(14.0f, 10.0f);
            g.setFont(sans(juce::jmin(26.0f, chipH_ * 0.32f), true));
            g.drawText(h_.names[static_cast<std::size_t>(i)], in.removeFromTop(in.getHeight() * 0.6f), juce::Justification::topLeft);
            g.setFont(mono(13.0f, false));
            g.setColour((lit ? colour::ground : colour::text).withAlpha(0.75f));
            g.drawText(notes_[static_cast<std::size_t>(i)], in, juce::Justification::bottomLeft);
        } else {
            g.setFont(sans(juce::jmin(26.0f, w * 0.22f), lit));
            g.drawFittedText(h_.names[static_cast<std::size_t>(i)], r.toNearestInt(), juce::Justification::centred, 1);
        }
    }
}

// ---- LaneCells -------------------------------------------------------------------------------------------

void LaneCells::set(const Lane& lane, bool playing) {
    if (lane.name != lane_.name || lane.chord != lane_.chord) {
        setTitle(juce::String(lane.name) + " lane");
        setDescription(lane.chord.empty() ? juce::String(lane.steps) + " steps" : juce::String(lane.steps) + " steps, " + juce::String(lane.chord));
    }
    lane_ = lane;
    playing_ = playing;
    repaint();
}

void LaneCells::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    if (lane_.held) {
        auto band = r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 92.0f));
        g.setColour(colour::pink.withAlpha(playing_ ? 1.0f : 0.55f));
        g.fillRoundedRectangle(band, 12.0f);
        g.setColour(colour::ground);
        g.setFont(sans(30.0f, true));
        g.drawText(lane_.chord, band.reduced(22.0f, 0).toNearestInt(), juce::Justification::centredLeft);
        g.setFont(mono(15.0f, false));
        g.drawText("held", band.reduced(22.0f, 0).toNearestInt(), juce::Justification::centredRight);
        return;
    }
    const int perLine = 16, lines = (lane_.steps + perLine - 1) / perLine;
    const float gap = 5.0f, cw = (r.getWidth() - gap * (perLine - 1)) / perLine;
    const float ch = (r.getHeight() - gap * (lines - 1)) / lines;
    for (int s = 0; s < lane_.steps && s < static_cast<int>(lane_.cells.size()); ++s) {
        const LaneCell& c = lane_.cells[static_cast<std::size_t>(s)];
        const auto cell = juce::Rectangle<float>((s % perLine) * (cw + gap), (s / perLine) * (ch + gap), cw, ch);
        g.setColour(colour::raised);
        g.fillRoundedRectangle(cell, 10.0f);
        if (c.hit) {
            auto area = cell.reduced(6.0f);
            auto label = area.removeFromBottom(ch > 56.0f ? 18.0f : 0.0f);
            auto mark = area.withWidth(area.getWidth() * (c.gate == 1 ? 0.45f : c.gate == 2 ? 0.72f : 1.0f));
            if (!c.accent) mark = mark.withTrimmedTop(mark.getHeight() * 0.45f);
            g.setColour(colour::voice(c.owner));
            g.fillRoundedRectangle(mark, 6.0f);
            if (!label.isEmpty()) {
                g.setColour(colour::soft);
                g.setFont(mono(12.0f, false));
                g.drawFittedText(lane_.chord.empty() ? noteLabel(c.note) : lane_.chord, label.toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
            }
        }
        if (playing_ && s == lane_.playing) {
            g.setColour(colour::text);
            g.drawRoundedRectangle(cell.reduced(1.0f), 10.0f, 3.0f);
        }
    }
    if (lane_.lock >= 0) {
        // §16.10: a Register lane shows its Lock as a small meter, so you can see it settle (16 locked, 17 inverted).
        const auto m = juce::Rectangle<float>(r.getRight() - 96.0f, r.getBottom() - 12.0f, 92.0f, 8.0f);
        g.setColour(colour::ground.withAlpha(0.8f));
        g.fillRoundedRectangle(m.expanded(3.0f), 5.0f);
        g.setColour(colour::raised.brighter(0.15f));
        g.fillRoundedRectangle(m, 4.0f);
        g.setColour(lane_.lock >= 17 ? colour::pink : lane_.lock == 16 ? colour::olive : colour::yellow);
        g.fillRoundedRectangle(m.withWidth(juce::jmax(8.0f, m.getWidth() * juce::jmin(16, lane_.lock) / 16.0f)), 4.0f);
    }
}

// ---- FormStrip -------------------------------------------------------------------------------------------

void FormStrip::set(std::vector<std::string> groupNames, std::vector<std::vector<std::string>> letters, std::vector<std::string> chords,
                    std::int64_t firstBar) {
    if (groupNames == names_ && letters == letters_ && chords == chords_ && firstBar == first_) return;
    names_ = std::move(groupNames);
    letters_ = std::move(letters);
    chords_ = std::move(chords);
    first_ = firstBar;
    setTitle("Form, the next bars");
    juce::StringArray bars;
    for (std::size_t i = 0; i < chords_.size(); ++i) bars.add("bar " + juce::String(first_ + 1 + static_cast<std::int64_t>(i)) + " " + juce::String(chords_[i]));
    setDescription(bars.joinIntoString(", "));
    repaint();
}

void FormStrip::paint(juce::Graphics& g) {
    const int bars = static_cast<int>(chords_.size());
    if (bars == 0) return;
    const float nameW = 82.0f, gap = 6.0f;
    const float cw = (getWidth() - nameW - gap * bars) / bars;
    const int rows = 2 + static_cast<int>(names_.size());
    const float rh = juce::jmin(34.0f, (getHeight() - gap * (rows - 1)) / rows);
    float y = 0.0f;
    // Bar numbers, then chords, then each group's letters.
    g.setFont(mono(13.0f, false));
    g.setColour(colour::dim);
    for (int b = 0; b < bars; ++b)
        g.drawText(juce::String(first_ + b + 1), juce::Rectangle<float>(nameW + gap + b * (cw + gap), y, cw, rh * 0.7f).toNearestInt(), juce::Justification::centred);
    y += rh * 0.7f + gap;
    auto rowName = [&](const juce::String& n) {
        g.setColour(colour::soft);
        g.setFont(sans(14.0f));
        g.drawText(n, juce::Rectangle<float>(0, y, nameW, rh).toNearestInt(), juce::Justification::centredLeft);
    };
    rowName("Harmony");
    for (int b = 0; b < bars; ++b) {
        const auto c = juce::Rectangle<float>(nameW + gap + b * (cw + gap), y, cw, rh);
        const bool moves = b > 0 && chords_[static_cast<std::size_t>(b)] != chords_[static_cast<std::size_t>(b - 1)];
        g.setColour(b == 0 ? juce::Colour(0xff3a3420) : colour::raised);
        g.fillRoundedRectangle(c, 8.0f);
        g.setColour(b == 0 ? colour::yellow : moves ? colour::text : colour::soft);
        g.setFont(sans(13.0f, b == 0));
        g.drawFittedText(chords_[static_cast<std::size_t>(b)], c.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    y += rh + gap;
    for (std::size_t gi = 0; gi < names_.size(); ++gi) {
        rowName(names_[gi]);
        for (int b = 0; b < bars && b < static_cast<int>(letters_[gi].size()); ++b) {
            const std::string& l = letters_[gi][static_cast<std::size_t>(b)];
            const auto c = juce::Rectangle<float>(nameW + gap + b * (cw + gap), y, cw, rh);
            const bool a = l == "A";
            // Under a form (§18.1) the letter leads: A′ and A·B are still A's, raised; B's and C's in their colours.
            const bool home = !l.empty() && l[0] == 'A';
            g.setColour(home ? colour::raised : colour::voice(3 + (l[0] - 'B') % 2));
            g.fillRoundedRectangle(c, 8.0f);
            if (b == 0) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(c.reduced(1.0f), 8.0f, 2.0f);
            }
            g.setColour(a ? colour::dim : home ? colour::text : colour::ground);
            g.setFont(mono(14.0f));
            g.drawFittedText(juce::String::fromUTF8(l.c_str()), c.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
        y += rh + gap;
    }
}

}  // namespace hocket::ui
