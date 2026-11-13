// Song: the key, the chords everything's built on, every line's shape, the seed, and where the MIDI goes.
#include "Pages.h"

namespace hocket::ui {

namespace {

constexpr int kFifths[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};

// Root, round the circle of fifths (the matrix moves the key by fifths, so neighbours here are neighbouring keys).
class RootPicker : public juce::Component {
public:
    explicit RootPicker(juce::AudioProcessorValueTreeState& s) : param_(*s.getParameter("root")) {
        attach_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { root_ = static_cast<int>(std::lround(v)); repaint(); }, nullptr);
        attach_->sendInitialUpdate();
    }
    juce::Rectangle<int> cell(int i) const {
        const int w = (getWidth() - 5 * 6) / 6, h = (getHeight() - 6) / 2;
        return {(i % 6) * (w + 6), (i / 6) * (h + 6), w, h};
    }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < 12; ++i) {
            const bool on = kFifths[i] == root_;
            g.setColour(on ? colour::text : colour::raised);
            g.fillRoundedRectangle(cell(i).toFloat(), 10.0f);
            g.setColour(on ? colour::ground : colour::text);
            g.setFont(sans(17.0f, true));
            g.drawText(noteName(kFifths[i]), cell(i), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < 12; ++i)
            if (cell(i).contains(e.getPosition())) attach_->setValueAsCompleteGesture(static_cast<float>(kFifths[i]));
    }

private:
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attach_;
    int root_ = 9;
};

