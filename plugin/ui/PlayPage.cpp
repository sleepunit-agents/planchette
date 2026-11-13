// Play: what's happening now and what's coming. The chords, the form of the next bars, every group's lane, and the
// few things you touch while playing (§9): mute a group or a voice, reroll a group, Panic.
#include "Pages.h"

namespace hocket::ui {

namespace {

// One group's header: name, length, its voices as chips (tap: mute that voice), Mute, Reroll.
class GroupHeader : public juce::Component {
public:
    GroupHeader(HocketProcessor& p, std::size_t gi) : proc_(p), gi_(gi) {
        addAndMakeVisible(mute_);
        addAndMakeVisible(reroll_);
        mute_.onClick = [this] {
            proc_.edit([this](Song& s) {
                if (gi_ < s.groups.size()) s.groups[gi_].mute = !s.groups[gi_].mute;
            });
        };
        reroll_.onClick = [this] {
            proc_.edit([this](Song& s) {
                if (gi_ < s.groups.size()) rerollRhythm(s, s.groups[gi_]);
            });
        };
    }
    void refresh() {
        const Song& d = proc_.document();
        if (gi_ >= d.groups.size()) return;
        const Group& g = d.groups[gi_];
        mute_.setKind(g.mute ? Pill::Kind::Selected : Pill::Kind::Neutral);
        mute_.setButtonText(g.mute ? "Muted" : "Mute");
        repaint();
    }
    juce::Rectangle<int> chip(std::size_t v) const {
        const int w = (getWidth() - 6) / 2;
        return {static_cast<int>(v % 2) * (w + 6), 40 + static_cast<int>(v / 2) * 42, w, 36};
    }
    void paint(juce::Graphics& g) override {
        const Song& d = proc_.document();
        if (gi_ >= d.groups.size()) return;
        const Group& grp = d.groups[gi_];
        g.setColour(colour::text);
        g.setFont(sans(20.0f, true));
        g.drawText(grp.name, 0, 0, getWidth(), 28, juce::Justification::centredLeft);
        g.setColour(colour::dim);
        g.setFont(mono(13.0f, false));
        static const char* divisions[] = {"1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "5/16", "1/3",
                                          "3/8", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "16", "32"};
        // §16.10: the lane names its rhythm generator when it isn't Random.
        const Generator& gen = playing(SlotKind::Rhythm, grp.rhythmGen);
        const juce::String made = std::string(gen.name) == defaultGenerator(SlotKind::Rhythm) ? juce::String() : juce::String::fromUTF8(" · ") + juce::String(gen.label);
        g.drawText(juce::String(grp.steps) + " · " + divisions[juce::jlimit(0, 23, grp.beatDivision)] + made, 0, 0, getWidth(), 28,
                   juce::Justification::centredRight);
        for (std::size_t v = 0; v < grp.voices.size(); ++v) {
            const Voice& voice = grp.voices[v];
            const auto r = chip(v).toFloat();
            g.setColour(voice.mute ? colour::raised : colour::voice(static_cast<int>(v)));
            g.fillRoundedRectangle(r, 9.0f);
            g.setColour(voice.mute ? colour::dim : colour::ground);
            g.setFont(sans(13.0f, true));
            static const char* types[] = {"Lead", "Bass", "Chord"};
            g.drawText(juce::String(types[static_cast<int>(voice.type)]) + " · ch " + juce::String(voice.channel), r.toNearestInt(),
                       juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const Song& d = proc_.document();
        if (gi_ >= d.groups.size()) return;
        for (std::size_t v = 0; v < d.groups[gi_].voices.size(); ++v)
            if (chip(v).contains(e.getPosition()))
                proc_.edit([this, v](Song& s) {
                    if (gi_ < s.groups.size() && v < s.groups[gi_].voices.size()) s.groups[gi_].voices[v].mute = !s.groups[gi_].voices[v].mute;
                });
    }
    void resized() override {
        auto r = getLocalBounds().removeFromBottom(44);
        mute_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 4));
        r.removeFromLeft(8);
        reroll_.setBounds(r);
    }

private:
    HocketProcessor& proc_;
    std::size_t gi_;
    Pill mute_{"Mute"}, reroll_{"Reroll"};
};

class PlayPage : public Page {
public:
    explicit PlayPage(HocketProcessor& p) : Page(p) {
        addAndMakeVisible(chips_);
        addAndMakeVisible(form_);
        addAndMakeVisible(panic_);
        panic_.setAccent(colour::danger);
        panic_.onClick = [this] { proc.panic(); };
        // §9.2: Freeze, the host's "freeze" parameter (a controller's button can hold it too).
        addAndMakeVisible(freeze_);
        freeze_.setTitle("Freeze");
        freeze_.onClick = [this] {
            if (auto* f = proc.params.getParameter("freeze")) {
                f->beginChangeGesture();
                f->setValueNotifyingHost(f->getValue() > 0.5f ? 0.0f : 1.0f);
                f->endChangeGesture();
            }
            showFreeze();
        };
        songChanged();
    }

    void songChanged() override {
        const Song& d = proc.document();
        if (headers_.size() != d.groups.size()) {
            headers_.clear();
            cells_.clear();
            for (std::size_t gi = 0; gi < d.groups.size(); ++gi) {
                headers_.push_back(std::make_unique<GroupHeader>(proc, gi));
                cells_.push_back(std::make_unique<LaneCells>());
                addAndMakeVisible(*headers_.back());
                addAndMakeVisible(*cells_.back());
            }
            resized();
        }
        for (auto& h : headers_) h->refresh();
    }

