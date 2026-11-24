// Groups: make groups and voices, and shape each group's rhythm, dealing and voices (the iPad design's second
// screen). Every edit goes through the processor and lands on the next bar line.
#include "Pages.h"

namespace hocket::ui {

namespace {

// The first channel no voice uses (16 if they all are): a new voice shouldn't land on someone else's synth.
int freeChannel(const Song& s) {
    std::uint32_t used = 0;
    for (const Group& g : s.groups)
        for (const Voice& v : g.voices) used |= 1u << (v.channel - 1);
    int ch = 1;
    while (ch < 16 && (used & (1u << (ch - 1)))) ++ch;
    return ch;
}

const char* const kDivisions[] = {"1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "5/16", "1/3",
                                  "3/8",  "1/2",  "3/4",  "1",    "1.5",  "2",    "3",   "4",   "6",    "8",   "16",  "32"};

// §16.10: a slot's generators as a row of buttons, and how a generator's knob reads.
juce::StringArray generatorLabels(SlotKind slot) {
    juce::StringArray out;
    for (const Generator* g : generatorsFor(slot)) out.add(g->label);
    return out;
}
int generatorIndex(SlotKind slot, const GenSlot& s) {
    const auto all = generatorsFor(slot);
    const Generator& on = playing(slot, s);
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i] == &on) return static_cast<int>(i);
    return 0;
}
juce::String genValueText(const GenParam& p, int v) {
    if (p.choice) {
        const juce::StringArray o = juce::StringArray::fromTokens(p.options, "|", "");
        return v >= 0 && v < o.size() ? o[v] : juce::String(v);
    }
    if (juce::String(p.key) == "lock") return v >= 17 ? juce::String("Invert") : v == 16 ? juce::String("Locked") : juce::String(v);
    return juce::String(v);
}

// A generator's own knobs, under its slot's buttons: as many as it has, each writing its value under the generator's
// name, so a swap away and back brings it back (§16.9).
class GenKnobs {
public:
    GenKnobs(juce::Component& parent, juce::Colour fill, int count) {
        for (int i = 0; i < count; ++i) {
            bars_.push_back(std::make_unique<ValueBar>("", 0, 1, 0, fill));
            bars_.back()->setLayout(ValueBar::Layout::Stacked);  // label over the bar: three side by side stay readable
            parent.addChildComponent(*bars_.back());
        }
    }
    // `write` sets one parameter of the slot's generator; `writeId` a derived generator's source (§16.6), chosen from the
    // song's other groups (a rhythm) or other Lead and Bass voices (notes), "none" first. `self` is the slot's own id.
    void show(SlotKind slot, const GenSlot& s, const std::function<void(std::string, std::string, int)>& write, const Song* song = nullptr,
              std::uint64_t self = 0, const std::function<void(std::string, std::uint64_t)>& writeId = {}) {
        const Generator& gen = playing(slot, s);
        shown_ = 0;
        for (std::size_t i = 0; i < bars_.size(); ++i) {
            ValueBar& b = *bars_[i];
            if (i >= gen.params.size()) {
                b.setVisible(false);
                continue;
            }
            const GenParam& p = gen.params[i];
            if (p.id) {
                std::vector<std::pair<std::uint64_t, juce::String>> c;  // the candidates, in song order
                if (song)
                    for (const Group& g : song->groups) {
                        if (slot == SlotKind::Rhythm) {
                            if (g.id != self) c.push_back({g.id, juce::String(g.name)});
                            continue;
                        }
                        for (std::size_t k = 0; k < g.voices.size(); ++k)
                            if (g.voices[k].id != self && g.voices[k].type != VoiceType::Chord)
                                c.push_back({g.voices[k].id, juce::String(g.name) + u8(" · ") + juce::String(static_cast<int>(k) + 1)});
                    }
                const std::uint64_t now = sourceOf(s, gen.name);
                int at = 0;
                for (std::size_t k = 0; k < c.size(); ++k)
                    if (c[k].first == now) at = static_cast<int>(k) + 1;
                b.setLabel(p.label);
                b.setRange(0, static_cast<int>(c.size()));
                b.setDefault(0);
                b.format = [c](int v) { return v >= 1 && v <= static_cast<int>(c.size()) ? c[static_cast<std::size_t>(v - 1)].second : juce::String("none"); };
                b.setValue(at);
                b.onChange = [writeId, c, name = std::string(gen.name)](int v) {
                    if (writeId) writeId(name, v >= 1 && v <= static_cast<int>(c.size()) ? c[static_cast<std::size_t>(v - 1)].first : 0);
                };
                b.setTitle(p.label);
                b.setVisible(true);
                ++shown_;
                continue;
            }
            b.setLabel(p.label);
            b.setRange(p.lo, p.hi);
            b.setDefault(slot == SlotKind::Rhythm ? p.rhythmDefault : p.notesDefault);
            b.format = [p](int v) { return genValueText(p, v); };
            b.setValue(paramValue(slot, s, gen, p));
            b.onChange = [write, name = std::string(gen.name), key = std::string(p.key)](int v) { write(name, key, v); };
            b.setTitle(p.label);
            b.setVisible(true);
            ++shown_;
        }
    }
    int shown() const { return shown_; }
    void setTrack(juce::Colour c) {
        for (auto& b : bars_) b->setTrack(c);
    }
    // In a row, side by side.
    void layout(juce::Rectangle<int> r) {
        if (shown_ == 0) return;
        const int w = r.getWidth() / shown_;
        for (auto& b : bars_)
            if (b->isVisible()) b->setBounds(r.removeFromLeft(w).reduced(6, 0));
    }
    void hide() {
        for (auto& b : bars_) b->setVisible(false);
    }

private:
    std::vector<std::unique_ptr<ValueBar>> bars_;
    int shown_ = 0;
};

Group* findGroup(Song& s, std::uint64_t id) {
    for (Group& g : s.groups)
        if (g.id == id) return &g;
    return nullptr;
}
Voice* findVoice(Song& s, std::uint64_t id) {
    for (Group& g : s.groups)
        for (Voice& v : g.voices)
            if (v.id == id) return &v;
    return nullptr;
}