// Scales, grouped by feel; a chip each.
class ScalePicker : public juce::Component {
public:
    struct Group {
        const char* name;
        std::vector<int> scales;
    };
    explicit ScalePicker(juce::AudioProcessorValueTreeState& s) : param_(*s.getParameter("scale")) {
        groups_ = {{"Modes", {0, 1, 2, 3, 4, 5, 6}},
                   {"Pentatonic and blues", {11, 12, 10}},
                   {"Colour", {13, 17, 14, 15, 16, 19, 18, 20, 23, 22, 21}},
                   {"Japanese and Balinese", {24, 25, 26, 27, 28, 29}},
                   {"Symmetric", {7, 8, 9, 30, 31, 32, 33, 34}}};
        attach_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { scale_ = static_cast<int>(std::lround(v)); repaint(); }, nullptr);
        attach_->sendInitialUpdate();
    }
    // The biggest chips that fit every scale in the space there is.
    void resized() override {
        for (int h : {36, 32, 28, 24})
            if (layout(h) <= getHeight()) return;
    }
    int layout(int chipH) {
        chips_.clear();
        headings_.clear();
        int y = 0;
        juce::Font f(sans(13.5f, true));
        for (const auto& grp : groups_) {
            headings_.push_back({grp.name, y});
            y += 20;
            int x = 0;
            for (int sc : grp.scales) {
                const int w = static_cast<int>(juce::GlyphArrangement::getStringWidth(f, kScales[static_cast<std::size_t>(sc)].name)) + 26;
                if (x > 0 && x + w > getWidth()) x = 0, y += chipH + 6;
                chips_.push_back({sc, {x, y, w, chipH}});
                x += w + 6;
            }
            y += chipH + 10;
        }
        return y;
    }
    void paint(juce::Graphics& g) override {
        for (const auto& [name, y] : headings_) {
            g.setColour(juce::Colour(0xff8d8a93));
            g.setFont(sans(12.5f, true));
            g.drawText(name, 0, y, getWidth(), 18, juce::Justification::centredLeft);
        }
        for (const auto& [sc, r] : chips_) {
            const bool on = sc == scale_;
            g.setColour(on ? colour::text : colour::raised);
            g.fillRoundedRectangle(r.toFloat(), 10.0f);
            g.setColour(on ? colour::ground : colour::text);
            g.setFont(sans(13.5f, true));
            g.drawText(kScales[static_cast<std::size_t>(sc)].name, r, juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (const auto& [sc, r] : chips_)
            if (r.contains(e.getPosition())) attach_->setValueAsCompleteGesture(static_cast<float>(sc));
    }

private:
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> attach_;
    std::vector<Group> groups_;
    std::vector<std::pair<int, juce::Rectangle<int>>> chips_;
    std::vector<std::pair<const char*, int>> headings_;
    int scale_ = 1;
};

class SongPage : public Page {
public:
    explicit SongPage(HocketProcessor& p)
        : Page(p), root_(p.params), scales_(p.params),
          every_(p.params, "chordEvery", {"1/4", "1/2", "1", "2", "4 bars"}),
          length_(p.params, "chordLength", "Chords before it repeats", colour::yellow),
          reach_(p.params, "chordRange", "Reach", colour::yellow),
          wander_(p.params, "chordWander", "Wander", colour::yellow),
          moves_(p.params, "moves", {"Ranked chords", "Mode moves"}),
          range_(p.params, "range", "Range"), lineWander_(p.params, "wander", "Wander"), accent_(p.params, "accent", "Accent"),
          gate_(p.params, "gate", "Gate"), octLow_(p.params, "octaveLow", "Octave low"), octHigh_(p.params, "octaveHigh", "Octave high"),
          colour_(p.params, "colour", "Mode colour", colour::lilac), jumps_(p.params, "jumps", "Octave jumps"),
          barLength_("Beats a bar", 1, 16, 4), beatUnit_("Beat", 0, 3, 1), phraseLength_("Bars a phrase", 1, 16, 4),
          output_({"Host", "Port", "Both"}) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&root_, &scales_, &chips_, &every_, &length_, &reach_, &wander_, &moves_, &lockChords_, &newChords_,
                                  &colour_, &range_, &lineWander_, &jumps_, &accent_, &gate_, &octLow_, &octHigh_, &generate_, &newSong_, &songs_, &save_, &saveAs_, &output_, &port_,
                                  &barLength_, &beatUnit_, &phraseLength_})
            addAndMakeVisible(c);
        chips_.setPerRow(2);
        chips_.setChipHeight(82.0f);
        accent_.format = [](int v) { return juce::String(v); };
        gate_.format = [](int v) { return juce::String(v) + " %"; };
        for (ValueBar* b : {&range_, &lineWander_, &accent_, &gate_, &octLow_, &octHigh_, &colour_, &jumps_})
            b->setLayout(ValueBar::Layout::Inline);
        colour_.format = [](int v) { return v == 0 ? juce::String("off") : juce::String(v); };
        for (ValueBar* b : {&barLength_, &beatUnit_, &phraseLength_}) b->setLayout(ValueBar::Layout::Inline);
        barLength_.onChange = [this](int v) { proc.edit([v](Song& s) { s.barLength = v; }); };
        beatUnit_.format = [](int i) { return "1/" + juce::String(2 << i); };
        beatUnit_.onChange = [this](int i) { proc.edit([i](Song& s) { s.beatUnit = 2 << i; }); };
        phraseLength_.onChange = [this](int v) { proc.edit([v](Song& s) { s.phraseLength = v; }); };
        generate_.onClick = [this] { proc.generate(); };
        newSong_.onClick = [this] { proc.newSong(); };
        songs_.onClick = [this] { showSongs(); };
        save_.onClick = [this] { proc.saveSong(proc.songName()); };
        saveAs_.onClick = [this] { askName(); };
        newChords_.onClick = [this] { proc.edit([](Song& s) { rerollHarmony(s); }); };
        lockChords_.onClick = [this] { proc.edit([](Song& s) { s.harmony.locked = !s.harmony.locked; }); };
        output_.setSelected(static_cast<int>(proc.output()));
        output_.onChange = [this](int i) {
            proc.setOutput(static_cast<HocketProcessor::Output>(i));
            port_.setEnabled(i != 0);
        };
        port_.setTextWhenNothingSelected("Choose a MIDI output");
        port_.setColour(juce::ComboBox::backgroundColourId, colour::raised);
        port_.setColour(juce::ComboBox::outlineColourId, colour::line);
        port_.setColour(juce::ComboBox::textColourId, colour::text);
        port_.onChange = [this] {
            const int i = port_.getSelectedId() - 1;
            if (i >= 0 && i < portNames_.size()) proc.setPort(portNames_[i]);
        };
        refreshPorts();
        // The list keeps itself current: a device plugged in or out shows up here at once (no refresh button).
        devices_ = juce::MidiDeviceListConnection::make([this] { refreshPorts(); });
        port_.setEnabled(proc.output() != HocketProcessor::Output::Host);
        songChanged();
    }

    // The MIDI outputs there are now; the chosen one stays chosen, marked "not connected" while it's unplugged.
    void refreshPorts() {
        port_.clear(juce::dontSendNotification);
        portNames_.clear();
        for (const auto& d : juce::MidiOutput::getAvailableDevices()) {
            portNames_.add(d.name);
            port_.addItem(d.name, portNames_.size());
            if (d.name == proc.portName()) port_.setSelectedId(portNames_.size(), juce::dontSendNotification);
        }
        const juce::String chosen = proc.portName();
        if (chosen.isNotEmpty() && !portNames_.contains(chosen)) {
            portNames_.add(chosen);
            port_.addItem(chosen + " (not connected)", portNames_.size());
            port_.setItemEnabled(portNames_.size(), false);
            port_.setSelectedId(portNames_.size(), juce::dontSendNotification);
        }
    }

    bool isUserSong(const juce::String& name) const {
        for (const auto& e : proc.songs())
            if (e.name == name) return !e.isStarter();
        return false;
    }

    // Your songs and the starters; tap one to load it (Undo goes back). Delete is a level down, and asks.
    void showSongs() {
        juce::PopupMenu m, del;
        const auto& list = proc.songs();
        m.addSectionHeader("Starter songs");
        bool mine = false;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto e = list[i];
            if (!e.isStarter() && !mine) {
                m.addSectionHeader("Your songs");
                mine = true;
            }
            m.addItem(e.name, true, e.name == proc.songName(), [this, e] { proc.loadSong(e); });
            if (!e.isStarter()) del.addItem(e.name, [this, e] { confirmDelete(e); });
        }
        if (!mine) m.addItem("Your songs show up here once you save one", false, false, [] {});
        m.addSeparator();
        if (mine) m.addSubMenu("Delete", del);
