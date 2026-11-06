#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAutoPurchaseSaveCompatibilityTest,
    "SpaceFlight.Progress.AutoPurchaseSave.Compatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAutoPurchaseSaveCompatibilityTest::RunTest(const FString& Parameters)
{
    float Time = 0.0f;
    int32 Brightness = 0;
    bool bAuto = false;
    SpaceFlight::FProgress Restored;

    USpaceFlightSaveGame* Legacy = NewObject<USpaceFlightSaveGame>();
    Legacy->Version = 1;
    Legacy->Resources = 123;
    Legacy->TotalCollected = 123;
    Legacy->AutoPurchasePolicy = 0;
    if (!TestTrue(TEXT("v1 restores"), Legacy->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("v1 enables automatic purchases regardless of policy field"), Restored.bAutoPurchasesEnabled);
    TestEqual(TEXT("v1 resource balance survives"), Restored.Resources, int64(123));

    SpaceFlight::FProgress Original;
    Original.AddResources(int64(MAX_int32) + 100);
    Original.bAutoPurchasesEnabled = false;
    USpaceFlightSaveGame* Wide = NewObject<USpaceFlightSaveGame>();
    Wide->Capture(Original, 10.0f, 20, true, 7);
    Wide->Version = 2;
    Wide->AutoPurchasePolicy = -1;
    if (!TestTrue(TEXT("v2 with missing policy restores"), Wide->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("v2 enables automatic purchases"), Restored.bAutoPurchasesEnabled);
    TestEqual(TEXT("v2 64-bit resources survive"), Restored.Resources, Original.Resources);
    Wide->AutoPurchasePolicy = 0;
    if (!TestTrue(TEXT("v2 ignores policy field"), Wide->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("v2 policy remains enabled"), Restored.bAutoPurchasesEnabled);

    USpaceFlightSaveGame* Current = NewObject<USpaceFlightSaveGame>();
    Current->Capture(Original, 10.0f, 20, true, 8);
    TestEqual(TEXT("capture writes the current version"), Current->Version, SpaceFlight::CurrentSaveVersion);
    // Explicitly retain coverage for policy loading from older v3 saves.
    Current->Version = 3;
    TestEqual(TEXT("disabled policy is encoded as zero"), Current->AutoPurchasePolicy, 0);
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("v3 serializes"), UGameplayStatics::SaveGameToMemory(Current, Bytes))) return false;
    const USpaceFlightSaveGame* Reloaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("v3 deserializes"), Reloaded)) return false;
    if (!TestTrue(TEXT("v3 restores"), Reloaded->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestFalse(TEXT("disabled policy survives serialization"), Restored.bAutoPurchasesEnabled);
    TestEqual(TEXT("v3 preserves 64-bit resources"), Restored.Resources, Original.Resources);
    Original.bAutoPurchasesEnabled = true;
    Current->Capture(Original, 10.0f, 20, true, 9);
    Current->Version = 3;
    TestEqual(TEXT("enabled policy is encoded as one"), Current->AutoPurchasePolicy, 1);
    if (!TestTrue(TEXT("enabled v3 restores"), Current->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestTrue(TEXT("enabled v3 policy survives"), Restored.bAutoPurchasesEnabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAutoPurchaseInvalidPolicyTest,
    "SpaceFlight.Progress.AutoPurchaseSave.InvalidPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAutoPurchaseInvalidPolicyTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress Original;
    Original.AddResources(123);
    USpaceFlightSaveGame* Valid = NewObject<USpaceFlightSaveGame>();
    Valid->Capture(Original, 10.0f, 20, true, 2);
    USpaceFlightSaveGame* Invalid = NewObject<USpaceFlightSaveGame>();
    Invalid->Capture(Original, 10.0f, 20, true, 3);
    Invalid->Version = 3;
    SpaceFlight::FProgress Output;
    Output.AddResources(42);
    Output.bAutoPurchasesEnabled = false;
    float Time = 5.0f;
    int32 Brightness = 30;
    bool bAuto = false;
    for (const int32 Policy : {-1, 2, -2})
    {
        Invalid->AutoPurchasePolicy = Policy;
        TestFalse(FString::Printf(TEXT("v3 rejects invalid policy %d"), Policy), Invalid->Restore(Output, Time, Brightness, bAuto));
        TestEqual(TEXT("rejected save leaves output resources unchanged"), Output.Resources, int64(42));
        TestFalse(TEXT("rejected save leaves output policy unchanged"), Output.bAutoPurchasesEnabled);
        TestEqual(TEXT("rejected save leaves elapsed time unchanged"), Time, 5.0f);
        TestEqual(TEXT("rejected save leaves brightness unchanged"), Brightness, 30);
        TestFalse(TEXT("rejected save leaves autopilot unchanged"), bAuto);
        TestTrue(TEXT("newest valid chooses older good snapshot"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    }
    Invalid->Version = SpaceFlight::CurrentSaveVersion + 1;
    Invalid->AutoPurchasePolicy = 1;
    TestFalse(TEXT("future save versions are rejected"), Invalid->Restore(Output, Time, Brightness, bAuto));
    Invalid->Version = 3;
    Invalid->Resources64 = -1;
    TestFalse(TEXT("v3 still rejects missing wide resource fields"), Invalid->Restore(Output, Time, Brightness, bAuto));
    Invalid->Version = 1;
    Invalid->Resources = -1;
    TestFalse(TEXT("v1 still rejects invalid legacy resource fields"), Invalid->Restore(Output, Time, Brightness, bAuto));
    return true;
}
#endif
