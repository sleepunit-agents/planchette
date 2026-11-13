// Form: the song's long shape (§18). The form string (A B A′ C …) as chips you tap, how long a letter lasts and what
// happens at the end, and the letters: what each one rolls again and sets, and which one the screens are editing.
// Under them, the colotomic clock (§18.2): the gong cycle and its kenongs.
#include "Pages.h"

#include "hocket/form.hpp"

namespace hocket::ui {

namespace {

// The form string, a chip per letter. Tap: the next letter (A, then each the form has, then A again). Double-tap:
// primed or not. The last two chips add a letter (a copy of the last) and take the last away.
class FormChips : public juce::Component {
public:
    explicit FormChips(HocketProcessor& p) : proc_(p) { setTitle("The form string"); }

    void set(std::vector<FormEntry> entries, std::vector<char> letters, int playing) {
        if (entries == entries_ && letters == letters_ && playing == playing_) return;
        entries_ = std::move(entries), letters_ = std::move(letters), playing_ = playing;
        setDescription(juce::String(formText(entries_)));
        repaint();
    }
    juce::Rectangle<int> cell(int i) const {
        const int w = 62, h = 56, gap = 8, per = juce::jmax(1, (getWidth() + gap) / (w + gap));
        return {(i % per) * (w + gap), (i / per) * (h + gap), w, h};
    }
    int count() const { return static_cast<int>(entries_.size()); }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < count(); ++i) {
            const FormEntry& e = entries_[static_cast<std::size_t>(i)];
            const auto r = cell(i).toFloat();
            g.setColour(e.letter == 'A' ? colour::raised : colour::voice(3 + (e.letter - 'B') % 2));
            g.fillRoundedRectangle(r, 10.0f);
            if (i == playing_) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(r.reduced(1.0f), 10.0f, 2.5f);
            }
            g.setColour(e.letter == 'A' ? colour::text : colour::ground);
            g.setFont(mono(24.0f));
            g.drawText(u8(entryName(e).c_str()), r.toNearestInt(), juce::Justification::centred);
        }
        for (int k = 0; k < 2; ++k) {
            const auto r = cell(count() + k).toFloat();
            g.setColour(colour::line);
            g.drawRoundedRectangle(r.reduced(1.0f), 10.0f, 1.5f);
            g.setColour(colour::dim);
            g.setFont(sans(24.0f, true));
            g.drawText(k == 0 ? "+" : u8("−"), r.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const int i = hit(e.getPosition());
        if (i < 0) return;
        if (e.getNumberOfClicks() > 1 && i < count()) return;  // a double-tap primes (mouseDoubleClick)
        if (i == count()) change([](std::vector<FormEntry>& es) { es.push_back(es.empty() ? FormEntry{} : FormEntry{es.back().letter, false}); });
        else if (i == count() + 1) change([](std::vector<FormEntry>& es) { if (!es.empty()) es.pop_back(); });
        else
            change([this, i](std::vector<FormEntry>& es) {
                // A, then each letter the form defines, then A again.
                std::vector<char> order{'A'};
                for (char c : letters_) order.push_back(c);
                auto& x = es[static_cast<std::size_t>(i)].letter;
                const auto it = std::find(order.begin(), order.end(), x);
                x = it == order.end() || it + 1 == order.end() ? 'A' : *(it + 1);
            });
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int i = hit(e.getPosition());
        if (i >= 0 && i < count())
            change([i](std::vector<FormEntry>& es) { es[static_cast<std::size_t>(i)].primed = !es[static_cast<std::size_t>(i)].primed; });
    }

private:
    int hit(juce::Point<int> p) const {
        for (int i = 0; i < count() + 2; ++i)
            if (cell(i).contains(p)) return i;
        return -1;
    }
    void change(const std::function<void(std::vector<FormEntry>&)>& f) {
        proc_.edit([f](Song& s) {
            std::vector<FormEntry> es = parseForm(s.form.text);
            f(es);
            s.form.text = formText(es);
        });
    }
    HocketProcessor& proc_;
    std::vector<FormEntry> entries_;
    std::vector<char> letters_;
    int playing_ = -1;
};

// What a letter sets, in words: "Leads Density 14 · Root E".
juce::String describe(const Song& s, const FormLetter& l) {
    juce::StringArray parts;
    for (const auto& [d, value] : l.params) {
        juce::String where;
        if (d.scope == Scope::Group || d.scope == Scope::Voice)
            for (const Group& g : s.groups) {
                if (g.id == d.target) where = juce::String(g.name) + " ";
                for (std::size_t v = 0; v < g.voices.size(); ++v)
                    if (g.voices[v].id == d.target) where = juce::String(g.name) + " " + juce::String(static_cast<int>(v) + 1) + " ";
            }
        if (d.scope == Scope::Harmony) where = "Chords ";
        const juce::String v = d.key == "root" ? juce::String(noteName(value)) : d.key == "scale" ? juce::String(kScales[static_cast<std::size_t>(juce::jlimit(0, 34, value))].name)
                                                                                                 : juce::String(value);
        parts.add(where + juce::String(d.key) + " " + v);
    }
    juce::StringArray rolls;
    if (l.notes) rolls.add("notes");
    if (l.rhythm) rolls.add("rhythm");
    if (l.harmony) rolls.add("chords");
    if (l.deal) rolls.add("deal");
    if (l.matrix) rolls.add("changes");
    juce::String out = rolls.isEmpty() ? juce::String("A's seeds") : "new " + rolls.joinIntoString(", ");
    if (!parts.isEmpty()) out << u8(" · sets ") << parts.joinIntoString(", ");
    return out;
}

// One letter's row: its name, what it rolls again (toggles), New roll, Edit, Delete; under them, what it sets.
class LetterRow : public juce::Component {
public:
    LetterRow(HocketProcessor& p, char name) : proc_(p), name_(name) {
        const juce::String n = juce::String::charToString(static_cast<juce::juce_wchar>(name));
        setTitle(n);
        edit_.setButtonText("Edit " + n);
        const char* labels[] = {"Notes", "Rhythm", "Chords", "Deal", "Changes"};
        for (int i = 0; i < 5; ++i) {
            rolls_[i].setButtonText(labels[i]);
            addAndMakeVisible(rolls_[i]);
            rolls_[i].onClick = [this, i] {
                proc_.edit([this, i](Song& s) {
                    for (FormLetter& l : s.form.letters)
                        if (l.name == name_) {
                            bool* f[] = {&l.notes, &l.rhythm, &l.harmony, &l.deal, &l.matrix};
                            *f[i] = !*f[i];
                        }
                });
            };
        }
        for (Pill* b : {&roll_, &edit_, &delete_}) addAndMakeVisible(*b);
        roll_.setButtonText("New roll " + n);
        delete_.setButtonText("Delete " + n);
        roll_.onClick = [this] {
            proc_.edit([this](Song& s) {
                const std::uint64_t salt = newId(s);
                for (FormLetter& l : s.form.letters)
                    if (l.name == name_) l.salt = salt;
            });
        };
        edit_.onClick = [this] { proc_.editLetter(proc_.editingLetter() == name_ ? 0 : name_); };
        delete_.onClick = [this] {
            if (proc_.editingLetter() == name_) proc_.editLetter(0);
            proc_.edit([this](Song& s) {
                auto& ls = s.form.letters;
                ls.erase(std::remove_if(ls.begin(), ls.end(), [this](const FormLetter& l) { return l.name == name_; }), ls.end());
            });
        };
        delete_.setAccent(colour::danger);
    }
    void refresh(const Song& s) {
        const FormLetter* l = letterOf(s, name_);
        if (!l) return;
        const bool on[] = {l->notes, l->rhythm, l->harmony, l->deal, l->matrix};
        for (int i = 0; i < 5; ++i) rolls_[i].setKind(on[i] ? Pill::Kind::Selected : Pill::Kind::Neutral);
        edit_.setKind(proc_.editingLetter() == name_ ? Pill::Kind::Primary : Pill::Kind::Neutral);
        text_ = describe(s, *l);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds();
        auto chip = r.removeFromLeft(56).withHeight(56).toFloat();
        g.setColour(colour::voice(3 + (name_ - 'B') % 2));
        g.fillRoundedRectangle(chip, 10.0f);
        g.setColour(colour::ground);
        g.setFont(mono(26.0f));
        g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(name_)), chip.toNearestInt(), juce::Justification::centred);
        g.setColour(colour::dim);
        g.setFont(sans(13.0f));
        g.drawFittedText(text_, r.withTrimmedLeft(14).removeFromBottom(22), juce::Justification::centredLeft, 1, 0.8f);
    }
    void resized() override {
        auto r = getLocalBounds().withTrimmedLeft(70);
        auto top = r.removeFromTop(40);
        auto right = top.removeFromRight(330);
        for (auto& b : rolls_) {
            b.setBounds(top.removeFromLeft(juce::jmax(64, (top.getWidth() + 6) / 5 - 6)).withTrimmedRight(6));
        }
        roll_.setBounds(right.removeFromLeft(126).withTrimmedRight(6));
        edit_.setBounds(right.removeFromLeft(96).withTrimmedRight(6));
        delete_.setBounds(right);
    }

