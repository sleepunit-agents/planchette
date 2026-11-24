// Changes: what changes, and when (§7, §17.8). The sources, each a card; every row and cord as a sentence; the panel
// edits the selected one; the form strip shows what they will do to the next bars.
#include "Pages.h"

#include "hocket/form.hpp"

#include "hocket/mod.hpp"
#include "hocket/rng.hpp"

#include <cctype>
#include <cmath>
#include <map>

namespace hocket::ui {

namespace {

// ---- names --------------------------------------------------------------------------------------------------

const Group* groupOf(const Song& s, std::uint64_t id) {
    for (const Group& g : s.groups)
        if (g.id == id) return &g;
    return nullptr;
}
std::string voiceName(const Song& s, std::uint64_t id) {
    static const char* types[] = {"Lead", "Bass", "Chord"};
    for (const Group& g : s.groups)
        for (const Voice& v : g.voices)
            if (v.id == id) return std::string(types[static_cast<int>(v.type)]) + " ch " + std::to_string(v.channel);
    return "a voice that's gone";
}
std::string targetName(const Song& s, const Row& r) {
    switch (r.scope) {
        case Scope::Global: return "the song";
        case Scope::Harmony: return "the chords";
        case Scope::Group: {
            const Group* g = groupOf(s, r.target);
            return g ? g->name : "a group that's gone";
        }
        case Scope::Voice: return voiceName(s, r.target);
    }
    return "";
}
std::string possessive(const std::string& n) {
    if (n == "the song" || n == "the chords") return n + "'s";
    return "the " + n + (!n.empty() && n.back() == 's' ? "'" : "'s");  // the Leads' notes, the Bass's notes
}

// §17.2: a row aimed at a generator's knob (its key), and that knob.
std::optional<GenTarget> genTargetOf(const Row& r) { return r.key.empty() ? std::nullopt : generatorTarget(r.key); }
std::string genKnobName(const GenTarget& t) {
    return std::string(t.gen->label) + " " + juce::String(t.param->label).toLowerCase().toStdString();
}
// Whether the row's target plays the generator the row's knob belongs to (§16.10: a row on one that isn't is dimmed).
bool genPlaying(const Song& s, const Row& r, const GenTarget& t) {
    for (const Group& g : s.groups) {
        if (t.slot == SlotKind::Rhythm && r.scope == Scope::Group && g.id == r.target) return &playing(SlotKind::Rhythm, g.rhythmGen) == t.gen;
        for (const Voice& v : g.voices)
            if (t.slot == SlotKind::Notes && r.scope == Scope::Voice && v.id == r.target) return &playing(SlotKind::Notes, v.notesGen) == t.gen;
    }
    return false;
}
bool rowNumeric(const Row& r) {
    if (const auto t = genTargetOf(r)) return !t->param->choice;
    return r.key.empty() && isNumericParam(r.param);
}
// §18: a long-form destination by key (no param number): a group's irama.
bool formDest(const std::string& key) { return key == "irama" || key == "tihai"; }
const char* formDestName(const std::string& key) {
    return key == "irama" ? "irama (notes a chord)" : key == "tihai" ? "tihai now (one on each phrase's sam)" : "";
}
bool rowPick(const Row& r) {
    if (formDest(r.key)) return true;
    if (const auto t = genTargetOf(r)) return t->param->choice;
    return r.key.empty() && isPickParam(r.param);
}

const char* paramName(Param p) {
    switch (p) {
        case Param::Range: return "range";
        case Param::Wander: return "wander";
        case Param::Accent: return "accent";
        case Param::GatePercent: return "gate";
        case Param::OctaveLow: return "lowest octave";
        case Param::OctaveHigh: return "highest octave";
        case Param::Density: return "density";
        case Param::Steps: return "length";
        case Param::Swing: return "swing";
        case Param::Offset: return "offset";
        case Param::Weight: return "weight";
        case Param::Voicing: return "voicing";
        case Param::ChordSize: return "chord size";
        case Param::Length: return "length";
        case Param::SoloMember: return "solo voice";
        case Param::DealMode: return "dealing";
        case Param::Direction: return "direction";
        case Param::Articulation: return "articulation";
        case Param::ChangesEvery: return "rhythm";
        case Param::Probability: return "probability";
        case Param::Ratchet: return "ratchet chance";
        case Param::Repeats: return "ratchet repeats";
        case Param::Fill: return "fill";
        case Param::ModeMoves: return "mode moves";
        case Param::VoiceLeading: return "voice leading";
        case Param::HumanVelocity: return "velocity feel";
        case Param::HumanTiming: return "timing feel";
        case Param::Handoff: return "handoff";
        case Param::CleanHandoffs: return "clean handoffs";
        case Param::Stagger: return "stagger";
        case Param::StaggerOrder: return "stagger order";
        case Param::StaggerLean: return "stagger lean";
        case Param::Top: return "top";
        default: return "setting";
    }
}

std::string ordinalFromEnd(int k) {
    if (k == 1) return "last";
    if (k == 2) return "second-to-last";
    return std::to_string(k) + "th-from-last";
}

// What a row (or a trigger cord, as a row) does when it acts, and for how long: the sentence after its "when".
std::string actionOf(const Song& s, const Row& r) {
    const std::string t = targetName(s, r);
    const int k = r.maxShift;
    std::string what;
    const std::string seed = r.fixedSeed ? ", the same way each time" : ", new each time";
    if (!r.key.empty()) {
        const auto gt = genTargetOf(r);
        if (formDest(r.key)) {
            what = r.key == "irama" ? "change " + possessive(t) + " " + formDestName(r.key) + " at the next gong"  // §18.3
                                    : "turn " + possessive(t) + " " + formDestName(r.key) + " on or off";       // §18.4
        } else if (!gt) {
            what = "change a setting this version doesn't know (" + r.key + ")";
        } else {
            const std::string knob = genKnobName(*gt);
            what = gt->param->choice ? "change " + possessive(t) + " " + knob : "nudge " + possessive(t) + " " + knob + " by up to " + std::to_string(k);
            if (!gt->param->choice && (r.min || r.max))
                what += ", between " + (r.min ? std::to_string(*r.min) : std::string("the lowest")) + " and " +
                        (r.max ? std::to_string(*r.max) : std::string("the highest"));
            if (!genPlaying(s, r, *gt)) what += " (" + std::string(gt->gen->label) + " isn't playing)";
        }
        const std::string lasts = r.lasts == 0 ? ". It stays." : ", for " + std::to_string(r.lasts) + (r.lasts == 1 ? " bar." : " bars.");
        return what + lasts;
    }
    switch (r.param) {
        case Param::VaryPitch:
            what = r.scope == Scope::Harmony ? "turn " + std::to_string(k) + (k == 1 ? " chord" : " chords") + " of the progression"
                 : r.scope == Scope::Global ? "vary " + std::to_string(k) + " notes in every line"
                                            : "vary " + std::to_string(k) + " of " + possessive(t) + " notes";
            what += seed;
            break;
        case Param::VaryRhythm: what = "move " + std::to_string(k) + " of " + possessive(t) + " hits" + seed; break;
        case Param::RerollRhythm: what = "give " + (r.scope == Scope::Global ? std::string("every group") : "the " + t) + " a new rhythm" + seed; break;
        case Param::RerollPitch: what = r.scope == Scope::Harmony ? "a new progression" + seed : "give " + (r.scope == Scope::Global ? std::string("every voice") : t) + " new notes" + seed; break;
        case Param::RerollDeal: what = "deal " + possessive(t) + " hits again" + seed; break;
        case Param::RerollAll: what = r.scope == Scope::Harmony ? "a new progression" + seed : "roll everything in " + t + " again" + seed; break;
        case Param::Root: what = "move the key by up to " + std::to_string(k) + (k == 1 ? " fifth" : " fifths"); break;
        case Param::Scale:
            what = r.allowed.empty() ? "change the scale (none allowed yet)"
                                     : "change the scale by up to " + std::to_string(k) + (k == 1 ? " note" : " notes");
            break;
        case Param::Fill: {
            // The allowed set says which way: a row allowed only On is a fill when it fires (§4.6).
            const bool onOnly = r.allowed.size() == 1 && r.allowed[0] == 1, offOnly = r.allowed.size() == 1 && r.allowed[0] == 0;
            what = std::string(onOnly ? "fill " : offOnly ? "stop " : "switch ") + (r.scope == Scope::Global ? std::string("every group") : "the " + t) +
                   (onOnly ? "" : offOnly ? "'s fill" : "'s fill on or off");
            break;
        }
        default:
            if (isPickParam(r.param)) what = "change " + possessive(t) + " " + paramName(r.param);
            else {
                what = "nudge " + possessive(t) + " " + paramName(r.param) + " by up to " + std::to_string(k);
                if (r.min || r.max)
                    what += ", between " + (r.min ? std::to_string(*r.min) : std::string("the lowest")) + " and " +
                            (r.max ? std::to_string(*r.max) : std::string("the highest"));
            }
    }
    const std::string lasts = r.lasts == 0 ? ". It stays." : ", for " + std::to_string(r.lasts) + (r.lasts == 1 ? " bar." : " bars.");
    return what + lasts;
}

// §17.5: a condition in words, to follow a sentence's "when": ", every other time", ", the first time only".
std::string ordinal(int n) {
    static const char* words[] = {"", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth"};
    return n >= 1 && n <= 8 ? words[n] : std::to_string(n) + "th";
}
// An item's place in the list (rows, then cords), counted from 1: how a NEI names it.
int itemNumber(const Song& s, std::uint64_t id) {
    for (std::size_t i = 0; i < s.matrix.size(); ++i)
        if (s.matrix[i].id == id) return static_cast<int>(i) + 1;
    for (std::size_t i = 0; i < s.mod.cords.size(); ++i)
        if (s.mod.cords[i].id == id) return static_cast<int>(s.matrix.size() + i) + 1;
    return 0;
}
std::string condPhrase(const Song& s, const Condition& c) {
    using K = Condition::Kind;
    const bool n = c.negate;
    switch (c.kind) {
        case K::None: return "";
        case K::Unknown: return ", on a condition this version doesn't know (so never)";
        case K::AB: {
            const int B = std::max(1, std::min(8, c.b)), A = std::max(1, std::min(B, c.a));
            if (B == 1) return n ? ", never" : "";
            const std::string t = A == 1 && B == 2 ? "every other time" : "the " + ordinal(A) + " of every " + std::to_string(B) + " times";
            return ", " + std::string(n ? "except " : "") + t + " (" + (n ? "not " : "") + std::to_string(A) + ":" + std::to_string(B) + ")";
        }
        case K::First: return n ? ", except the first time" : ", the first time only";
        case K::Pre: return n ? ", if the change before it didn't act" : ", if the change before it acted";
        case K::Nei: {
            const int k = itemNumber(s, c.item);
            const std::string who = k > 0 ? "change " + std::to_string(k) : "a change that's gone";
            return ", if " + who + (n ? " didn't act" : " acted") + " last time";
        }
        case K::Fill: return n ? ", except on fill bars" : ", on fill bars";
        case K::Gong: return n ? ", except on the gong" : ", on the gong";            // §18.2
        case K::Kenong: return n ? ", except on a kenong" : ", on a kenong (the gong's too)";
    }
    return "";
}

// The row as a sentence (the design: you edit the sentence, not a spreadsheet).
std::string sentence(const Song& s, const Row& r) {
    const int period = std::max(1, r.every);
    if (r.clock == 3) {  // §18.2: kenong strokes
        std::string when = period == 1 ? "On every kenong" : "On every " + std::to_string(period) + " kenongs";
        if (period > 1) when += ", the " + ordinal(static_cast<int>((r.offset % period + period) % period) + 1);
        return when + condPhrase(s, r.cond) + ", " + actionOf(s, r);
    }
    std::string when = r.clock == 2 ? (period == 1 ? "Every gong" : "Every " + std::to_string(period) + " gongs")
                       : r.inPhrases ? (period == 1 ? "Every phrase" : "Every " + std::to_string(period) + " phrases")
                                     : (period == 1 ? "Every bar" : "Every " + std::to_string(period) + " bars");
    const int bars = period * (r.clock == 2 ? static_cast<int>(gongBars(s)) : r.inPhrases ? std::max(1, s.phraseLength) : 1);
    if (bars > 1) when += r.offset < 0 ? ", on its " + ordinalFromEnd(-r.offset) + " bar" : ", on bar " + std::to_string(r.offset % bars + 1);
    return when + condPhrase(s, r.cond) + ", " + actionOf(s, r);
}

// When it next fires, from `bar` on: the engine's own rule (§7.3), chance roll and all, so it's exact.
std::string nextFire(const Song& s, const Row& r, std::int64_t bar) {
    if (!r.on) return "Off";
    const Firing f = nextFiring(s, r, bar);
    if (f.bar < 0) return "Not in the next 64 times";
    return "Next: bar " + std::to_string(f.bar + 1) + (f.skipped ? " (skips " + std::to_string(f.skipped) + ")" : "");
}

// ---- the kinds of change ----------------------------------------------------------------------------------

enum Kind { VaryNotes, VaryRhythm, NewNotes, NewRhythm, Setting, TheKey };
Kind kindOf(const Row& r) {
    if (!r.key.empty()) return Setting;
    switch (r.param) {
        case Param::VaryPitch: return VaryNotes;
        case Param::VaryRhythm: return VaryRhythm;
        case Param::RerollPitch: case Param::RerollAll: case Param::RerollDeal: return NewNotes;
        case Param::RerollRhythm: return NewRhythm;
        case Param::Root: case Param::Scale: return TheKey;
        default: return Setting;
    }
}

// One of a target's settings: a param, or a generator's knob by its key (§17.2).
struct Setting_ {
    Param param = Param::Range;
    std::string key;
    bool operator==(const Setting_& o) const { return param == o.param && key == o.key; }
};
std::vector<Param> paramsFor(Scope sc);
// Settings a target has: its params (§7.2), then each of its slot's generators' knobs (§16.10), under the generator.
std::vector<Setting_> settingsFor(Scope sc) {
    std::vector<Setting_> out;
    for (Param p : paramsFor(sc)) out.push_back({p, {}});
    const SlotKind slot = sc == Scope::Group ? SlotKind::Rhythm : SlotKind::Notes;
    if (sc == Scope::Group || sc == Scope::Voice)
        for (const Generator* g : generatorsFor(slot))
            for (const GenParam& p : g->params)
                if (p.destination) out.push_back({Param::Range, generatorKey(slot, *g, p)});
    if (sc == Scope::Group) out.push_back({Param::Range, "irama"}), out.push_back({Param::Range, "tihai"});  // §18.3, §18.4
    return out;
}
Setting_ settingOf(const Row& r) { return {r.key.empty() ? r.param : Param::Range, r.key}; }
void setSetting(Row& r, const Setting_& x) { r.param = x.param, r.key = x.key; }

std::vector<Param> paramsFor(Scope sc) {
    switch (sc) {
        case Scope::Global: return {Param::Range, Param::Wander, Param::Accent, Param::GatePercent, Param::OctaveLow, Param::OctaveHigh, Param::Fill,
                                    Param::ModeMoves};
        case Scope::Harmony: return {Param::Range, Param::Wander, Param::Length, Param::ChangesEvery};
        case Scope::Group: return {Param::Density, Param::Steps, Param::Swing, Param::Offset, Param::Range, Param::Wander, Param::Accent,
                                   Param::GatePercent, Param::SoloMember, Param::DealMode, Param::Direction, Param::Probability, Param::Ratchet,
                                   Param::Repeats, Param::Fill, Param::Handoff, Param::CleanHandoffs};
        case Scope::Voice: return {Param::Range, Param::Wander, Param::Weight, Param::Density, Param::Accent, Param::GatePercent, Param::Voicing, Param::ChordSize, Param::OctaveLow,
                                   Param::OctaveHigh, Param::Articulation, Param::VoiceLeading, Param::HumanVelocity, Param::HumanTiming,
                                   Param::Stagger, Param::StaggerOrder, Param::StaggerLean, Param::Top};
    }
    return {};
}
juce::StringArray pickOptions(const Song& s, const Row& r) {
    if (!r.key.empty()) {
        if (r.key == "irama") return {"Off", "1 a chord", "2 a chord", "4 a chord", "8 a chord", "16 a chord"};  // §18.3
        if (r.key == "tihai") return {"Off", "On"};  // §18.4
        const auto t = genTargetOf(r);
        return t && t->param->choice ? juce::StringArray::fromTokens(t->param->options, "|", "") : juce::StringArray();
    }
    switch (r.param) {
        case Param::DealMode: return {"Fixed deal", "Every cycle", "Round robin", "Solo", "Dice", "Shuffle", "Runs", "Sweep", "Alternate", "Bells", "Tutti"};
        case Param::Direction: return {"Forward", "Backward", "Pendulum"};
        case Param::Articulation: return {"Sustain", "Restrike accents", "Restrike hits", "Pulse"};
        case Param::ChangesEvery: return {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars"};
        case Param::Fill: case Param::ModeMoves: case Param::CleanHandoffs: return {"Off", "On"};
        case Param::VoiceLeading: return {"Fixed", "Nearest"};
        case Param::StaggerOrder: return {"Up", "Down", "Seeded"};  // §3.4b
        case Param::StaggerLean: return {"Late", "Around"};
        case Param::SoloMember: {
            juce::StringArray o;
            if (const Group* g = groupOf(s, r.target))
                for (const Voice& v : g->voices) o.add(voiceName(s, v.id));
            return o;
        }
        case Param::Scale: {
            juce::StringArray o;
            for (const auto& sc : kScales) o.add(sc.name);
            return o;
        }
        default: return {};
    }
}

// ---- widgets ----------------------------------------------------------------------------------------------

// Toggle chips: the options a pick-one row (or a Scale row) may choose from.
class Allowed : public juce::Component {
public:
    std::function<void(std::vector<int>)> onChange;
    void set(juce::StringArray options, std::vector<int> allowed) {
        options_ = std::move(options);
        allowed_ = std::move(allowed);
        layout();
        repaint();
    }
    int layout() {
        chips_.clear();
        int x = 0, y = 0;
        juce::Font f(sans(13.0f, true));
        for (int i = 0; i < options_.size(); ++i) {
            const int w = static_cast<int>(juce::GlyphArrangement::getStringWidth(f, options_[i])) + 22;
            if (x > 0 && x + w > getWidth()) x = 0, y += 40;
            chips_.push_back({x, y, w, 34});
            x += w + 6;
        }
        return chips_.empty() ? 0 : chips_.back().getBottom();
    }
    void resized() override { layout(); }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < options_.size() && i < static_cast<int>(chips_.size()); ++i) {
            const bool on = std::find(allowed_.begin(), allowed_.end(), i) != allowed_.end();
            g.setColour(on ? colour::olive : colour::raised);
            g.fillRoundedRectangle(chips_[static_cast<std::size_t>(i)].toFloat(), 9.0f);
            g.setColour(on ? colour::ground : colour::soft);
            g.setFont(sans(13.0f, true));
            g.drawText(options_[i], chips_[static_cast<std::size_t>(i)], juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < static_cast<int>(chips_.size()); ++i)
            if (chips_[static_cast<std::size_t>(i)].contains(e.getPosition())) {
                auto it = std::find(allowed_.begin(), allowed_.end(), i);
                if (it == allowed_.end()) allowed_.push_back(i);
                else allowed_.erase(it);
                std::sort(allowed_.begin(), allowed_.end());
                repaint();
                if (onChange) onChange(allowed_);
            }
    }

private:
    juce::StringArray options_;
    std::vector<int> allowed_;
    std::vector<juce::Rectangle<int>> chips_;
};

// §17.5: a row's or a trigger cord's condition: which (Always, A:B, 1st, Pre, Nei, Fill), whether it's negated, A and
// B, and for Nei which change it reads.
class ConditionEditor : public juce::Component {
public:
    std::function<void(Condition)> onChange;
    ConditionEditor()
        : kind_({"Always", "A:B", "1st", "Pre", "Nei", "Fill", "Gong", "Kenong"}, 0, 4), not_({"When it is", "When it isn't"}),
          a_("A", 1, 8, 1, colour::yellow), b_("B", 1, 8, 2, colour::yellow) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&kind_, &not_, &a_, &b_, &item_}) addAndMakeVisible(c);
        a_.setLayout(ValueBar::Layout::Inline), b_.setLayout(ValueBar::Layout::Inline);
        kind_.setTitle("Condition");
        item_.setColour(juce::ComboBox::backgroundColourId, colour::raised);
        item_.setColour(juce::ComboBox::outlineColourId, colour::line);
        item_.setColour(juce::ComboBox::textColourId, colour::text);
        kind_.onChange = [this](int k) {
            Condition c = cond_;
            c.kind = static_cast<Condition::Kind>(k), c.extra.clear();
            if (c.kind == Condition::Kind::AB && c.b < 2) c.a = 1, c.b = 2;  // a new A:B starts as every other time
            send(c);
        };
        not_.onChange = [this](int v) {
            Condition c = cond_;
            c.negate = v == 1;
            send(c);
        };
        a_.onChange = [this](int v) {
            Condition c = cond_;
            c.a = std::min(v, c.b);
            send(c);
        };
        b_.onChange = [this](int v) {
            Condition c = cond_;
            c.b = v, c.a = std::min(c.a, v);
            send(c);
        };
        item_.onChange = [this] {
            const int i = item_.getSelectedItemIndex();
            if (i < 0 || i >= static_cast<int>(items_.size())) return;
            Condition c = cond_;
            c.item = items_[static_cast<std::size_t>(i)];
            send(c);
        };
    }
    // The condition, and the song's rows and cords that a Nei may read (by id, named by their place in the list).
    void set(const Condition& c, const Song& s) {
        cond_ = c;
        const bool unknown = c.kind == Condition::Kind::Unknown;
        kind_.setSelected(unknown ? -1 : static_cast<int>(c.kind));
        not_.setSelected(c.negate ? 1 : 0);
        a_.setValue(c.a), b_.setValue(c.b);
        items_.clear();
        item_.clear(juce::dontSendNotification);
        auto add = [&](std::uint64_t id, const std::string& text) {
            items_.push_back(id);
            item_.addItem(u8(text.c_str()), static_cast<int>(items_.size()));
            if (id == c.item) item_.setSelectedId(static_cast<int>(items_.size()), juce::dontSendNotification);
        };
        for (const Row& r : s.matrix) add(r.id, "Change " + std::to_string(itemNumber(s, r.id)) + ": " + sentence(s, r));
        for (const Cord& cd : s.mod.cords) add(cd.id, "Change " + std::to_string(itemNumber(s, cd.id)));
        const bool on = c.kind != Condition::Kind::None && !unknown;
        not_.setVisible(on);
        a_.setVisible(c.kind == Condition::Kind::AB);
        b_.setVisible(c.kind == Condition::Kind::AB);
        item_.setVisible(c.kind == Condition::Kind::Nei);
        setSize(getWidth(), heightNeeded());
        resized();
    }
    int heightNeeded() const {
        return 94 + (not_.isVisible() ? 52 : 0) + (a_.isVisible() ? 92 : 0) + (item_.isVisible() ? 54 : 0);
    }
    void resized() override {
        auto r = getLocalBounds();
        kind_.setBounds(r.removeFromTop(94));
        if (not_.isVisible()) r.removeFromTop(8), not_.setBounds(r.removeFromTop(44));
        if (a_.isVisible()) r.removeFromTop(2), a_.setBounds(r.removeFromTop(46)), b_.setBounds(r.removeFromTop(44));
        if (item_.isVisible()) r.removeFromTop(6), item_.setBounds(r.removeFromTop(48));
    }

private:
    void send(const Condition& c) {
        if (onChange) onChange(c);
    }
    Condition cond_;
    Segmented kind_, not_;
    ValueBar a_, b_;
    juce::ComboBox item_;
    std::vector<std::uint64_t> items_;
};

