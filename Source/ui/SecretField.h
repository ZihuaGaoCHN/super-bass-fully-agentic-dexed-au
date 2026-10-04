#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace agentic_dexed::ui
{
class SecretField final : public juce::Component
{
public:
    enum class State { empty, newEntry, stored };

    SecretField();
    void setNewSecret(const juce::String& secret);
    void setStoredPlaceholder();
    void clear();
    juce::String newSecret() const;
    juce::String displayText() const { return editor_.getText(); }
    bool copyAllowed() const noexcept { return state_ == State::newEntry; }
    State state() const noexcept { return state_; }
    juce::TextEditor& editor() noexcept { return editor_; }
    void resized() override;

private:
    void setEditorText(
        const juce::String&, bool readOnly, juce::juce_wchar passwordCharacter);

    juce::TextEditor editor_;
    State state_ = State::empty;
    bool internalChange_ = false;
};
}
