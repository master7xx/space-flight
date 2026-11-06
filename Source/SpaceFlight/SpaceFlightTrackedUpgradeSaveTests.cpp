#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradeCompatibilityTest,
    "SpaceFlight.Progress.TrackedUpgradeSave.Compatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradeCompatibilityTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress Initial;
    Initial.AddResources(int64(MAX_int32) + 123);
    Initial.bAutoPurchasesEnabled = false;
    // Locked descendants can be chosen as a long-term objective.
    Initial.TrackedUpgrade = 2;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Initial, 12.0f, 20, true, 4);
    TestEqual(TEXT("capture writes the current version"), Save->Version, SpaceFlight::CurrentSaveVersion);
    TestEqual(TEXT("target is captured"), Save->TrackedUpgradeIndex, 2);

    float Time = 0.0f;
    int32 Brightness = 0;
    bool bAutopilot = false;
    SpaceFlight::FProgress Restored;
    for (const int32 Version : { 1, 2, 3 })
    {
        Save->Version = Version;
        if (Version == 1)
        {
            Save->Resources = 123;
            Save->TotalCollected = 123;
        }
        // Old versions do not know about the target, even if a new field is present.
        Restored.TrackedUpgrade = 9;
        if (!TestTrue(FString::Printf(TEXT("version %d restores"), Version), Save->Restore(Restored, Time, Brightness, bAutopilot))) return false;
        TestEqual(FString::Printf(TEXT("version %d starts without target"), Version), Restored.TrackedUpgrade, INDEX_NONE);
        if (Version >= 2) TestEqual(TEXT("wide legacy balance is preserved"), Restored.Resources, Initial.Resources);
        if (Version == 3) TestFalse(TEXT("v3 auto-purchase policy is preserved"), Restored.bAutoPurchasesEnabled);
    }

    Save->Capture(Initial, 12.0f, 20, true, 4);
    Save->Version = 4;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("v4 serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
    const USpaceFlightSaveGame* Reloaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("v4 deserializes"), Reloaded)) return false;
    if (!TestTrue(TEXT("v4 restores"), Reloaded->Restore(Restored, Time, Brightness, bAutopilot))) return false;
    TestEqual(TEXT("tracked target survives serialization"), Restored.TrackedUpgrade, 2);
    TestEqual(TEXT("v4 resources are unchanged"), Restored.Resources, Initial.Resources);
    TestFalse(TEXT("v4 auto-purchase policy is unchanged"), Restored.bAutoPurchasesEnabled);

    Initial.TrackedUpgrade = INDEX_NONE;
    Save->Capture(Initial, 12.0f, 20, true, 5);
    Save->Version = 4;
    if (!TestTrue(TEXT("v4 without target restores"), Save->Restore(Restored, Time, Brightness, bAutopilot))) return false;
    TestEqual(TEXT("absence of a target survives"), Restored.TrackedUpgrade, INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradeInvalidSaveTest,
    "SpaceFlight.Progress.TrackedUpgradeSave.Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradeInvalidSaveTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress Initial;
    Initial.AddResources(123);
    USpaceFlightSaveGame* Valid = NewObject<USpaceFlightSaveGame>();
    Valid->Capture(Initial, 12.0f, 20, true, 4);
    USpaceFlightSaveGame* Invalid = NewObject<USpaceFlightSaveGame>();
    Invalid->Capture(Initial, 12.0f, 20, true, 5);
    Invalid->Version = 4;
    SpaceFlight::FProgress Output;
    Output.AddResources(42);
    Output.TrackedUpgrade = 7;
    float Time = 5.0f;
    int32 Brightness = 30;
    bool bAutopilot = false;
    for (const int32 Index : { -2, -3, SpaceFlight::UpgradeCount, MAX_int32 })
    {
        Invalid->TrackedUpgradeIndex = Index;
        TestFalse(FString::Printf(TEXT("v4 rejects missing or invalid target %d"), Index),
            Invalid->Restore(Output, Time, Brightness, bAutopilot));
        TestEqual(TEXT("rejected target leaves output target unchanged"), Output.TrackedUpgrade, 7);
        TestEqual(TEXT("rejected target leaves output resources unchanged"), Output.Resources, int64(42));
        TestEqual(TEXT("rejected target leaves output time unchanged"), Time, 5.0f);
        TestEqual(TEXT("rejected target leaves brightness unchanged"), Brightness, 30);
        TestFalse(TEXT("rejected target leaves autopilot unchanged"), bAutopilot);
        TestTrue(TEXT("invalid newer snapshot falls back to valid one"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);
    }
    Invalid->TrackedUpgradeIndex = 0;
    Invalid->AcquiredIndices.Add(0);
    TestFalse(TEXT("v4 rejects an already acquired target"), Invalid->Restore(Output, Time, Brightness, bAutopilot));
    TestTrue(TEXT("acquired target cannot supersede valid snapshot"), USpaceFlightSaveGame::NewestValid(Valid, Invalid) == Valid);

    Invalid->AcquiredIndices.Reset();
    Invalid->TrackedUpgradeIndex = INDEX_NONE;
    Invalid->AutoPurchasePolicy = -1;
    TestFalse(TEXT("v4 retains mandatory auto-purchase policy validation"), Invalid->Restore(Output, Time, Brightness, bAutopilot));
    Invalid->AutoPurchasePolicy = 1;
    Invalid->Version = SpaceFlight::CurrentSaveVersion + 1;
    TestFalse(TEXT("future version remains unsupported"), Invalid->Restore(Output, Time, Brightness, bAutopilot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradePurchaseTest,
    "SpaceFlight.Progress.TrackedUpgradeSave.Purchase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradePurchaseTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress Progress;
    Progress.TrackedUpgrade = 0;
    TestFalse(TEXT("unaffordable target cannot be bought"), Progress.Purchase(0));
    TestEqual(TEXT("failed purchase keeps target"), Progress.TrackedUpgrade, 0);

    Progress.TrackedUpgrade = 1;
    Progress.AddResources(SpaceFlight::Upgrade(0).Cost + SpaceFlight::Upgrade(1).Cost);
    TestFalse(TEXT("closed prerequisite still prevents purchase"), Progress.Purchase(1));
    TestEqual(TEXT("failed locked purchase keeps target"), Progress.TrackedUpgrade, 1);
    TestTrue(TEXT("another upgrade can be purchased normally"), Progress.Purchase(0));
    TestEqual(TEXT("buying another upgrade keeps target"), Progress.TrackedUpgrade, 1);
    TestFalse(TEXT("already acquired upgrade cannot be bought again"), Progress.Purchase(0));
    TestEqual(TEXT("failed repeated purchase keeps target"), Progress.TrackedUpgrade, 1);
    TestTrue(TEXT("tracked target can be purchased"), Progress.Purchase(1));
    TestEqual(TEXT("successful target purchase clears it"), Progress.TrackedUpgrade, INDEX_NONE);
    TestTrue(TEXT("target remains acquired"), Progress.Has(1));
    return true;
}
#endif
