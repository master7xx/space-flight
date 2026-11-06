#include "SpaceFlightLogic.h"
#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTargetingSelectionTest, "SpaceFlight.Targeting.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTargetingSelectionTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    const FVector2D Origin(640, 500);
    auto Pick = [&](int32 Mode, int32 Weapons)
    {
        TArray<FLaserCandidate, TInlineAllocator<3>> Targets;
        AddLaserCandidate(Targets, { 1, 0, 12, FVector2D(650, 500), FLinearColor::White, 0, EOreKind::Common, false }, Origin, 720, 220, Weapons, Mode);
        AddLaserCandidate(Targets, { 2, 0, 12, FVector2D(700, 500), FLinearColor::White, 0, EOreKind::Blue, false }, Origin, 720, 220, Weapons, Mode);
        AddLaserCandidate(Targets, { 3, 0, 12, FVector2D(720, 500), FLinearColor::White, 0, EOreKind::Black, true }, Origin, 720, 220, Weapons, Mode);
        AddLaserCandidate(Targets, { 4, 0, 12, FVector2D(900, 500), FLinearColor::White, 0, EOreKind::Blue, true }, Origin, 720, 220, Weapons, Mode);
        AddLaserCandidate(Targets, { 2, 0, 12, FVector2D(700, 500), FLinearColor::White, 0, EOreKind::Blue, false }, Origin, 720, 220, Weapons, Mode);
        return Targets;
    };
    TestEqual(TEXT("nearest remains the default"), Pick(0, 1)[0].Index, 1);
    TestEqual(TEXT("blue is preferred in range"), Pick(1, 1)[0].Index, 2);
    TestEqual(TEXT("black is preferred in range"), Pick(2, 1)[0].Index, 3);
    TestEqual(TEXT("marked event is preferred in range"), Pick(3, 1)[0].Index, 3);
    TestEqual(TEXT("no installed weapons means no targets"), Pick(1, 0).Num(), 0);
    const auto Blue = Pick(1, 3);
    TestEqual(TEXT("weapons use distinct targets"), Blue.Num(), 3);
    if (Blue.Num() == 3)
    {
        TestEqual(TEXT("first is preferred"), Blue[0].Index, 2);
        TestEqual(TEXT("fallback is nearest"), Blue[1].Index, 1);
        TestEqual(TEXT("remaining fallback stays in range"), Blue[2].Index, 3);
    }
    const auto Fallback = Pick(99, 1);
    TestEqual(TEXT("unknown priority safely falls back to nearest"), Fallback[0].Index, 1);
    TArray<FLaserCandidate, TInlineAllocator<3>> NoEvent;
    AddLaserCandidate(NoEvent, { 8, 0, 12, FVector2D(660, 500), FLinearColor::White, 0, EOreKind::Common, false }, Origin, 720, 220, 1, 3);
    AddLaserCandidate(NoEvent, { 9, 0, 12, FVector2D(640, -1), FLinearColor::White, 0, EOreKind::Black, true }, Origin, 720, 220, 1, 3);
    TestEqual(TEXT("event preference falls back when marked target is offscreen"), NoEvent[0].Index, 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTargetingForecastTest, "SpaceFlight.Targeting.Forecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTargetingForecastTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    const FForecastContext Context{ 1280, 720, 1, 640, 500, 500, 0, 100 };
    const FForecastRock Rocks[] = {
        { 650, 500, 0, 5, 12, false, 1, EOreKind::Black, false },
        { 700, 500, 0, 5, 12, false, 1, EOreKind::Blue, false }
    };
    const FForecastWeights Weights{ 4.0f, 1.0f };
    const float Nearest = ForecastOre(Progress, Context, Rocks, Weights, .25f);
    Progress.TargetingMode = 1;
    const float Blue = ForecastOre(Progress, Context, Rocks, Weights, .25f);
    TestTrue(TEXT("autopurchase forecast reflects blue priority"), Blue > Nearest * 2.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTargetingSaveTest, "SpaceFlight.Targeting.Save",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTargetingSaveTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Mode = 0; Mode <= 3; ++Mode)
    {
        FProgress Progress; Progress.TargetingMode = Mode;
        auto* Save = NewObject<USpaceFlightSaveGame>();
        Save->Capture(Progress, 0, 20, true, 1);
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("serialize"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
        const auto* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!TestNotNull(TEXT("deserialize"), Loaded)) return false;
        FProgress Out; float Time = 0; int32 Brightness = 0; bool bAuto = false;
        TestTrue(TEXT("restore"), Loaded->Restore(Out, Time, Brightness, bAuto));
        TestEqual(TEXT("target mode survives"), Out.TargetingMode, Mode);
        Save->TargetingSchema = 0; Save->TargetingMode = 99;
        TestTrue(TEXT("older save without targeting settings restores"), Save->Restore(Out, Time, Brightness, bAuto));
        TestEqual(TEXT("older save chooses nearest"), Out.TargetingMode, 0);
        Save->TargetingSchema = 1;
        TestFalse(TEXT("invalid saved targeting mode is rejected"), Save->Restore(Out, Time, Brightness, bAuto));
        Save->TargetingSchema = 2;
        TestFalse(TEXT("unknown targeting schema is rejected"), Save->Restore(Out, Time, Brightness, bAuto));
    }
    return true;
}
#endif