private:
    HocketProcessor& proc_;
    char name_;
    Pill rolls_[5]{Pill{""}, Pill{""}, Pill{""}, Pill{""}, Pill{""}}, roll_{"New roll"}, edit_{"Edit"}, delete_{"Delete"};
    juce::String text_;
};

// §18.2: the gong cycle, a cell per bar: the gong where the cycle ends and starts again, the kenong strokes marked
// with their numbers, the bar playing lit.
class GongCycle : public juce::Component {
public:
    void set(const Song& s, std::int64_t bar) {
        const std::int64_t N = gongBars(s);
        std::vector<int> marks;
        for (std::int64_t b = 0; b < N; ++b) marks.push_back(kenongNumber(s, b));
        const int at = bar >= 0 ? static_cast<int>(bar % N) : -1;
        if (marks == marks_ && at == at_) return;
        marks_ = std::move(marks), at_ = at;
        setTitle("Gong cycle");
        setDescription(juce::String(static_cast<int>(marks_.size())) + " bars, " + juce::String(kenongCount(s)) + " kenongs");
        repaint();
    }
    void paint(juce::Graphics& g) override {
        const int n = static_cast<int>(marks_.size());
        if (n == 0) return;
        const float gap = n > 32 ? 2.0f : 4.0f, w = (getWidth() - gap * (n - 1)) / n, h = static_cast<float>(getHeight()) - 18.0f;
        for (int b = 0; b < n; ++b) {
            const auto r = juce::Rectangle<float>(b * (w + gap), 0.0f, w, h);
            const int k = marks_[static_cast<std::size_t>(b)];
            g.setColour(b == 0 ? colour::yellow : k ? colour::olive : colour::raised);
            g.fillRoundedRectangle(r, juce::jmin(6.0f, w / 3));
            if (b == at_) {
                g.setColour(colour::text);
                g.drawRoundedRectangle(r.reduced(1.0f), juce::jmin(6.0f, w / 3), 2.0f);
            }
            if (k && w > 9.0f) {
                g.setColour(colour::ground);
                g.setFont(mono(juce::jmin(13.0f, w * 0.7f)));
                g.drawText(b == 0 ? "G" : juce::String(k), r.toNearestInt(), juce::Justification::centred);
            }
        }
        g.setColour(colour::dim);
        g.setFont(sans(12.0f));
        g.drawText(u8("gong ends the cycle · kenong strokes · a cell a bar"), getLocalBounds().removeFromBottom(16), juce::Justification::centredLeft);
    }

private:
    std::vector<int> marks_;
    int at_ = -1;
};