    void tick(std::int64_t tick, View& view) override {
        const bool playing = tick >= 0;
        const std::int64_t at = playing ? tick : 0;
        const Song& s = proc.song();
        const HarmonyView h = view.harmony(at);
        chips_.set(h, playing);
        const std::int64_t bar = at / barTicks(s);
        std::vector<std::string> names;
        for (const auto& g : s.groups) names.push_back(g.name);
        form_.set(names, view.form(bar, 8), view.chordsByBar(bar, 8), bar);
        const std::vector<Lane> lanes = view.lanes(at);
        for (std::size_t i = 0; i < cells_.size() && i < lanes.size(); ++i) cells_[i]->set(lanes[i], playing);
        const std::int64_t beat = barTicks(s) / std::max(1, s.barLength);  // a beat of the meter: an 8th in 7/8
        status_ = playing ? juce::String(bar + 1) + "." + juce::String((at % barTicks(s)) / beat + 1) : "Stopped";
        sub_ = playing ? "phrase bar " + juce::String(h.phraseBar + 1) + " of " + juce::String(h.phraseLength) : "press Play in the host";
        // §9.2: under Freeze, what's held, and from when (bars count from 1 on screen).
        const std::int64_t held = proc.frozenFrom();
        if (playing && held >= 1) sub_ = "frozen: holding bar " + juce::String(held) + u8(" · let go to move on");
        else if (playing && freezeAsked()) sub_ = "Freeze takes hold at bar " + juce::String(bar + 2);
        showFreeze();
        key_ = juce::String(noteName(s.root)) + " " + kScales[static_cast<std::size_t>(s.scale)].name + " · chords every " +
               juce::StringArray{"quarter bar", "half bar", "bar", "2 bars", "4 bars"}[juce::jlimit(0, 4, s.harmony.changesEvery)];
        repaint(footer_);
        repaint(keyArea_);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground);
        heading(g, "Chords", chordsHead_);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawText(key_, keyArea_, juce::Justification::centredLeft);
        heading(g, "Form · the next 8 bars", formHead_);
        for (const auto& r : laneCards_) panel(g, r);
        g.setColour(colour::line);
        g.drawHorizontalLine(footer_.getY(), static_cast<float>(footer_.getX()), static_cast<float>(footer_.getRight()));
        g.setColour(colour::text);
        g.setFont(mono(22.0f));
        g.drawText(status_, footer_.withTrimmedRight(170).withTrimmedTop(18).removeFromTop(30), juce::Justification::centredRight);
        g.setColour(colour::dim);
        g.setFont(sans(13.0f));
        g.drawText(sub_, footer_.withTrimmedRight(170).withTrimmedTop(50).removeFromTop(20), juce::Justification::centredRight);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(28, 0);
        footer_ = r.removeFromBottom(96);
        panic_.setBounds(footer_.withLeft(footer_.getRight() - 150).withSizeKeepingCentre(150, 60).withX(footer_.getRight() - 150));
        freeze_.setBounds(footer_.withSizeKeepingCentre(170, 60).withX(footer_.getX()));
        r.removeFromTop(20);
        auto top = r.removeFromTop(200);
        auto left = top.removeFromLeft(top.getWidth() * 55 / 100);
        top.removeFromLeft(28);
        chordsHead_ = left.removeFromTop(20);
        keyArea_ = chordsHead_.withTrimmedLeft(90);
        left.removeFromTop(12);
        chips_.setChipHeight(74.0f);
        chips_.setPerRow(4);
        chips_.setBounds(left.removeFromTop(160));
        formHead_ = top.removeFromTop(20);
        top.removeFromTop(8);
        form_.setBounds(top);
        r.removeFromTop(14);
        laneCards_.clear();
        const int n = static_cast<int>(headers_.size());
        if (n == 0) return;
        const int gap = 14, h = juce::jmin(180, (r.getHeight() - gap * (n - 1) - 14) / n);
        for (int i = 0; i < n; ++i) {
            auto card = r.removeFromTop(h);
            r.removeFromTop(gap);
            laneCards_.push_back(card);
            auto in = card.reduced(16);
            headers_[static_cast<std::size_t>(i)]->setBounds(in.removeFromLeft(236));
            in.removeFromLeft(16);
            cells_[static_cast<std::size_t>(i)]->setBounds(in);
        }
    }

private:
    bool freezeAsked() const {
        const auto* f = proc.params.getParameter("freeze");
        return f && f->getValue() > 0.5f;
    }
    void showFreeze() {
        const bool holding = proc.frozenFrom() >= 1;
        freeze_.setKind(holding ? Pill::Kind::Selected : freezeAsked() ? Pill::Kind::Primary : Pill::Kind::Neutral);
        freeze_.setButtonText(holding ? "Frozen" : "Freeze");
    }

    ChordChips chips_;
    FormStrip form_;
    Pill panic_{"Panic"}, freeze_{"Freeze"};
    std::vector<std::unique_ptr<GroupHeader>> headers_;
    std::vector<std::unique_ptr<LaneCells>> cells_;
    std::vector<juce::Rectangle<int>> laneCards_;
    juce::Rectangle<int> footer_, chordsHead_, keyArea_, formHead_;
    juce::String status_, sub_, key_;
};

}  // namespace

std::unique_ptr<Page> makePlayPage(HocketProcessor& p) { return std::make_unique<PlayPage>(p); }

}  // namespace hocket::ui