// The list (§17.8): rows and cords, one list in §17.4's order. A card per item: its letter, its sentence, how often
// and when it next acts, and its on switch.
class RowList : public juce::Component {
public:
    std::function<void(std::size_t)> onPick;
    std::function<void(std::size_t)> onToggle;
    struct Item {
        std::string tag, sentence, chance, next;
        bool on, temporary;
        bool level = false;  // a level cord: no letter, a wave
        int wave = 0;        // which of its source's levels: the colour its card draws it in
    };
    void set(std::vector<Item> items, std::size_t selected) {
        items_ = std::move(items);
        selected_ = selected;
        setSize(getWidth(), heightNeeded());
        repaint();
    }
    int heightNeeded() const { return static_cast<int>(items_.size()) * 104; }
    juce::Rectangle<int> card(std::size_t i) const { return {0, static_cast<int>(i) * 104, getWidth(), 94}; }
    juce::Rectangle<int> toggle(std::size_t i) const { return card(i).removeFromRight(80).withSizeKeepingCentre(56, 32); }
    void paint(juce::Graphics& g) override {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const auto r = card(i);
            const Item& it = items_[i];
            const bool sel = i == selected_;
            g.setColour(sel ? colour::raised : colour::panel);
            g.fillRoundedRectangle(r.toFloat(), 16.0f);
            if (sel) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(r.toFloat().reduced(1.0f), 16.0f, 2.0f);
            }
            auto in = r.reduced(18, 14);
            const auto tag = in.removeFromLeft(40).withSizeKeepingCentre(40, 40).toFloat();
            g.setColour(it.temporary ? colour::yellow : colour::raised.brighter(0.08f));
            g.fillRoundedRectangle(tag, 10.0f);
            if (it.level) {
                // A level cord: a small wave, in the source's colour.
                juce::Path wave;
                const auto w = tag.reduced(8.0f, 12.0f);
                for (int k = 0; k <= 16; ++k) {
                    const float x = w.getX() + w.getWidth() * static_cast<float>(k) / 16.0f;
                    const float y = w.getCentreY() - w.getHeight() * 0.5f * std::sin(static_cast<float>(k) / 16.0f * 6.2832f);
                    if (k == 0) wave.startNewSubPath(x, y);
                    else wave.lineTo(x, y);
                }
                g.setColour(it.on ? colour::voice(it.wave + 4) : colour::dim);
                g.strokePath(wave, juce::PathStrokeType(2.0f));
            } else {
                g.setColour(it.temporary ? colour::ground : (it.on ? colour::soft : colour::dim));
                g.setFont(mono(16.0f));
                g.drawText(it.tag, tag.toNearestInt(), juce::Justification::centred);
            }
            in.removeFromLeft(16);
            in.removeFromRight(80);
            g.setColour(it.on ? colour::text : colour::dim);
            g.setFont(sans(16.5f));
            g.drawFittedText(it.sentence, in.removeFromTop(46), juce::Justification::topLeft, 2, 0.9f);
            g.setColour(colour::dim);
            g.setFont(sans(13.0f));
            g.drawText(it.chance + "    " + it.next, in, juce::Justification::bottomLeft);
            const auto t = toggle(i).toFloat();
            g.setColour(it.on ? colour::olive : juce::Colour(0xff3a3942));
            g.fillRoundedRectangle(t, 16.0f);
            g.setColour(colour::text);
            g.fillEllipse(juce::Rectangle<float>(it.on ? t.getRight() - 28.0f : t.getX() + 4.0f, t.getY() + 4.0f, 24.0f, 24.0f));
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (toggle(i).expanded(8).contains(e.getPosition())) {
                if (onToggle) onToggle(i);
                return;
            }
            if (card(i).contains(e.getPosition()) && onPick) onPick(i);
        }
    }