// The group list: a card each (name, length, a mini pattern in the voices' colours), and New group.
class GroupList : public juce::Component {
public:
    std::function<void(std::size_t)> onPick;
    void set(const Song& s, std::size_t selected) {
        cards_.clear();
        for (const Group& g : s.groups) {
            Card c;
            c.name = g.name;
            c.meta = juce::String(g.steps) + u8(" · ") + kDivisions[juce::jlimit(0, 23, g.beatDivision)];
            const std::vector<bool> h = rhythmHits(g, g.density);  // what its rhythm generator makes
            for (int i = 0; i < 16 && i < static_cast<int>(h.size()); ++i) c.mini.push_back(h[static_cast<std::size_t>(i)]);
            juce::StringArray voices;
            static const char* types[] = {"Lead", "Bass", "Chord"};
            for (const Voice& v : g.voices) voices.add(juce::String(types[static_cast<int>(v.type)]) + " ch " + juce::String(v.channel));
            c.voices = voices.joinIntoString(u8(" · "));
            cards_.push_back(c);
        }
        selected_ = selected;
        repaint();
    }
    juce::Rectangle<int> card(std::size_t i) const { return {0, static_cast<int>(i) * 124, getWidth(), 112}; }
    void paint(juce::Graphics& g) override {
        for (std::size_t i = 0; i < cards_.size(); ++i) {
            const auto r = card(i);
            const bool on = i == selected_;
            g.setColour(on ? colour::raised : colour::panel);
            g.fillRoundedRectangle(r.toFloat(), 16.0f);
            if (on) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(r.toFloat().reduced(1.0f), 16.0f, 2.0f);
            }
            auto in = r.reduced(14);
            auto top = in.removeFromTop(24);
            g.setColour(colour::text);
            g.setFont(sans(18.0f, true));
            g.drawText(cards_[i].name, top, juce::Justification::centredLeft);
            g.setColour(colour::dim);
            g.setFont(mono(13.0f, false));
            g.drawText(cards_[i].meta, top, juce::Justification::centredRight);
            in.removeFromTop(8);
            auto mini = in.removeFromTop(18);
            const float w = (mini.getWidth() - 3.0f * 15) / 16.0f;
            for (std::size_t k = 0; k < cards_[i].mini.size(); ++k) {
                g.setColour(cards_[i].mini[k] ? colour::soft : juce::Colour(0xff2e2d35));
                g.fillRoundedRectangle(juce::Rectangle<float>(mini.getX() + k * (w + 3.0f), static_cast<float>(mini.getY()), w, 16.0f), 3.0f);
            }
            in.removeFromTop(8);
            g.setColour(colour::soft);
            g.setFont(sans(13.0f));
            g.drawText(cards_[i].voices, in, juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (std::size_t i = 0; i < cards_.size(); ++i)
            if (card(i).contains(e.getPosition()) && onPick) onPick(i);
    }
    int heightNeeded() const { return static_cast<int>(cards_.size()) * 124; }

private:
    struct Card {
        juce::String name, meta, voices;
        std::vector<bool> mini;
    };
    std::vector<Card> cards_;
    std::size_t selected_ = 0;
};

// One voice: its colour, type, channel, octave, weight (or, under the group's Density from voices, its own Density,
// §5.8); its own Accent and Gate % (§4.3); a line's Tie or a chord's size, voicing and articulation;
// lock its notes, reroll them, delete it.
class VoiceRow : public juce::Component {
public:
    VoiceRow(HocketProcessor& p, std::uint64_t id, int index)
        : proc_(p), id_(id), index_(index), type_({"Lead", "Bass", "Chord"}), channel_("Channel", 1, 16, 1, colour::voice(index)),
          octave_("Octave", -3, 3, 0, colour::voice(index)), weight_("Weight", 0, 16, 8, colour::voice(index)),
          size_("Chord size", 1, 6, 4, colour::voice(index)), voicing_("Voicing", 1, 16, 1, colour::voice(index)),
          feelVelocity_("Feel: velocity", 0, 32, 0, colour::voice(index)), feelTiming_("Feel: timing", 0, 100, 0, colour::voice(index)),
          accent_("Accent", 0, 100, 50, colour::voice(index)), gate_("Gate", 1, 200, 100, colour::voice(index)),
          stagger_("Stagger", 0, 16, 0, colour::voice(index)), top_("Top", 0, 16, 0, colour::voice(index)),
          articulation_({"Sustain", "Restrike accents", "Restrike hits", "Pulse"}), staggerOrder_({"Up", "Down", "Seeded"}),
          staggerLean_({"Late", "Around"}), notesGen_(generatorLabels(SlotKind::Notes)),
          notesKnobs_(*this, colour::voice(index), 3) {
        addAndMakeVisible(notesGen_);
        notesGen_.setOff(colour::panel);
        notesGen_.setTitle("Notes");
        notesKnobs_.setTrack(colour::panel);
        // §16.10: the line's Notes generator, swapped (an edit: on the next bar line, one Undo step).
        notesGen_.onChange = [this](int i) {
            const auto all = generatorsFor(SlotKind::Notes);
            if (i >= 0 && i < static_cast<int>(all.size())) change([name = std::string(all[static_cast<std::size_t>(i)]->name)](Voice& v) { v.notesGen.use = name; });
        };
        for (juce::Component* c : std::initializer_list<juce::Component*>{&type_, &channel_, &octave_, &weight_, &tie_, &lock_, &reroll_, &remove_, &size_, &voicing_, &nearest_, &articulation_, &feelVelocity_, &feelTiming_, &staggerOrder_, &staggerLean_})
            addAndMakeVisible(c);
        for (ValueBar* b : {&channel_, &octave_, &weight_, &size_, &voicing_, &feelVelocity_, &feelTiming_, &accent_, &gate_, &stagger_, &top_}) {
            addAndMakeVisible(b);
            b->setLayout(ValueBar::Layout::Inline);
            b->setTrack(colour::panel);
        }
        // §4.3: how hard and how long this voice's own notes are: the group's (the badge says so), or set here.
        gate_.format = [](int v) { return juce::String(v) + " %"; };
        auto own = [this](ValueBar& bar, std::optional<int> Overrides::*field) {
            bar.setInheritedWord("group");
            bar.onChange = [this, field](int x) { change([field, x](Voice& v) { v.overrides.*field = x; }); };
            bar.onInherit = [this, &bar, field](bool inherited) {
                const int x = bar.value();
                change([field, inherited, x](Voice& v) {
                    if (inherited) v.overrides.*field = std::nullopt;
                    else v.overrides.*field = x;
                });
            };
        };
        own(accent_, &Overrides::accent);
        own(gate_, &Overrides::gatePercent);
        type_.setOff(colour::panel);
        articulation_.setOff(colour::panel);
        // §3.4b: a hand, not a block. Stagger spreads the notes a chord strikes (up to a 1/16 note at 16) in its Order,
        // from the strike on or Around it; Top plays the top note louder and the inner notes softer.
        staggerOrder_.setOff(colour::panel);
        staggerLean_.setOff(colour::panel);
        stagger_.format = [](int v) { return v == 0 ? juce::String("off") : juce::String(v); };
        top_.format = [](int v) { return v == 0 ? juce::String("off") : "+" + juce::String(v); };
        stagger_.onChange = [this](int x) { change([x](Voice& v) { v.stagger = x; }); };
        top_.onChange = [this](int x) { change([x](Voice& v) { v.top = x; }); };
        staggerOrder_.onChange = [this](int i) { change([i](Voice& v) { v.staggerOrder = static_cast<StaggerOrder>(i); }); };
        staggerLean_.onChange = [this](int i) { change([i](Voice& v) { v.staggerLean = static_cast<StaggerLean>(i); }); };
        octave_.format = [](int v) { return v > 0 ? "+" + juce::String(v) : juce::String(v); };
        // §4.7: humanize, off at 0. Velocity moves each note by up to ± this; timing lets each line note sound late by
        // up to this much of the room left in its step (a chord's notes move only in velocity).
        feelVelocity_.format = [](int v) { return v == 0 ? juce::String("off") : juce::String::fromUTF8("\xc2\xb1") + juce::String(v); };
        feelTiming_.format = [](int v) { return v == 0 ? juce::String("off") : juce::String(v) + " %"; };
        feelVelocity_.onChange = [this](int x) { change([x](Voice& v) { v.humanVelocity = x; }); };
        feelTiming_.onChange = [this](int x) { change([x](Voice& v) { v.humanTiming = x; }); };
        type_.onChange = [this](int t) { change([t](Voice& v) { v.type = static_cast<VoiceType>(t); }); };
        channel_.onChange = [this](int c) { change([c](Voice& v) { v.channel = c; }); };
        octave_.onChange = [this](int o) { change([o](Voice& v) { v.octaveTranspose = o; }); };
        // §5.8: under Density from voices the bar is the voice's own Density, and Weight is kept, unused.
        weight_.onChange = [this](int w) { change([w, own = fromVoices_](Voice& v) { (own ? v.density : v.weight) = w; }); };
        size_.onChange = [this](int n) { change([n](Voice& v) { v.chordSize = n; }); };
        voicing_.onChange = [this](int n) { change([n](Voice& v) { v.voicing = n; }); };
        articulation_.onChange = [this](int a) { change([a](Voice& v) { v.articulation = static_cast<Articulation>(a); }); };
        tie_.onClick = [this] { change([](Voice& v) { v.tie = !v.tie; }); };
        // §3.4a: the Voicing's other option: each chord the inversion nearest the one before.
        nearest_.onClick = [this] { change([](Voice& v) { v.nearest = !v.nearest; }); };
        lock_.onClick = [this] { change([](Voice& v) { v.lockNotes = !v.lockNotes; }); };
        reroll_.onClick = [this] {
            proc_.edit([this](Song& s) {
                if (Voice* v = findVoice(s, id_)) rerollNotes(s, *v);
            });
        };
        remove_.onClick = [this] { proc_.edit([this](Song& s) { removeVoice(s, id_); }); };
    }
    void refresh(const Voice& v, const Song& song, std::size_t gi, int vi) {
        type_.setSelected(static_cast<int>(v.type));
        channel_.setValue(v.channel);
        octave_.setValue(v.octaveTranspose);
        const Group& grp = song.groups[gi];
        fromVoices_ = grp.densityFromVoices;
        weight_.setLabel(fromVoices_ ? "Density" : "Weight");
        weight_.setValue(fromVoices_ ? v.density : v.weight);
        accent_.setInheritable(true, !v.overrides.accent);
        accent_.setValue(v.overrides.accent ? *v.overrides.accent : grp.overrides.accent ? *grp.overrides.accent : song.accent);
        gate_.setInheritable(true, !v.overrides.gatePercent);
        gate_.setValue(v.overrides.gatePercent ? *v.overrides.gatePercent : grp.overrides.gatePercent ? *grp.overrides.gatePercent : song.gatePercent);
        slot_ = v.slot > 0 ? "Automation: Voice " + juce::String(v.slot) : juce::String("No automation slot");
        size_.setValue(v.chordSize);
        voicing_.setValue(v.voicing);
        feelVelocity_.setValue(v.humanVelocity);
        feelTiming_.setValue(v.humanTiming);
        articulation_.setSelected(static_cast<int>(v.articulation));
        stagger_.setValue(v.stagger);
        top_.setValue(v.top);
        staggerOrder_.setSelected(static_cast<int>(v.staggerOrder));
        staggerLean_.setSelected(static_cast<int>(v.staggerLean));
        tie_.setKind(v.tie ? Pill::Kind::Selected : Pill::Kind::Neutral);
        tie_.setButtonText(v.tie ? "Ties" : "Tie");
        lock_.setKind(v.lockNotes ? Pill::Kind::Selected : Pill::Kind::Neutral);
        lock_.setButtonText(v.lockNotes ? "Notes locked" : "Lock notes");
        const bool chord = v.type == VoiceType::Chord;
        tie_.setVisible(!chord);
        size_.setVisible(chord);
        voicing_.setVisible(chord);
        nearest_.setVisible(chord);
        feelTiming_.setVisible(!chord);
        nearest_.setKind(v.nearest ? Pill::Kind::Selected : Pill::Kind::Neutral);
        nearest_.setButtonText(v.nearest ? "Nearest: on" : "Nearest");
        articulation_.setVisible(chord);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&stagger_, &top_, &staggerOrder_, &staggerLean_}) c->setVisible(chord);
        chord_ = chord;
        // A line's Notes slot (§16.10); a chord voices the harmony and has none (§16.1).
        notesGen_.setVisible(!chord);
        notesGen_.setSelected(generatorIndex(SlotKind::Notes, v.notesGen));
        if (chord) notesKnobs_.hide();
        else
            notesKnobs_.show(
                SlotKind::Notes, v.notesGen, [this](std::string gen, std::string key, int x) { change([gen, key, x](Voice& voice) { voice.notesGen.params[gen][key] = x; }); },
                &song, v.id, [this](std::string gen, std::uint64_t id) { change([gen, id](Voice& voice) { voice.notesGen.sources[gen] = id; }); });
        const Generator& gen = playing(SlotKind::Notes, v.notesGen);
        why_ = chord || gen.readsWander ? juce::String() : juce::String(gen.why);
        // §16.6: a Kotekan whose source isn't live plays Wander, and says why.
        if (!chord && std::string(gen.name) == "kotekan" && !kotekanSource(song, gi, vi))
            why_ = sourceOf(v.notesGen, "kotekan") == 0 ? "No source: playing Wander" : "Its source is gone or closes a loop: playing Wander";
        resized();  // a swap can change how many knobs the row has
        repaint();
    }
    int heightNeeded() const { return chord_ ? 352 : 292; }
    void paint(juce::Graphics& g) override {
        g.setColour(colour::raised);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 14.0f);
        g.setColour(colour::voice(index_));
        g.fillEllipse(juce::Rectangle<float>(16.0f, 28.0f, 14.0f, 14.0f));
        g.setColour(colour::dim);  // §12.2: the slot its host parameters are ("Voice 3 Density")
        g.setFont(sans(12.5f));
        g.drawText(slot_, slotArea_, juce::Justification::centredLeft);
        if (!chord_) {
            g.setColour(colour::soft);
            g.setFont(sans(15.0f));
            g.drawText("Notes", notesLabel_, juce::Justification::centredLeft);
            if (why_.isNotEmpty()) {
                g.setColour(colour::dim);
                g.setFont(sans(12.5f));
                g.drawText(why_, whyArea_, juce::Justification::centredLeft);
            }
        }
    }
    void resized() override {
        // Line one: the voice and what you do to it. Line two: where it plays and how much. A chord: its shape.
        auto r = getLocalBounds().reduced(12, 10);
        auto top = r.removeFromTop(50);
        top.removeFromLeft(30);
        type_.setBounds(top.removeFromLeft(240).withSizeKeepingCentre(240, 44));
        remove_.setBounds(top.removeFromRight(96).withSizeKeepingCentre(96, 44));
        top.removeFromRight(6);
        reroll_.setBounds(top.removeFromRight(96).withSizeKeepingCentre(96, 44));
        top.removeFromRight(6);
        lock_.setBounds(top.removeFromRight(140).withSizeKeepingCentre(140, 44));
        slotArea_ = top.withTrimmedLeft(16).withTrimmedRight(8);
        r.removeFromTop(6);
        auto mid = r.removeFromTop(48);
        mid.removeFromLeft(30);
        tie_.setBounds(mid.removeFromRight(96).withSizeKeepingCentre(96, 44));
        mid.removeFromRight(16);
        const int w = mid.getWidth() / 3;
        channel_.setBounds(mid.removeFromLeft(w).reduced(6, 4));
        octave_.setBounds(mid.removeFromLeft(w).reduced(6, 4));
        weight_.setBounds(mid.reduced(6, 4));
        r.removeFromTop(8);
        auto feel = r.removeFromTop(48);  // humanize (§4.7)
        feel.removeFromLeft(30);
        feel.removeFromRight(112);
        const int half = feel.getWidth() / 2;
        feelVelocity_.setBounds(feel.removeFromLeft(half).reduced(6, 4));
        feelTiming_.setBounds(feel.reduced(6, 4));
        r.removeFromTop(8);
        auto ownRow = r.removeFromTop(48);  // its own Accent and Gate % (§4.3)
        ownRow.removeFromLeft(30);
        ownRow.removeFromRight(112);
        accent_.setBounds(ownRow.removeFromLeft(half).reduced(6, 4));
        gate_.setBounds(ownRow.reduced(6, 4));
        if (!chord_) {
            // The Notes row: its generators' buttons, then the chosen one's knobs (§16.10).
            r.removeFromTop(8);
            auto notes = r.removeFromTop(48);
            notes.removeFromLeft(30);
            notesLabel_ = notes.removeFromLeft(60);
            const int buttons = juce::jmin(notes.getWidth() / 2, 96 * notesGen_.options().size());
            notesGen_.setBounds(notes.removeFromLeft(buttons).withSizeKeepingCentre(buttons, 44));
            notes.removeFromLeft(10);
            whyArea_ = notes.withHeight(16).withY(notes.getBottom() - 4);
            notesKnobs_.layout(notes.withTrimmedBottom(notesKnobs_.shown() > 0 && why_.isNotEmpty() ? 12 : 0).reduced(0, 2));
        }
        if (chord_) {
            r.removeFromTop(8);
            auto low = r.removeFromTop(48);
            low.removeFromLeft(30);
            articulation_.setBounds(low.removeFromRight(560).withSizeKeepingCentre(560, 44));
            low.removeFromRight(16);
            nearest_.setBounds(low.removeFromRight(130).withSizeKeepingCentre(130, 44));
            low.removeFromRight(6);
            size_.setBounds(low.removeFromLeft(low.getWidth() / 2).reduced(6, 4));
            voicing_.setBounds(low.reduced(6, 4));
            r.removeFromTop(8);
            auto hand = r.removeFromTop(48);  // Stagger and Top (§3.4b)
            hand.removeFromLeft(30);
            staggerLean_.setBounds(hand.removeFromRight(200).withSizeKeepingCentre(200, 44));
            hand.removeFromRight(6);
            staggerOrder_.setBounds(hand.removeFromRight(270).withSizeKeepingCentre(270, 44));
            hand.removeFromRight(6);
            stagger_.setBounds(hand.removeFromLeft(hand.getWidth() / 2).reduced(6, 4));
            top_.setBounds(hand.reduced(6, 4));
        }
    }

