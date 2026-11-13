#include "ClipButton.h"

#include "../Clip.h"
#include "Theme.h"

namespace hocket::ui {

juce::PopupMenu legacyClipMenu(const juce::File& documents, const juce::File& applicationData,
                               const std::function<void(const juce::File&)>& browse) {
    juce::PopupMenu menu;
    for (const auto& base : {documents, applicationData}) {
        const auto old = base.getChildFile("Hocket").getChildFile(base == documents ? "Clips" : "Outgoing");
        if (old.isDirectory())
            menu.addItem(base == documents ? "Browse older clips..." : "Browse older outgoing clips...", [old, browse] { browse(old); });
    }
    return menu;
}

ClipButton::ClipButton(HocketProcessor& p) : proc_(p) {
    setTitle("MIDI clip");
    setDescription("Drag onto a track for the next bars as a MIDI clip; tap for options");
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

ClipButton::~ClipButton() = default;

void ClipButton::setBars(int b) {
    bars_ = juce::jlimit(1, 64, b);
    repaint();
}

std::int64_t ClipButton::firstBar() const {
    const std::int64_t tick = proc_.playheadTick();
    if (fromTop_ || tick < 0) return 0;
    return tick / barTicks(proc_.song());
}

juce::File ClipButton::write(const juce::File& dir) const {
    return clip::write(proc_.song(), firstBar(), bars_, proc_.hostBpm(), dir);
}

void ClipButton::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(dragging_ ? colour::line : colour::raised);
    g.fillRoundedRectangle(r, 11.0f);
    g.setColour(colour::line);
    g.drawRoundedRectangle(r, 11.0f, 1.0f);
    // A grip: three short bars, the clip you're picking up.
    auto b = getLocalBounds().reduced(12, 0);
    const auto grip = b.removeFromLeft(14).withSizeKeepingCentre(14, 14).toFloat();
    g.setColour(colour::blue);
    for (int i = 0; i < 3; ++i) g.fillRoundedRectangle(grip.getX(), grip.getY() + i * 5.5f, i == 1 ? 9.0f : 14.0f, 3.0f, 1.5f);
    b.removeFromLeft(8);
    g.setColour(colour::text);
    g.setFont(sans(15.0f, true));
    g.drawFittedText("Drag " + juce::String(bars_) + " bars", b, juce::Justification::centredLeft, 1, 0.8f);
}

void ClipButton::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    menu_ = e.mods.isPopupMenu();
    if (menu_) showMenu();
}

void ClipButton::mouseDrag(const juce::MouseEvent& e) {
    if (menu_ || dragging_ || e.getDistanceFromDragStart() < 6) return;
    dragging_ = true;
    repaint();
    const juce::File f = write(clip::folder());
    if (f == juce::File() ||
        !juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this, [safe = SafePointer<ClipButton>(this)] {
            if (safe) safe->dragging_ = false, safe->repaint();
        })) {
        dragging_ = false;  // this platform can't drag a file out (the iPad): the menu's Share does it there
        repaint();
    }
}

void ClipButton::mouseUp(const juce::MouseEvent&) {
    if (!menu_ && !dragging_) showMenu();
}

void ClipButton::showMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Length");
    for (int b : {4, 8, 16, 32}) m.addItem(juce::String(b) + " bars", true, bars_ == b, [this, b] { setBars(b); });
    m.addSectionHeader("Starts");
    m.addItem("On the bar playing now", true, !fromTop_, [this] { fromTop_ = false; });
    m.addItem("On bar 1", true, fromTop_, [this] { fromTop_ = true; });
    m.addSeparator();
#if JUCE_CONTENT_SHARING
    m.addItem(u8("Share…"), [this] { share(); });
#endif
    m.addItem(u8("Save as a file…"), [this] { saveAs(); });
#if !(JUCE_IOS || JUCE_ANDROID)
    m.addItem("Show the clips folder", [] {
        const juce::File dir = clip::folder();
        dir.createDirectory();
        dir.revealToUser();
    });
#endif
    // Available on mobile too: a picker and Share there, reveal in the file manager on desktop.
    auto older = legacyClipMenu(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                               juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory),
                               [safe = SafePointer<ClipButton>(this)](const juce::File& dir) { if (safe) safe->browseLegacy(dir); });
    if (older.getNumItems() > 0) m.addSubMenu("Older clips", older);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void ClipButton::browseLegacy(const juce::File& dir) {
#if JUCE_CONTENT_SHARING
    chooser_ = std::make_unique<juce::FileChooser>("Share an older clip", dir, "*.mid");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = SafePointer<ClipButton>(this)](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (safe && file.existsAsFile())
                                  safe->sharer_ = juce::ContentSharer::shareFilesScoped({juce::URL(file)}, [](bool, const juce::String&) {}, safe.getComponent());
                          });
#else
    dir.revealToUser();
#endif
}

void ClipButton::saveAs() {
    const juce::File made = write(clip::stagingFolder());
    if (made == juce::File()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Save the clip", clip::folder().getChildFile(made.getFileName()), "*.mid");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [made](const juce::FileChooser& fc) {
                              const juce::File to = fc.getResult();
                              if (to != juce::File()) made.copyFileTo(to.withFileExtension("mid"));
                              made.deleteFile();
                          });
}

void ClipButton::share() {
#if JUCE_CONTENT_SHARING
    const juce::File made = write(clip::stagingFolder());
    if (made == juce::File()) return;
    sharer_ = juce::ContentSharer::shareFilesScoped({juce::URL(made)}, [](bool, const juce::String&) {}, this);
#endif
}

}  // namespace hocket::ui