class FormPage : public Page {
public:
    explicit FormPage(HocketProcessor& p)
        : Page(p), chips_(p), on_({"Plays as written", "Plays the form"}), section_("Each letter lasts", 1, 16, 2, colour::olive),
          end_({"Then from the top", "Then holds the last"}), prime_(u8("A′ changes"), 1, 16, 4, colour::olive) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{&chips_, &on_, &section_, &end_, &prime_, &add_, &back_, &editA_,
                                                                          &gongCycle_, &kenongs_, &cycle_})
            addAndMakeVisible(c);
        // §18.2: the colotomic clock.
        gongCycle_.setLayout(ValueBar::Layout::Inline);
        kenongs_.setLayout(ValueBar::Layout::Inline);
        gongCycle_.format = [this](int v) {
            return juce::String(v) + (v == 1 ? " phrase" : " phrases") + u8(" · ") + juce::String(v * proc.document().phraseLength) + " bars";
        };
        gongCycle_.onChange = [this](int v) { proc.edit([v](Song& s) { s.gongCycle = v; }); };
        kenongs_.onChange = [this](int v) { proc.edit([v](Song& s) { s.kenongs = v; }); };
        on_.setTitle("Form");
        end_.setTitle("Form end");
        section_.setLayout(ValueBar::Layout::Inline);
        prime_.setLayout(ValueBar::Layout::Inline);
        section_.format = [this](int v) {
            return juce::String(v) + (v == 1 ? " phrase" : " phrases") + u8(" · ") + juce::String(v * proc.document().phraseLength) + " bars";
        };
        prime_.format = [](int v) { return juce::String(v) + (v == 1 ? " note a line" : " notes a line"); };
        on_.onChange = [this](int i) { proc.edit([i](Song& s) { s.form.on = i == 1; }); };
        end_.onChange = [this](int i) { proc.edit([i](Song& s) { s.form.end = i; }); };
        section_.onChange = [this](int v) { proc.edit([v](Song& s) { s.form.section = v; }); };
        prime_.onChange = [this](int v) { proc.edit([v](Song& s) { s.form.prime = v; }); };
        add_.onClick = [this] {
            proc.edit([](Song& s) {
                if (s.form.text.empty()) s.form.text = "A";
                if (FormLetter* l = addLetter(s)) s.form.text += std::string(" ") + l->name;  // a new letter joins the string
            });
        };
        back_.onClick = [this] { proc.editLetter(0); };
        editA_.onClick = [this] { proc.editLetter(0); };
        songChanged();
    }

    void songChanged() override {
        const Song& d = proc.document();
        if (static_cast<int>(rows_.size()) != static_cast<int>(d.form.letters.size()) || !sameNames(d)) {
            rows_.clear();
            for (const FormLetter& l : d.form.letters) {
                rows_.push_back(std::make_unique<LetterRow>(proc, l.name));
                addAndMakeVisible(*rows_.back());
            }
            resized();
        }
        for (auto& r : rows_) r->refresh(d);
        // §18.3: a group's irama, a row each.
        if (iramas_.size() != d.groups.size()) {
            iramas_.clear();
            for (std::size_t gi = 0; gi < d.groups.size(); ++gi) {
                auto seg = std::make_unique<Segmented>(juce::StringArray{"Off", "1", "2", "4", "8", "16"});
                seg->setTitle("Irama " + juce::String(d.groups[gi].name));
                seg->onChange = [this, gi](int v) {
                    proc.edit([gi, v](Song& s) {
                        if (gi < s.groups.size()) s.groups[gi].irama = v;
                    });
                };
                addAndMakeVisible(*seg);
                iramas_.push_back(std::move(seg));
            }
            resized();
        }
        for (std::size_t gi = 0; gi < iramas_.size(); ++gi) iramas_[gi]->setSelected(d.groups[gi].irama);
        // §18.4: a group's tihai: when it lands, the phrase's length and the gap.
        if (tihais_.size() != d.groups.size()) {
            tihais_.clear();
            for (std::size_t gi = 0; gi < d.groups.size(); ++gi) {
                auto t = std::make_unique<TihaiRow>();
                t->when.setTitle("Tihai " + juce::String(d.groups[gi].name));
                t->when.onChange = [this, gi](int i) {
                    static const int every[] = {0, 1, 2, 4, 1}, unit[] = {0, 0, 0, 0, 1};
                    proc.edit([gi, i](Song& s) {
                        if (gi < s.groups.size()) s.groups[gi].tihai = every[i], s.groups[gi].tihaiUnit = unit[i];
                    });
                };
                t->length.onChange = [this, gi](int v) {
                    proc.edit([gi, v](Song& s) {
                        if (gi < s.groups.size()) s.groups[gi].tihaiLength = v;
                    });
                };
                t->gap.onChange = [this, gi](int v) {
                    proc.edit([gi, v](Song& s) {
                        if (gi < s.groups.size()) s.groups[gi].tihaiGap = v;
                    });
                };
                for (juce::Component* c : std::initializer_list<juce::Component*>{&t->when, &t->length, &t->gap}) addAndMakeVisible(c);
                tihais_.push_back(std::move(t));
            }
            resized();
        }
        for (std::size_t gi = 0; gi < tihais_.size(); ++gi) {
            const Group& g = d.groups[gi];
            tihais_[gi]->when.setSelected(g.tihai == 0 ? 0 : g.tihaiUnit == 1 ? 4 : g.tihai == 1 ? 1 : g.tihai == 2 ? 2 : g.tihai == 4 ? 3 : -1);
            tihais_[gi]->length.setValue(g.tihaiLength);
            tihais_[gi]->gap.setValue(g.tihaiGap);
        }
        on_.setSelected(d.form.on ? 1 : 0);
        end_.setSelected(d.form.end);
        section_.setValue(d.form.section);
        prime_.setValue(d.form.prime);
        gongCycle_.setValue(d.gongCycle);
        kenongs_.setValue(d.kenongs);
        cycle_.set(d, shownBar_);
        add_.setEnabled(d.form.letters.size() < 7);
        const char e = proc.editingLetter();
        back_.setVisible(e != 0);
        editA_.setKind(e == 0 ? Pill::Kind::Primary : Pill::Kind::Neutral);
        std::vector<char> names;
        for (const FormLetter& l : d.form.letters) names.push_back(l.name);
        chips_.set(parseForm(d.form.text), names, playing_);
        repaint();
    }

    void tick(std::int64_t tick, View&) override {
        const Song& s = proc.song();
        const std::int64_t bar = tick >= 0 ? tick / barTicks(s) : 0;
        shownBar_ = tick >= 0 ? (hasForm(s) ? bar - sectionAt(s, bar).start : bar) : -1;  // the gong counts a section's bars
        cycle_.set(proc.document(), shownBar_);
        int playing = -1;
        juce::String now = tick >= 0 ? juce::String() : juce::String("Stopped");
        if (proc.editingLetter()) {
            now = u8("Editing ") + juce::String::charToString(static_cast<juce::juce_wchar>(proc.editingLetter())) +
                  u8(": it plays on its own, round and round");
        } else if (hasForm(s)) {
            const Section sec = sectionAt(s, bar);
            playing = static_cast<int>(sec.position);
            if (tick >= 0)
                now = juce::String(u8(entryName(sec.entry).c_str())) + u8(" · bar ") + juce::String(bar - sec.start + 1) +
                      (sec.bars > 0 ? " of " + juce::String(sec.bars) : juce::String(u8(" · held")));
        } else if (tick >= 0) {
            now = "The song as written";
        }
        if (now != now_) {
            now_ = now;
            repaint(nowArea_);
        }
        if (playing != playing_) {
            playing_ = playing;
            songChanged();
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground);
        for (const auto& r : panels_) panel(g, r);
        heading(g, "Form: the letters, in order", formHead_);
        heading(g, u8("Letters: what each one changes from A"), lettersHead_);
        heading(g, u8("Colotomy: big changes on the gong, small on the kenongs"), gongHead_);
        heading(g, u8("Irama: notes a chord, a new level on the gong"), iramaHead_);
        const Song& d = proc.document();
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        for (std::size_t gi = 0; gi < iramaNames_.size() && gi < d.groups.size(); ++gi)
            g.drawText(juce::String(d.groups[gi].name), iramaNames_[gi], juce::Justification::centredLeft);
        heading(g, u8("Tihai: a phrase three times, landing on the sam"), tihaiHead_);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        for (std::size_t gi = 0; gi < tihaiNames_.size() && gi < d.groups.size(); ++gi)
            g.drawText(juce::String(d.groups[gi].name), tihaiNames_[gi], juce::Justification::centredLeft);
        g.setColour(colour::text);
        g.setFont(mono(20.0f));
        g.drawText(now_, nowArea_, juce::Justification::centredLeft);
        g.setColour(colour::dim);
        g.setFont(sans(13.0f));
        g.drawFittedText(u8("Each letter plays from its own first bar, so it comes back exactly: every A is the same A. "
                            "Tap a letter to change it, double-tap to prime it: A′ is A with a few notes changed from its second bar. "
                            "The matrix and every source start again with each letter."),
                         hint_, juce::Justification::topLeft, 4);
        // A, the song itself.
        auto a = aRow_;
        auto chip = a.removeFromLeft(56).withHeight(56).toFloat();
        g.setColour(colour::raised);
        g.fillRoundedRectangle(chip, 10.0f);
        g.setColour(colour::text);
        g.setFont(mono(26.0f));
        g.drawText("A", chip.toNearestInt(), juce::Justification::centred);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawText("The song as written: every screen, every knob", a.withTrimmedLeft(14).removeFromTop(40), juce::Justification::centredLeft);
        if (proc.editingLetter()) {
            g.setColour(juce::Colour(0xff3a3420));
            g.fillRoundedRectangle(banner_.toFloat(), 12.0f);
            g.setColour(colour::yellow);
            g.setFont(sans(15.0f, true));
            g.drawFittedText(u8("Editing ") + juce::String::charToString(static_cast<juce::juce_wchar>(proc.editingLetter())) +
                                 u8(": every screen and the host's knobs set its values; A keeps its own."),
                             banner_.reduced(16, 0).withTrimmedRight(150), juce::Justification::centredLeft, 2);
        }
    }

    void resized() override {
        panels_.clear();
        auto r = getLocalBounds().reduced(28, 22);
        auto left = r.removeFromLeft(r.getWidth() * 45 / 100);
        r.removeFromLeft(18);
        panels_.push_back(left);
        panels_.push_back(r);
        left = left.reduced(20);
        formHead_ = left.removeFromTop(20);
        left.removeFromTop(12);
        on_.setBounds(left.removeFromTop(48));
        left.removeFromTop(16);
        chips_.setBounds(left.removeFromTop(2 * 64));
        left.removeFromTop(12);
        section_.setBounds(left.removeFromTop(44));
        left.removeFromTop(8);
        end_.setBounds(left.removeFromTop(48));
        left.removeFromTop(8);
        prime_.setBounds(left.removeFromTop(44));
        left.removeFromTop(16);
        nowArea_ = left.removeFromTop(30);
        left.removeFromTop(10);
        hint_ = left.removeFromTop(64);
        left.removeFromTop(14);
        gongHead_ = left.removeFromTop(20);
        left.removeFromTop(8);
        gongCycle_.setBounds(left.removeFromTop(44));
        kenongs_.setBounds(left.removeFromTop(44));
        left.removeFromTop(8);
        cycle_.setBounds(left.removeFromTop(64));

        auto right = r.reduced(20);
        lettersHead_ = right.removeFromTop(20);
        right.removeFromTop(12);
        banner_ = right.removeFromTop(56);  // painted while a letter is being edited
        back_.setBounds(banner_.withLeft(banner_.getRight() - 140).reduced(8));
        right.removeFromTop(12);
        aRow_ = right.removeFromTop(64);
        editA_.setBounds(aRow_.withLeft(aRow_.getRight() - 110).withHeight(40));
        right.removeFromTop(12);
        for (auto& row : rows_) {
            row->setBounds(right.removeFromTop(70));
            right.removeFromTop(12);
        }
        add_.setBounds(right.removeFromTop(48).withWidth(160));
        right.removeFromTop(22);
        iramaHead_ = right.removeFromTop(20);
        right.removeFromTop(8);
        iramaNames_.clear();
        for (auto& seg : iramas_) {
            auto row = right.removeFromTop(44);
            iramaNames_.push_back(row.removeFromLeft(120));
            seg->setBounds(row);
            right.removeFromTop(8);
        }
        right.removeFromTop(14);
        tihaiHead_ = right.removeFromTop(20);
        right.removeFromTop(8);
        tihaiNames_.clear();
        for (auto& t : tihais_) {
            auto row = right.removeFromTop(44);
            tihaiNames_.push_back(row.removeFromLeft(120));
            t->gap.setBounds(row.removeFromRight(130));
            row.removeFromRight(8);
            t->length.setBounds(row.removeFromRight(130));
            row.removeFromRight(8);
            t->when.setBounds(row);
            right.removeFromTop(8);
        }
    }

