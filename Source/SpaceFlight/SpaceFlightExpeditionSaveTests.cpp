#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSaveRoundTrip,
    "SpaceFlight.Progress.ExpeditionSave.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSaveRoundTrip::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Case = 0; Case < 3; ++Case)
    {
        FProgress Original;
        Original.Objective.Sequence = 8;
        Original.SectorReadyAt = 108.0f;
        if (Case != 0)
        {
            Original.Expedition.State = Case == 1 ? EExpeditionState::Active : EExpeditionState::Ready;
            Original.Expedition.Route = 2;
            Original.Expedition.Target = 100;
            Original.Expedition.Progress = Case == 1 ? 37 : 100;
        }
        USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
        Save->Capture(Original, 100.0f, 20, true, 1);
        TestEqual(TEXT("new capture uses the current version"), Save->Version, CurrentSaveVersion);
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("snapshot serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
        const auto* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!TestNotNull(TEXT("snapshot deserializes"), Loaded)) return false;
        FProgress Out; float Time = 0; int32 Brightness = 0; bool bAuto = false;
        if (!TestTrue(TEXT("snapshot restores"), Loaded->Restore(Out, Time, Brightness, bAuto))) return false;
        TestEqual(TEXT("remaining sector delay survives"), Out.SectorReadyAt - Time, 8.0f);
        TestTrue(TEXT("expedition state survives"), Out.Expedition.State == Original.Expedition.State);
        TestEqual(TEXT("route survives"), Out.Expedition.Route, Original.Expedition.Route);
        TestEqual(TEXT("target survives"), Out.Expedition.Target, Original.Expedition.Target);
        TestEqual(TEXT("progress survives"), Out.Expedition.Progress, Original.Expedition.Progress);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSaveLegacy,
    "SpaceFlight.Progress.ExpeditionSave.Legacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSaveLegacy::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Original;
    Original.AddResources(123);
    Original.TrackedUpgrade = 2;
    Original.bAutoPurchasesEnabled = false;
    Original.Surprise.bDelivered = true;
    Original.Surprise.NotBeforeSeconds = 2120.0f;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Original, 2100.0f, 20, true, 1);
    Save->Resources = 123;
    Save->TotalCollected = 123;
    Save->SectorReadyAt = -1.0f;
    Save->ExpeditionRoute = -2;
    Save->ExpeditionTarget = -1;
    Save->ExpeditionProgress = -1;
    for (int32 Version = 1; Version <= 5; ++Version)
    {
        Save->Version = Version;
        FProgress Out; float Time = 0; int32 Brightness = 0; bool bAuto = false;
        if (!TestTrue(FString::Printf(TEXT("v%d restores"), Version), Save->Restore(Out, Time, Brightness, bAuto))) return false;
        TestEqual(TEXT("legacy sector delay is clear"), Out.SectorReadyAt, 0.0f);
        TestTrue(TEXT("legacy expedition is idle"), Out.Expedition.State == EExpeditionState::Idle && Out.Expedition.IsValid());
        TestEqual(TEXT("resources retained"), Out.Resources, int64(123));
        TestEqual(TEXT("tracked target retained where supported"), Out.TrackedUpgrade, Version >= 4 ? 2 : INDEX_NONE);
        if (Version >= 3) TestFalse(TEXT("auto purchase choice retained"), Out.bAutoPurchasesEnabled);
        if (Version == 5)
        {
            TestTrue(TEXT("v5 delivery retained"), Out.Surprise.bDelivered);
            TestEqual(TEXT("v5 timing retained"), Out.Surprise.NotBeforeSeconds, 2120.0f);
        }
        else
        {
            TestFalse(TEXT("older delivery migrated"), Out.Surprise.bDelivered);
            TestEqual(TEXT("older timing migrated"), Out.Surprise.NotBeforeSeconds, 2220.0f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSaveInvalid,
    "SpaceFlight.Progress.ExpeditionSave.Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSaveInvalid::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Original;
    Original.Objective.Sequence = 3;
    USpaceFlightSaveGame* Valid = NewObject<USpaceFlightSaveGame>();
    Valid->Capture(Original, 100.0f, 20, true, 1);
    USpaceFlightSaveGame* Invalid = NewObject<USpaceFlightSaveGame>();
    FProgress Out; Out.AddResources(42);
    float Time = 5.0f; int32 Brightness = 30; bool bAuto = false;
    const auto Reset = [&]() { Invalid->Capture(Original, 100.0f, 20, true, 2); };
    const auto Rejected = [&](const TCHAR* Name)
    {
        TestFalse(Name, Invalid->Restore(Out, Time, Brightness, bAuto));
        TestEqual(TEXT("failed restore leaves progress alone"), Out.Resources, int64(42));
        TestEqual(TEXT("failed restore leaves time alone"), Time, 5.0f);
        TestTrue(TEXT("invalid newer snapshot cannot displace valid one"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    };
    for (const float Value : { -1.0f, 112.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        Reset(); Invalid->SectorReadyAt = Value; Rejected(TEXT("invalid deadline rejected"));
    }
    Reset(); Invalid->ExpeditionRoute = -2; Rejected(TEXT("missing route rejected"));
    Reset(); Invalid->ExpeditionTarget = -1; Rejected(TEXT("missing target rejected"));
    Reset(); Invalid->ExpeditionProgress = -1; Rejected(TEXT("missing progress rejected"));
    Reset(); Invalid->ExpeditionProgress = 1; Rejected(TEXT("idle with progress rejected"));
    Reset(); Invalid->ExpeditionRoute = 1; Invalid->ExpeditionTarget = 100; Invalid->ExpeditionProgress = 101;
    Rejected(TEXT("progress beyond target rejected"));
    Reset(); Invalid->ExpeditionRoute = 2; Invalid->ExpeditionTarget = 100; Invalid->ExpeditionProgress = 0;
    Rejected(TEXT("locked route rejected"));
    Reset(); Invalid->Version = CurrentSaveVersion + 1; Rejected(TEXT("future version rejected"));
    return true;
}
#endif
