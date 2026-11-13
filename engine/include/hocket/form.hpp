// The form (SPEC §18.1): a string of letters (A B A′ C …), each a saved seed-and-parameter snapshot of the song that
// returns exactly. A is the song as written; B…H are kept as what they change from it. Each section of the form
// plays its letter's song from that song's own bar 0, so every A is the same A, note for note.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hocket/mod.hpp"

namespace hocket {

struct Song;

// A letter other than A: which of A's seeds it rolls again (each from A's own seed and the letter's salt, so it
// follows A through a Generate), and the parameter values it sets, by §17.2's addresses.
struct FormLetter {
    char name = 'B';            // 'B' … 'H'
    std::uint64_t salt = 0;     // "New roll" draws a new one
    bool notes = true, rhythm = true, harmony = false, deal = false, matrix = false;
    std::vector<std::pair<Dest, int>> params;
    std::string extra;  // what this build couldn't read of it, as JSON text, written back as read
};

struct Form {
    bool on = false;
    std::string text;      // the form string: "A B A' C", a ′ or ' after a letter primes it
    int section = 2;       // phrases per letter, 1–16
    int end = 0;           // 0 Loop: the string again; 1 Hold: the last letter plays on
    int prime = 4;         // how many slots a primed letter varies, per voice, 1–16
    std::vector<FormLetter> letters;
    std::string extra;
};

struct FormEntry {
    char letter = 'A';
    bool primed = false;
    bool operator==(const FormEntry& o) const { return letter == o.letter && primed == o.primed; }
};

// §18.1: the form string read (letters A–H, each optionally primed; anything else is skipped), at most 64 entries;
// and written back in its canonical text ("A B A′ C").
std::vector<FormEntry> parseForm(const std::string& text);
std::string formText(const std::vector<FormEntry>& entries);
// The letter's name as the strip shows it: "A", "A′".
std::string entryName(const FormEntry& e);

// Whether the song plays a form: on, with at least one entry.
bool hasForm(const Song& song);
std::int64_t sectionBars(const Song& song);  // section · phrase length

// §18.1: the section a bar is in. `index` counts sections from bar 0; `position` is its entry in the string;
// `start` its first bar; `bars` its length (−1: it never ends, the last letter held).
struct Section {
    std::int64_t index = 0, position = 0, start = 0, bars = -1;
    FormEntry entry;
};
Section sectionAt(const Song& song, std::int64_t bar);

// §18.1: the song a section plays: A (the song without its form), its letter's seeds rolled again and its values set,
// and, primed, a Vary Pitch on every Lead and Bass at the section's bar 1. `position` is the entry's place in the string
// (a primed letter's variation is its place's own). A letter the form doesn't define plays as A.
Song letterSong(const Song& song, const FormEntry& entry, std::int64_t position);
const FormLetter* letterOf(const Song& song, char name);

// For the editor: the values a letter would set to make A into `x`, every address the letter vocabulary has
// (§18.1): the song's numeric and pick-one settings, each group's and voice's, and their generators' parameters.
std::vector<std::pair<Dest, int>> letterValues(const Song& a, const Song& x);
// The other way: every value of that vocabulary copied from `from` onto `to`, where both have it (the editor keeps A's
// own values while a letter is being edited; everything else, structure included, stays `to`'s).
void keepValues(const Song& from, Song& to);
// A new letter, the next free name after the last (B, C, …, H), its salt from the song's id counter. None past H.
FormLetter* addLetter(Song& song);

// ---- §18.2, the colotomic clock -----------------------------------------------------------------------------
// N = gong cycle · phrase length bars; K = min(kenongs, N); p = b mod N. Bar b is a gong boundary when p == 0, and a
// kenong boundary when (p · K) mod N < K: the bars ceilDiv(j · N, K) for j = 0 … K − 1.
std::int64_t gongBars(const Song& song);
int kenongCount(const Song& song);
bool gongAt(const Song& song, std::int64_t bar);
bool kenongAt(const Song& song, std::int64_t bar);
// A kenong boundary's stroke: 1 … K through the cycle, K at the gong (the gong falls with the last kenong). 0 elsewhere.
int kenongNumber(const Song& song, std::int64_t bar);
// The kenong strokes from bar 0 up to and including bar b: (b div N) · K + (p · K) div N + 1.
std::int64_t kenongStrokes(const Song& song, std::int64_t bar);

}  // namespace hocket