private:
    std::vector<Item> items_;
    std::size_t selected_ = 0;
};

// ---- the panel --------------------------------------------------------------------------------------------

class RowEditor : public juce::Component {
public:
    explicit RowEditor(HocketProcessor& p)
        : proc_(p), kind_({"Vary notes", "Vary rhythm", "New notes", "New rhythm", "A setting", "The key"}, 0, 3),
          unit_({"bars", "phrases", "gongs", "kenongs"}), from_({"From the start", "From the end"}), every_("Every", 1, 32, 4, colour::yellow),
          on_("On bar", 1, 64, 1, colour::yellow), amount_("How far", 1, 16, 1, colour::yellow), chance_("How often it fires", 0, 100, 100, colour::yellow),
          min_("No lower than", 0, 1, 0, colour::yellow), max_("No higher than", 0, 1, 1, colour::yellow), lasts_({"Stays", "Reverts"}),
          bars_("For", 1, 16, 1, colour::yellow), seed_({"Same each time", "New each time"}) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&kind_, &target_, &setting_, &every_, &unit_, &on_, &from_, &cond_, &amount_, &chance_,
                                  &min_, &max_, &allowed_, &lasts_, &bars_, &seed_, &duplicate_, &remove_})
            addAndMakeVisible(c);
        cond_.onChange = [this](Condition c) { row([c](Song&, Row& r) { r.cond = c; }); };
        for (ValueBar* b : {&every_, &on_, &amount_, &chance_, &min_, &max_, &bars_}) b->setLayout(ValueBar::Layout::Inline);
        chance_.format = [](int v) { return v == 100 ? juce::String("always") : juce::String(v) + " %"; };
        bars_.format = [](int v) { return juce::String(v) + (v == 1 ? " bar" : " bars"); };
        on_.format = [this](int v) { return from_.selected() == 1 ? juce::String(ordinalFromEnd(v)) : "bar " + juce::String(v); };
        for (juce::ComboBox* c : {&target_, &setting_}) {
            c->setColour(juce::ComboBox::backgroundColourId, colour::raised);
            c->setColour(juce::ComboBox::outlineColourId, colour::line);
            c->setColour(juce::ComboBox::textColourId, colour::text);
        }
        kind_.onChange = [this](int k) {
            row([k](Song& s, Row& r) {
                switch (k) {
                    case VaryNotes: r.param = Param::VaryPitch; break;
                    case VaryRhythm: r.param = Param::VaryRhythm; if (r.scope != Scope::Group && r.scope != Scope::Global) r.scope = Scope::Group, r.target = s.groups.empty() ? 0 : s.groups[0].id; break;
                    case NewNotes: r.param = Param::RerollPitch; break;
                    case NewRhythm: r.param = Param::RerollRhythm; if (r.scope == Scope::Voice || r.scope == Scope::Harmony) r.scope = Scope::Group, r.target = s.groups.empty() ? 0 : s.groups[0].id; break;
                    case Setting: setSetting(r, settingsFor(r.scope).front()); break;
                    case TheKey: r.param = Param::Root, r.scope = Scope::Global, r.target = 0; break;
                }
                if (k != Setting) r.key.clear();
                r.min.reset(), r.max.reset(), r.allowed.clear();
            });
        };
        target_.onChange = [this] {
            const int i = target_.getSelectedItemIndex();
            if (i < 0 || i >= static_cast<int>(targets_.size())) return;
            const auto [scope, id] = targets_[static_cast<std::size_t>(i)];
            row([scope = scope, id = id](Song&, Row& r) {
                r.scope = scope, r.target = id;
                if (kindOf(r) == Setting) {
                    const auto opts = settingsFor(scope);
                    if (std::find(opts.begin(), opts.end(), settingOf(r)) == opts.end()) setSetting(r, opts.front());
                }
                r.min.reset(), r.max.reset(), r.allowed.clear();
            });
        };
        setting_.onChange = [this] {
            const int i = setting_.getSelectedItemIndex();
            if (i < 0 || i >= static_cast<int>(settings_.size())) return;
            const Setting_ p = settings_[static_cast<std::size_t>(i)];
            row([p](Song&, Row& r) {
                setSetting(r, p);
                r.min.reset(), r.max.reset(), r.allowed.clear();
            });
        };
        every_.onChange = [this](int v) { row([v](Song&, Row& r) { r.every = v; }); };
        unit_.onChange = [this](int u) { row([u](Song&, Row& r) { r.inPhrases = u == 1, r.clock = u >= 2 ? u : 0; }); };  // §18.2
        from_.onChange = [this](int f) {
            const int k = on_.value();
            row([f, k](Song&, Row& r) { r.offset = f == 1 ? -k : k - 1; });
        };
        on_.onChange = [this](int k) {
            const bool end = from_.selected() == 1;
            row([end, k](Song&, Row& r) { r.offset = end ? -k : k - 1; });
        };
        amount_.onChange = [this](int v) { row([v](Song&, Row& r) { r.maxShift = v; }); };
        chance_.onChange = [this](int v) { row([v](Song&, Row& r) { r.chance = v; }); };
        min_.onChange = [this](int v) { row([v](Song&, Row& r) { r.min = v; }); };
        max_.onChange = [this](int v) { row([v](Song&, Row& r) { r.max = v; }); };
        allowed_.onChange = [this](std::vector<int> a) { row([a](Song&, Row& r) { r.allowed = a; }); };
        lasts_.onChange = [this](int l) {
            const int n = bars_.value();
            row([l, n](Song&, Row& r) { r.lasts = l == 0 ? 0 : n; });
        };
        bars_.onChange = [this](int n) { row([n](Song&, Row& r) { if (r.lasts > 0) r.lasts = n; }); };
        seed_.onChange = [this](int s) { row([s](Song&, Row& r) { r.fixedSeed = s == 0; }); };
        duplicate_.onClick = [this] {
            proc_.edit([this](Song& s) {
                for (std::size_t i = 0; i < s.matrix.size(); ++i)
                    if (s.matrix[i].id == id_) {
                        Row copy = s.matrix[i];
                        Row& fresh = addRow(s);
                        const std::uint64_t newId = fresh.id;
                        fresh = copy;
                        fresh.id = newId;
                        id_ = newId;
                        return;
                    }
            });
        };
        remove_.onClick = [this] { proc_.edit([this](Song& s) { removeRow(s, id_); }); };
    }

    void show(std::uint64_t id) {
        id_ = id;
        const Song& s = proc_.document();
        const Row* found = nullptr;
        for (const Row& r : s.matrix)
            if (r.id == id) found = &r;
        setVisible(found != nullptr);
        if (!found) return;
        const Row& r = *found;
        sentence_ = sentence(s, r);
        const Kind k = kindOf(r);
        kind_.setSelected(k);
        // What it applies to: the song, the chords, each group, each voice (by kind, the ones that make sense).
        targets_.clear();
        target_.clear(juce::dontSendNotification);
        auto addTarget = [&](Scope sc, std::uint64_t tid, const std::string& name) {
            targets_.push_back({sc, tid});
            target_.addItem(name, static_cast<int>(targets_.size()));
            if (sc == r.scope && (sc == Scope::Global || sc == Scope::Harmony || tid == r.target))
                target_.setSelectedId(static_cast<int>(targets_.size()), juce::dontSendNotification);
        };
        if (k != TheKey) {
            addTarget(Scope::Global, 0, "The song: every group");
            if (k == VaryNotes || k == NewNotes || k == Setting) addTarget(Scope::Harmony, 0, "The chords");
            for (const Group& g : s.groups) addTarget(Scope::Group, g.id, "Group: " + g.name);
            if (k == VaryNotes || k == NewNotes || k == Setting)
                for (const Group& g : s.groups)
                    for (const Voice& v : g.voices) addTarget(Scope::Voice, v.id, "Voice: " + voiceName(s, v.id) + " (" + g.name + ")");
        }
        target_.setVisible(k != TheKey);
        // Which setting (a setting row), or which of the key (root or scale).
        settings_.clear();
        setting_.clear(juce::dontSendNotification);
        if (k == Setting) settings_ = settingsFor(r.scope);
        if (k == TheKey) settings_ = {{Param::Root, {}}, {Param::Scale, {}}};
        for (std::size_t i = 0; i < settings_.size(); ++i) {
            const Setting_& x = settings_[i];
            juce::String name;
            if (k == TheKey) name = x.param == Param::Root ? "The root (by fifths)" : "The scale (one note at a time)";
            else if (const auto gt = x.key.empty() ? std::nullopt : generatorTarget(x.key))
                name = juce::String(gt->gen->label) + ": " + gt->param->label;  // a generator's knob, under its generator
            else if (formDest(x.key)) name = x.key == "irama" ? "Irama (notes a chord)" : "Tihai now";
            else name = juce::String(paramName(x.param)).substring(0, 1).toUpperCase() + juce::String(paramName(x.param)).substring(1);
            setting_.addItem(name, static_cast<int>(i) + 1);
            if (x == settingOf(r)) setting_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        setting_.setVisible(!settings_.empty());
        every_.setValue(r.every);
        unit_.setSelected(r.clock >= 2 ? r.clock : r.inPhrases ? 1 : 0);
        from_.setSelected(r.offset < 0 ? 1 : 0);
        // §18.2: a gong cycle's Offset is in bars; a kenong row's in strokes.
        const int period = std::max(1, r.every) * (r.clock == 2 ? static_cast<int>(gongBars(s)) : r.clock == 3 ? 1 : r.inPhrases ? std::max(1, s.phraseLength) : 1);
        on_.setRange(1, std::max(1, period));
        on_.setValue(r.offset < 0 ? -r.offset : r.offset % std::max(1, period) + 1);
        on_.setVisible(period > 1);
        from_.setVisible(period > 1);
        amount_.setLabel(k == VaryNotes ? (r.scope == Scope::Harmony ? "How many chords" : "How many notes") : k == VaryRhythm ? "How many hits" : "How far");
        amount_.setVisible(k == VaryNotes || k == VaryRhythm || rowNumeric(r) || (r.key.empty() && (r.param == Param::Root || r.param == Param::Scale)) ||
                           rowPick(r));
        amount_.setValue(r.maxShift);
        chance_.setValue(r.chance);
        const bool bounds = k == Setting && rowNumeric(r);
        min_.setVisible(bounds);
        max_.setVisible(bounds);
        if (bounds) {
            const auto gt = genTargetOf(r);
            const auto [lo, hi] = gt ? std::pair<int, int>{gt->param->lo, gt->param->hi} : spanOf(r.param);
            min_.setRange(lo, hi);
            max_.setRange(lo, hi);
            min_.setValue(r.min ? *r.min : lo);
            max_.setValue(r.max ? *r.max : hi);
        }
        const juce::StringArray opts = pickOptions(s, r);
        allowed_.setVisible(!opts.isEmpty());
        if (!opts.isEmpty()) {
            std::vector<int> a = r.allowed;
            if (a.empty() && r.param != Param::Scale)
                for (int i = 0; i < opts.size(); ++i) a.push_back(i);  // empty means all, except for Scale (opt in, §7.2)
            allowed_.set(opts, a);
        }
        lasts_.setSelected(r.lasts == 0 ? 0 : 1);
        bars_.setValue(r.lasts == 0 ? 1 : r.lasts);
        bars_.setVisible(r.lasts > 0);
        seed_.setSelected(r.fixedSeed ? 0 : 1);
        seed_.setVisible(k == VaryNotes || k == VaryRhythm || k == NewNotes || k == NewRhythm);
        cond_.setSize(getWidth() - 48, 10);
        cond_.set(r.cond, s);
        resized();
        setSize(getWidth(), heightNeeded());
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::sidebar);
        g.setColour(colour::line);
        g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
        g.setColour(colour::text);
        g.setFont(sans(19.0f));
        g.drawFittedText(sentence_, sentenceArea_, juce::Justification::topLeft, 5, 1.0f);
        for (const auto& [text, r] : labels_) heading(g, text, r);
    }

    void resized() override {
        labels_.clear();
        // A column with no bottom: the panel grows to fit what the row needs, and the viewport scrolls it.
        auto r = juce::Rectangle<int>(24, 22, getWidth() - 48, 1 << 20);
        sentenceArea_ = r.removeFromTop(118);
        r.removeFromTop(8);
        auto section = [&](const char* name, int h) {
            labels_.push_back({name, r.removeFromTop(20)});
            r.removeFromTop(6);
            auto s = r.removeFromTop(h);
            r.removeFromTop(14);
            return s;
        };
        kind_.setBounds(section("Change", 94));
        if (target_.isVisible() || setting_.isVisible()) {
            auto t = section(target_.isVisible() ? "To" : "Which", target_.isVisible() && setting_.isVisible() ? 102 : 48);
            if (target_.isVisible()) target_.setBounds(t.removeFromTop(48)), t.removeFromTop(6);
            if (setting_.isVisible()) setting_.setBounds(t.removeFromTop(48));
        }
        auto when = section("When", on_.isVisible() ? 150 : 46);
        auto everyRow = when.removeFromTop(46);
        unit_.setBounds(everyRow.removeFromRight(170));
        everyRow.removeFromRight(10);
        every_.setBounds(everyRow);
        if (on_.isVisible()) {
            when.removeFromTop(8);
            from_.setBounds(when.removeFromTop(44));
            when.removeFromTop(8);
            on_.setBounds(when.removeFromTop(44));
        }
        cond_.setBounds(section("Only when", cond_.heightNeeded()));
        auto how = section("How much", 46 * (1 + (amount_.isVisible() ? 1 : 0) + (min_.isVisible() ? 2 : 0)));
        if (amount_.isVisible()) amount_.setBounds(how.removeFromTop(46));
        chance_.setBounds(how.removeFromTop(46));
        if (min_.isVisible()) {
            min_.setBounds(how.removeFromTop(46));
            max_.setBounds(how.removeFromTop(46));
        }
        if (allowed_.isVisible()) {
            allowed_.setBounds(0, 0, r.getWidth(), 10);
            allowed_.setBounds(section("May choose", allowed_.layout()));
        }
        auto lasts = section("Lasts", bars_.isVisible() ? 98 : 44);
        lasts_.setBounds(lasts.removeFromTop(44));
        if (bars_.isVisible()) lasts.removeFromTop(8), bars_.setBounds(lasts.removeFromTop(46));
        if (seed_.isVisible()) seed_.setBounds(section("Each time it fires", 44));
        r.removeFromTop(6);
        auto buttons = r.removeFromTop(48);
        duplicate_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 5));
        buttons.removeFromLeft(10);
        remove_.setBounds(buttons);
    }

    // Laid out top-down, so its height is wherever the last button ends.
    int heightNeeded() const { return remove_.getBottom() + 24; }

