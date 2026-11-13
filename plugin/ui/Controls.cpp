#include "Controls.h"

namespace hocket::ui {

void showParameterMenu(juce::Component& c, juce::AudioProcessorParameter& param, const juce::MouseEvent& e,
                       std::function<void()> backToDefault) {
    if (auto* editor = c.findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* host = editor->getHostContext())
            if (const auto menu = host->getContextMenuForParameter(&param)) {
                menu->showNativeMenu(editor->getLocalPoint(&c, e.getPosition()));
                return;
            }
    juce::PopupMenu m;
    m.addItem("Back to default", std::move(backToDefault));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&c));
}

namespace {

// A screen reader's view of a bar: a value it can read out and step up and down (VoiceOver's swipe up and down).
struct BarValue : juce::AccessibilityValueInterface {
    explicit BarValue(ValueBar& b) : bar(b) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return bar.value(); }
    juce::String getCurrentValueAsString() const override { return bar.valueText(); }
    void setValue(double v) override {
        if (bar.onGestureStart) bar.onGestureStart();
        bar.setValue(static_cast<int>(std::lround(v)), true);
        if (bar.onGestureEnd) bar.onGestureEnd();
    }
    void setValueAsString(const juce::String& s) override { setValue(s.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return {{static_cast<double>(bar.low()), static_cast<double>(bar.high())}, 1.0}; }
    ValueBar& bar;
};

// A row of choices reads as one adjustable value: its option's name, stepped through.
struct ChoiceValue : juce::AccessibilityValueInterface {
    explicit ChoiceValue(Segmented& s) : seg(s) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return seg.selected(); }
    juce::String getCurrentValueAsString() const override { return seg.options()[seg.selected()]; }
    void setValue(double v) override { seg.setSelected(juce::jlimit(0, seg.options().size() - 1, static_cast<int>(std::lround(v))), true); }
    void setValueAsString(const juce::String& s) override {
        const int i = seg.options().indexOf(s, true);
        if (i >= 0) seg.setSelected(i, true);
    }
    AccessibleValueRange getRange() const override { return {{0.0, static_cast<double>(juce::jmax(0, seg.options().size() - 1))}, 1.0}; }
    Segmented& seg;
};

void announce(juce::Component& c) {
    if (auto* h = c.getAccessibilityHandler()) h->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

}  // namespace

// ---- ValueBar --------------------------------------------------------------------------------------------

ValueBar::ValueBar(juce::String label, int lo, int hi, int value, juce::Colour fill)
    : label_(std::move(label)), lo_(lo), hi_(hi), value_(juce::jlimit(lo, hi, value)), default_(value_), fill_(fill) {
    setRepaintsOnMouseActivity(false);
    setTitle(label_);
}

std::unique_ptr<juce::AccessibilityHandler> ValueBar::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                        juce::AccessibilityHandler::Interfaces{std::make_unique<BarValue>(*this)});
}

void ValueBar::setValue(int v, bool notify) {
    v = juce::jlimit(lo_, hi_, v);
    if (v == value_) return;
    value_ = v;
    repaint();
    announce(*this);
    if (notify && onChange) onChange(value_);
}

void ValueBar::setRange(int lo, int hi) {
    lo_ = lo;
    hi_ = juce::jmax(lo, hi);
    value_ = juce::jlimit(lo_, hi_, value_);
    repaint();
}

void ValueBar::setLabel(const juce::String& l) {
    label_ = l;
    setTitle(l);
    repaint();
}

void ValueBar::setInheritable(bool on, bool inherited) {
    inheritable_ = on;
    inherited_ = inherited;
    repaint();
}

juce::Rectangle<int> ValueBar::badgeArea() const {
    if (!inheritable_) return {};
    auto r = getLocalBounds();
    return layout_ == Layout::Inline ? r.removeFromRight(78).withSizeKeepingCentre(72, 28)
                                     : r.removeFromTop(22).removeFromRight(78).withSizeKeepingCentre(72, 20);
}

juce::Rectangle<int> ValueBar::barArea() const {
    auto r = getLocalBounds();
    if (layout_ == Layout::Inline) {
        if (inheritable_) r.removeFromRight(84);
        r.removeFromLeft(juce::jmin(110, r.getWidth() / 3));
        r.removeFromRight(64);
        return r.withSizeKeepingCentre(r.getWidth(), juce::jmin(30, r.getHeight()));
    }
    r.removeFromTop(26);
    return r.withHeight(juce::jmin(34, r.getHeight()));
}

int ValueBar::valueAt(int x) const {
    const auto b = barArea();
    if (hi_ == lo_ || b.getWidth() <= 0) return lo_;
    const double f = juce::jlimit(0.0, 1.0, (x - b.getX()) / static_cast<double>(b.getWidth()));
    return lo_ + static_cast<int>(std::lround(f * (hi_ - lo_)));
}

