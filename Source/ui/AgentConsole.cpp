#include "AgentConsole.h"

#include "WorkbenchTheme.h"
#include "../state/ParameterRegistry.h"

namespace agentic_dexed::ui
{
namespace
{
juce::String readableProse(const std::string& source)
{
    static const auto registry = ParameterRegistry::createDexed();
    juce::String result;
    bool fenced = false;
    for (auto line : juce::StringArray::fromLines(juce::String::fromUTF8(source.c_str())))
    {
        const auto trimmed = line.trimStart();
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~"))
        {
            fenced = !fenced;
            continue;
        }
        if (fenced || trimmed.startsWith("{") || trimmed.startsWith("[{")
            || trimmed.startsWith("TOOL ") || trimmed.startsWith("DATA "))
            continue;
        while (line.startsWithChar('#'))
            line = line.substring(1).trimStart();
        line = line.replace("**", "").replace("`", "");
        for (const auto& parameter : registry.all())
        {
            const auto id = juce::String::fromUTF8(parameter.id.c_str());
            if (line.contains(id))
                line = line.replace(id, WorkbenchTheme::parameterLabel(
                    juce::String::fromUTF8(parameter.displayName.c_str())));
        }
        result += line + "\n";
    }
    return result.trim();
}
}

AgentConsole::AgentConsole()
{
    const auto localizedFont = WorkbenchTheme::bodyFont(15.0f);
    transcript_.setName("Agent transcript");
    transcript_.setTitle("Agent conversation");
    transcript_.setMultiLine(true);
    transcript_.setReadOnly(true);
    transcript_.setScrollbarsShown(true);
    transcript_.setFont(localizedFont);
    transcript_.setColour(juce::TextEditor::backgroundColourId, WorkbenchTheme::ink);
    transcript_.setColour(juce::TextEditor::textColourId, WorkbenchTheme::paper);
    transcript_.setColour(juce::TextEditor::outlineColourId, WorkbenchTheme::accentBlue);
    addAndMakeVisible(transcript_);

    prompt_.setName("Sound request");
    prompt_.setTitle("Describe a sound or a change to the current patch");
    prompt_.setMultiLine(true);
    prompt_.setReturnKeyStartsNewLine(true);
    prompt_.setFont(localizedFont);
    prompt_.setTextToShowWhenEmpty(
        juce::String::fromUTF8(u8"描述想要的声音… 回车发送，Shift+回车换行"),
        WorkbenchTheme::inkSoft);
    prompt_.setColour(juce::TextEditor::backgroundColourId, WorkbenchTheme::paper);
    prompt_.setColour(juce::TextEditor::textColourId, WorkbenchTheme::ink);
    prompt_.setColour(juce::TextEditor::outlineColourId, WorkbenchTheme::accentBlue);
    prompt_.addKeyListener(this);
    addAndMakeVisible(prompt_);
}

AgentConsole::~AgentConsole()
{
    prompt_.removeKeyListener(this);
}

bool AgentConsole::keyPressed(const juce::KeyPress& key, juce::Component* source)
{
    return source == &prompt_ && handlePromptKey(key);
}

bool AgentConsole::handlePromptKey(const juce::KeyPress& key)
{
    const auto isSubmit = key.getKeyCode() == juce::KeyPress::returnKey
        && !key.getModifiers().isShiftDown() && !key.getModifiers().isAltDown();
    if (!isSubmit || prompt_.composing)
        return false;
    if (submitHandler_)
        submitHandler_();
    return true;
}

juce::String AgentConsole::transcriptText(
    const agent::session::AgentSessionSnapshot& snapshot)
{
    juce::String result;
    for (const auto& entry : snapshot.transcript)
    {
        if (entry.kind == agent::session::AgentTranscriptKind::user)
            result += juce::String::fromUTF8(u8"你：")
                + juce::String::fromUTF8(entry.text.c_str()) + "\n\n";
        else if (entry.kind == agent::session::AgentTranscriptKind::assistant)
        {
            const auto prose = readableProse(entry.text);
            if (prose.isNotEmpty())
                result += juce::String::fromUTF8(u8"助手：") + prose + "\n\n";
        }
    }
    if (!snapshot.streamingText.empty())
        result += readableProse(snapshot.streamingText);
    else if (!snapshot.finalText.empty()
             && (snapshot.transcript.empty()
                 || snapshot.transcript.back().text != snapshot.finalText))
        result += readableProse(snapshot.finalText);
    return result;
}

void AgentConsole::setSnapshot(const agent::session::AgentSessionSnapshot& snapshot)
{
    const auto text = transcriptText(snapshot);
    if (transcript_.getText() != text)
    {
        transcript_.setText(text, false);
        transcript_.moveCaretToEnd();
    }
}

void AgentConsole::resized()
{
    auto area = getLocalBounds();
    prompt_.setBounds(area.removeFromBottom(76).reduced(0, 4));
    transcript_.setBounds(area);
}
}