private:
    void change(const std::function<void(Voice&)>& f) {
        proc_.edit([this, f](Song& s) {
            if (Voice* v = findVoice(s, id_)) f(*v);
        });
    }
    HocketProcessor& proc_;
    std::uint64_t id_;
    int index_;
    bool chord_ = false;
    bool fromVoices_ = false;  // §5.8: the group's Density from voices: the Weight bar is the voice's Density
    Segmented type_;
    ValueBar channel_, octave_, weight_, size_, voicing_, feelVelocity_, feelTiming_, accent_, gate_, stagger_, top_;
    juce::String slot_;
    juce::Rectangle<int> slotArea_;
    Segmented articulation_, staggerOrder_, staggerLean_;
    Segmented notesGen_;
    GenKnobs notesKnobs_;
    juce::String why_;
    juce::Rectangle<int> notesLabel_, whyArea_;
    Pill tie_{"Tie"}, nearest_{"Nearest"}, lock_{"Lock notes"}, reroll_{"Reroll"}, remove_{"Delete", Pill::Kind::Danger};
};

class VoiceList : public juce::Component {
public:
    std::vector<std::unique_ptr<VoiceRow>> rows;
    void resized() override {
        int y = 0;
        for (auto& r : rows) {
            r->setBounds(0, y, getWidth(), r->heightNeeded());
            y += r->heightNeeded() + 10;
        }
    }
    int heightNeeded() const {
        int h = 0;
        for (auto& r : rows) h += r->heightNeeded() + 10;
        return h;
    }
};