#if !(JUCE_IOS || JUCE_ANDROID)
        m.addItem("Show the songs folder", [this] {
            proc.libraryFolder().createDirectory();
            proc.libraryFolder().revealToUser();
        });
#endif
        m.addItem("Look for new songs in the folder", [this] { proc.refreshSongs(); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&songs_));
    }

    void askName() {
        auto* w = new juce::AlertWindow("Save the song", "A name for it:", juce::MessageBoxIconType::NoIcon, this);
        const juce::String name = proc.songName().isNotEmpty() ? proc.songName() : juce::String();
        w->addTextEditor("name", name);
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            const juce::String n = w->getTextEditorContents("name").trim();
            if (r != 1 || n.isEmpty()) return;
            if (isUserSong(juce::File::createLegalFileName(n)) && n != proc.songName()) {
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Replace \"" + n + "\"?",
                                                   "There's already a song with that name.", "Replace", "Cancel", this,
                                                   juce::ModalCallbackFunction::create([this, n](int ok) {
                                                       if (ok) proc.saveSong(n);
                                                   }));
                return;
            }
            proc.saveSong(n);
        }), true);
    }

    void confirmDelete(const library::Entry& e) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete \"" + e.name + "\"?",
                                           "The file goes; this can't be undone.", "Delete", "Cancel", this,
                                           juce::ModalCallbackFunction::create([this, e](int ok) {
                                               if (ok) proc.deleteSong(e);
                                           }));
    }

    void songChanged() override {
        const Song& d = proc.document();
        save_.setEnabled(proc.edited() && isUserSong(proc.songName()));
        // A host that reports its meter sets it (its bars are the song's); the song's own is for hosts that don't.
        const auto [beats, unit] = proc.hostMeter();
        const bool fromHost = beats > 0;
        barLength_.setValue(fromHost ? beats : d.barLength);
        beatUnit_.setValue(juce::jlimit(0, 3, static_cast<int>(std::log2(fromHost ? unit : d.beatUnit)) - 1));
        barLength_.setLabel(fromHost ? "Beats (host)" : "Beats a bar");
        beatUnit_.setLabel(fromHost ? "Beat (host)" : "Beat");
        for (ValueBar* b : {&barLength_, &beatUnit_}) {
            b->setEnabled(!fromHost);
            b->setAlpha(fromHost ? 0.55f : 1.0f);
        }
        phraseLength_.setValue(d.phraseLength);
        if (d.harmony.length != shownLength_) {
            shownLength_ = d.harmony.length;
            resized();
        }
        lockChords_.setButtonText(d.harmony.locked ? "Chords locked" : "Lock chords");
        lockChords_.setKind(d.harmony.locked ? Pill::Kind::Selected : Pill::Kind::Neutral);
        repaint();
    }

    void tick(std::int64_t tick, View& view) override {
        const Song& s = proc.song();
        const HarmonyView h = view.harmony(std::max<std::int64_t>(0, tick));
        // Each chord's notes, low to high from its root.
        std::vector<std::string> notes;
        int size = 3;  // a triad, unless a Chord voice says (the first one)
        for (auto g = s.groups.rbegin(); g != s.groups.rend(); ++g)
            for (auto v = g->voices.rbegin(); v != g->voices.rend(); ++v)
                if (v->type == VoiceType::Chord) size = v->chordSize;
        for (int r : h.roots) {
            std::vector<int> rk = ranked(r, s.root, s.scale);
            rk.resize(static_cast<std::size_t>(juce::jmin(size, static_cast<int>(rk.size()))));
            std::sort(rk.begin(), rk.end(), [r](int a, int b) { return (a - r + 12) % 12 < (b - r + 12) % 12; });
            std::string text;
            for (int pc : rk) text += (text.empty() ? "" : " ") + noteName(pc);
            notes.push_back(text);
        }
        chips_.set(h, tick >= 0, true, notes);
        if (s.root != shownRoot_ || s.scale != shownScale_) {
            shownRoot_ = s.root, shownScale_ = s.scale;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground);
        for (const auto& r : panels_) panel(g, r);
        heading(g, "Key", keyHead_);
        heading(g, "Chords: the core everything's built on", chordsHead_);
        heading(g, "Every line, unless a group says", linesHead_);
        heading(g, "Song", seedHead_);
        heading(g, "MIDI out", outHead_);
        const Song& s = proc.song();
        // The key, big.
        g.setColour(colour::text);
        g.setFont(sans(40.0f, true));
        g.drawText(noteName(s.root), keyName_, juce::Justification::centredLeft);
        g.setColour(colour::soft);
        g.setFont(sans(26.0f));
        g.drawText(kScales[static_cast<std::size_t>(s.scale)].name, keyName_.withTrimmedLeft(64), juce::Justification::centredLeft);
        g.setColour(colour::dim);
        g.setFont(sans(13.0f));
        g.drawText("Root, round the circle of fifths", rootHint_, juce::Justification::centredLeft);
        // One octave, the scale's notes lit and the root in yellow.
        const int w = (keyboard_.getWidth() - 11 * 3) / 12;
        for (int pc = 0; pc < 12; ++pc) {
            const auto k = juce::Rectangle<int>(keyboard_.getX() + pc * (w + 3), keyboard_.getY(), w, keyboard_.getHeight()).toFloat();
            const bool in = (kScales[static_cast<std::size_t>(s.scale)].mask >> ((pc - s.root + 12) % 12)) & 1;
            const bool black = noteName(pc).size() > 1, root = pc == s.root;
            g.setColour(root ? colour::yellow : in ? colour::text : black ? juce::Colour(0xff0e0d11) : juce::Colour(0xff3a3942));
            g.fillRoundedRectangle(k, 6.0f);
            if (in) {
                g.setColour(colour::ground);
                g.setFont(sans(11.0f, true));
                g.drawText(noteName(pc), k.toNearestInt().withTrimmedBottom(6), juce::Justification::centredBottom);
            }
        }
        // The song's name, and its seed.
        const juce::String name = proc.songName().isNotEmpty() ? proc.songName() : juce::String("Untitled");
        g.setColour(colour::text);
        g.setFont(sans(24.0f, true));
        g.drawText(name, nameText_, juce::Justification::centredLeft);
        if (proc.edited() && proc.songName().isNotEmpty()) {
            g.setColour(colour::yellow);
            g.setFont(sans(14.0f, true));
            const int w = juce::GlyphArrangement::getStringWidthInt(juce::Font(sans(24.0f, true)), name);
            g.drawText("edited", nameText_.withTrimmedLeft(w + 12), juce::Justification::centredLeft);
        }
        g.setColour(colour::dim);
        g.setFont(mono(14.0f, false));
        const Song& d = proc.document();
        g.drawText(juce::String::toHexString(static_cast<juce::int64>(d.masterSeed)).paddedLeft('0', 16) +
                       (d.generateCount ? "  · Generate " + juce::String(static_cast<juce::int64>(d.generateCount)) : juce::String()),
                   seedText_, juce::Justification::centredLeft);
        // What the chord knobs mean.
        g.setColour(juce::Colour(0xff8d8a93));
        g.setFont(sans(13.0f));
        g.drawText("One per change, then round again.", hintLength_, juce::Justification::centredLeft);
        g.drawText("Which chords it may use, nearest the key first.", hintReach_, juce::Justification::centredLeft);
        g.drawText("How often it leaves home. It always starts there.", hintWander_, juce::Justification::centredLeft);
        g.drawText(proc.song().colour > 0 ? "The mode's own move; no V-I into its relative major."
                                          : "Mode moves need Mode colour above 0.",
                   hintMoves_, juce::Justification::centredLeft);
    }

    void resized() override {
        panels_.clear();
        auto r = getLocalBounds().reduced(28, 22);
        const int gap = 18, w = (r.getWidth() - 2 * gap) / 3;
        auto key = r.removeFromLeft(w);
        r.removeFromLeft(gap);
        auto chords = r.removeFromLeft(w);
        r.removeFromLeft(gap);
        auto right = r;
        panels_.push_back(key);
        panels_.push_back(chords);

        key = key.reduced(20);
        keyHead_ = key.removeFromTop(20);
        key.removeFromTop(10);
        keyName_ = key.removeFromTop(48);
        key.removeFromTop(8);
        rootHint_ = key.removeFromTop(18);
        root_.setBounds(key.removeFromTop(102));
        key.removeFromTop(14);
        keyboard_ = key.removeFromTop(58);
        key.removeFromTop(16);
        scales_.setBounds(key);

        chords = chords.reduced(20);
        chordsHead_ = chords.removeFromTop(20);
        chords.removeFromTop(12);
        // Two chords a row, as many rows as the progression has pairs.
        const int rows = (juce::jlimit(1, 16, proc.document().harmony.length) + 1) / 2;
        chips_.setBounds(chords.removeFromTop(juce::jmin(rows * 92 - 10, 4 * 92 - 10)));
        chords.removeFromTop(16);
        every_.setBounds(chords.removeFromTop(44));
        chords.removeFromTop(16);
        for (auto [bar, hint] : {std::pair{&length_, &hintLength_}, std::pair{&reach_, &hintReach_}, std::pair{&wander_, &hintWander_}}) {
            bar->setBounds(chords.removeFromTop(62));
            *hint = chords.removeFromTop(20);
            chords.removeFromTop(10);
        }
        moves_.setBounds(chords.removeFromTop(44));
        hintMoves_ = chords.removeFromTop(20);
        auto buttons = chords.removeFromBottom(48);
        lockChords_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft(8);
        newChords_.setBounds(buttons);

        const int rowH = 40;
        auto lines = right.removeFromTop(11 * rowH + 60);
        right.removeFromTop(gap);
        panels_.push_back(lines);
        panels_.push_back(right);
        lines = lines.reduced(20);
        linesHead_ = lines.removeFromTop(20);
        lines.removeFromTop(8);
        for (ValueBar* b : {static_cast<ValueBar*>(&colour_), static_cast<ValueBar*>(&jumps_), static_cast<ValueBar*>(&range_), static_cast<ValueBar*>(&lineWander_), static_cast<ValueBar*>(&accent_),
                            static_cast<ValueBar*>(&gate_), static_cast<ValueBar*>(&octLow_), static_cast<ValueBar*>(&octHigh_),
                            &barLength_, &beatUnit_, &phraseLength_})
            b->setBounds(lines.removeFromTop(rowH));

        auto seed = right.reduced(20);
        seedHead_ = seed.removeFromTop(20);
        seed.removeFromTop(6);
        nameText_ = seed.removeFromTop(34);
        seedText_ = seed.removeFromTop(22);
        seed.removeFromTop(10);
        auto files = seed.removeFromTop(48);
        const int third = (files.getWidth() - 16) / 3;
        songs_.setBounds(files.removeFromLeft(third));
        files.removeFromLeft(8);
        save_.setBounds(files.removeFromLeft(third));
        files.removeFromLeft(8);
        saveAs_.setBounds(files);
        seed.removeFromTop(8);
        auto row = seed.removeFromTop(48);
        generate_.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4));
        row.removeFromLeft(8);
        newSong_.setBounds(row);
        seed.removeFromTop(18);
        outHead_ = seed.removeFromTop(20);
        seed.removeFromTop(8);
        output_.setBounds(seed.removeFromTop(44));
        seed.removeFromTop(8);
        port_.setBounds(seed.removeFromTop(48));
    }