private:
    static std::pair<int, int> spanOf(Param p) {
        switch (p) {
            case Param::Accent: case Param::Swing: case Param::Probability: case Param::Ratchet: case Param::HumanTiming: return {0, 100};
            case Param::HumanVelocity: return {0, 32};
            case Param::Handoff: return {0, 50};
            case Param::Repeats: return {2, 4};
            case Param::GatePercent: return {1, 200};
            case Param::OctaveLow: case Param::OctaveHigh: return {-1, 2};
            case Param::Steps: return {1, 64};
            case Param::Offset: return {0, 63};
            case Param::Weight: case Param::Stagger: case Param::Top: return {0, 16};
            case Param::ChordSize: return {1, 6};
            default: return {1, 16};
        }
    }
    void row(const std::function<void(Song&, Row&)>& f) {
        proc_.edit([this, f](Song& s) {
            for (Row& r : s.matrix)
                if (r.id == id_) f(s, r);
        });
    }

    HocketProcessor& proc_;
    std::uint64_t id_ = 0;
    std::string sentence_;
    Segmented kind_;
    juce::ComboBox target_, setting_;
    std::vector<std::pair<Scope, std::uint64_t>> targets_;
    std::vector<Setting_> settings_;
    Segmented unit_, from_;
    ConditionEditor cond_;
    ValueBar every_, on_, amount_, chance_, min_, max_;
    Allowed allowed_;
    Segmented lasts_;
    ValueBar bars_;
    Segmented seed_;
    Pill duplicate_{"Duplicate"}, remove_{"Delete", Pill::Kind::Danger};
    juce::Rectangle<int> sentenceArea_;
    std::vector<std::pair<const char*, juce::Rectangle<int>>> labels_;
};

// ---- modulation: sources and cords (§17) ------------------------------------------------------------------------

std::string sourceName(const ModSource* src) {
    if (!src) return "a source that's gone";
    if (!src->name.empty()) return src->name;
    const SourceKind* k = findSourceKind(src->kind);
    return k ? k->label : src->kind;
}
// An output as the editor names it: "LFO 1", "Converge".
std::string outputLabel(const std::string& out) {
    if (out.size() == 4 && out.compare(0, 3, "lfo") == 0) return "LFO " + out.substr(3);
    if (out.empty()) return out;
    std::string o = out;
    o[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(o[0])));
    return o;
}
// What a trigger output's firing is called in a sentence: "When Tides converge, …".
std::string eventOf(const std::string& out) {
    if (out == "converge") return "converge";
    if (out == "step") return "steps";
    if (out == "change") return "changes";
    if (out == "section") return "reaches a new section";
    if (out == "gong") return "strikes the gong";  // §18.2
    if (out == "kenong") return "strikes a kenong";
    return "fires";
}
// Whether a trigger's amount is always 1 (so As given moves by Scale exactly).
bool amountIsOne(const std::string& out) { return out == "converge" || out == "gong"; }

// A trigger cord as the row it acts like (§17.3), for its sentence and its panel.
Row asRow(const Cord& c) {
    Row r;
    r.id = c.id;
    r.scope = c.dest.scope;
    r.target = c.dest.target;
    if (const auto p = paramFromKey(c.dest.key)) r.param = *p;
    else r.key = c.dest.key;
    r.maxShift = c.maxShift, r.min = c.min, r.max = c.max, r.lasts = c.lasts, r.fixedSeed = c.fixedSeed, r.allowed = c.allowed;
    r.chance = c.chance, r.on = c.on;
    return r;
}

// A destination key's name in a sentence: "density", "Euclidean pulses".
std::string keyName(const std::string& key) {
    if (key == "mute") return "mute";
    if (const auto p = paramFromKey(key)) return paramName(*p);
    if (const auto g = generatorTarget(key)) return genKnobName(*g);
    if (formDest(key)) return formDestName(key);
    return "a setting this version doesn't know (" + key + ")";
}
std::string targetName(const Song& s, Scope scope, std::uint64_t target) {
    Row r;
    r.scope = scope, r.target = target;
    return targetName(s, r);
}

struct DestOption {
    std::string key;
    juce::String label;
};
// What a cord may aim at, at a scope (§17.2): for a trigger, the actions and every setting; for a level, the numeric
// settings that move no clock, and Mute.
std::vector<DestOption> destOptions(Scope sc, bool level) {
    std::vector<DestOption> out;
    if (!level) {
        switch (sc) {
            case Scope::Global:
                out = {{"vary.pitch", "Vary every line's notes"}, {"vary.rhythm", "Vary every rhythm"}, {"reroll.pitch", "New notes"},
                       {"reroll.rhythm", "New rhythm"}, {"reroll.deal", "Deal again"}, {"reroll.all", "Everything new"},
                       {"root", "The root (by fifths)"}, {"scale", "The scale"}};
                break;
            case Scope::Harmony: out = {{"vary.pitch", "Turn chords"}, {"reroll.all", "A new progression"}}; break;
            case Scope::Group:
                out = {{"vary.pitch", "Vary notes"}, {"vary.rhythm", "Vary rhythm"}, {"reroll.pitch", "New notes"}, {"reroll.rhythm", "New rhythm"},
                       {"reroll.deal", "Deal again"}, {"reroll.all", "Everything new"}};
                break;
            case Scope::Voice: out = {{"vary.pitch", "Vary notes"}, {"reroll.pitch", "New notes"}}; break;
        }
    }
    for (const Setting_& x : settingsFor(sc)) {
        const std::string key = x.key.empty() ? paramKey(x.param) : x.key;
        if (level && !levelMayDrive(key)) continue;
        juce::String name;
        if (const auto gt = x.key.empty() ? std::nullopt : generatorTarget(x.key)) name = juce::String(gt->gen->label) + ": " + gt->param->label;
        else if (formDest(x.key)) name = x.key == "irama" ? "Irama (notes a chord)" : "Tihai now";
        else name = juce::String(paramName(x.param)).substring(0, 1).toUpperCase() + juce::String(paramName(x.param)).substring(1);
        out.push_back({key, name});
    }
    if (level && (sc == Scope::Group || sc == Scope::Voice)) out.push_back({"mute", "Mute"});
    return out;
}

std::string signedText(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }

// §17.8: a cord as a sentence. "Tides 1 moves the Leads' density up to ±4"; "When Tides converge, every other time,
// reroll the Bass rhythm for 4 bars".
std::string cordSentence(const Song& s, const Cord& c) {
    const ModSource* src = sourceById(s, c.source);
    const std::string name = sourceName(src);
    const auto kind = cordKind(s, c);
    const std::string t = targetName(s, c.dest.scope, c.dest.target);
    if (!kind) return name + " (" + (src ? "an output it doesn't have: " + c.output : std::string("gone")) + ") does nothing.";
    if (*kind == OutputKind::Level) {
        // "Tides 1" for one of several LFOs; a source with one level is just its name.
        const std::string who = name + (outputLabel(c.output).rfind("LFO ", 0) == 0 ? " " + c.output.substr(3) : std::string());
        if (c.dest.key == "mute") {
            if (c.dest.scope != Scope::Group && c.dest.scope != Scope::Voice) return who + " can only mute a group or a voice.";
            return who + " mutes " + (c.dest.scope == Scope::Group ? "the " + t : t) + " while it's " + (c.invert ? "at or above " : "below ") +
                   std::to_string(c.threshold) + ".";
        }
        if (!levelMayDrive(c.dest.key)) return who + " can't move " + possessive(t) + " " + keyName(c.dest.key) + " (a level moves only numbers that keep time).";
        return who + " moves " + possessive(t) + " " + keyName(c.dest.key) + " up to ±" + std::to_string(std::abs(c.depth)) +
               (c.depth < 0 ? ", against it." : ".");
    }
    std::string when = "When " + name + " " + eventOf(c.output) + condPhrase(s, c.cond) + ", ";
    if (c.dest.key == "mute") return when + "nothing: a trigger can't mute (patch a level).";
    const Row r = asRow(c);
    const auto p = paramFromKey(c.dest.key);
    const bool numeric = rowNumeric(r) || (p && *p == Param::Root);
    if (numeric && c.move == 1) {
        const int k = std::max(1, c.maxShift);
        std::string what = p && *p == Param::Root ? "move the key " : "move " + possessive(t) + " " + keyName(c.dest.key) + " ";
        if (amountIsOne(c.output)) {
            const int d = std::max(-k, std::min(k, c.scale));
            what += d == 0 ? "by nothing" : "by " + signedText(d) + (p && *p == Param::Root ? (std::abs(d) == 1 ? " fifth" : " fifths") : "");
        } else {
            what += std::string("by ") + (c.scale == 1 ? "the amount" : c.scale == -1 ? "against the amount" : "the amount × " + std::to_string(c.scale)) +
                    ", up to " + std::to_string(k);
        }
        if (c.min || c.max)
            what += ", between " + (c.min ? std::to_string(*c.min) : std::string("the lowest")) + " and " +
                    (c.max ? std::to_string(*c.max) : std::string("the highest"));
        return when + what + (c.lasts == 0 ? ". It stays." : ", for " + std::to_string(c.lasts) + (c.lasts == 1 ? " bar." : " bars."));
    }
    return when + actionOf(s, r);
}

