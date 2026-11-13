// The song as JSON (SPEC §12.1): what a session saves and a .planchette file holds. Seeds, settings, locks, groups,
// voices and the matrix; never anything generated. 64-bit ids and seeds are hex strings (JSON numbers can't hold
// them). Reading is forgiving: a missing field keeps the default song's value.
#pragma once

#include <juce_core/juce_core.h>

#include "hocket/song.hpp"

namespace hocket::json {

juce::var toVar(const Song& song);
Song fromVar(const juce::var& v);

inline juce::String toString(const Song& song) { return juce::JSON::toString(toVar(song), true); }
inline Song fromString(const juce::String& text) { return fromVar(juce::JSON::parse(text)); }

}  // namespace hocket::json