void ValueBar::set(int v) {
    v = juce::jlimit(lo_, hi_, v);
    if (inheritable_ && inherited_) {
        // Moving an inherited value makes it this level's own.
        inherited_ = false;
        if (onInherit) onInherit(false);
    }
    if (v != value_) {
        value_ = v;
        repaint();
        announce(*this);
        if (onChange) onChange(value_);
    }
}

void ValueBar::paint(juce::Graphics& g) {
    const auto b = barArea();
    const juce::String shown = format ? format(value_) : juce::String(value_);
    if (layout_ == Layout::Inline) {
        auto r = getLocalBounds();
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawText(label_, r.removeFromLeft(juce::jmin(110, r.getWidth() / 3)), juce::Justification::centredLeft);
        g.setColour(colour::text);
        g.setFont(mono(15.0f));
        g.drawText(shown, juce::Rectangle<int>(b.getRight(), r.getY(), 60, r.getHeight()), juce::Justification::centredRight);
    } else {
        auto top = getLocalBounds().removeFromTop(22);
        if (inheritable_) top.removeFromRight(84);
        g.setColour(colour::soft);
        g.setFont(sans(15.0f));
        g.drawText(label_, top, juce::Justification::centredLeft);
        g.setColour(colour::text);
        g.setFont(mono(15.0f));
        g.drawText(shown, top, juce::Justification::centredRight);
    }
    g.setColour(track_);
    g.fillRoundedRectangle(b.toFloat(), 9.0f);
    const float f = hi_ == lo_ ? 1.0f : (value_ - lo_) / static_cast<float>(hi_ - lo_);
    g.setColour(inheritable_ && inherited_ ? juce::Colour(0xff4a4852) : fill_);
    g.fillRoundedRectangle(b.toFloat().withWidth(juce::jmax(12.0f, b.getWidth() * f)), 9.0f);
    if (inheritable_) {
        const auto badge = badgeArea().toFloat();
        g.setColour(inherited_ ? colour::raised : colour::olive);
        g.fillRoundedRectangle(badge, 6.0f);
        g.setColour(inherited_ ? colour::dim : colour::ground);
        g.setFont(sans(12.0f, true));
        g.drawText(inherited_ ? inheritedWord_ : juce::String("set here"), badge, juce::Justification::centred);
    }
}

void ValueBar::mouseDown(const juce::MouseEvent& e) {
    menu_ = e.mods.isPopupMenu();
    if (menu_) {
        if (onMenu) {
            onMenu(e);
        } else {
            juce::PopupMenu m;
            m.addItem("Back to default", [safe = juce::Component::SafePointer<ValueBar>(this)] {
                if (safe) safe->resetToDefault();
            });
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
        }
        return;
    }
    dragStartValue_ = value_;
    dragged_ = false;
    gesture_ = true;
    if (onGestureStart) onGestureStart();
    if (inheritable_ && badgeArea().contains(e.getPosition())) {
        inherited_ = !inherited_;
        repaint();
        if (onInherit) onInherit(inherited_);
        dragged_ = true;  // the tap was the badge's
    }
}

void ValueBar::mouseDrag(const juce::MouseEvent& e) {
    if (menu_) return;
    if (inheritable_ && badgeArea().contains(e.getMouseDownPosition())) return;
    if (std::abs(e.getDistanceFromDragStartX()) < 4 && !dragged_) return;
    dragged_ = true;
    // Relative: a finger anywhere on the bar moves the value by how far it travels.
    const auto b = barArea();
    const double per = b.getWidth() / static_cast<double>(juce::jmax(1, hi_ - lo_));
    set(dragStartValue_ + static_cast<int>(std::lround(e.getDistanceFromDragStartX() / per)));
}

void ValueBar::mouseUp(const juce::MouseEvent& e) {
    if (menu_) return;
    // A tap on the bar sets the value there.
    if (!dragged_ && barArea().expanded(0, 8).contains(e.getPosition())) set(valueAt(e.x));
    gesture_ = false;
    if (onGestureEnd) onGestureEnd();
}

void ValueBar::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (onGestureStart && !gesture_) onGestureStart();
    set(default_);
    if (onGestureEnd && !gesture_) onGestureEnd();
}

// ---- ParamBar --------------------------------------------------------------------------------------------

ParamBar::ParamBar(juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String label, juce::Colour fill)
    : ValueBar(std::move(label), 0, 1, 0, fill), param_(*state.getParameter(id)) {
    const auto range = param_.getNormalisableRange();
    setRange(static_cast<int>(range.start), static_cast<int>(range.end));
    setDefault(static_cast<int>(std::lround(param_.convertFrom0to1(param_.getDefaultValue()))));
    attach_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { setValue(static_cast<int>(std::lround(v))); }, nullptr);
    // A drag is one gesture to the host (one automation move it records, one step of its undo).
    onGestureStart = [this] {
        if (!gesture_) attach_->beginGesture();
        gesture_ = true;
    };
    onGestureEnd = [this] {
        if (gesture_) attach_->endGesture();
        gesture_ = false;
    };
    onChange = [this](int v) {
        if (gesture_) attach_->setValueAsPartOfGesture(static_cast<float>(v));
        else attach_->setValueAsCompleteGesture(static_cast<float>(v));
    };
    onMenu = [this](const juce::MouseEvent& e) { showParameterMenu(*this, param_, e, [this] { resetToDefault(); }); };
    attach_->sendInitialUpdate();
}