class GroupsPage : public Page {
public:
    explicit GroupsPage(HocketProcessor& p)
        : Page(p), density_("Density", 1, 16, 8), steps_("Steps", 1, 64, 16), division_("Step length", 0, 23, 4),
          offset_("Offset", 0, 63, 0), swing_("Swing", 0, 100, 0), jitter_("Step jitter", 0, 100, 0),
          direction_({"Forward", "Backward", "Pendulum"}), deal_({"Fixed", "Every cycle", "Round robin", "Solo", "Dice", "Shuffle", "Runs", "Sweep", "Alternate", "Bells", "Tutti"}, 0, 4),
          overlap_({"Others ring", "Cut them"}), span_("Span", 1, 16, 2), handoff_("Handoff", 0, 50, 10), range_("Range", 1, 16, 8, colour::olive), wander_("Wander", 1, 16, 8, colour::olive),
          accent_("Accent", 0, 100, 50, colour::olive), gate_("Gate", 1, 200, 100, colour::olive), probability_("Probability", 0, 100, 100),
          ratchet_("Ratchet", 0, 100, 0, colour::yellow), repeats_("Repeats", 2, 4, 2, colour::yellow), ramp_("Ramp", -100, 100, 0, colour::yellow),
          fillEvery_("Fill every", 0, 16, 0, colour::yellow), fillBars_("Last", 1, 16, 1, colour::yellow), fillAmount_("Busier by", 1, 16, 8, colour::yellow),
          rhythmGen_(generatorLabels(SlotKind::Rhythm)), rhythmKnobs_(*this, colour::blue, 4) {
        addAndMakeVisible(rhythmGen_);
        rhythmGen_.setTitle("Rhythm generator");
        // §16.10: the group's Rhythm generator, swapped; its frame (accents, gates, swing, the deal…) stays (§16.2).
        rhythmGen_.onChange = [this](int i) {
            const auto all = generatorsFor(SlotKind::Rhythm);
            if (i >= 0 && i < static_cast<int>(all.size())) group([name = std::string(all[static_cast<std::size_t>(i)]->name)](Group& g) { g.rhythmGen.use = name; });
        };
        addAndMakeVisible(list_);
        addAndMakeVisible(newGroup_);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&name_, &lockRhythm_, &reroll_, &remove_, &density_, &steps_, &division_, &offset_, &swing_, &jitter_,
                                  &probability_, &direction_, &deal_, &span_, &handoff_, &overlap_, &lockDeal_, &fromVoices_, &range_, &wander_, &accent_, &gate_,
                                  &ratchet_, &repeats_, &ramp_, &fillEvery_, &fillBars_, &fillAmount_, &fillNow_, &clean_, &voicesView_, &addVoice_})
            addAndMakeVisible(c);
        voicesView_.setViewedComponent(&voices_, false);
        voicesView_.setScrollBarsShown(true, false);
        name_.setFont(sans(28.0f, true));
        name_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        name_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        name_.setColour(juce::TextEditor::focusedOutlineColourId, colour::line);
        name_.onReturnKey = name_.onFocusLost = [this] {
            const juce::String n = name_.getText().trim().substring(0, 40);
            if (n.isNotEmpty()) group([n](Group& g) { g.name = n.toStdString(); });
        };
        division_.format = [](int v) { return juce::String(kDivisions[juce::jlimit(0, 23, v)]); };
        jitter_.format = [](int v) { return juce::String(v) + " %"; };
        gate_.format = [](int v) { return juce::String(v) + " %"; };
        density_.onChange = [this](int v) { group([v](Group& g) { g.density = v; }); };
        steps_.onChange = [this](int v) { group([v](Group& g) { g.steps = v; }); };
        division_.onChange = [this](int v) { group([v](Group& g) { g.beatDivision = v; }); };
        offset_.onChange = [this](int v) { group([v](Group& g) { g.offset = v; }); };
        swing_.onChange = [this](int v) { group([v](Group& g) { g.swing = v; }); };
        jitter_.onChange = [this](int v) { group([v](Group& g) { g.jitter = v; }); };
        // §4.6: a hit plays with Probability, decided afresh at every step; one that plays may ratchet (repeat inside
        // its step); a fill makes the last bars before a boundary busier. All seeded: the same every time it plays.
        probability_.format = [](int v) { return v >= 100 ? juce::String("always") : juce::String(v) + " %"; };
        ratchet_.format = [](int v) { return v <= 0 ? juce::String("off") : juce::String(v) + " %"; };
        repeats_.format = [](int v) { return v == 2 ? juce::String("2 times") : "2-" + juce::String(v) + " times"; };
        ramp_.format = [](int v) { return v == 0 ? juce::String("level") : (v > 0 ? "+" : "") + juce::String(v) + " %"; };
        fillEvery_.format = [](int v) { return v == 0 ? juce::String("off") : v == 1 ? juce::String("phrase") : juce::String(v) + " phrases"; };
        fillBars_.format = [](int v) { return v == 1 ? juce::String("1 bar") : juce::String(v) + " bars"; };
        fillAmount_.format = [](int v) { return "+" + juce::String(v); };
        probability_.onChange = [this](int v) { group([v](Group& g) { g.probability = v; }); };
        ratchet_.onChange = [this](int v) { group([v](Group& g) { g.ratchet = v; }); };
        repeats_.onChange = [this](int v) { group([v](Group& g) { g.repeats = v; }); };
        ramp_.onChange = [this](int v) { group([v](Group& g) { g.ramp = v; }); };
        fillEvery_.onChange = [this](int v) { group([v](Group& g) { g.fillEvery = v; }); };
        fillBars_.onChange = [this](int v) { group([v](Group& g) { g.fillBars = v; }); };
        fillAmount_.onChange = [this](int v) { group([v](Group& g) { g.fillAmount = v; }); };
        fillNow_.onClick = [this] { group([](Group& g) { g.fillNow = !g.fillNow; }); };
        // §5.7: a note handed to another member on the pitch class of the one before it is redrawn.
        clean_.onClick = [this] { group([](Group& g) { g.cleanHandoffs = !g.cleanHandoffs; }); };
        for (ValueBar* b : {&ratchet_, &repeats_, &ramp_, &fillEvery_, &fillBars_, &fillAmount_}) b->setLayout(ValueBar::Layout::Inline);
        direction_.onChange = [this](int v) { group([v](Group& g) { g.direction = static_cast<Direction>(v); }); };
        deal_.onChange = [this](int v) { group([v](Group& g) { g.deal = static_cast<DealMode>(v); }); };
        // Who plays each hit (§5.1, after Koenig's Project 2): Fixed and Every cycle deal by weight, Dice rolls per hit,
        // Shuffle plays every voice once before any repeats, Runs gives a voice `span` hits in a row, Sweep picks
        // inside a window of `span` voices that moves across the group a step per pass.
        span_.setLayout(ValueBar::Layout::Inline);
        span_.onChange = [this](int v) { group([v](Group& g) { g.dealSpan = v; }); };
        // §5.5: under Alternate and Bells, how far a note overlaps the next member's at a handoff.
        handoff_.setLayout(ValueBar::Layout::Inline);
        handoff_.format = [](int v) { return v == 0 ? juce::String("none") : juce::String(v) + " %"; };
        handoff_.onChange = [this](int v) { group([v](Group& g) { g.handoff = v; }); };
        overlap_.onChange = [this](int v) { group([v](Group& g) { g.overlap = static_cast<Overlap>(v); }); };
        lockRhythm_.onClick = [this] { group([](Group& g) { g.lockRhythm = !g.lockRhythm; }); };
        lockDeal_.onClick = [this] { group([](Group& g) { g.lockDeal = !g.lockDeal; }); };
        // §5.8: each voice has its own Density, a place inside the group's; the line and the deal come from them.
        fromVoices_.onClick = [this] { group([](Group& g) { g.densityFromVoices = !g.densityFromVoices; }); };
        reroll_.onClick = [this] {
            proc.edit([this](Song& s) {
                if (Group* g = findGroup(s, selectedId_)) rerollRhythm(s, *g);
            });
        };
        remove_.onClick = [this] { proc.edit([this](Song& s) { removeGroup(s, selectedId_); }); };
        newGroup_.onClick = [this] {
            proc.edit([this](Song& s) {
                // A new group with one lead, on the first channel nothing else uses.
                const int ch = freeChannel(s);
                Group& g = addGroup(s, "Group " + std::to_string(s.groups.size() + 1));
                addVoice(s, s.groups.back(), VoiceType::Lead, ch);
                selectedId_ = g.id;
            });
        };
        addVoice_.onClick = [this] {
            proc.edit([this](Song& s) {
                const int ch = freeChannel(s);
                if (Group* g = findGroup(s, selectedId_)) addVoice(s, *g, g->voices.empty() ? VoiceType::Lead : g->voices.back().type, ch);
            });
        };
        // The group's own shape: each value either the song's (inherited) or set here (§1.2).
        auto own = [this](ValueBar& bar, std::optional<int> Overrides::*field) {
            bar.setLayout(ValueBar::Layout::Inline);
            bar.onChange = [this, field](int v) { group([field, v](Group& g) { g.overrides.*field = v; }); };
            bar.onInherit = [this, &bar, field](bool inherited) {
                const int v = bar.value();
                group([field, inherited, v](Group& g) {
                    if (inherited) g.overrides.*field = std::nullopt;
                    else g.overrides.*field = v;
                });
            };
        };
        own(range_, &Overrides::range);
        own(wander_, &Overrides::wander);
        own(accent_, &Overrides::accent);
        own(gate_, &Overrides::gatePercent);
        list_.onPick = [this](std::size_t i) {
            const Song& d = proc.document();
            if (i < d.groups.size()) {
                selectedId_ = d.groups[i].id;
                songChanged();
            }
        };
        songChanged();
    }

    void songChanged() override {
        const Song& d = proc.document();
        std::size_t sel = 0;
        bool found = false;
        for (std::size_t i = 0; i < d.groups.size(); ++i)
            if (d.groups[i].id == selectedId_) sel = i, found = true;
        if (!found && !d.groups.empty()) selectedId_ = d.groups[0].id;
        list_.set(d, sel);
        const bool any = !d.groups.empty();
        for (juce::Component* c : std::initializer_list<juce::Component*>{&name_, &lockRhythm_, &reroll_, &remove_, &density_, &steps_, &division_, &offset_, &swing_, &jitter_,
                                  &probability_, &direction_, &deal_, &span_, &handoff_, &overlap_, &lockDeal_, &fromVoices_, &range_, &wander_, &accent_, &gate_,
                                  &ratchet_, &repeats_, &ramp_, &fillEvery_, &fillBars_, &fillAmount_, &fillNow_, &clean_, &voicesView_, &addVoice_})
            c->setVisible(any);
        rhythmGen_.setVisible(any);
        if (!any) rhythmKnobs_.hide();
        if (!any) {
            resized();
            repaint();
            return;
        }
        const Group& g = d.groups[sel];
        if (!name_.hasKeyboardFocus(true)) name_.setText(g.name, false);
        rhythmGen_.setSelected(generatorIndex(SlotKind::Rhythm, g.rhythmGen));
        rhythmKnobs_.show(
            SlotKind::Rhythm, g.rhythmGen, [this](std::string gen, std::string key, int x) { group([gen, key, x](Group& grp) { grp.rhythmGen.params[gen][key] = x; }); },
            &d, g.id, [this](std::string gen, std::uint64_t id) { group([gen, id](Group& grp) { grp.rhythmGen.sources[gen] = id; }); });
        // A frame knob the generator doesn't read is kept, and greyed with a reason (§16.2).
        const Generator& gen = playing(SlotKind::Rhythm, g.rhythmGen);
        density_.setAlpha(gen.readsDensity ? 1.0f : 0.55f);  // dimmed: under Even it still shapes the gates (§16.2)
        rhythmWhy_ = gen.readsDensity ? juce::String() : juce::String(gen.why);
        if (std::string(gen.name) == "interlock" && !interlockSource(d, sel))  // §16.6: not live, so Random
            rhythmWhy_ = sourceOf(g.rhythmGen, "interlock") == 0 ? "No source: playing Random" : "Its source is gone or closes a loop: playing Random";
        if (std::string(gen.name) == "register") {
            // §16.7: with few bits a low Density's threshold is 0, and the group never plays.
            const int w = paramValue(SlotKind::Rhythm, g.rhythmGen, "register", "bits");
            int first = 1;
            while (first < 16 && !registerHit(0, w, first)) ++first;
            density_.setAlpha(g.density < first ? 0.55f : 1.0f);
            if (g.density < first) rhythmWhy_ = "With " + juce::String(w) + (w == 1 ? " bit" : " bits") + ", Density " + juce::String(first) + " is the lowest that plays";
        }
        density_.setValue(g.density);
        steps_.setValue(g.steps);
        division_.setValue(g.beatDivision);
        offset_.setRange(0, juce::jmax(0, g.steps - 1));
        offset_.setValue(g.offset);
        swing_.setValue(g.swing);
        jitter_.setValue(g.jitter);
        probability_.setValue(g.probability);
        ratchet_.setValue(g.ratchet);
        repeats_.setValue(g.repeats);
        ramp_.setValue(g.ramp);
        fillEvery_.setValue(g.fillEvery);
        fillBars_.setValue(g.fillBars);
        fillAmount_.setValue(g.fillAmount);
        fillNow_.setKind(g.fillNow ? Pill::Kind::Selected : Pill::Kind::Neutral);
        fillNow_.setButtonText(g.fillNow ? "Filling" : "Fill now");
        clean_.setKind(g.cleanHandoffs ? Pill::Kind::Selected : Pill::Kind::Neutral);
        clean_.setButtonText(g.cleanHandoffs ? "Clean handoffs: on" : "Clean handoffs");
        direction_.setSelected(static_cast<int>(g.direction));
        deal_.setSelected(static_cast<int>(g.deal));
        span_.setValue(g.dealSpan);
        span_.setLabel(g.deal == DealMode::Tendency ? "Window" : g.deal == DealMode::Bells ? "Bells each" : "Run length");
        span_.setVisible(g.deal == DealMode::Group || g.deal == DealMode::Tendency || g.deal == DealMode::Bells);
        handoff_.setValue(g.handoff);
        handoff_.setVisible(g.deal == DealMode::Alternate || g.deal == DealMode::Bells);
        overlap_.setSelected(static_cast<int>(g.overlap));
        lockRhythm_.setKind(g.lockRhythm ? Pill::Kind::Selected : Pill::Kind::Neutral);
        lockRhythm_.setButtonText(g.lockRhythm ? "Rhythm locked" : "Lock rhythm");
        lockDeal_.setKind(g.lockDeal ? Pill::Kind::Selected : Pill::Kind::Neutral);
        lockDeal_.setButtonText(g.lockDeal ? "Deal locked" : "Lock deal");
        fromVoices_.setKind(g.densityFromVoices ? Pill::Kind::Selected : Pill::Kind::Neutral);
        fromVoices_.setButtonText(g.densityFromVoices ? "Voice density: on" : "Voice density");
        slot_ = g.slot > 0 ? "Automation: Group " + juce::String(g.slot) : juce::String("No automation slot");
        auto showOwn = [](ValueBar& bar, std::optional<int> own, int song) {
            bar.setInheritable(true, !own);
            bar.setValue(own ? *own : song);
        };
        const Song& s = proc.document();
        showOwn(range_, g.overrides.range, s.range);
        showOwn(wander_, g.overrides.wander, s.wander);
        showOwn(accent_, g.overrides.accent, s.accent);
        showOwn(gate_, g.overrides.gatePercent, s.gatePercent);
        // Voices: rebuilt when the set changes, refreshed otherwise.
        std::vector<std::uint64_t> ids;
        for (const Voice& v : g.voices) ids.push_back(v.id);
        if (ids != voiceIds_) {
            voiceIds_ = ids;
            voices_.rows.clear();
            for (std::size_t i = 0; i < g.voices.size(); ++i) {
                voices_.rows.push_back(std::make_unique<VoiceRow>(proc, g.voices[i].id, static_cast<int>(i)));
                voices_.addAndMakeVisible(*voices_.rows.back());
            }
        }
        for (std::size_t i = 0; i < g.voices.size(); ++i) voices_.rows[i]->refresh(g.voices[i], d, sel, static_cast<int>(i));
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground);
        g.setColour(colour::line);
        g.drawVerticalLine(aside_.getRight(), static_cast<float>(aside_.getY()), static_cast<float>(aside_.getBottom()));
        heading(g, "Groups", groupsHead_);
        if (proc.document().groups.empty()) {
            g.setColour(colour::dim);
            g.setFont(sans(18.0f));
            g.drawText("No groups yet. Make one, and give it a voice.", main_, juce::Justification::centred);
            return;
        }
        for (const auto& r : panels_) panel(g, r);
        heading(g, "Rhythm", rhythmHead_);
        heading(g, "Who plays which hit", dealHead_);
        heading(g, "This group's shape", ownHead_);
        heading(g, "Ratchets and fills", moreHead_);
        heading(g, "Voices", voicesHead_);
        g.setColour(colour::dim);  // §12.2: the slot the group's host parameters are ("Group 1 Density")
        g.setFont(sans(12.5f));
        g.drawText(slot_, rhythmHead_, juce::Justification::centredRight);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawText("Direction", directionLabel_, juce::Justification::centredLeft);
        if (rhythmWhy_.isNotEmpty()) {
            g.setColour(colour::dim);
            g.setFont(sans(12.5f));
            g.drawText(rhythmWhy_, rhythmWhyArea_, juce::Justification::centredLeft);
            g.setColour(colour::soft);
            g.setFont(sans(15.0f));
        }
        g.drawText("When a voice starts a note", overlapLabel_, juce::Justification::centredLeft);
        g.drawText("When the next voice repeats the note", cleanLabel_, juce::Justification::centredLeft);
        g.setColour(colour::dim);
        g.setFont(sans(13.0f));
        g.drawText("A group has one rhythm. Its voices take turns playing it.", hint_, juce::Justification::topLeft);
    }

    void resized() override {
        panels_.clear();
        auto r = getLocalBounds();
        aside_ = r.removeFromLeft(330);
        auto a = aside_.reduced(20, 22);
        groupsHead_ = a.removeFromTop(20);
        a.removeFromTop(12);
        hint_ = a.removeFromBottom(40);
        newGroup_.setBounds(a.removeFromBottom(56));
        a.removeFromBottom(12);
        list_.setBounds(a.withHeight(juce::jmin(a.getHeight(), list_.heightNeeded())));

        main_ = r.reduced(28, 22);
        auto m = main_;
        auto head = m.removeFromTop(48);
        remove_.setBounds(head.removeFromRight(100));
        head.removeFromRight(8);
        reroll_.setBounds(head.removeFromRight(150));
        head.removeFromRight(8);
        lockRhythm_.setBounds(head.removeFromRight(160));
        head.removeFromRight(16);
        name_.setBounds(head);
        m.removeFromTop(16);

        auto cols = m.removeFromTop(512);
        auto rhythm = cols.removeFromLeft((cols.getWidth() - 18) / 2);
        cols.removeFromLeft(18);
        panels_.push_back(rhythm);
        rhythm = rhythm.reduced(20, 18);
        rhythmHead_ = rhythm.removeFromTop(20);
        rhythm.removeFromTop(6);
        // §16.10: the generators' buttons, the chosen one's knobs, then the frame's.
        rhythmGen_.setBounds(rhythm.removeFromTop(44));
        rhythm.removeFromTop(6);
        if (rhythmKnobs_.shown() > 0) {
            rhythmKnobs_.layout(rhythm.removeFromTop(48));
            rhythm.removeFromTop(4);
        }
        rhythmWhyArea_ = rhythmWhy_.isNotEmpty() ? rhythm.removeFromTop(18) : juce::Rectangle<int>();
        for (ValueBar* b : {&density_, &steps_, &division_, &offset_, &swing_, &jitter_, &probability_}) {
            b->setLayout(ValueBar::Layout::Inline);
            b->setBounds(rhythm.removeFromTop(rhythmKnobs_.shown() > 0 ? 36 : 42));
        }
        rhythm.removeFromTop(8);
        directionLabel_ = rhythm.removeFromTop(22);
        direction_.setBounds(rhythm.removeFromTop(44));

        auto deal = cols.removeFromTop(333);
        cols.removeFromTop(18);
        panels_.push_back(deal);
        deal = deal.reduced(20, 18);
        auto dh = deal.removeFromTop(20);
        dealHead_ = dh;
        lockDeal_.setBounds(juce::Rectangle<int>(dh.getRight() - 120, dh.getY() - 6, 120, 34));
        fromVoices_.setBounds(juce::Rectangle<int>(dh.getRight() - 120 - 8 - 150, dh.getY() - 6, 150, 34));
        deal.removeFromTop(10);
        deal_.setBounds(deal.removeFromTop(141));
        deal.removeFromTop(4);
        {
            // Span and Handoff share a row: Bells reads both (§5.5).
            auto knobs = deal.removeFromTop(40);
            if (span_.isVisible() && handoff_.isVisible()) {
                span_.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2).reduced(0, 0).withTrimmedRight(8));
                handoff_.setBounds(knobs.withTrimmedLeft(8));
            } else {
                span_.setBounds(knobs);
                handoff_.setBounds(knobs);
            }
        }
        deal.removeFromTop(6);
        auto ov = deal.removeFromTop(44);
        overlapLabel_ = ov.removeFromLeft(ov.getWidth() - 270);
        overlap_.setBounds(ov);
        deal.removeFromTop(6);
        auto cl = deal.removeFromTop(44);
        cleanLabel_ = cl.removeFromLeft(cl.getWidth() - 270);
        clean_.setBounds(cl);

        panels_.push_back(cols);
        auto own = cols.reduced(20, 18);
        ownHead_ = own.removeFromTop(20);
        own.removeFromTop(4);
        for (ValueBar* b : {&range_, &wander_, &accent_, &gate_}) b->setBounds(own.removeFromTop(42));

        // Ratchets and fills (§4.6): two rows of three, the manual Fill in the heading.
        m.removeFromTop(18);
        auto more = m.removeFromTop(142);
        panels_.push_back(more);
        more = more.reduced(20, 18);
        auto mh = more.removeFromTop(20);
        moreHead_ = mh;
        fillNow_.setBounds(juce::Rectangle<int>(mh.getRight() - 120, mh.getY() - 6, 120, 34));
        more.removeFromTop(6);
        for (auto row : {std::initializer_list<ValueBar*>{&ratchet_, &repeats_, &ramp_}, std::initializer_list<ValueBar*>{&fillEvery_, &fillBars_, &fillAmount_}}) {
            auto line = more.removeFromTop(40);
            const int w = line.getWidth() / 3;
            for (ValueBar* b : row) b->setBounds(line.removeFromLeft(w).reduced(6, 0));
        }

        m.removeFromTop(18);
        panels_.push_back(m);
        auto v = m.reduced(20, 18);
        auto vh = v.removeFromTop(44);
        voicesHead_ = vh.withHeight(20).withY(vh.getY() + 12);
        addVoice_.setBounds(vh.removeFromRight(140));
        v.removeFromTop(10);
        voicesView_.setBounds(v);
        voices_.setSize(v.getWidth() - 10, voices_.heightNeeded());
    }