// ---- the sources' cards -------------------------------------------------------------------------------------------

// §17.8: a card per source: its name and kind, each level drawn over the next 32 bars, each trigger's firings as
// ticks under it, and the next one, exact.
class SourceCards : public juce::Component {
public:
    struct Card {
        std::string name, kind, next;
        std::vector<std::vector<int>> levels;  // per level output: 32 values, −1000 … 1000
        std::vector<int> fires;                // bars (0 … 31 ahead) a trigger fires
    };
    std::function<void(std::size_t)> onPick;
    void set(std::vector<Card> cards, int selected) {
        cards_ = std::move(cards);
        selected_ = selected;
        setSize(std::max(1, static_cast<int>(cards_.size()) * (kW + 12)), getHeight());
        repaint();
    }
    static constexpr int kW = 264;
    juce::Rectangle<int> card(std::size_t i) const { return {static_cast<int>(i) * (kW + 12), 0, kW, getHeight()}; }
    void paint(juce::Graphics& g) override {
        for (std::size_t i = 0; i < cards_.size(); ++i) {
            const Card& c = cards_[i];
            const auto r = card(i);
            const bool sel = static_cast<int>(i) == selected_;
            g.setColour(sel ? colour::raised : colour::panel);
            g.fillRoundedRectangle(r.toFloat(), 16.0f);
            if (sel) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(r.toFloat().reduced(1.0f), 16.0f, 2.0f);
            }
            auto in = r.reduced(16, 12);
            auto top = in.removeFromTop(24);
            g.setColour(colour::text);
            g.setFont(sans(17.0f, true));
            g.drawText(u8(c.name.c_str()), top, juce::Justification::centredLeft);
            g.setColour(colour::dim);
            g.setFont(sans(13.0f, true));
            g.drawText(juce::String(c.kind).toUpperCase(), top, juce::Justification::centredRight);
            in.removeFromTop(6);
            auto bottom = in.removeFromBottom(20);
            auto graph = in.toFloat();
            g.setColour(colour::line);
            g.drawHorizontalLine(static_cast<int>(graph.getCentreY()), graph.getX(), graph.getRight());
            for (std::size_t k = 0; k < c.levels.size(); ++k) {
                juce::Path p;
                const auto& v = c.levels[k];
                for (std::size_t b = 0; b < v.size(); ++b) {
                    const float x = graph.getX() + graph.getWidth() * static_cast<float>(b) / 31.0f;
                    const float y = graph.getCentreY() - graph.getHeight() * 0.45f * static_cast<float>(v[b]) / 1000.0f;
                    if (b == 0) p.startNewSubPath(x, y);
                    else p.lineTo(x, y);
                }
                g.setColour(colour::voice(static_cast<int>(k) + 4));  // lilac, pink, blue, …
                g.strokePath(p, juce::PathStrokeType(2.2f));
            }
            g.setColour(colour::yellow);
            for (int b : c.fires) {
                const float x = graph.getX() + graph.getWidth() * static_cast<float>(b) / 31.0f;
                g.fillRoundedRectangle(x - 2.0f, graph.getBottom() - 12.0f, 4.0f, 12.0f, 2.0f);
            }
            g.setColour(colour::soft);
            g.setFont(sans(13.0f));
            g.drawText(u8(c.next.c_str()), bottom, juce::Justification::bottomLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (std::size_t i = 0; i < cards_.size(); ++i)
            if (card(i).contains(e.getPosition()) && onPick) onPick(i);
    }

private:
    std::vector<Card> cards_;
    int selected_ = -1;
};

// A source's card, worked out from the engine: its levels over the next 32 bars, its triggers' next firings.
SourceCards::Card cardOf(const Song& s, const ModSource& src, std::int64_t bar,
                         const std::function<std::optional<std::int64_t>(const SourceOutput&)>& nextOf) {
    SourceCards::Card c;
    c.name = sourceName(&src);
    const SourceKind* k = findSourceKind(src.kind);
    c.kind = k ? k->label : src.kind + " (from a later version)";
    std::string next;
    for (const SourceOutput& o : outputsOf(src)) {
        if (o.kind == OutputKind::Level) {
            std::vector<int> v;
            for (std::int64_t b = bar; b < bar + 32; ++b) {
                const int x = levelAt(s, src, o.name, b).value_or(0);
                v.push_back(o.bipolar ? x : 2 * x - 1000);  // a unipolar level drawn over the whole height
            }
            c.levels.push_back(std::move(v));
        } else {
            for (std::int64_t b : triggerBars(s, src, o.name, bar, 32, 32)) c.fires.push_back(static_cast<int>(b - bar));
            const auto n = nextOf(o);
            if (next.empty()) next = !n ? outputLabel(o.name) + ": not within " + std::to_string(kScanBars) + " bars" : outputLabel(o.name) + ": bar " + std::to_string(*n + 1);
        }
    }
    c.next = next;
    return c;
}

// ---- a source's panel ---------------------------------------------------------------------------------------------

std::int64_t gcd64(std::int64_t a, std::int64_t b) { return b == 0 ? a : gcd64(b, a % b); }

class SourceEditor : public juce::Component {
public:
    std::function<void(std::uint64_t)> onPatched;  // a cord was made from it: select that
    explicit SourceEditor(HocketProcessor& p) : proc_(p) {
        addAndMakeVisible(name_);
        name_.setColour(juce::TextEditor::backgroundColourId, colour::raised);
        name_.setColour(juce::TextEditor::outlineColourId, colour::line);
        name_.setColour(juce::TextEditor::textColourId, colour::text);
        name_.setFont(juce::Font(sans(18.0f)));
        name_.onTextChange = [this] {
            const std::string n = name_.getText().toStdString();
            source([n](Song&, ModSource& m) { m.name = n; });
        };
        addAndMakeVisible(patch_);
        addAndMakeVisible(remove_);
        patch_.onClick = [this] {
            std::uint64_t made = 0;
            proc_.edit([this, &made](Song& s) {
                const ModSource* src = sourceById(s, id_);
                if (!src) return;
                const auto outs = outputsOf(*src);
                if (outs.empty()) return;
                Cord& c = addCord(s, id_, outs.front().name);
                c.dest = {s.groups.empty() ? Scope::Global : Scope::Group, s.groups.empty() ? 0 : s.groups[0].id,
                          s.groups.empty() ? "range" : "density"};
                made = c.id;
            });
            if (made && onPatched) onPatched(made);
        };
        remove_.onClick = [this] { proc_.edit([this](Song& s) { removeSource(s, id_); }); };
    }

    void show(std::uint64_t id) {
        id_ = id;
        const Song& s = proc_.document();
        const ModSource* src = sourceById(s, id);
        setVisible(src != nullptr);
        if (!src) return;
        if (name_.getText().toStdString() != src->name) name_.setText(u8(src->name.c_str()), juce::dontSendNotification);
        for (auto& [k, c] : controls_) c->setVisible(false);
        layout_.clear();
        const SourceKind* kind = findSourceKind(src->kind);
        title_ = sourceName(src) + ": " + (kind ? kind->label : "a kind from a later version; it does nothing here");
        if (kind) {
            for (const SourceParam& p : kind->params) place(*src, p, -1);
            const int count = *kind->listKey ? sourceValue(*src, "count") : 0;
            for (int i = 0; i < count; ++i) {
                layout_.push_back({"LFO " + std::to_string(i + 1), nullptr, 0});
                for (const SourceParam& p : kind->per) place(*src, p, i);
            }
            if (*kind->setKey) {
                Allowed& a = allowed("set");
                juce::StringArray names;
                for (int i = 0; i < count; ++i) names.add("LFO " + juce::String(i + 1));
                a.set(names, sourceSet(*src));
                layout_.push_back({"Converge when these meet", &a, -1});
            }
        }
        info_ = infoOf(s, *src);
        resized();
        setSize(getWidth(), heightNeeded());
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::sidebar);
        g.setColour(colour::line);
        g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
        g.setColour(colour::text);
        g.setFont(sans(19.0f));
        g.drawFittedText(u8(title_.c_str()), titleArea_, juce::Justification::topLeft, 3, 1.0f);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawFittedText(u8(info_.c_str()), infoArea_, juce::Justification::topLeft, 4, 1.0f);
        for (const auto& [text, r] : labels_) heading(g, text.c_str(), r);
    }

    void resized() override {
        labels_.clear();
        auto r = juce::Rectangle<int>(24, 22, getWidth() - 48, 1 << 20);
        titleArea_ = r.removeFromTop(56);
        infoArea_ = r.removeFromTop(66);
        r.removeFromTop(8);
        labels_.push_back({"Name", r.removeFromTop(20)});
        r.removeFromTop(6);
        name_.setBounds(r.removeFromTop(44));
        r.removeFromTop(14);
        for (const Slot& s : layout_) {
            if (!s.c) {
                labels_.push_back({s.label, r.removeFromTop(20)});
                r.removeFromTop(6);
                continue;
            }
            if (s.h < 0) {  // the chips: a heading, then as tall as they wrap
                labels_.push_back({s.label, r.removeFromTop(20)});
                r.removeFromTop(6);
                s.c->setBounds(0, 0, r.getWidth(), 10);
                s.c->setBounds(r.removeFromTop(static_cast<Allowed*>(s.c)->layout()));
                r.removeFromTop(14);
                continue;
            }
            s.c->setBounds(r.removeFromTop(s.h));
            r.removeFromTop(8);
        }
        r.removeFromTop(10);
        auto buttons = r.removeFromTop(48);
        patch_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 5));
        buttons.removeFromLeft(10);
        remove_.setBounds(buttons);
    }
    int heightNeeded() const { return remove_.getBottom() + 24; }