private:
    RootPicker root_;
    ScalePicker scales_;
    ChordChips chips_;
    ParamChoice every_, moves_;
    ParamBar length_, reach_, wander_;
    Pill lockChords_{"Lock chords"}, newChords_{"New progression"};
    ParamBar range_, lineWander_, accent_, gate_, octLow_, octHigh_, colour_, jumps_;
    Pill generate_{"Generate", Pill::Kind::Primary}, newSong_{"New song"};
    Pill songs_{u8("Songs ▾")}, save_{"Save"}, saveAs_{u8("Save as…")};
    ValueBar barLength_, beatUnit_, phraseLength_;
    Segmented output_;
    juce::ComboBox port_;
    juce::StringArray portNames_;  // by item id − 1
    juce::MidiDeviceListConnection devices_;
    std::vector<juce::Rectangle<int>> panels_;
    juce::Rectangle<int> keyHead_, keyName_, rootHint_, keyboard_, chordsHead_, linesHead_, seedHead_, seedText_, nameText_, outHead_;
    juce::Rectangle<int> hintLength_, hintReach_, hintWander_, hintMoves_;
    int shownRoot_ = -1, shownScale_ = -1, shownLength_ = -1;
};

}  // namespace

std::unique_ptr<Page> makeSongPage(HocketProcessor& p) { return std::make_unique<SongPage>(p); }

}  // namespace hocket::ui
