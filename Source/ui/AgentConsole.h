#pragma once

#include "../agent/session/AgentSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace agentic_dexed::ui
{
class AgentConsole final : public juce::Component, private juce::KeyListener
{
public:
    AgentConsole();
    ~AgentConsole() override;

    void setSnapshot(const agent::session::AgentSessionSnapshot& snapshot);
    juce::TextEditor& promptEditor() noexcept { return prompt_; }
    const juce::TextEditor& promptEditor() const noexcept { return prompt_; }
    juce::String displayText() const { return transcript_.getText(); }
    void setSubmitHandler(std::function<void()> handler)
    {
        submitHandler_ = std::move(handler);
    }
    void setCompositionInProgressForTest(bool composing) noexcept
    {
        prompt_.setTemporaryUnderlining(composing
            ? juce::Array<juce::Range<int>> { juce::Range<int>(0, 1) }
            : juce::Array<juce::Range<int>> {});
    }
    bool handlePromptKeyForTest(const juce::KeyPress& key)
    {
        return handlePromptKey(key);
    }
    void resized() override;

private:
    class PromptEditor final : public juce::TextEditor
    {
    public:
        void setTemporaryUnderlining(const juce::Array<juce::Range<int>>& ranges) override
        {
            composing = !ranges.isEmpty();
            juce::TextEditor::setTemporaryUnderlining(ranges);
        }
        bool composing = false;
    };
    static juce::String transcriptText(
        const agent::session::AgentSessionSnapshot& snapshot);
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool handlePromptKey(const juce::KeyPress&);

    juce::TextEditor transcript_;
    PromptEditor prompt_;
    std::function<void()> submitHandler_;
};
}