private:
    struct Slot {
        std::string label;
        juce::Component* c;
        int h;
    };

    // A parameter's control, made once and kept (a drag must not lose its bar), shown where the layout says.
    void place(const ModSource& src, const SourceParam& p, int i) {
        const std::string id = std::string(p.key) + "/" + std::to_string(i);
        const int value = i < 0 ? sourceValue(src, p.key) : itemValue(src, i, p.key);
        if (p.choice) {
            auto it = controls_.find(id);
            if (it == controls_.end()) {
                auto seg = std::make_unique<Segmented>(juce::StringArray::fromTokens(p.options, "|", ""));
                seg->onChange = [this, key = std::string(p.key), i](int v) { setParam(key, i, v); };
                addChildComponent(*seg);
                it = controls_.emplace(id, std::move(seg)).first;
            }
            auto* seg = static_cast<Segmented*>(it->second.get());
            seg->setSelected(value);
            seg->setVisible(true);
            layout_.push_back({p.label, seg, 44});
            return;
        }
        auto it = controls_.find(id);
        if (it == controls_.end()) {
            auto bar = std::make_unique<ValueBar>(p.label, p.lo, p.hi, value, colour::lilac);
            bar->setLayout(ValueBar::Layout::Inline);
            bar->onChange = [this, key = std::string(p.key), i](int v) { setParam(key, i, v); };
            addChildComponent(*bar);
            it = controls_.emplace(id, std::move(bar)).first;
        }
        auto* bar = static_cast<ValueBar*>(it->second.get());
        int lo = p.lo, hi = p.hi;
        if (std::string(p.key) == "phase" && i >= 0) {  // −1 (Seeded), or 0 … P − 1 in bars
            hi = static_cast<int>(std::min<std::int64_t>(p.hi, lfoPeriodBars(proc_.document(), src, i) - 1));
            bar->format = [](int v) { return v < 0 ? juce::String("Seeded") : juce::String(v) + (v == 1 ? " bar" : " bars"); };
        }
        const std::string key = p.key;
        if (src.kind == "accumulator" && key == "start") hi = sourceValue(src, "positions") - 1;  // 0 … S − 1
        if (src.kind == "accumulator" && key == "every") {
            const bool phrases = sourceValue(src, "inPhrases") == 1;
            bar->format = [phrases](int v) { return juce::String(v) + (phrases ? (v == 1 ? " phrase" : " phrases") : (v == 1 ? " bar" : " bars")); };
        }
        if (src.kind == "accumulator" && key == "offset")
            bar->format = [](int v) { return v < 0 ? juce::String(v == -1 ? "last bar" : juce::String(-v) + " from the end") : "bar " + juce::String(v + 1); };
        if (src.kind == "accumulator" && key == "step") bar->format = [](int v) { return v == 0 ? juce::String("+1") : (v > 0 ? "+" : "") + juce::String(v); };
        if (src.kind == "accumulator" && key == "reset") bar->format = [](int v) { return v == 0 ? juce::String("never") : juce::String(v) + " ticks"; };
        if (src.kind == "arc" && key == "span") {
            const int phrase = std::max(1, proc_.document().phraseLength);
            bar->format = [phrase](int v) { return juce::String(v) + " phrases (" + juce::String(v * phrase) + " bars)"; };
        }
        if (std::string(p.key) == "period" && i >= 0) {
            const bool phrases = itemValue(src, i, "unit") == 1;
            bar->format = [phrases](int v) { return juce::String(v) + (phrases ? (v == 1 ? " phrase" : " phrases") : (v == 1 ? " bar" : " bars")); };
        }
        bar->setRange(lo, hi);
        bar->setDefault(p.defs && i >= 0 ? p.defs[i] : p.def);
        bar->setValue(std::min(value, hi));
        bar->setVisible(true);
        layout_.push_back({"", bar, 46});
    }
    Allowed& allowed(const std::string& id) {
        auto it = controls_.find(id);
        if (it == controls_.end()) {
            auto a = std::make_unique<Allowed>();
            a->onChange = [this](std::vector<int> v) {
                source([v](Song&, ModSource& m) {
                    if (const SourceKind* k = findSourceKind(m.kind)) m.sets[k->setKey] = v;
                });
            };
            addChildComponent(*a);
            it = controls_.emplace(id, std::move(a)).first;
        }
        it->second->setVisible(true);
        return *static_cast<Allowed*>(it->second.get());
    }
    void setParam(const std::string& key, int i, int v) {
        source([key, i, v](Song&, ModSource& m) {
            if (i < 0) {
                if (key == "step" && m.kind == "accumulator" && v == 0) return;  // never 0 (§17.9)
                m.params[key] = v;
                return;
            }
            const SourceKind* k = findSourceKind(m.kind);
            if (!k) return;
            auto& list = m.lists[k->listKey];
            if (static_cast<int>(list.size()) <= i) list.resize(static_cast<std::size_t>(i) + 1);
            // An item written for the first time keeps its own defaults for the rest (the periods differ by item).
            for (const SourceParam& p : k->per)
                if (!list[static_cast<std::size_t>(i)].count(p.key)) list[static_cast<std::size_t>(i)][p.key] = itemValue(m, i, p.key);
            list[static_cast<std::size_t>(i)][key] = v;
        });
    }
    void source(const std::function<void(Song&, ModSource&)>& f) {
        proc_.edit([this, f](Song& s) {
            for (ModSource& m : s.mod.sources)
                if (m.id == id_) f(s, m);
        });
    }
    // What it does next, in words: the LFOs' periods, the next convergence, and a warning when two share a factor.
    static std::string infoOf(const Song& s, const ModSource& src) {
        if (src.kind == "accumulator") {
            static const char* edges[] = {"wrapping", "bouncing", "holding"};
            const int S = sourceValue(src, "positions"), every = sourceValue(src, "every");
            const bool phrases = sourceValue(src, "inPhrases") == 1;
            const auto next = triggerBars(s, src, "step", 1, 1);
            return "Steps by " + signedText(sourceValue(src, "step") == 0 ? 1 : sourceValue(src, "step")) + " every " +
                   (every == 1 ? std::string(phrases ? "phrase" : "bar") : std::to_string(every) + (phrases ? " phrases" : " bars")) + " through " +
                   std::to_string(S) + " positions, " + edges[sourceValue(src, "edge")] + " at its ends. " +
                   (next.empty() ? "It holds from here on." : "First step at bar " + std::to_string(next[0] + 1) + ".");
        }
        if (src.kind == "dice") {
            const int D = sourceValue(src, "dice");
            const bool phrases = sourceValue(src, "unit") == 1;
            return std::to_string(D) + (D == 1 ? " die" : " dice") + " on " + (phrases ? "phrases" : "bars") + ": the fastest changes every " +
                   (phrases ? "phrase" : "bar") + ", the slowest every " + std::to_string(1 << (D - 1)) + (phrases ? " phrases." : " bars.");
        }
        if (src.kind == "arc") {
            const int span = sourceValue(src, "span"), N = span * std::max(1, s.phraseLength);
            return std::to_string(span) + " phrases (" + std::to_string(N) + " bars): alap, jor, a breakdown, jhala, the peak, the release; then " +
                   (sourceValue(src, "end") == 0 ? "again from the start." : "it holds at the end.");
        }
        if (src.kind != "lfo") return "";
        std::string out;
        const std::vector<int> set = sourceSet(src);
        const auto next = triggerBars(s, src, "converge", 1, 1);
        out = set.empty() ? "No LFO is in the converge set: it never fires."
                          : next.empty() ? "They don't converge within a million bars." : "They next converge at bar " + std::to_string(next[0] + 1) + ".";
        for (std::size_t a = 0; a < set.size(); ++a)
            for (std::size_t b = a + 1; b < set.size(); ++b) {
                const std::int64_t pa = lfoPeriodBars(s, src, set[a]), pb = lfoPeriodBars(s, src, set[b]);
                if (gcd64(pa, pb) > 1) {
                    out += " LFOs " + std::to_string(set[a] + 1) + " and " + std::to_string(set[b] + 1) + " share a factor (" + std::to_string(pa) +
                           " and " + std::to_string(pb) + " bars), so they meet more often.";
                    return out;
                }
            }
        return out;
    }

    HocketProcessor& proc_;
    std::uint64_t id_ = 0;
    std::string title_, info_;
    juce::TextEditor name_;
    std::map<std::string, std::unique_ptr<juce::Component>> controls_;
    std::vector<Slot> layout_;
    Pill patch_{"Patch a cord", Pill::Kind::Primary}, remove_{"Delete", Pill::Kind::Danger};
    juce::Rectangle<int> titleArea_, infoArea_;
    std::vector<std::pair<std::string, juce::Rectangle<int>>> labels_;
};

// ---- a cord's panel -----------------------------------------------------------------------------------------------

