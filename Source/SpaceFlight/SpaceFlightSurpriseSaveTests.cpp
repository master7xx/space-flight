#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSurpriseSaveVersionTest,
    "SpaceFlight.Progress.SurpriseSave.Version",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSurpriseSaveVersionTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress Progress;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Progress, 0.0f, 20, true, 1);
    TestEqual(TEXT("capture writes the current version"), Save->Version, SpaceFlight::CurrentSaveVersion);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSurpriseSaveRoundTripTest,
    "SpaceFlight.Progress.SurpriseSave.RoundTripAndLegacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSurpriseSaveRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Original;
    Original.AddResources(123);
    Original.Objective.Sequence = 8;
    Original.Surprise.NotBeforeSeconds = 2120.0f;
    Original.Surprise.bDelivered = true;
    Original.TrackedUpgrade = 2;
    Original.bAutoPurchasesEnabled = false;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Original, 2100.0f, 20, true, 6);
    // V5 had no expedition or sector-deadline fields. Defaults make these
    // properties absent in the serialized fixture, as in a real V5 save.
    Save->Version = 5;
    Save->SectorReadyAt = -1.0f;
    Save->ExpeditionRoute = -2;
    Save->ExpeditionTarget = -1;
    Save->ExpeditionProgress = -1;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("v5 serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
    const auto* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("v5 deserializes"), Loaded)) return false;
    FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    if (!TestTrue(TEXT("v5 restores"), Loaded->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("delivered status survives"), Restored.Surprise.bDelivered);
    TestEqual(TEXT("not-before survives"), Restored.Surprise.NotBeforeSeconds, 2120.0f);
    TestEqual(TEXT("target survives"), Restored.TrackedUpgrade, 2);
    TestFalse(TEXT("autobuy selection survives"), Restored.bAutoPurchasesEnabled);
    TestEqual(TEXT("resources survive"), Restored.Resources, int64(123));
    Original.Surprise.bDelivered = false;
    Save->Capture(Original, 2100.0f, 20, true, 7);
    Save->Version = 5;
    if (!TestTrue(TEXT("pending v5 restores"), Save->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestFalse(TEXT("pending survives"), Restored.Surprise.bDelivered);

    // Legacy versions ignore newly added fields even if a synthetic save
    // happens to contain them. A real old file omits them entirely.
    for (int32 Version = 1; Version <= 4; ++Version)
    {
        Save->Version = Version;
        Save->Resources = 123;
        Save->TotalCollected = 123;
        Save->SurpriseDelivered = 1;
        Save->SurpriseNotBefore = 9999.0f;
        if (!TestTrue(FString::Printf(TEXT("v%d restores"), Version), Save->Restore(Restored, Time, Brightness, bAuto))) return false;
        TestFalse(TEXT("legacy delivery is pending"), Restored.Surprise.bDelivered);
        TestEqual(TEXT("advanced legacy flight receives a delay"), Restored.Surprise.NotBeforeSeconds, 2220.0f);
        TestEqual(TEXT("legacy resource balance survives"), Restored.Resources, int64(123));
        TestEqual(TEXT("v4 target is preserved"), Restored.TrackedUpgrade, Version >= 4 ? 2 : INDEX_NONE);
        USpaceFlightSaveGame* Migrated = NewObject<USpaceFlightSaveGame>();
        Migrated->Capture(Restored, 2110.0f, 20, true, 8);
        if (!TestTrue(TEXT("migrated snapshot restores"), Migrated->Restore(Restored, Time, Brightness, bAuto))) return false;
        TestEqual(TEXT("restart does not extend the saved delay"), Restored.Surprise.NotBeforeSeconds, 2220.0f);
    }
    Save->Version = 4;
    Save->ElapsedSeconds = 300.0f;
    if (!TestTrue(TEXT("early legacy restores"), Save->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestEqual(TEXT("early legacy retains ordinary threshold"), Restored.Surprise.NotBeforeSeconds, 1800.0f);
    Save->ElapsedSeconds = std::numeric_limits<float>::max();
    if (!TestTrue(TEXT("largest finite legacy time restores without overflow"), Save->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("largest migrated threshold remains finite"), FMath::IsFinite(Restored.Surprise.NotBeforeSeconds));
    USpaceFlightSaveGame* LargeMigrated = NewObject<USpaceFlightSaveGame>();
    LargeMigrated->Capture(Restored, Time, 20, true, 9);
    TestTrue(TEXT("largest finite migrated snapshot remains readable"), LargeMigrated->Restore(Restored, Time, Brightness, bAuto));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSurpriseSaveInvalidTest,
    "SpaceFlight.Progress.SurpriseSave.Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSurpriseSaveInvalidTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Original;
    Original.AddResources(123);
    USpaceFlightSaveGame* Valid = NewObject<USpaceFlightSaveGame>();
    Valid->Capture(Original, 2000.0f, 20, true, 4);
    USpaceFlightSaveGame* Invalid = NewObject<USpaceFlightSaveGame>();
    Invalid->Capture(Original, 2000.0f, 20, true, 5);
    FProgress Out; Out.AddResources(42); Out.Surprise.bDelivered = true;
    float Time = 5.0f; int32 Brightness = 30; bool bAuto = false;
    for (const int32 Value : { -1, -2, 2, MAX_int32 })
    {
        Invalid->SurpriseDelivered = Value;
        TestFalse(TEXT("invalid delivered flag is rejected"), Invalid->Restore(Out, Time, Brightness, bAuto));
        TestEqual(TEXT("rejected save leaves output unchanged"), Out.Resources, int64(42));
        TestTrue(TEXT("rejected save leaves delivered status unchanged"), Out.Surprise.bDelivered);
        TestTrue(TEXT("invalid newer save falls back to older valid save"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    }
    Invalid->SurpriseDelivered = 0;
    for (const float Value : { -1.0f, 1799.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        Invalid->SurpriseNotBefore = Value;
        TestFalse(TEXT("missing or invalid not-before is rejected"), Invalid->Restore(Out, Time, Brightness, bAuto));
        TestTrue(TEXT("invalid newer save does not supersede prior snapshot"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    }
    Invalid->SurpriseNotBefore = 2121.0f;
    TestFalse(TEXT("unreasonably delayed event is rejected"), Invalid->Restore(Out, Time, Brightness, bAuto));
    TestTrue(TEXT("delayed invalid newer save cannot supersede prior snapshot"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    Invalid->SurpriseNotBefore = 2120.0f;
    TestTrue(TEXT("legacy migration delay at the permitted boundary restores"), Invalid->Restore(Out, Time, Brightness, bAuto));
    Invalid->SurpriseNotBefore = 1800.0f;
    Invalid->Version = SpaceFlight::CurrentSaveVersion + 1;
    TestFalse(TEXT("future version is rejected"), Invalid->Restore(Out, Time, Brightness, bAuto));
    return true;
}
#endif