private:
    void group(const std::function<void(Group&)>& f) {
        proc.edit([this, f](Song& s) {
            if (Group* g = findGroup(s, selectedId_)) f(*g);
        });
    }

    GroupList list_;
    Pill newGroup_{"New group", Pill::Kind::Dashed};
    juce::TextEditor name_;
    Pill lockRhythm_{"Lock rhythm"}, reroll_{"Reroll rhythm"}, remove_{"Delete", Pill::Kind::Danger};
    ValueBar density_, steps_, division_, offset_, swing_, jitter_;
    Segmented direction_, deal_, overlap_;
    ValueBar span_, handoff_;
    Pill lockDeal_{"Lock deal"}, fromVoices_{"Voice density"};
    juce::String slot_;
    ValueBar range_, wander_, accent_, gate_;
    ValueBar probability_, ratchet_, repeats_, ramp_, fillEvery_, fillBars_, fillAmount_;
    Pill fillNow_{"Fill now"}, clean_{"Clean handoffs"};
    Segmented rhythmGen_;
    GenKnobs rhythmKnobs_;
    juce::String rhythmWhy_;
    juce::Rectangle<int> rhythmWhyArea_;
    juce::Viewport voicesView_;
    VoiceList voices_;
    Pill addVoice_{"Add voice", Pill::Kind::Dashed};
    std::uint64_t selectedId_ = 0;
    std::vector<std::uint64_t> voiceIds_;
    std::vector<juce::Rectangle<int>> panels_;
    juce::Rectangle<int> aside_, main_, groupsHead_, hint_, rhythmHead_, dealHead_, ownHead_, moreHead_, voicesHead_, directionLabel_, overlapLabel_, cleanLabel_;
};

}  // namespace

std::unique_ptr<Page> makeGroupsPage(HocketProcessor& p) { return std::make_unique<GroupsPage>(p); }

}  // namespace hocket::ui
