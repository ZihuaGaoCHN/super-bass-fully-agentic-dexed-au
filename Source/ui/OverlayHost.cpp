#include "OverlayHost.h"

#include "WorkbenchTheme.h"

#include <algorithm>
#include <utility>

namespace agentic_dexed::ui
{
OverlayHost::OverlayHost()
{
    setName(juce::String::fromUTF8(u8"工作台弹层 / WORKBENCH OVERLAY"));
    setTitle(getName());
    setAccessible(true);
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
    setVisible(false);
}

OverlayHost::~OverlayHost()
{
    if (content_ != nullptr)
        removeChildComponent(content_.get());
}

void OverlayHost::show(std::unique_ptr<juce::Component> content,
                       juce::String accessibleTitle)
{
    if (content == nullptr)
    {
        close();
        return;
    }

    if (!hasOverlay())
        focusReturnTarget_ = juce::Component::getCurrentlyFocusedComponent();
    if (content_ != nullptr)
        removeChildComponent(content_.get());

    overlayTitle_ = std::move(accessibleTitle);
    content_ = std::move(content);
    content_->setName(overlayTitle_);
    content_->setTitle(overlayTitle_);
    content_->setAccessible(true);
    addAndMakeVisible(*content_);
    setVisible(true);
    resized();
    content_->grabKeyboardFocus();
    repaint();
}

void OverlayHost::close()
{
    if (content_ == nullptr)
        return;
    removeChildComponent(content_.get());
    content_.reset();
    overlayTitle_.clear();
    setVisible(false);
    repaint();
    if (focusReturnTarget_ != nullptr)
        focusReturnTarget_->grabKeyboardFocus();
    focusReturnTarget_ = nullptr;
}

bool OverlayHost::keyPressed(const juce::KeyPress& key)
{
    if (hasOverlay() && key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void OverlayHost::paint(juce::Graphics& graphics)
{
    if (!hasOverlay())
        return;
    graphics.setColour(WorkbenchTheme::ink.withAlpha(0.42f));
    graphics.fillRect(getLocalBounds());
}

void OverlayHost::resized()
{
    if (content_ == nullptr)
        return;
    auto area = getLocalBounds().reduced(24);
    const auto preferredWidth = content_->getWidth() > 0 ? content_->getWidth() : 520;
    const auto preferredHeight = content_->getHeight() > 0 ? content_->getHeight() : 360;
    const auto width = std::min(area.getWidth(), preferredWidth);
    const auto height = std::min(area.getHeight(), preferredHeight);
    content_->setBounds(juce::Rectangle<int>(width, height).withCentre(area.getCentre()));
}
}