class CordEditor : public juce::Component {
public:
    explicit CordEditor(HocketProcessor& p)
        : proc_(p), output_(juce::StringArray()), invert_({"While below", "While at or above"}), move_({"Random", "As given"}),
          depth_("How far", -64, 64, 4, colour::lilac), threshold_("Threshold", -1000, 1000, 500, colour::lilac),
          scale_("Scale", -16, 16, 1, colour::yellow), amount_("How far", 1, 16, 1, colour::yellow),
          chance_("How often it acts", 0, 100, 100, colour::yellow), min_("No lower than", 0, 1, 0, colour::yellow),
          max_("No higher than", 0, 1, 1, colour::yellow), lasts_({"Stays", "Reverts"}), bars_("For", 1, 16, 1, colour::yellow),
          seed_({"Same each time", "New each time"}) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&from_, &output_, &target_, &dest_, &depth_, &threshold_, &invert_, &cond_, &move_,
                                  &scale_, &amount_, &chance_, &min_, &max_, &allowed_, &lasts_, &bars_, &seed_, &duplicate_, &remove_})
            addAndMakeVisible(c);
        cond_.onChange = [this](Condition x) { cord([x](Song&, Cord& c) { c.cond = x; }); };
        for (ValueBar* b : {&depth_, &threshold_, &scale_, &amount_, &chance_, &min_, &max_, &bars_}) b->setLayout(ValueBar::Layout::Inline);
        depth_.format = [](int v) { return juce::String(v > 0 ? "+" : "") + juce::String(v); };
        scale_.format = [](int v) { return juce::String(v > 0 ? "+" : "") + juce::String(v) + juce::String::fromUTF8(" ×"); };
        chance_.format = [](int v) { return v == 100 ? juce::String("always") : juce::String(v) + " %"; };
        bars_.format = [](int v) { return juce::String(v) + (v == 1 ? " bar" : " bars"); };
        for (juce::ComboBox* c : {&from_, &target_, &dest_}) {
            c->setColour(juce::ComboBox::backgroundColourId, colour::raised);
            c->setColour(juce::ComboBox::outlineColourId, colour::line);
            c->setColour(juce::ComboBox::textColourId, colour::text);
        }
        from_.onChange = [this] {
            const int i = from_.getSelectedItemIndex();
            const Song& s = proc_.document();
            if (i < 0 || i >= static_cast<int>(s.mod.sources.size())) return;
            const std::uint64_t src = s.mod.sources[static_cast<std::size_t>(i)].id;
            cord([src](Song& song, Cord& c) {
                c.source = src;
                const ModSource* m = sourceById(song, src);
                if (m && !outputOf(*m, c.output) && !outputsOf(*m).empty()) c.output = outputsOf(*m).front().name;
                fitDest(c, song);
            });
        };
        output_.onChange = [this](int i) {
            if (i < 0 || i >= static_cast<int>(outputs_.size())) return;
            const std::string out = outputs_[static_cast<std::size_t>(i)];
            cord([out](Song& s, Cord& c) {
                c.output = out;
                fitDest(c, s);
            });
        };
        target_.onChange = [this] {
            const int i = target_.getSelectedItemIndex();
            if (i < 0 || i >= static_cast<int>(targets_.size())) return;
            const auto [scope, id] = targets_[static_cast<std::size_t>(i)];
            cord([scope = scope, id = id](Song& s, Cord& c) {
                c.dest.scope = scope, c.dest.target = id;
                fitDest(c, s);
                c.min.reset(), c.max.reset(), c.allowed.clear();
            });
        };
        dest_.onChange = [this] {
            const int i = dest_.getSelectedItemIndex();
            if (i < 0 || i >= static_cast<int>(dests_.size())) return;
            const std::string key = dests_[static_cast<std::size_t>(i)].key;
            cord([key](Song&, Cord& c) {
                c.dest.key = key;
                c.min.reset(), c.max.reset(), c.allowed.clear();
            });
        };
        depth_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.depth = v; }); };
        threshold_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.threshold = v; }); };
        invert_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.invert = v == 1; }); };
        move_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.move = v; }); };
        scale_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.scale = v; }); };
        amount_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.maxShift = v; }); };
        chance_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.chance = v; }); };
        min_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.min = v; }); };
        max_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.max = v; }); };
        allowed_.onChange = [this](std::vector<int> a) { cord([a](Song&, Cord& c) { c.allowed = a; }); };
        lasts_.onChange = [this](int l) {
            const int n = bars_.value();
            cord([l, n](Song&, Cord& c) { c.lasts = l == 0 ? 0 : n; });
        };
        bars_.onChange = [this](int n) { cord([n](Song&, Cord& c) { if (c.lasts > 0) c.lasts = n; }); };
        seed_.onChange = [this](int v) { cord([v](Song&, Cord& c) { c.fixedSeed = v == 0; }); };
        duplicate_.onClick = [this] {
            proc_.edit([this](Song& s) {
                for (const Cord& c : s.mod.cords)
                    if (c.id == id_) {
                        const Cord copy = c;
                        Cord& fresh = addCord(s, copy.source, copy.output);
                        const std::uint64_t newId = fresh.id;
                        fresh = copy;
                        fresh.id = newId;
                        id_ = newId;
                        return;
                    }
            });
        };
        remove_.onClick = [this] { proc_.edit([this](Song& s) { removeCord(s, id_); }); };
    }

    std::uint64_t selected() const { return id_; }

    void show(std::uint64_t id) {
        id_ = id;
        const Song& s = proc_.document();
        const Cord* found = nullptr;
        for (const Cord& c : s.mod.cords)
            if (c.id == id) found = &c;
        setVisible(found != nullptr);
        if (!found) return;
        const Cord& c = *found;
        sentence_ = cordSentence(s, c);
        // From: the source, and which of its outputs.
        from_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < s.mod.sources.size(); ++i) {
            from_.addItem(u8(sourceName(&s.mod.sources[i]).c_str()), static_cast<int>(i) + 1);
            if (s.mod.sources[i].id == c.source) from_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        outputs_.clear();
        juce::StringArray outs;
        int sel = -1;
        if (const ModSource* src = sourceById(s, c.source))
            for (const SourceOutput& o : outputsOf(*src)) {
                if (o.name == c.output) sel = static_cast<int>(outputs_.size());
                outputs_.push_back(o.name);
                outs.add(u8(outputLabel(o.name).c_str()));
            }
        output_.setOptions(outs);
        output_.setSelected(sel);
        const auto kind = cordKind(s, c);
        const bool level = kind == OutputKind::Level;
        // To: what it moves, scope → target → parameter (a generator's knobs under their generator).
        targets_.clear();
        target_.clear(juce::dontSendNotification);
        auto addTarget = [&](Scope sc, std::uint64_t tid, const std::string& name) {
            targets_.push_back({sc, tid});
            target_.addItem(u8(name.c_str()), static_cast<int>(targets_.size()));
            if (sc == c.dest.scope && (sc == Scope::Global || sc == Scope::Harmony || tid == c.dest.target))
                target_.setSelectedId(static_cast<int>(targets_.size()), juce::dontSendNotification);
        };
        addTarget(Scope::Global, 0, "The song: every group");
        addTarget(Scope::Harmony, 0, "The chords");
        for (const Group& g : s.groups) addTarget(Scope::Group, g.id, "Group: " + g.name);
        for (const Group& g : s.groups)
            for (const Voice& v : g.voices) addTarget(Scope::Voice, v.id, "Voice: " + voiceName(s, v.id) + " (" + g.name + ")");
        dests_ = destOptions(c.dest.scope, level);
        dest_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < dests_.size(); ++i) {
            dest_.addItem(dests_[i].label, static_cast<int>(i) + 1);
            if (dests_[i].key == c.dest.key) dest_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        const bool mute = c.dest.key == "mute";
        const Row r = asRow(c);
        const auto p = paramFromKey(c.dest.key);
        const bool root = p && *p == Param::Root, scale = p && *p == Param::Scale;
        const bool numeric = rowNumeric(r), pick = rowPick(r);
        const bool action = p && (*p >= Param::RerollRhythm && *p <= Param::VaryRhythm);
        const bool vary = p && (*p == Param::VaryPitch || *p == Param::VaryRhythm);
        depth_.setVisible(level && !mute);
        depth_.setValue(c.depth);
        threshold_.setVisible(level && mute);
        invert_.setVisible(level && mute);
        threshold_.setValue(c.threshold);
        invert_.setSelected(c.invert ? 1 : 0);
        const bool trig = kind == OutputKind::Trigger;
        move_.setVisible(trig && (numeric || root));
        move_.setSelected(c.move);
        scale_.setVisible(trig && (numeric || root) && c.move == 1);
        scale_.setValue(c.scale);
        amount_.setVisible(trig && (numeric || root || scale || pick || vary));
        amount_.setLabel(vary ? (c.dest.scope == Scope::Harmony ? "How many chords" : p && *p == Param::VaryRhythm ? "How many hits" : "How many notes")
                              : c.move == 1 && (numeric || root) ? "At most" : "How far");
        amount_.setValue(c.maxShift);
        chance_.setVisible(trig);
        chance_.setValue(c.chance);
        min_.setVisible(trig && numeric);
        max_.setVisible(trig && numeric);
        if (numeric) {
            const auto gt = c.dest.key.empty() ? std::nullopt : generatorTarget(c.dest.key);
            const auto [lo, hi] = gt ? std::pair<int, int>{gt->param->lo, gt->param->hi} : spanOfParam(p.value_or(Param::Range));
            min_.setRange(lo, hi);
            max_.setRange(lo, hi);
            min_.setValue(c.min ? *c.min : lo);
            max_.setValue(c.max ? *c.max : hi);
        }
        const juce::StringArray opts = trig && (pick || scale) ? pickOptions(s, r) : juce::StringArray();
        allowed_.setVisible(!opts.isEmpty());
        if (!opts.isEmpty()) {
            std::vector<int> a = c.allowed;
            if (a.empty() && !scale)
                for (int i = 0; i < opts.size(); ++i) a.push_back(i);
            allowed_.set(opts, a);
        }
        lasts_.setVisible(trig);
        lasts_.setSelected(c.lasts == 0 ? 0 : 1);
        bars_.setValue(c.lasts == 0 ? 1 : c.lasts);
        bars_.setVisible(trig && c.lasts > 0);
        seed_.setVisible(trig && action);
        seed_.setSelected(c.fixedSeed ? 0 : 1);
        cond_.setVisible(trig);  // a level isn't an item: it has no condition (§17.5)
        cond_.setSize(getWidth() - 48, 10);
        if (trig) cond_.set(c.cond, s);
        resized();
        setSize(getWidth(), heightNeeded());
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::sidebar);
        g.setColour(colour::line);
        g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
        g.setColour(colour::text);
        g.setFont(sans(19.0f));
        g.drawFittedText(u8(sentence_.c_str()), sentenceArea_, juce::Justification::topLeft, 5, 1.0f);
        for (const auto& [text, r] : labels_) heading(g, text, r);
    }

    void resized() override {
        labels_.clear();
        auto r = juce::Rectangle<int>(24, 22, getWidth() - 48, 1 << 20);
        sentenceArea_ = r.removeFromTop(118);
        r.removeFromTop(8);
        auto section = [&](const char* name, int h) {
            labels_.push_back({name, r.removeFromTop(20)});
            r.removeFromTop(6);
            auto s = r.removeFromTop(h);
            r.removeFromTop(14);
            return s;
        };
        auto from = section("From", 100);
        from_.setBounds(from.removeFromTop(48));
        from.removeFromTop(8);
        output_.setBounds(from.removeFromTop(44));
        auto to = section("To", 102);
        target_.setBounds(to.removeFromTop(48));
        to.removeFromTop(6);
        dest_.setBounds(to.removeFromTop(48));
        if (depth_.isVisible()) depth_.setBounds(section("Moves it", 46));
        if (threshold_.isVisible()) {
            auto m = section("Muted", 98);
            invert_.setBounds(m.removeFromTop(44));
            m.removeFromTop(8);
            threshold_.setBounds(m.removeFromTop(46));
        }
        if (cond_.isVisible()) cond_.setBounds(section("Only when", cond_.heightNeeded()));
        if (chance_.isVisible()) {
            const int rows = (move_.isVisible() ? 1 : 0) + (scale_.isVisible() ? 1 : 0) + (amount_.isVisible() ? 1 : 0) + 1 + (min_.isVisible() ? 2 : 0);
            auto how = section("How much", 46 * rows);
            if (move_.isVisible()) move_.setBounds(how.removeFromTop(44)), how.removeFromTop(2);
            if (scale_.isVisible()) scale_.setBounds(how.removeFromTop(46));
            if (amount_.isVisible()) amount_.setBounds(how.removeFromTop(46));
            chance_.setBounds(how.removeFromTop(46));
            if (min_.isVisible()) {
                min_.setBounds(how.removeFromTop(46));
                max_.setBounds(how.removeFromTop(46));
            }
        }
        if (allowed_.isVisible()) {
            allowed_.setBounds(0, 0, r.getWidth(), 10);
            allowed_.setBounds(section("May choose", allowed_.layout()));
        }
        if (lasts_.isVisible()) {
            auto lasts = section("Lasts", bars_.isVisible() ? 98 : 44);
            lasts_.setBounds(lasts.removeFromTop(44));
            if (bars_.isVisible()) lasts.removeFromTop(8), bars_.setBounds(lasts.removeFromTop(46));
        }
        if (seed_.isVisible()) seed_.setBounds(section("Each time it acts", 44));
        r.removeFromTop(6);
        auto buttons = r.removeFromTop(48);
        duplicate_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 5));
        buttons.removeFromLeft(10);
        remove_.setBounds(buttons);
    }
    int heightNeeded() const { return remove_.getBottom() + 24; }

private:
    // A destination that suits the cord's output: a level can't move a clock or pick; a trigger can't mute.
    static void fitDest(Cord& c, const Song& s) {
        const bool level = cordKind(s, c) == OutputKind::Level;
        const auto opts = destOptions(c.dest.scope, level);
        for (const DestOption& o : opts)
            if (o.key == c.dest.key) return;
        if (!opts.empty()) c.dest.key = opts.front().key;
    }
    static std::pair<int, int> spanOfParam(Param p) {
        switch (p) {
            case Param::Accent: case Param::Swing: case Param::Probability: case Param::Ratchet: case Param::HumanTiming: return {0, 100};
            case Param::HumanVelocity: return {0, 32};
            case Param::Handoff: return {0, 50};
            case Param::Repeats: return {2, 4};
            case Param::GatePercent: return {1, 200};
            case Param::OctaveLow: case Param::OctaveHigh: return {-1, 2};
            case Param::Steps: return {1, 64};
            case Param::Offset: return {0, 63};
            case Param::Weight: case Param::Stagger: case Param::Top: return {0, 16};
            case Param::ChordSize: return {1, 6};
            default: return {1, 16};
        }
    }
    void cord(const std::function<void(Song&, Cord&)>& f) {
        proc_.edit([this, f](Song& s) {
            for (Cord& c : s.mod.cords)
                if (c.id == id_) f(s, c);
        });
    }

    HocketProcessor& proc_;
    std::uint64_t id_ = 0;
    std::string sentence_;
    juce::ComboBox from_;
    Segmented output_;
    std::vector<std::string> outputs_;
    juce::ComboBox target_, dest_;
    std::vector<std::pair<Scope, std::uint64_t>> targets_;
    std::vector<DestOption> dests_;
    Segmented invert_, move_;
    ConditionEditor cond_;
    ValueBar depth_, threshold_, scale_, amount_, chance_, min_, max_;
    Allowed allowed_;
    Segmented lasts_;
    ValueBar bars_;
    Segmented seed_;
    Pill duplicate_{"Duplicate"}, remove_{"Delete", Pill::Kind::Danger};
    juce::Rectangle<int> sentenceArea_;
    std::vector<std::pair<const char*, juce::Rectangle<int>>> labels_;
};

// ---- the page ---------------------------------------------------------------------------------------------

// Changes (§17.8): the form strip; the sources, a card each; then the rows and cords, one list in §17.4's order. The
// panel on the right edits whichever is selected: a row, a cord or a source.
class MatrixPage : public Page {
public:
    explicit MatrixPage(HocketProcessor& p) : Page(p), editor_(p), cordEditor_(p), sourceEditor_(p) {
        addAndMakeVisible(form_);
        addAndMakeVisible(add_);
        add_.setAccent(colour::yellow);
        addAndMakeVisible(addCord_);
        addAndMakeVisible(lock_);
        // Generate gives the matrix and every source new rolls (which way a drift goes, what a fresh reroll lands on, a
        // seeded phase); locked, they keep them (§17.1).
        lock_.onClick = [this] { proc.edit([](Song& s) { s.matrixLocked = !s.matrixLocked; }); };
        for (const SourceKind& k : sourceKinds()) {
            auto b = std::make_unique<Pill>(u8("+ ") + k.label, Pill::Kind::Dashed);
            const std::string kind = k.name;
            b->onClick = [this, kind] {
                proc.edit([this, kind](Song& s) {
                    ModSource& src = addSource(s, kind);
                    sel_ = Sel::Source, selected_ = src.id;
                });
            };
            addAndMakeVisible(*b);
            addSource_.push_back(std::move(b));
        }
        addAndMakeVisible(cardsView_);
        cardsView_.setViewedComponent(&cards_, false);
        cardsView_.setScrollBarsShown(false, true);
        cards_.onPick = [this](std::size_t i) {
            const Song& d = proc.document();
            if (i < d.mod.sources.size()) {
                sel_ = Sel::Source, selected_ = d.mod.sources[i].id;
                songChanged();
            }
        };
        addAndMakeVisible(rowsView_);
        addAndMakeVisible(editorView_);
        rowsView_.setViewedComponent(&rows_, false);
        rowsView_.setScrollBarsShown(true, false);
        editorView_.setViewedComponent(&panelHolder_, false);
        editorView_.setScrollBarsShown(true, false);
        panelHolder_.addChildComponent(editor_);
        panelHolder_.addChildComponent(cordEditor_);
        panelHolder_.addChildComponent(sourceEditor_);
        sourceEditor_.onPatched = [this](std::uint64_t id) {
            sel_ = Sel::Cord, selected_ = id;
            songChanged();
        };
        rows_.onPick = [this](std::size_t i) {
            const Song& d = proc.document();
            if (i < d.matrix.size()) sel_ = Sel::Row, selected_ = d.matrix[i].id;
            else if (i - d.matrix.size() < d.mod.cords.size()) sel_ = Sel::Cord, selected_ = d.mod.cords[i - d.matrix.size()].id;
            songChanged();
        };
        rows_.onToggle = [this](std::size_t i) {
            proc.edit([i](Song& s) {
                if (i < s.matrix.size()) s.matrix[i].on = !s.matrix[i].on;
                else if (i - s.matrix.size() < s.mod.cords.size()) s.mod.cords[i - s.matrix.size()].on = !s.mod.cords[i - s.matrix.size()].on;
            });
        };
        add_.onClick = [this] {
            proc.edit([this](Song& s) {
                // A new change starts as a turnaround: the first group's notes, on the last bar of each phrase, for a bar.
                Row& r = addRow(s);
                r.scope = s.groups.empty() ? Scope::Global : Scope::Group;
                r.target = s.groups.empty() ? 0 : s.groups[0].id;
                r.param = Param::VaryPitch;
                r.every = 1, r.inPhrases = true, r.offset = -1, r.maxShift = 2, r.lasts = 1;
                sel_ = Sel::Row, selected_ = r.id;
            });
        };
        addCord_.onClick = [this] {
            proc.edit([this](Song& s) {
                // From the selected source (or the first), its first output, to the first group's Density.
                const ModSource* src = nullptr;
                for (const ModSource& m : s.mod.sources)
                    if (!src || (sel_ == Sel::Source && m.id == selected_)) src = &m;
                if (!src) return;
                const auto outs = outputsOf(*src);
                Cord& c = addCord(s, src->id, outs.empty() ? std::string() : outs.front().name);
                c.dest = {s.groups.empty() ? Scope::Global : Scope::Group, s.groups.empty() ? 0 : s.groups[0].id, s.groups.empty() ? "range" : "density"};
                sel_ = Sel::Cord, selected_ = c.id;
            });
        };
        songChanged();
    }

