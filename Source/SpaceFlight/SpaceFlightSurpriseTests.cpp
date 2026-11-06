#include "SpaceFlightSurprise.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSurpriseEligibilityTest,
    "SpaceFlight.Progress.Surprise.Eligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSurpriseEligibilityTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FSurpriseProgress Surprise;
    TestFalse(TEXT("seven objectives are insufficient"), CanPresentSurprise(Surprise, 1800.0f, 7, false));
    TestFalse(TEXT("time below the threshold is insufficient"), CanPresentSurprise(Surprise, 1799.0f, 8, false));
    TestTrue(TEXT("both thresholds permit presentation"), CanPresentSurprise(Surprise, 1800.0f, 8, false));
    TestFalse(TEXT("a higher-priority activity postpones presentation"), CanPresentSurprise(Surprise, 1900.0f, 8, true));
    TestTrue(TEXT("presentation is available when the blocking activity ends"), CanPresentSurprise(Surprise, 1900.0f, 8, false));
    Surprise.NotBeforeSeconds = 2300.0f;
    TestFalse(TEXT("migration delay has not elapsed"), CanPresentSurprise(Surprise, 2299.0f, 8, false));
    TestTrue(TEXT("migration delay elapses at its boundary"), CanPresentSurprise(Surprise, 2300.0f, 8, false));
    Surprise.bDelivered = true;
    TestFalse(TEXT("delivered event does not trigger again"), CanPresentSurprise(Surprise, 2400.0f, 8, false));
    TestFalse(TEXT("invalid time cannot trigger presentation"), CanPresentSurprise(FSurpriseProgress(), -1.0f, 8, false));
    return true;
}
#endif
