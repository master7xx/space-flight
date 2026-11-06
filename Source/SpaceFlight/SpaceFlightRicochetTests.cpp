#include "SpaceFlightLogic.h"
#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRicochetDamageTest, "SpaceFlight.Ricochet.Damage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRicochetDamageTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FAsteroidDurability Rock;
    Rock.Prepare(0, 0, 9);
    TestTrue(TEXT("fresh rock has not been hit"), Rock.IsUndamaged(9));
    TestFalse(TEXT("40 percent alone cannot destroy a small rock"), Rock.HitTenths(4));
    TestFalse(TEXT("partial damage removes clean first-hit status"), Rock.IsUndamaged(9));
    TestEqual(TEXT("partial remaining durability is exact"), Rock.RemainingTenths, 6);
    TestFalse(TEXT("two partial hits do not destroy a small rock"), Rock.HitTenths(4));
    TestTrue(TEXT("third partial hit destroys the rock"), Rock.HitTenths(4));
    TestFalse(TEXT("destroyed rock cannot award a second destruction"), Rock.HitTenths(4));
    Rock.Prepare(1, 0, 18);
    TestEqual(TEXT("new pass restores medium rock"), Rock.RemainingTenths, 20);
    TestFalse(TEXT("direct hit remains one full point"), Rock.Hit(1));
    TestEqual(TEXT("direct damage and partial damage use same units"), Rock.RemainingTenths, 10);
    TestTrue(TEXT("second direct hit destroys medium rock"), Rock.Hit(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRicochetSelectionTest, "SpaceFlight.Ricochet.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRicochetSelectionTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    const FVector2D LaserOrigin(0, 0), ShipCenter(0, 50), From(100, 0);
    const TArray<FLaserCandidate> Rocks{
        { 1, 0, 9, FVector2D(100, 0), FLinearColor::White, 0, EOreKind::Common, false },
        { 2, 0, 9, FVector2D(115, 0), FLinearColor::White, 0, EOreKind::Common, false },
        { 3, 0, 9, FVector2D(150, 0), FLinearColor::White, 0, EOreKind::Blue, false },
        { 4, 0, 9, FVector2D(221, 0), FLinearColor::White, 0, EOreKind::Blue, false },
        { 5, 0, 9, FVector2D(100, -1), FLinearColor::White, 0, EOreKind::Blue, false }
    };
    const TArray<int32> Reserved{ 1 };
    TestEqual(TEXT("blue priority wins over a nearer common asteroid"),
        SelectRicochetTarget(Rocks, Reserved, From, LaserOrigin, ShipCenter, 20, 220, 720, 1), 3);
    TestEqual(TEXT("nearest target wins in default mode"),
        SelectRicochetTarget(Rocks, Reserved, From, LaserOrigin, ShipCenter, 20, 220, 720, 0), 2);
    const TArray<int32> AllReserved{ 1, 2, 3 };
    TestEqual(TEXT("offscreen and outside radar targets cannot be hit"),
        SelectRicochetTarget(Rocks, AllReserved, From, LaserOrigin, ShipCenter, 20, 220, 720, 1), INDEX_NONE);
    const TArray<FLaserCandidate> AcrossShip{
        { 6, 0, 9, FVector2D(50, 50), FLinearColor::White, 0, EOreKind::Common, false }
    };
    TestEqual(TEXT("reflected beam cannot cross the ship"),
        SelectRicochetTarget(AcrossShip, {}, FVector2D(-50, 50), LaserOrigin, ShipCenter, 20, 220, 720, 0), INDEX_NONE);
    const TArray<FLaserCandidate> Boundary{
        { 7, 0, 9, FVector2D(220, 0), FLinearColor::White, 0, EOreKind::Common, false }
    };
    TestEqual(TEXT("120-pixel and radar boundaries are inclusive"),
        SelectRicochetTarget(Boundary, {}, From, LaserOrigin, ShipCenter, 20, 220, 720, 0), 7);
    const TArray<FLaserCandidate> DamagedFirst{
        { 8, 0, 9, FVector2D(110, 0), FLinearColor::White, 0, EOreKind::Common, false, false },
        { 9, 0, 9, FVector2D(140, 0), FLinearColor::White, 0, EOreKind::Common, false, true },
        { 10, 0, 9, FVector2D(160, 0), FLinearColor::White, 0, EOreKind::Blue, false, false }
    };
    TestEqual(TEXT("damaged asteroid is preferred within the same ore priority"),
        SelectRicochetTarget(DamagedFirst, {}, From, LaserOrigin, ShipCenter, 20, 220, 720, 0), 9);
    TestEqual(TEXT("chosen ore priority still wins over damage preference"),
        SelectRicochetTarget(DamagedFirst, {}, From, LaserOrigin, ShipCenter, 20, 220, 720, 1), 10);
    TestTrue(TEXT("weak bounce protects a fresh one-shot bonus"), RicochetWouldSpoilFirstHitBonus(9, 1, false, true));
    TestFalse(TEXT("already damaged rock has no first-hit bonus to protect"), RicochetWouldSpoilFirstHitBonus(9, 1, true, true));
    TestFalse(TEXT("without a bonus the weak bounce is allowed"), RicochetWouldSpoilFirstHitBonus(9, 1, false, false));
    TestFalse(TEXT("bounce may finish the rock and earn its bonus"), RicochetWouldSpoilFirstHitBonus(9, 3, false, true));
    TestFalse(TEXT("rock needing multiple direct shots is still eligible"), RicochetWouldSpoilFirstHitBonus(18, 1, false, true));
    const TArray<FLaserCandidate> Protected{
        { 11, 0, 9, FVector2D(110, 0), FLinearColor::White, 0, EOreKind::Blue, false, false, true },
        { 12, 0, 18, FVector2D(140, 0), FLinearColor::White, 0, EOreKind::Common, false, false, false }
    };
    TestEqual(TEXT("protection skips the risky rock even when its ore is preferred"),
        SelectRicochetTarget(Protected, {}, From, LaserOrigin, ShipCenter, 20, 220, 720, 1), 12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRicochetSaveTest, "SpaceFlight.Ricochet.Save",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRicochetSaveTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    Progress.AddResources(100000);
    TestFalse(TEXT("ricochet requires its parent"), Progress.Purchase(RicochetUpgrade));
    TestTrue(TEXT("buy left laser"), Progress.Purchase(5));
    TestTrue(TEXT("buy clean-cut parent"), Progress.Purchase(71));
    TestTrue(TEXT("buy ricochet"), Progress.Purchase(RicochetUpgrade));
    auto* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Progress, 0, 20, true, 1);
    TestEqual(TEXT("ricochet writes new save version"), Save->Version, 7);
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("serialize"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
    const auto* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("deserialize"), Loaded)) return false;
    FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("new save restores"), Loaded->Restore(Restored, Time, Brightness, bAuto));
    TestTrue(TEXT("ricochet remains purchased"), Restored.Has(RicochetUpgrade));
    Save->Version = 6;
    TestFalse(TEXT("legacy save cannot contain a future upgrade"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->AcquiredIndices.Remove(RicochetUpgrade);
    TestTrue(TEXT("old version without ricochet still restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestFalse(TEXT("old saves do not grant ricochet"), Restored.Has(RicochetUpgrade));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRicochetForecastTest, "SpaceFlight.Ricochet.Forecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRicochetForecastTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    Progress.Acquired[14] = true;
    Progress.Acquired[22] = true;
    const FForecastContext Context{ 1280, 720, 1, 640, 500, 500, 0, 100 };
    const FForecastRock Rocks[]{ { 750, 500, 0, 5, 9, false }, { 800, 500, 0, 5, 9, false } };
    const float Baseline = ForecastOre(Progress, Context, Rocks, {}, .25f);
    Progress.Acquired[RicochetUpgrade] = true;
    const float Bouncing = ForecastOre(Progress, Context, Rocks, {}, .25f);
    TestTrue(TEXT("autopurchase forecast values reachable reflected hits"), Bouncing > Baseline);
    FProgress LastLeaf;
    for (int32 Index = 0; Index < UpgradeCount; ++Index) LastLeaf.Acquired[Index] = true;
    LastLeaf.Acquired[RicochetUpgrade] = false;
    LastLeaf.AddResources(1000000);
    const TArray<FForecastRock> NoRocks;
    const auto Choice = ChooseAutoUpgrade(LastLeaf, Context, NoRocks);
    TestEqual(TEXT("autopilot chooses ricochet as the last available leaf"), Choice.Next, RicochetUpgrade);
    TestTrue(TEXT("autopilot can purchase the chosen leaf"), LastLeaf.Purchase(Choice.Next));
    return true;
}
#endif
