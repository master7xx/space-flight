#include "SpaceFlightSave.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightResourceLimitTest, "SpaceFlight.Progress.ResourceLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightResourceLimitTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    P.SetRefiningMode(1);
    P.AddResources(ResourceAccountingLimit - 2);
    TestEqual(TEXT("partial collection reports accepted amount"), P.AddOre(10, EOreKind::Blue), int64(2));
    TestEqual(TEXT("lifetime collection remains in range"), P.TotalCollected, ResourceAccountingLimit);
    TestEqual(TEXT("total resources remain consistent"), P.Resources, ResourceAccountingLimit);
    TestEqual(TEXT("only the accepted colored ore is recorded"), P.BlueOre, int64(2));
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 1, 20, true, 1);
    FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("saturated resource snapshot stays valid"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestTrue(TEXT("an upgrade remains purchasable at the limit"), P.Purchase(0));
    TestEqual(TEXT("unaccepted collection reports zero"), P.AddOre(5, EOreKind::Black), int64(0));
    TestEqual(TEXT("lifetime limit remains explicit after spending"), P.TotalCollected, ResourceAccountingLimit);
    TestEqual(TEXT("unaccepted colored ore is not counted"), P.BlackOre, int64(0));
    Save->Capture(P, 1, 20, true, 2);
    TestTrue(TEXT("purchase at resource limit preserves accounting"), Save->Restore(Restored, Time, Brightness, bAuto));
    FProgress Wide;
    Wide.SetRefiningMode(1);
    const int64 LargeAmount = static_cast<int64>(MAX_int32) + 321;
    TestEqual(TEXT("single ore grant can exceed the former 32-bit limit"), Wide.AddOre(LargeAmount, EOreKind::Blue), LargeAmount);
    TestEqual(TEXT("wide colored ore remains exact"), Wide.BlueOre, LargeAmount);
    TestEqual(TEXT("wide resources remain exact"), Wide.Resources, LargeAmount);
    Save->Capture(Wide, 1, 20, true, 3);
    TestTrue(TEXT("wide resource values restore"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("wide saved resources are not truncated"), Restored.Resources, LargeAmount);
    TestEqual(TEXT("wide saved ore is not truncated"), Restored.BlueOre, LargeAmount);
    Wide.AddOre(LargeAmount, EOreKind::Black);
    Wide.SetRefiningMode(2);
    Save->Capture(Wide, 1, 20, true, 4);
    TestTrue(TEXT("large colored inventories retain valid refining accounting"), Save->Restore(Restored, Time, Brightness, bAuto));
    FFlightObjective OreGoal; OreGoal.Kind = 1;
    OreGoal.Record(EOreKind::Blue, 0, false, false);
    TestEqual(TEXT("unaccepted ore does not advance an ore objective"), OreGoal.Progress, 0);
    FFlightObjective KillGoal;
    KillGoal.Record(EOreKind::Blue, 0, false, false);
    TestEqual(TEXT("asteroid destruction still advances at the cap"), KillGoal.Progress, 1);
    FProgress Last;
    for (int32 I = 0; I < UpgradeCount; ++I) if (I != 54) Last.Acquired[I] = true;
    Last.Resources = ResourceAccountingLimit - 1;
    Last.TotalCollected = Last.Resources;
    const FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    const TArray<FForecastRock> NoRocks;
    TestEqual(TEXT("autopilot selects affordable final leaf near limit"), ChooseAutoUpgrade(Last, Context, NoRocks).Next, 54);
    return true;
}
#endif
