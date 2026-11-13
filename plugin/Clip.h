// A stretch of the song as a MIDI file (a clip to drag into Live, save, or share): what Planchette would play over those
// bars, a track per group, with the song's tempo and meter. Notes already ringing when the clip starts (a held
// chord) start with it; notes still ringing when it ends stop there.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "hocket/engine.hpp"

namespace hocket::clip {

// Bars [fromBar, fromBar + bars), at 960 ticks a quarter note (the engine's own ticks, unscaled).
juce::MidiFile render(const Song& song, std::int64_t fromBar, int bars, double bpm);

// The same, written to `dir` as "Planchette <seed> bars 5-12.mid" (replacing one of that name). Empty on failure.
juce::File write(const Song& song, std::int64_t fromBar, int bars, double bpm, const juce::File& dir);

// Where clips go when they're dragged out: Documents/Planchette/Clips (a host may keep pointing at the file).
juce::File folder();
juce::File stagingFolder();  // app-data Planchette/Outgoing, for Save/Share

}  // namespace hocket::clip
