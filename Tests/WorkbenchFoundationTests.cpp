#include <JuceHeader.h>

#include "ui/OverlayHost.h"
#include "ui/WorkbenchControls.h"
#include "ui/WorkbenchLookAndFeel.h"
#include "ui/WorkbenchTheme.h"

namespace
{
using namespace agentic_dexed::ui;

class FocusTarget final : public juce::TextButton
{
public:
    FocusTarget() : juce::TextButton("触发 / TRIGGER")
    {
        setWantsKeyboardFocus(true);
    }
};

class WorkbenchFoundationTests final : public juce::UnitTest
{
public:
    WorkbenchFoundationTests()
        : juce::UnitTest("Workbench visual foundation", "Workbench")
    {
    }

    void runTest() override
    {
        beginTest("approved palette and text pairs meet AA");
        expect(WorkbenchTheme::paper == juce::Colour(0xfff7f4ed));
        expect(WorkbenchTheme::paperRaised == juce::Colour(0xffe9e6df));
        expect(WorkbenchTheme::ink == juce::Colour(0xff1c1b16));
        expect(WorkbenchTheme::inkSoft == juce::Colour(0xff484842));
        expect(WorkbenchTheme::accentBlue == juce::Colour(0xff444a73));
        expect(WorkbenchTheme::focusBlue == juce::Colour(0xffa7adc4));
        expect(WorkbenchTheme::success == juce::Colour(0xff485b4c));
        expect(WorkbenchTheme::warning == juce::Colour(0xffa46a31));
        expect(WorkbenchTheme::error == juce::Colour(0xff9b3d38));
        expect(WorkbenchTheme::contrastRatio(WorkbenchTheme::ink,
                                             WorkbenchTheme::paper) >= 4.5);
        expect(WorkbenchTheme::contrastRatio(WorkbenchTheme::inkSoft,
                                             WorkbenchTheme::paper) >= 4.5);
        expect(WorkbenchTheme::contrastRatio(WorkbenchTheme::paper,
                                             WorkbenchTheme::accentBlue) >= 4.5);
        expectEquals(WorkbenchTheme::borderThickness, 1);
        expectEquals(WorkbenchTheme::maximumCornerRadius, 2);
        expectEquals(WorkbenchTheme::spacingUnit, 4);

        beginTest("controls expose every visual and accessible state");
        WorkbenchKnob knob("截止频率 / CUTOFF");
        WorkbenchToggle toggle("启用 / ENABLED");
        WorkbenchButton button("保存 / SAVE");
        WorkbenchSegmentedControl segments("模式 / MODE",
                                           { "比率 / RATIO", "固定 / FIXED" });
        WorkbenchPanel panel("滤波器 / FILTER");
        WorkbenchDataScreen screen("响应 / RESPONSE");
        WorkbenchLabel label("声音 / SOUND");

        for (auto* component : std::array<juce::Component*, 7> {
                 &knob, &toggle, &button, &segments, &panel, &screen, &label })
        {
            expect(component->getName().isNotEmpty());
            expect(component->getTitle().isNotEmpty());
            expect(component->isAccessible());
        }
        expect(knob.getWantsKeyboardFocus());
        expect(toggle.getWantsKeyboardFocus());
        expect(button.getWantsKeyboardFocus());
        expect(segments.getWantsKeyboardFocus());

        for (const auto state : { WorkbenchState::normal,
                                  WorkbenchState::active,
                                  WorkbenchState::success,
                                  WorkbenchState::warning,
                                  WorkbenchState::error,
                                  WorkbenchState::disabled })
        {
            knob.setWorkbenchState(state);
            expect(knob.workbenchState() == state);
        }
        expect(!knob.isEnabled());
        knob.setWorkbenchState(WorkbenchState::normal);
        expect(knob.isEnabled());

        expectEquals(segments.itemCount(), 2);
        segments.setSelectedIndex(1, juce::dontSendNotification);
        expectEquals(segments.selectedIndex(), 1);
        expect(segments.button(1).getToggleState());
        expect(!segments.button(0).getToggleState());

        beginTest("overlay owns only one modal surface");
        juce::Component root;
        FocusTarget trigger;
        OverlayHost overlays;
        root.addAndMakeVisible(trigger);
        root.addAndMakeVisible(overlays);
        root.setBounds(0, 0, 640, 400);
        trigger.setBounds(8, 8, 120, 28);
        overlays.setBounds(root.getLocalBounds());
        trigger.grabKeyboardFocus();

        const auto firstTitle = juce::String::fromUTF8("第一个弹层 / FIRST OVERLAY");
        overlays.show(std::make_unique<juce::TextButton>("FIRST"), firstTitle);
        expect(overlays.hasOverlay());
        expectEquals(overlays.getNumChildComponents(), 1);
        expectEquals(overlays.overlayTitle(),
                     juce::String::fromUTF8("第一个弹层 / FIRST OVERLAY"));

        const auto secondTitle = juce::String::fromUTF8("第二个弹层 / SECOND OVERLAY");
        overlays.show(std::make_unique<juce::TextButton>("SECOND"), secondTitle);
        expect(overlays.hasOverlay());
        expectEquals(overlays.getNumChildComponents(), 1);
        expectEquals(overlays.overlayTitle(),
                     juce::String::fromUTF8("第二个弹层 / SECOND OVERLAY"));
        expect(overlays.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        expect(!overlays.hasOverlay());
        expectEquals(overlays.getNumChildComponents(), 0);

        beginTest("CJK font selection never chooses the Latin pixel face");
        const auto body = WorkbenchTheme::bodyFont();
        const auto labelFont = WorkbenchTheme::labelFont();
        const auto value = WorkbenchTheme::valueFont();
        for (const auto& font : { body, labelFont, value })
        {
            expect(font.getHeight() >= 11.0f);
            expect(!font.getTypefaceName().containsIgnoreCase("pixel"));
            expect(!font.getTypefaceName().containsIgnoreCase("silkscreen"));
        }

        beginTest("scale presets and popup menu metrics are deterministic");
        for (const auto scale : { 100, 125, 150, 200 })
            expect(WorkbenchTheme::isScalePreset(scale));
        expect(!WorkbenchTheme::isScalePreset(175));
        expectEquals(WorkbenchTheme::scaledSpacing(100), 4);
        expectEquals(WorkbenchTheme::scaledSpacing(125), 5);
        expectEquals(WorkbenchTheme::scaledSpacing(150), 6);
        expectEquals(WorkbenchTheme::scaledSpacing(200), 8);

        WorkbenchLookAndFeel lookAndFeel;
        expect(lookAndFeel.findColour(juce::PopupMenu::backgroundColourId)
               == WorkbenchTheme::paper);
        expect(lookAndFeel.findColour(juce::PopupMenu::textColourId)
               == WorkbenchTheme::ink);
        expect(lookAndFeel.findColour(juce::PopupMenu::highlightedBackgroundColourId)
               == WorkbenchTheme::accentBlue);
        expect(lookAndFeel.findColour(juce::PopupMenu::highlightedTextColourId)
               == WorkbenchTheme::paper);
        expectEquals(lookAndFeel.getPopupMenuBorderSize(), 1);
    }
};

WorkbenchFoundationTests workbenchFoundationTests;
}