// ---- Segmented -------------------------------------------------------------------------------------------

Segmented::Segmented(juce::StringArray options, int selected, int columns)
    : options_(std::move(options)), selected_(selected), columns_(columns) {}

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                        juce::AccessibilityHandler::Interfaces{std::make_unique<ChoiceValue>(*this)});
}

void Segmented::setSelected(int i, bool notify) {
    if (i == selected_) return;
    selected_ = i;
    repaint();
    announce(*this);
    if (notify && onChange) onChange(i);
}

void Segmented::setOptions(juce::StringArray options) {
    options_ = std::move(options);
    repaint();
}

juce::Rectangle<int> Segmented::cell(int i) const {
    const int n = options_.size();
    const int cols = columns_ > 0 ? columns_ : n;
    const int rows = (n + cols - 1) / juce::jmax(1, cols);
    const int gap = 6;
    const int w = (getWidth() - gap * (cols - 1)) / juce::jmax(1, cols);
    const int h = (getHeight() - gap * (rows - 1)) / juce::jmax(1, rows);
    return {(i % cols) * (w + gap), (i / cols) * (h + gap), w, h};
}

void Segmented::paint(juce::Graphics& g) {
    for (int i = 0; i < options_.size(); ++i) {
        const auto r = cell(i).toFloat();
        const bool on = i == selected_;
        g.setColour(on ? colour::text : off_);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(on ? colour::ground : colour::text);
        g.setFont(sans(14.5f, true));
        g.drawFittedText(options_[i], r.toNearestInt().reduced(6, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void Segmented::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onMenu) onMenu(e);
        return;
    }
    for (int i = 0; i < options_.size(); ++i)
        if (cell(i).contains(e.getPosition())) setSelected(i, true);
}

ParamChoice::ParamChoice(juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::StringArray labels, int columns)
    : Segmented(std::move(labels), 0, columns), param_(*state.getParameter(id)) {
    attach_ = std::make_unique<juce::ParameterAttachment>(param_, [this](float v) { setSelected(static_cast<int>(std::lround(v))); }, nullptr);
    onChange = [this](int i) { attach_->setValueAsCompleteGesture(static_cast<float>(i)); };
    onMenu = [this](const juce::MouseEvent& e) {
        showParameterMenu(*this, param_, e, [this] {
            attach_->setValueAsCompleteGesture(param_.convertFrom0to1(param_.getDefaultValue()));
        });
    };
    setTitle(param_.getName(64));
    attach_->sendInitialUpdate();
}

// ---- Pill ------------------------------------------------------------------------------------------------

Pill::Pill(const juce::String& text, Kind kind) : juce::Button(text), kind_(kind) {}

void Pill::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    juce::Colour fill = colour::raised, ink = colour::text, edge = colour::line;
    switch (kind_) {
        case Kind::Primary: fill = accent_.isTransparent() ? colour::pink : accent_, ink = colour::ground, edge = fill; break;
        case Kind::Selected: fill = colour::text, ink = colour::ground, edge = fill; break;
        case Kind::Danger: fill = juce::Colours::transparentBlack, ink = colour::danger, edge = juce::Colour(0xff4a2b33); break;
        case Kind::Dashed: fill = juce::Colours::transparentBlack, ink = colour::soft, edge = juce::Colour(0xff4a4852); break;
        case Kind::Ghost: fill = juce::Colours::transparentBlack, ink = colour::soft, edge = juce::Colours::transparentBlack; break;
        case Kind::Neutral: if (!accent_.isTransparent()) edge = accent_, ink = accent_, fill = juce::Colours::transparentBlack; break;
    }
    if (!isEnabled()) ink = ink.withAlpha(0.35f), edge = edge.withAlpha(0.35f), fill = fill.withAlpha(0.5f);
    if (down) fill = fill.isTransparent() ? colour::raised : fill.darker(0.15f);
    else if (over && fill.isOpaque()) fill = fill.brighter(0.05f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 11.0f);
    if (kind_ == Kind::Dashed) {
        juce::Path p;
        p.addRoundedRectangle(r, 11.0f);
        juce::PathStrokeType stroke(1.2f);
        const float dashes[] = {5.0f, 4.0f};
        juce::Path dashed;
        stroke.createDashedStroke(dashed, p, dashes, 2);
        g.setColour(edge);
        g.fillPath(dashed);
    } else if (!edge.isTransparent()) {
        g.setColour(edge);
        g.drawRoundedRectangle(r, 11.0f, 1.0f);
    }
    g.setColour(ink);
    g.setFont(sans(15.0f, true));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(10, 0), juce::Justification::centred, 1, 0.8f);
}

}  // namespace hocket::ui
