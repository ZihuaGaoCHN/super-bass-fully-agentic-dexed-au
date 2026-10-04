#include <JuceHeader.h>

#include <iostream>

#if JUCE_MAC
namespace juce
{
void initialiseNSApplication();
}
#endif

namespace
{
class ConsoleTestRunner final : public juce::UnitTestRunner
{
protected:
    void logMessage(const juce::String& message) override
    {
        std::cout << message << '\n';
    }
};

bool matchesFilter(const juce::UnitTest& test, const juce::String& filter)
{
    // Paid network tests run only when explicitly selected, never in ordinary CTest.
    if (test.getCategory() == "LiveAgent" && filter != "LiveAgent")
        return false;
    return filter.isEmpty()
        || test.getName().containsIgnoreCase(filter)
        || test.getCategory().containsIgnoreCase(filter);
}
} // namespace

int main(int argc, char** argv)
{
   #if JUCE_MAC
    juce::initialiseNSApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String filter;
    if (argc != 1)
    {
        if (argc != 3 || juce::String(argv[1]) != "--filter")
        {
            std::cerr << "usage: AgenticDexedTests [--filter substring]\n";
            return 2;
        }

        filter = juce::String::fromUTF8(argv[2]).trim();
        if (filter.isEmpty())
        {
            std::cerr << "--filter requires a non-empty substring\n";
            return 2;
        }
    }

    juce::Array<juce::UnitTest*> selected;
    for (auto* test : juce::UnitTest::getAllTests())
        if (test != nullptr && matchesFilter(*test, filter))
            selected.add(test);

    if (selected.isEmpty())
    {
        std::cerr << "No tests matched filter '" << filter << "'\n";
        return 2;
    }

    ConsoleTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTests(selected);

    int passes = 0;
    int failures = 0;
    for (int index = 0; index < runner.getNumResults(); ++index)
    {
        if (const auto* result = runner.getResult(index))
        {
            passes += result->passes;
            failures += result->failures;
        }
    }

    std::cout << "SUMMARY: " << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