    void songChanged() override {
        ++version_;  // every cached "next" is stale
        needsView_ = true;  // and a conditioned item's waits for the next tick's View
        const Song& d = proc.document();
        // The selection, if it's still there; otherwise the first row, cord or source.
        bool found = false;
        if (sel_ == Sel::Row) for (const Row& r : d.matrix) found = found || r.id == selected_;
        if (sel_ == Sel::Cord) for (const Cord& c : d.mod.cords) found = found || c.id == selected_;
        if (sel_ == Sel::Source) for (const ModSource& m : d.mod.sources) found = found || m.id == selected_;
        if (!found) {
            if (!d.matrix.empty()) sel_ = Sel::Row, selected_ = d.matrix[0].id;
            else if (!d.mod.cords.empty()) sel_ = Sel::Cord, selected_ = d.mod.cords[0].id;
            else if (!d.mod.sources.empty()) sel_ = Sel::Source, selected_ = d.mod.sources[0].id;
            else selected_ = 0;
        }
        lock_.setButtonText(d.matrixLocked ? "Matrix locked" : "Lock matrix");
        lock_.setKind(d.matrixLocked ? Pill::Kind::Selected : Pill::Kind::Neutral);
        addCord_.setEnabled(!d.mod.sources.empty());
        refreshRows();
        refreshCards();
        showPanel();
        repaint();
    }

    void tick(std::int64_t tick, View& view) override {
        const std::int64_t at = tick >= 0 ? tick : 0;
        const Song& s = proc.song();
        bar_ = at / barTicks(s);
        std::vector<std::string> names;
        for (const auto& g : s.groups) names.push_back(g.name);
        form_.set(names, view.form(bar_, 8), view.chordsByBar(bar_, 8), bar_);
        if (bar_ != shownBar_ || needsView_) {
            shownBar_ = bar_;
            needsView_ = false;
            refreshRows(&view);
            refreshCards();
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground);
        g.setColour(colour::sidebar);
        g.fillRect(editorView_.getBounds());
        g.setColour(colour::line);
        g.drawVerticalLine(editorView_.getX(), 0.0f, static_cast<float>(getHeight()));
        panel(g, formPanel_);
        g.setColour(colour::text);
        g.setFont(sans(24.0f, true));
        g.drawText("Sources", sourcesTitle_, juce::Justification::centredLeft);
        g.drawText("What changes, and when", titleArea_, juce::Justification::centredLeft);
        const Song& d = proc.document();
        g.setColour(colour::dim);
        g.setFont(sans(16.0f));
        if (d.mod.sources.empty())
            g.drawFittedText(u8("No sources yet. Each moves settings over time and fires events: slow LFOs, a stepping accumulator, 1/f dice, an energy arc over the whole set."), cardsView_.getBounds(),
                             juce::Justification::centredLeft, 2);
        if (d.matrix.empty() && d.mod.cords.empty())
            g.drawText("Nothing changes yet. Add a change: a turnaround, a slow drift, a new groove every 16 bars.", rowsView_.getBounds(),
                       juce::Justification::centredTop);
    }

    void resized() override {
        auto r = getLocalBounds();
        editorView_.setBounds(r.removeFromRight(480));
        panelHolder_.setSize(editorView_.getWidth() - 8, juce::jmax(panelHolder_.getHeight(), 100));
        for (juce::Component* c : std::initializer_list<juce::Component*>{&editor_, &cordEditor_, &sourceEditor_})
            c->setSize(panelHolder_.getWidth(), juce::jmax(c->getHeight(), 100));
        showPanel();
        r = r.reduced(28, 22);
        formPanel_ = r.removeFromTop(200);
        form_.setBounds(formPanel_.reduced(18, 12));
        r.removeFromTop(14);
        auto shead = r.removeFromTop(44);
        for (auto it = addSource_.rbegin(); it != addSource_.rend(); ++it) {
            (*it)->setBounds(shead.removeFromRight(140));
            shead.removeFromRight(8);
        }
        sourcesTitle_ = shead;
        r.removeFromTop(10);
        cardsView_.setBounds(r.removeFromTop(150));
        cards_.setSize(cards_.getWidth(), 138);
        r.removeFromTop(14);
        auto head = r.removeFromTop(48);
        add_.setBounds(head.removeFromRight(160));
        head.removeFromRight(8);
        addCord_.setBounds(head.removeFromRight(140));
        head.removeFromRight(8);
        lock_.setBounds(head.removeFromRight(140));
        titleArea_ = head;
        r.removeFromTop(14);
        rowsView_.setBounds(r);
        rows_.setSize(r.getWidth() - 10, rows_.heightNeeded());
    }

private:
    enum class Sel { Row, Cord, Source };

    void showPanel() {
        editor_.setVisible(false), cordEditor_.setVisible(false), sourceEditor_.setVisible(false);
        juce::Component* shown = nullptr;
        if (selected_ != 0) {
            if (sel_ == Sel::Row) editor_.show(selected_), shown = &editor_;
            if (sel_ == Sel::Cord) cordEditor_.show(selected_), shown = &cordEditor_;
            if (sel_ == Sel::Source) sourceEditor_.show(selected_), shown = &sourceEditor_;
        }
        if (shown && !shown->isVisible()) shown = nullptr;
        panelHolder_.setSize(panelHolder_.getWidth(), shown ? shown->getHeight() : 100);
    }

    // A row's or cord's next act when it has a condition (§17.5): only the replay knows (PRE, NEI and FILL read what
    // other items did), so it's read from the View's own replay, up to 256 bars ahead, and kept like the others.
    static constexpr int kLookBars = 256;
    std::string conditionedNext(std::uint64_t id, View* view) {
        Cached& k = nextCache_[{id, "cond"}];
        if (view && (k.version != version_ || (k.bar >= 0 && k.bar < bar_) || (k.bar < 0 && bar_ >= k.from + kLookBars))) {
            const Firing f = view->nextAct(id, bar_, kLookBars);
            k = {version_, bar_, f.bar, f.skipped};
        }
        if (k.version != version_) return "Next: …";
        if (k.bar < 0) return "Not in the next " + std::to_string(kLookBars) + " bars";
        return "Next: bar " + std::to_string(k.bar + 1) + (k.from == bar_ && k.skipped ? " (skips " + std::to_string(k.skipped) + ")" : "");
    }

    void refreshRows(View* view = nullptr) {
        const Song& d = proc.document();
        const std::map<std::uint64_t, std::string> letters = formLetters(d);
        std::vector<RowList::Item> items;
        std::size_t sel = 0;
        for (std::size_t i = 0; i < d.matrix.size(); ++i) {
            const Row& r = d.matrix[i];
            RowList::Item it;
            it.temporary = r.lasts > 0;
            it.tag = letters.count(r.id) ? letters.at(r.id) : "~";
            it.sentence = sentence(d, r);
            it.chance = r.chance >= 100 ? "Always" : std::to_string(r.chance) + " % of the time";
            it.next = r.on && r.cond.kind != Condition::Kind::None ? conditionedNext(r.id, view) : nextFire(d, r, bar_);
            it.on = r.on;
            items.push_back(it);
            if (sel_ == Sel::Row && r.id == selected_) sel = i;
        }
        for (std::size_t i = 0; i < d.mod.cords.size(); ++i) {
            const Cord& c = d.mod.cords[i];
            RowList::Item it;
            const auto kind = cordKind(d, c);
            it.level = kind == OutputKind::Level;
            if (const ModSource* src = sourceById(d, c.source)) {
                int k = 0;
                for (const SourceOutput& o : outputsOf(*src)) {
                    if (o.name == c.output) it.wave = k;
                    k += o.kind == OutputKind::Level;
                }
            }
            it.temporary = letters.count(c.id) > 0;
            it.tag = it.temporary ? letters.at(c.id) : "~";
            it.sentence = cordSentence(d, c);
            it.chance = it.level ? "A level, every bar" : c.chance >= 100 ? "Always" : std::to_string(c.chance) + " % of the time";
            if (!it.level) {
                // Kept until the song changes or the bar passes it; its skip count is only shown when fresh.
                Cached& k = nextCache_[{c.id, std::string()}];
                if (k.version != version_ || (k.bar >= 0 && k.bar < bar_) || (k.bar < 0 && bar_ >= k.from + kScanBars)) {
                    const Firing f = c.on ? nextCordFiring(d, c, bar_) : Firing{};
                    k = {version_, bar_, f.bar, f.skipped};
                }
                if (!c.on) it.next = "Off";
                else if (!cordKind(d, c)) it.next = "";
                else if (c.cond.kind != Condition::Kind::None) it.next = conditionedNext(c.id, view);
                else if (k.bar < 0) it.next = "Not in its next 64 times";
                else it.next = "Next: bar " + std::to_string(k.bar + 1) + (k.from == bar_ && k.skipped ? " (skips " + std::to_string(k.skipped) + ")" : "");
            }
            it.on = c.on;
            items.push_back(it);
            if (sel_ == Sel::Cord && c.id == selected_) sel = d.matrix.size() + i;
        }
        rows_.set(items, sel_ == Sel::Source ? items.size() : sel);
        rows_.setSize(rowsView_.getWidth() - 10, rows_.heightNeeded());
    }

    void refreshCards() {
        const Song& d = proc.document();
        std::vector<SourceCards::Card> cards;
        int sel = -1;
        for (std::size_t i = 0; i < d.mod.sources.size(); ++i) {
            const ModSource& src = d.mod.sources[i];
            cards.push_back(cardOf(d, src, bar_, [&](const SourceOutput& o) -> std::optional<std::int64_t> {
                // Kept until the song changes or the bar passes it (a convergence may be millions of bars away).
                Cached& c = nextCache_[{src.id, o.name}];
                if (c.version != version_ || (c.bar >= 0 && c.bar < bar_) || (c.bar < 0 && bar_ >= c.from + kScanBars)) {
                    const auto n = triggerBars(d, src, o.name, bar_, 1);
                    c = {version_, bar_, n.empty() ? -1 : n[0], 0};
                }
                if (c.bar < 0) return std::nullopt;
                return c.bar;
            }));
            if (sel_ == Sel::Source && d.mod.sources[i].id == selected_) sel = static_cast<int>(i);
        }
        cards_.set(std::move(cards), sel);
    }

    FormStrip form_;
    Pill add_{"Add a change", Pill::Kind::Primary};
    Pill addCord_{"Add a cord"};
    Pill lock_{"Lock matrix"};
    std::vector<std::unique_ptr<Pill>> addSource_;
    juce::Viewport cardsView_, rowsView_, editorView_;
    SourceCards cards_;
    RowList rows_;
    juce::Component panelHolder_;
    RowEditor editor_;
    CordEditor cordEditor_;
    SourceEditor sourceEditor_;
    Sel sel_ = Sel::Row;
    // The next firings the list and the cards show, by (cord or source id, output), worked out once per song version.
    struct Cached {
        int version = -1;
        std::int64_t from = 0, bar = -1;
        int skipped = 0;
    };
    std::map<std::pair<std::uint64_t, std::string>, Cached> nextCache_;
    int version_ = 0;
    bool needsView_ = true;
    std::uint64_t selected_ = 0;
    std::int64_t bar_ = 0, shownBar_ = -1;
    juce::Rectangle<int> formPanel_, titleArea_, sourcesTitle_;
};

}  // namespace

std::unique_ptr<Page> makeMatrixPage(HocketProcessor& p) { return std::make_unique<MatrixPage>(p); }

}  // namespace hocket::ui
