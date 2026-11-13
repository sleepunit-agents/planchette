#include "Editor.h"

using namespace hocket::ui;

HocketEditor::HocketEditor(HocketProcessor& p)
    : AudioProcessorEditor(p), proc_(p), look_(std::make_unique<juce::LookAndFeel_V4>()), clip_(p), gestureEnd_(p) {
    look_->setColour(juce::ResizableWindow::backgroundColourId, colour::ground);
    look_->setColour(juce::Label::textColourId, colour::text);
    look_->setColour(juce::ComboBox::backgroundColourId, colour::raised);
    look_->setColour(juce::ComboBox::outlineColourId, colour::line);
    look_->setColour(juce::ComboBox::textColourId, colour::text);
    look_->setColour(juce::ComboBox::arrowColourId, colour::soft);
    look_->setColour(juce::PopupMenu::backgroundColourId, colour::panel);
    look_->setColour(juce::PopupMenu::textColourId, colour::text);
    look_->setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::raised);
    look_->setColour(juce::TextEditor::backgroundColourId, colour::raised);
    look_->setColour(juce::TextEditor::textColourId, colour::text);
    look_->setColour(juce::TextEditor::outlineColourId, colour::line);
    setLookAndFeel(look_.get());

    pages_[0] = makePlayPage(p);
    pages_[1] = makeGroupsPage(p);
    pages_[2] = makeMatrixPage(p);
    pages_[3] = makeSongPage(p);
    pages_[4] = makeFormPage(p);
    for (auto& page : pages_) addChildComponent(*page);
    addAndMakeVisible(tabs_);
    addAndMakeVisible(generate_);
    addAndMakeVisible(undo_);
    addAndMakeVisible(redo_);
    addAndMakeVisible(clip_);
    tabs_.setTitle("Screens");
    tabs_.onChange = [this](int i) { showPage(i); };
    generate_.onClick = [this] { proc_.generate(); };
    undo_.onClick = [this] { proc_.undoManager().undo(); };
    redo_.onClick = [this] { proc_.undoManager().redo(); };
    undo_.setTooltip(u8("Undo (⌘Z)"));
    redo_.setTooltip(u8("Redo (⇧⌘Z)"));
    addMouseListener(&gestureEnd_, true);
    setWantsKeyboardFocus(true);
    showPage(0);

    // Sized like the app it is: an iPad's full screen (1366 × 1024 points on a 12.9" iPad Pro) by default, and
    // anything from a small laptop window up.
    setResizable(true, true);
    setResizeLimits(1024, 720, 2560, 1600);
    setSize(1366, 1024);
    startTimerHz(24);
}

HocketEditor::~HocketEditor() {
    removeMouseListener(&gestureEnd_);
    setLookAndFeel(nullptr);
}

bool HocketEditor::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown()) return false;
    auto& undo = proc_.undoManager();
    if (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z') return mods.isShiftDown() ? undo.redo() : undo.undo();
    if (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y') return undo.redo();
    return false;
}

void HocketEditor::showPage(int i) {
    current_ = juce::jlimit(0, 4, i);
    tabs_.setSelected(current_);
    for (int k = 0; k < 5; ++k) pages_[k]->setVisible(k == current_);
    shownStep_ = -2;
}

void HocketEditor::timerCallback() {
    undo_.setEnabled(proc_.undoManager().canUndo());
    redo_.setEnabled(proc_.undoManager().canRedo());
    if (proc_.songVersion() != viewVersion_ || proc_.frozenFrom() != viewHold_) {
        viewVersion_ = proc_.songVersion();
        viewHold_ = proc_.frozenFrom();  // §9.2: under Freeze the screens show what's held
        view_ = std::make_unique<hocket::View>(proc_.song(), viewHold_);
        for (auto& page : pages_) page->songChanged();
        shownStep_ = -2;
        repaint(header_);
    }
    // The showing screen follows the host a step at a time (60 ticks: a 1/64 at most).
    const std::int64_t tick = proc_.playheadTick();
    const std::int64_t step = tick >= 0 ? tick / 60 : -1;
    if (step == shownStep_) return;
    shownStep_ = step;
    pages_[current_]->tick(tick, *view_);
}

void HocketEditor::paint(juce::Graphics& g) {
    g.fillAll(colour::ground);
    auto h = header_.reduced(28, 0);
    g.setColour(colour::text);
    g.setFont(sans(26.0f, true));
    g.drawText("Planchette", h.removeFromLeft(148), juce::Justification::centredLeft);
    g.setFont(sans(10.0f));
    g.drawText("Copyright (C) 2026 Jonathan Barket | AGPL-3.0-or-later | No warranty | See LICENSE",
               header_.withTop(header_.getBottom() - 14).reduced(28, 0), juce::Justification::centredLeft);
    // The song: its name (Untitled until it's saved), and a dot while it has changes since.
    g.setColour(colour::soft);
    g.setFont(sans(16.0f, true));
    const juce::String name = proc_.songName().isNotEmpty() ? proc_.songName() : juce::String("Untitled");
    // §18.1: while a letter is being edited, every screen says so.
    const char letter = proc_.editingLetter();
    const juce::String editing = letter ? u8(" · editing ") + juce::String::charToString(static_cast<juce::juce_wchar>(letter)) : juce::String();
    g.drawFittedText(name + (proc_.edited() && proc_.songName().isNotEmpty() ? u8(" •") : juce::String()) + editing, seedArea_,
                     juce::Justification::centredRight, 1, 0.8f);
    g.setColour(colour::line);
    g.drawHorizontalLine(header_.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
}

void HocketEditor::resized() {
    auto r = getLocalBounds();
    header_ = r.removeFromTop(76);
    auto h = header_.reduced(28, 14);
    h.removeFromLeft(176);
    tabs_.setBounds(h.removeFromLeft(480));
    generate_.setBounds(h.removeFromRight(130));
    h.removeFromRight(10);
    clip_.setBounds(h.removeFromRight(124));
    h.removeFromRight(10);
    redo_.setBounds(h.removeFromRight(62));
    h.removeFromRight(6);
    undo_.setBounds(h.removeFromRight(62));
    h.removeFromRight(10);
    seedArea_ = h;
    for (auto& page : pages_) page->setBounds(r);
}
