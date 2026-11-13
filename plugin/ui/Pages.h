// The screens (the iPad design): Play, Groups, Matrix, Song, and Form (§18). Each is told when the song changes and where the
// host is, and edits the song through the processor (every edit lands on the next bar line).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "../Processor.h"
#include "hocket/generators.hpp"
#include "Controls.h"
#include "Views.h"

namespace hocket::ui {

class Page : public juce::Component {
public:
    explicit Page(HocketProcessor& p) : proc(p) {}
    // The document changed (an edit here, Generate, automation): read it again.
    virtual void songChanged() {}
    // Where the host is (−1 stopped), a few times a second, with a View of the song that's playing.
    virtual void tick(std::int64_t /*tick*/, View& /*view*/) {}

protected:
    HocketProcessor& proc;
};

std::unique_ptr<Page> makePlayPage(HocketProcessor&);
std::unique_ptr<Page> makeGroupsPage(HocketProcessor&);
std::unique_ptr<Page> makeMatrixPage(HocketProcessor&);
std::unique_ptr<Page> makeSongPage(HocketProcessor&);
std::unique_ptr<Page> makeFormPage(HocketProcessor&);  // §18: the form, its letters

}  // namespace hocket::ui
