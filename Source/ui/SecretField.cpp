#include "SecretField.h"

#include "WorkbenchTheme.h"

namespace agentic_dexed::ui
{
SecretField::SecretField()
{
    editor_.setName("API key");
    editor_.setTitle("Enter a replacement API key; saved keys are never displayed");
    editor_.setTextToShowWhenEmpty(
        juce::String::fromUTF8("粘贴新的 API Key / Paste a new API key"),
        WorkbenchTheme::inkSoft);
    editor_.setPasswordCharacter(0x2022);
    editor_.setColour(juce::TextEditor::backgroundColourId, WorkbenchTheme::paper);
    editor_.setColour(juce::TextEditor::textColourId, WorkbenchTheme::ink);
    editor_.setColour(juce::TextEditor::outlineColourId, WorkbenchTheme::accentBlue);
    editor_.setFont(WorkbenchTheme::bodyFont());
    editor_.onTextChange = [this]
    {
        if (!internalChange_)
            state_ = editor_.getText().isEmpty() ? State::empty : State::newEntry;
    };
    addAndMakeVisible(editor_);
}

void SecretField::setEditorText(
    const juce::String& text, bool readOnly, juce::juce_wchar passwordCharacter)
{
    const juce::ScopedValueSetter<bool> guard(internalChange_, true);
    editor_.setReadOnly(readOnly);
    editor_.setPopupMenuEnabled(!readOnly);
    editor_.setPasswordCharacter(passwordCharacter);
    editor_.setText(text, false);
}

void SecretField::setNewSecret(const juce::String& secret)
{
    state_ = secret.isEmpty() ? State::empty : State::newEntry;
    setEditorText(secret, false, 0x2022);
}

void SecretField::setStoredPlaceholder()
{
    state_ = State::stored;
    setEditorText(juce::String::fromUTF8(u8"••••••••••••"), true, 0);
}

void SecretField::clear()
{
    state_ = State::empty;
    setEditorText({}, false, 0x2022);
}

juce::String SecretField::newSecret() const
{
    return state_ == State::stored ? juce::String() : editor_.getText();
}

void SecretField::resized() { editor_.setBounds(getLocalBounds()); }
}