private:
    bool sameNames(const Song& d) const {
        for (std::size_t i = 0; i < rows_.size() && i < d.form.letters.size(); ++i)
            if (rows_[i]->getTitle() != juce::String::charToString(static_cast<juce::juce_wchar>(d.form.letters[i].name))) return false;
        return true;
    }

    FormChips chips_;
    Segmented on_;
    ValueBar section_;
    Segmented end_;
    ValueBar prime_;
    Pill add_{"+ Letter", Pill::Kind::Dashed}, back_{"Back to A"}, editA_{"Edit A"};
    ValueBar gongCycle_{"Gong cycle", 1, 16, 4, colour::yellow}, kenongs_{"Kenongs", 1, 16, 4, colour::yellow};
    GongCycle cycle_;
    std::vector<std::unique_ptr<Segmented>> iramas_;
    struct TihaiRow {
        Segmented when{juce::StringArray{"Off", "Phrase", "2", "4", "Gong"}};
        ValueBar length{"Phrase", 1, 16, 5, colour::lilac}, gap{"Gap", 0, 8, 1, colour::lilac};
        TihaiRow() {
            length.setLayout(ValueBar::Layout::Inline), gap.setLayout(ValueBar::Layout::Inline);
        }
    };
    std::vector<std::unique_ptr<TihaiRow>> tihais_;
    std::vector<juce::Rectangle<int>> tihaiNames_;
    juce::Rectangle<int> tihaiHead_;
    std::vector<juce::Rectangle<int>> iramaNames_;
    juce::Rectangle<int> iramaHead_;
    std::int64_t shownBar_ = -1;
    std::vector<std::unique_ptr<LetterRow>> rows_;
    std::vector<juce::Rectangle<int>> panels_;
    juce::Rectangle<int> formHead_, lettersHead_, nowArea_, hint_, aRow_, banner_, gongHead_;
    juce::String now_;
    int playing_ = -1;
};

}  // namespace

std::unique_ptr<Page> makeFormPage(HocketProcessor& p) { return std::make_unique<FormPage>(p); }

}  // namespace hocket::ui
