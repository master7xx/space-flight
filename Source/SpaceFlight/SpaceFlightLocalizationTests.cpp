#include "SpaceFlightLocalization.h"
#include "SpaceFlightLogic.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightLocalizationTest, "SpaceFlight.Localization.SwitchAndFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightLocalizationTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    SetLanguage(ELanguage::Russian);
    TestEqual(TEXT("Russian remains the default text"), FString(Localize(TEXT("РУДА: %lld"))), FString(TEXT("РУДА: %lld")));
    SetLanguage(ELanguage::English);
    TestEqual(TEXT("English HUD"), FString(Localize(TEXT("РУДА: %lld"))), FString(TEXT("ORE: %lld")));
    TestEqual(TEXT("English talent"), FString(Localize(TEXT("ЧИСТЫЙ СРЕЗ"))), FString(TEXT("CLEAN CUT")));
    TestEqual(TEXT("English tree data updates"), FString(Upgrade(71).Name), FString(TEXT("CLEAN CUT")));
    SetLanguage(ELanguage::Spanish);
    TestEqual(TEXT("Spanish HUD"), FString(Localize(TEXT("РУДА: %lld"))), FString(TEXT("MINERAL: %lld")));
    TestEqual(TEXT("Spanish talent"), FString(Localize(TEXT("ЧИСТЫЙ СРЕЗ"))), FString(TEXT("CORTE LIMPIO")));
    TestEqual(TEXT("Spanish tree data updates"), FString(Upgrade(71).Name), FString(TEXT("CORTE LIMPIO")));
    TestEqual(TEXT("translated formatting preserves arguments"), SF_LOCAL_PRINTF(TEXT("РУДА: %lld"), TEXT("ORE: %lld"), TEXT("MINERAL: %lld"), static_cast<int64>(42)), FString(TEXT("MINERAL: 42")));
    TestEqual(TEXT("unknown keys fall back unchanged"), FString(Localize(TEXT("UNKNOWN"))), FString(TEXT("UNKNOWN")));
    TestEqual(TEXT("cycle returns to Russian"), static_cast<int32>(NextLanguage(ELanguage::Spanish)), static_cast<int32>(ELanguage::Russian));
    SetLanguage(ELanguage::Russian);
    return true;
}
#endif
