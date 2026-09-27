#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Runs every juce::UnitTest registered in this binary. Exit code = number of
// failures (0 = all passed), so CI / ctest can gate on it.
//   MixMindTests                 run everything
//   MixMindTests <category>      run one category (Metering, Shaper, Equalizer, Reference, Processor, AI)
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;   // message manager for timers / async loads

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1) runner.runTestsInCategory (argv[1]);
    else          runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes   += runner.getResult (i)->passes;
    }

    std::printf ("\n%d checks passed, %d failed\n", passes, failures);
    return failures;
}
