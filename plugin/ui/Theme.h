// The look: a dark studio instrument. A near-black
// ground, panels a step up, and the five voice colours, which always mean "this voice".
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hocket::ui {

namespace colour {
const juce::Colour ground{0xff141317}, panel{0xff1d1c22}, raised{0xff26252d}, line{0xff33323b}, sidebar{0xff18171c};
const juce::Colour text{0xffefebe4}, soft{0xffc9c5cd}, dim{0xff9a96a0};
const juce::Colour pink{0xfff0a3c8}, blue{0xff9cc0e8}, olive{0xffa7bd74}, yellow{0xfff2d16b}, lilac{0xffb8a6ff};
const juce::Colour danger{0xffffb3bd}, dangerGround{0xff3a2229};
inline juce::Colour voice(int i) {
    static const juce::Colour v[] = {pink, blue, olive, yellow, lilac};
    return i < 0 ? dim : v[i % 5];
}
}  // namespace colour

inline juce::FontOptions sans(float size, bool bold = false) {
    return juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain);
}
inline juce::FontOptions mono(float size, bool bold = true) {
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain);
}

// Literal text is UTF-8 (· × — …): JUCE reads a bare char* as Latin-1, so text goes through here.
inline juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

// A section's small capital heading.
inline void heading(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r) {
    g.setColour(colour::dim);
    g.setFont(sans(13.0f, true));
    g.drawText(text.toUpperCase(), r, juce::Justification::centredLeft);
}

inline void heading(juce::Graphics& g, const char* text, juce::Rectangle<int> r) { heading(g, u8(text), r); }

inline void panel(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c = colour::panel) {
    g.setColour(c);
    g.fillRoundedRectangle(r.toFloat(), 18.0f);
}

}  // namespace hocket::ui
