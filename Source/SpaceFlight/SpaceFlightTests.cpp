#include "SpaceFlightLogic.h"
#include "BlueAsteroidArt.h"
#include "SpaceFlightAudio.h"
#include "SpaceFlightSave.h"
#include "SpaceFlightSectors.h"
#include "SpaceFlightHudLayout.h"
#include "SpaceFlightTreeInput.h"
#include "SpaceFlightShield.h"
#include "SpaceFlightEvents.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightUpgradeComparisonTest, "SpaceFlight.Progress.UpgradeComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightUpgradeComparisonTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    const FString Radar = DescribeUpgradeComparison(Progress, 0);
    TestTrue(TEXT("radar compares current and future range"), Radar.Contains(TEXT("220")) && Radar.Contains(TEXT("235")));
    TestTrue(TEXT("locked upgrade can be inspected"), DescribeUpgradeComparison(Progress, 68).Contains(TEXT("КРУПН")));
    TestTrue(TEXT("first hit and range are both compared"), DescribeUpgradeComparison(Progress, 72).Contains(TEXT("1-Й")) && DescribeUpgradeComparison(Progress, 72).Contains(TEXT("РАДИУС")));
    TestTrue(TEXT("stability reports remaining bank"), DescribeUpgradeComparison(Progress, 77).Contains(TEXT("100%")) && DescribeUpgradeComparison(Progress, 77).Contains(TEXT("80%")));
    TestFalse(TEXT("inspection never changes progress"), Progress.Has(0) || Progress.Has(68) || Progress.Has(77));
    Progress.Acquired[0] = true;
    TestTrue(TEXT("owned upgrade has no hypothetical second application"), DescribeUpgradeComparison(Progress, 0).IsEmpty());
    TestTrue(TEXT("invalid index is ignored"), DescribeUpgradeComparison(Progress, -1).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightIsotopeForecastTest, "SpaceFlight.Progress.IsotopeForecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightIsotopeForecastTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    Progress.Acquired[0] = true; // Unlocks a micro upgrade.
    Progress.BlueOre = 100;
    Progress.Resources = 100;
    auto Weights = AutoForecastWeights(Progress);
    TestTrue(TEXT("missing black ore receives a modest priority"), Weights.Black > Weights.Blue && Weights.Blue == 1.0f);
    const FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    const TArray<FForecastRock> Blue{{800, 475, 0, 13, 9, false, 0, EOreKind::Blue}};
    const TArray<FForecastRock> Black{{800, 475, 0, 13, 9, false, 0, EOreKind::Black}};
    TestTrue(TEXT("forecast values the limiting color more"), ForecastOre(Progress, Context, Black, Weights) > ForecastOre(Progress, Context, Blue, Weights));
    const TArray<FForecastRock> Later{{330, 0, 15, 6, 9, false}};
    TestTrue(TEXT("longer forecast can see an asteroid entering range later"),
        ForecastOre(Progress, Context, Later, {}, 18.0f) > ForecastOre(Progress, Context, Later, {}, 12.0f));
    Progress.RefiningMode = 1;
    Weights = AutoForecastWeights(Progress);
    TestTrue(TEXT("disabled refining does not prioritize colored ore"), Weights.Black == 1.0f && Weights.Blue == 1.0f);
    Progress.RefiningMode = 0;
    Progress.Isotope = 100000;
    Weights = AutoForecastWeights(Progress);
    TestTrue(TEXT("sufficient isotope does not create artificial scarcity"), Weights.Black == 1.0f && Weights.Blue == 1.0f);
    Progress.Isotope = 0;
    Progress.BlueOre = 0;
    Weights = AutoForecastWeights(Progress);
    TestTrue(TEXT("both colors can be needed when balanced"), Weights.Black > 1.0f && Weights.Blue > 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightBalancedOreSpendingTest, "SpaceFlight.Progress.BalancedOreSpending",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightBalancedOreSpendingTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress BlackSurplus;
    BlackSurplus.SetRefiningMode(1);
    BlackSurplus.AddOre(12, EOreKind::Blue);
    BlackSurplus.AddOre(100, EOreKind::Black);
    TestTrue(TEXT("ordinary node can be paid using colored ore"), BlackSurplus.Purchase(0));
    TestEqual(TEXT("scarcer blue ore is retained"), BlackSurplus.BlueOre, int64(12));
    TestEqual(TEXT("surplus black ore pays first"), BlackSurplus.BlackOre, int64(88));
    FProgress BlueSurplus;
    BlueSurplus.SetRefiningMode(1);
    BlueSurplus.AddOre(100, EOreKind::Blue);
    BlueSurplus.AddOre(12, EOreKind::Black);
    TestTrue(TEXT("opposite surplus can pay"), BlueSurplus.Purchase(0));
    TestEqual(TEXT("surplus blue ore pays first"), BlueSurplus.BlueOre, int64(88));
    TestEqual(TEXT("scarcer black ore is retained"), BlueSurplus.BlackOre, int64(12));
    FProgress Equal;
    Equal.SetRefiningMode(1);
    Equal.AddOre(20, EOreKind::Blue);
    Equal.AddOre(20, EOreKind::Black);
    TestTrue(TEXT("equal reserves can pay"), Equal.Purchase(0));
    TestEqual(TEXT("equal reserves remain balanced blue"), Equal.BlueOre, int64(14));
    TestEqual(TEXT("equal reserves remain balanced black"), Equal.BlackOre, int64(14));
    TestEqual(TEXT("ordinary spending conserves total ore"), Equal.Resources, Equal.TotalCollected - Equal.TotalSpent - Equal.OreConverted);
    bool bAllCombinationsValid = true;
    for (int32 Blue = 0; Blue <= 20; ++Blue)
        for (int32 Black = 0; Black <= 20; ++Black)
            for (int32 Common : {0, 3, 6, 12})
            {
                if (Blue + Black + Common < 12) continue;
                FProgress Sample;
                Sample.SetRefiningMode(1);
                Sample.AddResources(Common);
                Sample.AddOre(Blue, EOreKind::Blue);
                Sample.AddOre(Black, EOreKind::Black);
                const int32 RemainingColored = Blue + Black - FMath::Max(0, 12 - Common);
                const int32 MaximumPairs = FMath::Min(FMath::Min(Blue, Black), RemainingColored / 2);
                const bool bPurchased = Sample.Purchase(0);
                bAllCombinationsValid &= bPurchased && Sample.BlueOre >= 0 && Sample.BlackOre >= 0 &&
                    FMath::Min(Sample.BlueOre, Sample.BlackOre) == MaximumPairs &&
                    Sample.Resources == Sample.TotalCollected - Sample.TotalSpent - Sample.OreConverted;
            }
    TestTrue(TEXT("spending conserves ore and preserves maximum isotope pairs across reserve combinations"), bAllCombinationsValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSideBranchesTest, "SpaceFlight.Progress.SideBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSideBranchesTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("five side branches and ricochet extend the original tree"), UpgradeCount, 81);
    for (int32 Column = 0; Column < 5; ++Column)
    {
        const int32 Root = 65 + 3 * Column;
        TestTrue(TEXT("side branch root is marked as a side upgrade"), IsSideUpgrade(Root));
        TestEqual(TEXT("all side nodes belong to their original branch"), Upgrade(Root).Column, Column);
        TestEqual(TEXT("second side tier follows the root"), Upgrade(Root + 1).Parent, Root);
        TestEqual(TEXT("third side tier follows the second"), Upgrade(Root + 2).Parent, Root + 1);
        FProgress Gate;
        Gate.AddResources(100000);
        TestFalse(TEXT("side branch requires its original prerequisite"), Gate.Purchase(Root));
    }
    FProgress Large;
    Large.AddResources(100000);
    TestTrue(TEXT("collector prerequisite I"), Large.Purchase(3));
    TestTrue(TEXT("collector prerequisite II"), Large.Purchase(4));
    const int32 SmallBefore = Large.Yield(9), LargeBefore = Large.Yield(27);
    TestTrue(TEXT("large-rock specialization can be purchased"), Large.Purchase(68));
    TestEqual(TEXT("large-rock bonus affects large asteroids"), Large.Yield(27), LargeBefore + 2);
    TestEqual(TEXT("large-rock bonus is reported for pickup feedback"), Large.ConditionalOreBonus(27, false), 2);
    TestEqual(TEXT("large-rock bonus leaves small asteroids unchanged"), Large.Yield(9), SmallBefore);

    FProgress Precision;
    Precision.AddResources(100000);
    TestTrue(TEXT("laser prerequisite is purchasable"), Precision.Purchase(5));
    const int32 CleanBefore = Precision.Yield(9, EOreKind::Common, false, true);
    const int32 DamagedBefore = Precision.Yield(9, EOreKind::Common, false, false);
    TestTrue(TEXT("precision side node is purchasable"), Precision.Purchase(71));
    TestEqual(TEXT("first-hit destruction yields one extra ore"), Precision.Yield(9, EOreKind::Common, false, true), CleanBefore + 1);
    TestEqual(TEXT("clean-cut bonus is reported for pickup feedback"), Precision.ConditionalOreBonus(9, true), 1);
    TestEqual(TEXT("a previously damaged asteroid receives no clean-cut bonus"), Precision.Yield(9, EOreKind::Common, false, false), DamagedBefore);
    FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    const TArray<FForecastRock> Fresh{{800, 475, 0, 13, 9, false}};
    FProgress BeforePrecision = Precision;
    BeforePrecision.Acquired[71] = false;
    TestTrue(TEXT("autopilot forecasts clean-cut income"), ForecastOre(Precision, Context, Fresh) > ForecastOre(BeforePrecision, Context, Fresh));

    FProgress Stable;
    Stable.AddResources(1000000);
    TestTrue(TEXT("engine prerequisite I"), Stable.Purchase(9));
    TestTrue(TEXT("engine prerequisite II"), Stable.Purchase(18));
    const float SpeedBefore = Stable.SpeedMultiplier();
    const float BankBefore = ShipManeuverBank(20, .1f, 1280, Stable);
    TestTrue(TEXT("stabilizer can be purchased"), Stable.Purchase(77));
    TestTrue(TEXT("stabilizer reduces bank"), FMath::Abs(ShipManeuverBank(20, .1f, 1280, Stable)) < FMath::Abs(BankBefore));
    TestTrue(TEXT("stabilizer preserves manual and autopilot speed"), FMath::IsNearlyEqual(Stable.SpeedMultiplier(), SpeedBefore));
    TestTrue(TEXT("second stabilizer can be purchased"), Stable.Purchase(78));
    TestTrue(TEXT("third stabilizer can be purchased"), Stable.Purchase(79));
    TestTrue(TEXT("maximum bank reduction is bounded"), FMath::IsNearlyEqual(Stable.BankStability(), .6f, .001f));

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Stable, 120, 20, true, 2);
    FProgress Loaded; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("side branches survive save validation"), Save->Restore(Loaded, Time, Brightness, bAuto));
    TestTrue(TEXT("all stabilizer levels survive restore"), Loaded.Has(77) && Loaded.Has(78) && Loaded.Has(79));
    TestTrue(TEXT("stabilization survives restore"), FMath::IsNearlyEqual(Loaded.BankStability(), Stable.BankStability()));

    FProgress OnlySide;
    OnlySide.AddResources(100000);
    OnlySide.Acquired.Init(true, UpgradeCount);
    for (int32 I : {71, 72, 73}) OnlySide.Acquired[I] = false;
    const TArray<FForecastRock> Empty;
    const auto Choice = ChooseAutoUpgrade(OnlySide, Context, Empty);
    TestEqual(TEXT("autopilot can choose the last remaining side branch"), Choice.Next, 71);
    TestTrue(TEXT("autopilot side choice can be purchased"), OnlySide.Purchase(Choice.Next));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightJournalTest, "SpaceFlight.Progress.DiscoveryJournal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightJournalTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    TestEqual(TEXT("new flight starts without confirmed discoveries"), Progress.DiscoveryBits, 0u);
    Progress.AddOre(2, EOreKind::Common);
    Progress.AddOre(4, EOreKind::Blue);
    TestTrue(TEXT("common ore is recorded"), Progress.HasDiscovery(EDiscovery::CommonOre));
    TestTrue(TEXT("blue ore is recorded"), Progress.HasDiscovery(EDiscovery::BlueOre));
    TestFalse(TEXT("unknown event is not invented"), Progress.HasDiscovery(EDiscovery::MineralFlow));
    Progress.AddOre(4, EOreKind::Black);
    TestTrue(TEXT("black ore is recorded"), Progress.HasDiscovery(EDiscovery::BlackOre));
    TestTrue(TEXT("actual isotope conversion is recorded"), Progress.HasDiscovery(EDiscovery::Isotope));
    Progress.RecordDiscovery(EDiscovery::UnstableCrystal);
    Progress.RecordDiscovery(EDiscovery::UnstableCrystal);
    TestTrue(TEXT("event discoveries are idempotent"), Progress.HasDiscovery(EDiscovery::UnstableCrystal));
    TestTrue(TEXT("mineral-flow encounter maps to its own discovery"), DiscoveryForEvent(ERareEventKind::MineralFlow) == EDiscovery::MineralFlow);
    TestTrue(TEXT("crystal encounter maps to its own discovery"), DiscoveryForEvent(ERareEventKind::UnstableCrystal) == EDiscovery::UnstableCrystal);
    TestTrue(TEXT("deposit encounter maps to its own discovery"), DiscoveryForEvent(ERareEventKind::LargeDeposit) == EDiscovery::LargeDeposit);
    Progress.Acquired[0] = true; Progress.Acquired[20] = true; Progress.Acquired[3] = true;
    const auto Radar = JournalBranchStats(Progress, 0);
    TestEqual(TEXT("branch includes ordinary and micro upgrades"), Radar.Owned, 2);
    TestTrue(TEXT("branch total includes future upgrades"), Radar.Total > Radar.Owned);
    TestEqual(TEXT("other branches are counted separately"), JournalBranchStats(Progress, 1).Owned, 1);
    TestEqual(TEXT("unknown branch is empty"), JournalBranchStats(Progress, -1).Total, 0);

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Progress, 100, 20, true, 1);
    FProgress Loaded; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("new journal saves restore"), Save->Restore(Loaded, Time, Brightness, bAuto));
    TestEqual(TEXT("journal discoveries round-trip"), Loaded.DiscoveryBits, Progress.DiscoveryBits);
    TArray<uint8> Encoded;
    TestTrue(TEXT("journal serializes to save bytes"), UGameplayStatics::SaveGameToMemory(Save, Encoded));
    USpaceFlightSaveGame* Decoded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Encoded));
    TestNotNull(TEXT("serialized journal loads as a save object"), Decoded);
    if (Decoded)
    {
        TestTrue(TEXT("serialized journal restores"), Decoded->Restore(Loaded, Time, Brightness, bAuto));
        TestEqual(TEXT("serialized journal retains all discoveries"), Loaded.DiscoveryBits, Progress.DiscoveryBits);
    }
    Save->JournalSchema = 0; Save->JournalFlags = 0;
    TestTrue(TEXT("old save still restores"), Save->Restore(Loaded, Time, Brightness, bAuto));
    TestTrue(TEXT("old isotope conversion proves blue and black ore"), Loaded.HasDiscovery(EDiscovery::BlueOre) && Loaded.HasDiscovery(EDiscovery::BlackOre) && Loaded.HasDiscovery(EDiscovery::Isotope));
    TestFalse(TEXT("old save does not invent event history"), Loaded.HasDiscovery(EDiscovery::UnstableCrystal));
    TestFalse(TEXT("old save does not guess ordinary ore history"), Loaded.HasDiscovery(EDiscovery::CommonOre));
    Save->JournalSchema = 2;
    TestFalse(TEXT("future journal schema is not silently discarded"), Save->Restore(Loaded, Time, Brightness, bAuto));
    Save->JournalSchema = 1; Save->JournalFlags = 1 << 20;
    TestFalse(TEXT("unknown discovery bits are rejected"), Save->Restore(Loaded, Time, Brightness, bAuto));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightMineralEventTest, "SpaceFlight.Events.MineralFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightMineralEventTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress EventProgress;
    TestEqual(TEXT("new pilots retain familiar encounter rewards"), ScaledEventBonus(9, EventProgress), 9);
    EventProgress.TotalCollected = 100000;
    TestEqual(TEXT("encounter reward increases with lifetime collection"), ScaledEventBonus(9, EventProgress), 18);
    EventProgress.TotalCollected = 1000000;
    TestEqual(TEXT("encounter reward multiplier is bounded"), ScaledEventBonus(9, EventProgress), 45);
    TestEqual(TEXT("no claim never awards anything"), ScaledEventBonus(0, EventProgress), 0);
    FMineralEvent A, B;
    A.Initialize(1234); B.Initialize(1234);
    TestTrue(TEXT("initial waiting time is bounded"), A.Remaining >= 150 && A.Remaining <= 210);
    A.Advance(1000, true, 15);
    TestEqual(TEXT("blocked flight does not advance the event"), A.Remaining, B.Remaining);
    A.Advance(A.Remaining, false, 15); B.Advance(B.Remaining, false, 15);
    TestTrue(TEXT("warning precedes active flow"), A.Phase == EMineralPhase::Warning && A.Remaining == 8.0);
    A.Advance(8, false, 15); B.Advance(8, false, 15);
    TestTrue(TEXT("flow starts after warning"), A.Phase == EMineralPhase::Active && A.Remaining == 35.0);
    int32 Marked = 0, TotalBonus = 0;
    for (int32 Index = 0; Index < 15; ++Index)
    {
        TestEqual(TEXT("same seed marks the same asteroids"), A.IsMarked(Index, 15), B.IsMarked(Index, 15));
        if (A.IsMarked(Index, 15)) ++Marked;
        TotalBonus += A.ClaimBonus(Index, 15);
        TestEqual(TEXT("one slot cannot pay twice"), A.ClaimBonus(Index, 15), 0);
    }
    TestEqual(TEXT("exactly four slots are marked"), Marked, 4);
    TestEqual(TEXT("event bonus is bounded"), TotalBonus, 8);
    for (int32 Index = 0; Index < 12; ++Index)
        TestEqual(TEXT("changing sectors cannot grant a fifth bonus"), A.ClaimBonus(Index, 12), 0);
    A.Advance(35, false, 15);
    TestTrue(TEXT("flow ends with a long quiet period"), A.Phase == EMineralPhase::Waiting && A.Remaining >= 180 && A.Remaining <= 300);
    TestEqual(TEXT("no bonus outside an active flow"), A.ClaimBonus(0, 15), 0);
    FMineralEvent Fine, Jump;
    Fine.Initialize(991); Jump.Initialize(991);
    for (int32 I = 0; I < 10000; ++I) Fine.Advance(.1, false, 12);
    Jump.Advance(1000, false, 12);
    TestTrue(TEXT("scheduling is independent of frame step"), Fine.Phase == Jump.Phase && FMath::IsNearlyEqual(Fine.Remaining, Jump.Remaining, .001));
    TestEqual(TEXT("invalid asteroid cannot grant bonus"), Jump.ClaimBonus(-1, 12), 0);
    const double Before = Jump.Remaining;
    Jump.Advance(-1, false, 12);
    Jump.Advance(10, false, 0);
    TestEqual(TEXT("invalid time and empty sectors do not advance event"), Jump.Remaining, Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightUnstableCrystalTest, "SpaceFlight.Events.UnstableCrystal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightUnstableCrystalTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FMineralEvent Event;
    Event.Initialize(1984);
    Event.Advance(Event.Remaining, false, 15);
    TestTrue(TEXT("first event remains the familiar mineral flow"), Event.Kind == ERareEventKind::MineralFlow);
    Event.Advance(8, false, 15);
    Event.Advance(35, false, 15);
    int32 Flows = 0, Crystals = 0, Deposits = 0;
    for (int32 I = 0; I < 400; ++I)
    {
        Event.Advance(Event.Remaining, false, 15);
        TestTrue(TEXT("each event is announced before it starts"), Event.Phase == EMineralPhase::Warning && Event.Remaining == 8.0);
        if (Event.Kind == ERareEventKind::MineralFlow) ++Flows;
        else if (Event.Kind == ERareEventKind::UnstableCrystal) ++Crystals;
        else ++Deposits;
        Event.Advance(8, false, 15);
        if (Event.Kind == ERareEventKind::UnstableCrystal)
        {
            int32 Marked = 0, Index = INDEX_NONE;
            for (int32 J = 0; J < 15; ++J) if (Event.IsMarked(J, 15)) { ++Marked; Index = J; }
            TestEqual(TEXT("a crystal event marks exactly one asteroid"), Marked, 1);
            TestEqual(TEXT("early crystal payout is six"), Event.CurrentBonus(), 6);
            FMineralEvent Late = Event;
            Late.Advance(15, false, 15);
            TestEqual(TEXT("at fifteen seconds the bonus falls to three"), Late.CurrentBonus(), 3);
            TestEqual(TEXT("late hit receives the reduced bonus"), Late.ClaimBonus(Index, 15), 3);
            TestEqual(TEXT("early hit receives the larger bonus"), Event.ClaimBonus(Index, 15), 6);
            for (int32 J = 0; J < 12; ++J)
                TestEqual(TEXT("repeat hits and changing sector cannot pay again"), Event.ClaimBonus(J, 12), 0);
            Late.Advance(20, false, 15);
            TestEqual(TEXT("expired crystal pays nothing"), Late.ClaimBonus(Index, 15), 0);
        }
        Event.Advance(Event.Remaining, false, 15);
        TestTrue(TEXT("events return to a quiet period"), Event.Phase == EMineralPhase::Waiting && Event.Remaining >= 180 && Event.Remaining <= 300);
    }
    TestTrue(TEXT("all three event types recur"), Flows > 80 && Crystals > 80 && Deposits > 80);
    FMineralEvent Fine, Jump;
    Fine.Initialize(777); Jump.Initialize(777);
    for (int32 I = 0; I < 10000; ++I) Fine.Advance(.1, false, 12);
    Jump.Advance(1000, false, 12);
    TestTrue(TEXT("event type and schedule do not depend on frame step"), Fine.Kind == Jump.Kind && Fine.Phase == Jump.Phase && FMath::IsNearlyEqual(Fine.Remaining, Jump.Remaining, .001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightLargeDepositTest, "SpaceFlight.Events.LargeDeposit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightLargeDepositTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    constexpr int32 OriginalSizes[] = { 9, 18, 27, 9, 18, 27, 18 };
    for (int32 I = 0; I < 21; ++I)
        TestEqual(TEXT("ordinary asteroid size pattern remains unchanged"), AsteroidSizeForIndex(I), OriginalSizes[I % UE_ARRAY_COUNT(OriginalSizes)]);
    TestEqual(TEXT("largest ordinary asteroid still has three hit points"), AsteroidHitPoints(AsteroidSizeForIndex(2)), 3);
    for (int32 Count : { 12, 15 })
    {
        int32 Seen = 0;
        for (uint32 Seed = 0; Seed < 20; ++Seed)
        {
            FMineralEvent Event;
            Event.Initialize(Seed);
            for (int32 Round = 0; Round < 30; ++Round)
            {
                Event.Advance(Event.Remaining, false, Count);
                TestTrue(TEXT("every encounter is announced before activation"), Event.Phase == EMineralPhase::Warning);
                Event.Advance(8, false, Count);
                if (Event.Kind == ERareEventKind::LargeDeposit)
                {
                    ++Seen;
                    int32 Marked = 0, Index = INDEX_NONE;
                    for (int32 I = 0; I < Count; ++I) if (Event.IsMarked(I, Count)) { ++Marked; Index = I; }
                    TestEqual(TEXT("one deposit is marked"), Marked, 1);
                    TestEqual(TEXT("deposit belongs to existing largest asteroid size"), AsteroidSizeForIndex(Index), 27);
                    const int32 OtherCount = Count == 12 ? 15 : 12;
                    int32 OtherIndex = INDEX_NONE, OtherMarked = 0;
                    for (int32 I = 0; I < OtherCount; ++I) if (Event.IsMarked(I, OtherCount)) { ++OtherMarked; OtherIndex = I; }
                    TestEqual(TEXT("sector changes retain a single marked deposit"), OtherMarked, 1);
                    TestEqual(TEXT("sector changes retain a large target"), AsteroidSizeForIndex(OtherIndex), 27);
                    TestEqual(TEXT("deposit pays nine of its ore once"), Event.ClaimBonus(Index, Count), 9);
                    for (int32 I = 0; I < OtherCount; ++I)
                        TestEqual(TEXT("sector changes and respawns cannot duplicate a payout"), Event.ClaimBonus(I, OtherCount), 0);
                }
                Event.Advance(Event.Remaining, false, Count);
                TestTrue(TEXT("an event finishes before the next begins"), Event.Phase == EMineralPhase::Waiting);
            }
        }
        TestTrue(TEXT("deposits recur in both real sector sizes"), Seen > 80);
    }
    FMineralEvent Event;
    Event.Initialize(11);
    for (int32 I = 0; I < 100; ++I)
    {
        Event.Advance(Event.Remaining, false, 2);
        TestTrue(TEXT("no deposit is selected if a large rock cannot exist"), Event.Kind != ERareEventKind::LargeDeposit);
        Event.Advance(8 + 35, false, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightShieldTest, "SpaceFlight.Shield.ScheduleAndGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightShieldTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FShieldTimeline A, B;
    A.Initialize(12345, 0.0);
    B.Initialize(12345, 0.0);
    int32 Patterns[3] = {};
    double Previous = 0.0, MinInterval = 8.0, MaxInterval = 2.0;
    for (int32 I = 0; I < 500; ++I)
    {
        const double Next = A.NextStart;
        const double Interval = Next - Previous;
        MinInterval = FMath::Min(MinInterval, Interval);
        MaxInterval = FMath::Max(MaxInterval, Interval);
        TestTrue(TEXT("fluctuation starts stay within two to eight seconds"), Interval >= 2.0 && Interval <= 8.0);
        TestTrue(TEXT("scheduled fluctuation begins"), A.Update(Next));
        TestTrue(TEXT("same seed reproduces events"), B.Update(Next));
        TestEqual(TEXT("same seed selects the same pattern"), A.Current.Pattern, B.Current.Pattern);
        TestTrue(TEXT("same seed schedules the same next event"), FMath::IsNearlyEqual(A.NextStart, B.NextStart, 1.e-7));
        TestTrue(TEXT("pattern is in range"), A.Current.Pattern >= 0 && A.Current.Pattern < 3);
        if (A.Current.Pattern >= 0 && A.Current.Pattern < 3) ++Patterns[A.Current.Pattern];
        TestTrue(TEXT("fluctuation ends before another can start"), A.Current.Duration > 0 && A.Current.Duration < 2.0f);
        TestTrue(TEXT("fluctuation starts and ends softly"), ShieldEnvelope(A.Current, Next) < 0.001f &&
            ShieldEnvelope(A.Current, Next + A.Current.Duration) < 0.001f);
        TestTrue(TEXT("fluctuation reaches a visible peak"), ShieldEnvelope(A.Current, Next + A.Current.Duration * .5) > .95f);
        Previous = Next;
    }
    TestTrue(TEXT("all patterns recur over a long run"), Patterns[0] > 80 && Patterns[1] > 80 && Patterns[2] > 80);
    TestTrue(TEXT("intervals vary substantially"), MinInterval < 2.3 && MaxInterval > 7.7);
    FShieldTimeline Fine, Jump;
    Fine.Initialize(77, 0.0);
    Jump.Initialize(77, 0.0);
    for (int32 I = 0; I <= 1000; ++I) Fine.Update(I * .1);
    Jump.Update(100.0);
    TestTrue(TEXT("schedule is independent of frame sampling"), FMath::IsNearlyEqual(Fine.NextStart, Jump.NextStart, 1.e-6));
    TestEqual(TEXT("frame sampling selects the same event"), Fine.Current.Pattern, Jump.Current.Pattern);

    FVector2D APoint, BPoint;
    TestTrue(TEXT("crossing segment is clipped to the field"), ClipShieldLine(FVector2D(-2, 0), FVector2D(2, 0), APoint, BPoint));
    TestTrue(TEXT("clipped endpoints touch the circle"), APoint.Equals(FVector2D(-1, 0), .001f) && BPoint.Equals(FVector2D(1, 0), .001f));
    TestFalse(TEXT("outside segment is discarded"), ClipShieldLine(FVector2D(-2, 2), FVector2D(2, 2), APoint, BPoint));
    const TArray<FShieldSegment> Segments = BuildShieldSegments();
    TestTrue(TEXT("field uses a bounded, nonempty hexagonal mesh"), Segments.Num() >= 30 && Segments.Num() <= 220);
    for (const FShieldSegment& Segment : Segments)
        TestTrue(TEXT("mesh is clipped to the circular projection"), Segment.A.SizeSquared() <= 1.0001 && Segment.B.SizeSquared() <= 1.0001);
    if (!Segments.IsEmpty())
    {
        FShieldPulse Pulse;
        Pulse.Start = 1.0;
        Pulse.Duration = 1.0f;
        for (int32 Pattern = 0; Pattern < 3; ++Pattern)
        {
            Pulse.Pattern = Pattern;
            for (int32 I = 0; I < Segments.Num(); ++I)
            {
                const float Strength = ShieldSegmentStrength(Pulse, Segments[I], I, 1.5);
                TestTrue(TEXT("animated segment strength remains finite and bounded"), FMath::IsFinite(Strength) && Strength >= 0 && Strength <= 1);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTreePointerTest, "SpaceFlight.Tree.MouseDragAndClick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTreePointerTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    constexpr float W = 1280, H = 720;
    const FVector2D Empty(90, 140);
    const FVector2D Card = UpgradeScreenCenter(0, W, H);
    TestEqual(TEXT("chosen empty canvas point has no card"), UpgradeAtScreenPosition(Empty, W, H), INDEX_NONE);
    TestEqual(TEXT("card point addresses the expected upgrade"), UpgradeAtScreenPosition(Card, W, H), 0);
    FTreePointerGesture Gesture;
    FVector2D Pan = FVector2D::ZeroVector;
    TestTrue(TEXT("left button can begin on empty canvas"), Gesture.Begin(ETreePointerButton::Left, Empty, W, H, DefaultTreeZoom, Pan));
    Gesture.Update(Empty + FVector2D(2, 1), W, H, Pan);
    TestTrue(TEXT("small pointer jitter does not pan"), Pan.IsNearlyZero());
    TestEqual(TEXT("empty click never purchases a node"), Gesture.End(ETreePointerButton::Left, Empty + FVector2D(2, 1), W, H, DefaultTreeZoom, Pan), INDEX_NONE);

    TestTrue(TEXT("empty canvas can be dragged"), Gesture.Begin(ETreePointerButton::Left, Empty, W, H, DefaultTreeZoom, Pan));
    Gesture.Update(Empty + FVector2D(80, 40), W, H, Pan);
    TestTrue(TEXT("canvas follows left-button drag"), Pan.Equals(FVector2D(80, 40), .01f));
    TestEqual(TEXT("finishing canvas drag never purchases"), Gesture.End(ETreePointerButton::Left, Empty + FVector2D(80, 40), W, H, DefaultTreeZoom, Pan), INDEX_NONE);

    Pan = FVector2D::ZeroVector;
    TestTrue(TEXT("card click begins on a card"), Gesture.Begin(ETreePointerButton::Left, Card, W, H, DefaultTreeZoom, Pan));
    TestEqual(TEXT("a completed click selects the card"), Gesture.End(ETreePointerButton::Left, Card, W, H, DefaultTreeZoom, Pan), 0);
    TestTrue(TEXT("another card press begins"), Gesture.Begin(ETreePointerButton::Left, Card, W, H, DefaultTreeZoom, Pan));
    Gesture.Update(Card + FVector2D(40, 30), W, H, Pan);
    TestTrue(TEXT("dragging from a card does not pan"), Pan.IsNearlyZero());
    TestEqual(TEXT("moving a pressed card does not purchase it"), Gesture.End(ETreePointerButton::Left, Card, W, H, DefaultTreeZoom, Pan), INDEX_NONE);

    TestTrue(TEXT("middle button starts on a card"), Gesture.Begin(ETreePointerButton::Middle, Card, W, H, DefaultTreeZoom, Pan));
    Gesture.Update(Card + FVector2D(30, -25), W, H, Pan);
    TestTrue(TEXT("middle button pans even over a card"), Pan.Equals(FVector2D(30, -25), .01f));
    TestEqual(TEXT("middle release cannot purchase"), Gesture.End(ETreePointerButton::Middle, Card + FVector2D(30, -25), W, H, DefaultTreeZoom, Pan), INDEX_NONE);
    Pan = FVector2D::ZeroVector;
    TestTrue(TEXT("right button still begins a drag"), Gesture.Begin(ETreePointerButton::Right, Empty, W, H, DefaultTreeZoom, Pan));
    Gesture.Update(Empty + FVector2D(-40, 25), W, H, Pan);
    TestTrue(TEXT("right button still pans"), Pan.Equals(FVector2D(-40, 25), .01f));
    Gesture.Cancel();
    TestFalse(TEXT("closing tree cancels the active gesture"), Gesture.IsActive());
    Pan = FVector2D::ZeroVector;
    TestTrue(TEXT("small viewport accepts a drag"), Gesture.Begin(ETreePointerButton::Middle, FVector2D(320, 180), 640, 360, 1.0f, Pan));
    Gesture.Update(FVector2D(360, 200), 640, 360, Pan);
    TestTrue(TEXT("drag accounts for viewport scale, not zoom"), Pan.Equals(FVector2D(80, 40), .01f));
    TestEqual(TEXT("another button cannot finish the gesture"), Gesture.End(ETreePointerButton::Left, FVector2D(360, 200), 640, 360, 1.0f, Pan), INDEX_NONE);
    TestTrue(TEXT("gesture remains active after unrelated release"), Gesture.IsActive());
    Gesture.Cancel();
    TestFalse(TEXT("left press outside the tree canvas is ignored"), Gesture.Begin(ETreePointerButton::Left, FVector2D(640, 70), W, H, DefaultTreeZoom, Pan));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightCompactHudTest, "SpaceFlight.HUD.CompactLayoutAndPreference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightCompactHudTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestFalse(TEXT("zero-sized viewport has no valid layout"), MakeCompactHudLayout(0, 0).bValid);
    for (const FVector2D Viewport : { FVector2D(1280, 720), FVector2D(1024, 768),
        FVector2D(1920, 1080), FVector2D(3440, 1440), FVector2D(888, 500) })
    {
        const FCompactHudLayout Layout = MakeCompactHudLayout(Viewport.X, Viewport.Y);
        TestTrue(TEXT("viewport produces a valid layout"), Layout.bValid);
        const FHudRect Regions[] = { Layout.Status, Layout.Goal, Layout.Clock, Layout.Resources };
        for (int32 I = 0; I < UE_ARRAY_COUNT(Regions); ++I)
        {
            TestTrue(TEXT("permanent block stays inside viewport"), Regions[I].Fits(Viewport.X, Viewport.Y));
            for (int32 J = I + 1; J < UE_ARRAY_COUNT(Regions); ++J)
                TestFalse(TEXT("permanent blocks do not overlap"), Regions[I].Intersects(Regions[J]));
        }
        TestTrue(TEXT("left action panel stays inside viewport"), Layout.LeftPopup.Fits(Viewport.X, Viewport.Y));
        TestTrue(TEXT("right action panel stays inside viewport"), Layout.RightPopup.Fits(Viewport.X, Viewport.Y));
        TestFalse(TEXT("left action panel clears the objective"), Layout.LeftPopup.Intersects(Layout.Goal));
        TestFalse(TEXT("right action panel clears resources"), Layout.RightPopup.Intersects(Layout.Resources));
        if (Viewport.X == 1280 && Viewport.Y == 720)
        {
            float Area = 0;
            for (const FHudRect& Region : Regions) Area += Region.Area();
            TestTrue(TEXT("permanent compact HUD occupies at most eight percent"), Area <= Viewport.X * Viewport.Y * .08f);
        }
        if (Viewport.X == 888 && Viewport.Y == 500)
            TestTrue(TEXT("small windows keep a more readable UI scale"), Layout.Scale > Viewport.X / 1280.0f);
    }
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    TestFalse(TEXT("existing and new saves default to compact view"), Save->bDetailedHUD);
    Save->Capture(FProgress(), 0, 20, true, 1);
    Save->bDetailedHUD = true;
    const FString Slot = FString::Printf(TEXT("SpaceFlightHudTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("HUD preference writes to an isolated slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("HUD preference loads from disk"), Loaded);
    if (Loaded) TestTrue(TEXT("detailed HUD preference survives restart"), Loaded->bDetailedHUD);
    TestTrue(TEXT("isolated HUD test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSpecializationTest, "SpaceFlight.Specialization.EffectsAndSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSpecializationTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Scout;
    TestEqual(TEXT("new flights have no specialization"), Scout.Specialization, 0);
    TestTrue(TEXT("exploration is selectable without spending resources"), Scout.SetSpecialization(1, 0));
    TestTrue(TEXT("exploration adds five percentage points to rich ore"), FMath::IsNearlyEqual(Scout.RichOreChance(), .05f));
    TestFalse(TEXT("specialization cannot change before the cooldown"), Scout.SetSpecialization(2, 59.99f));
    TestEqual(TEXT("failed change keeps the chosen profile"), Scout.Specialization, 1);

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Scout, 10, 20, true, 1);
    const FString Slot = FString::Printf(TEXT("SpaceFlightSpecializationTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("specialization writes to an isolated slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("specialization save loads"), Loaded);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    if (Loaded)
    {
        TestTrue(TEXT("specialization save restores"), Loaded->Restore(Restored, Time, Brightness, bAuto));
        TestEqual(TEXT("profile is preserved"), Restored.Specialization, 1);
        TestEqual(TEXT("change cooldown is preserved"), Restored.SpecializationReadyAt, 60.0f);
        TestFalse(TEXT("reload does not bypass the cooldown"), Restored.SetSpecialization(2, 59.99f));
        TestTrue(TEXT("profile can change at the ready time"), Restored.SetSpecialization(2, 60.0f));
    }
    TestTrue(TEXT("isolated specialization slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    Save->Specialization = 9;
    TestFalse(TEXT("invalid profile is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->Specialization = 1;
    Save->SpecializationReadyAt = 500;
    TestFalse(TEXT("invalid future cooldown is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->SpecializationSchema = 0;
    TestTrue(TEXT("old saves without specializations restore"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("legacy profile is neutral"), Restored.Specialization, 0);

    FProgress Precision;
    TestTrue(TEXT("precision profile is selectable"), Precision.SetSpecialization(2, 0));
    TestEqual(TEXT("first-shot destruction earns one extra ore"), Precision.Yield(9, EOreKind::Common, false, true), 2);
    TestEqual(TEXT("already damaged asteroid does not earn the precision bonus"), Precision.Yield(18, EOreKind::Common, false, false), 3);
    TestEqual(TEXT("precision stacks with rich deposits"), Precision.Yield(18, EOreKind::Common, true, true), 7);
    const FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    const TArray<FForecastRock> Fresh{{800, 475, 0, 13, 9, false}};
    const TArray<FForecastRock> Damaged{{800, 475, 0, 13, 18, false, 1}};
    TestTrue(TEXT("autopilot forecasts the bonus on a first-shot kill"), ForecastOre(Precision, Context, Fresh) > ForecastOre(FProgress(), Context, Fresh));
    TestTrue(TEXT("autopilot does not forecast the bonus on a damaged rock"), FMath::IsNearlyEqual(ForecastOre(Precision, Context, Damaged), ForecastOre(FProgress(), Context, Damaged)));

    FProgress Logistics;
    TestTrue(TEXT("logistics is selectable"), Logistics.SetSpecialization(3, 0));
    TestEqual(TEXT("logistics reduces module cost"), Logistics.ProductionCost(), 2);
    TestEqual(TEXT("logistics adds one to completed goal rewards"), Logistics.ObjectiveReward(5), 6);
    TestEqual(TEXT("an incomplete goal cannot pay a bonus"), Logistics.ObjectiveReward(0), 0);
    Logistics.AddResources(2);
    TestTrue(TEXT("module accepts the reduced cost"), Logistics.ProduceRareOre(EOreKind::Blue, 0));
    TestEqual(TEXT("reduced recipe produces one blue ore"), Logistics.BlueOre, int64(1));
    TestEqual(TEXT("reduced recipe spends only two common ore"), Logistics.TotalSpent, int64(2));
    TestEqual(TEXT("logistics preserves resource accounting"), Logistics.Resources,
        Logistics.TotalCollected - Logistics.TotalSpent - Logistics.OreConverted);

    FProgress TemporaryScout;
    TemporaryScout.SetSpecialization(1, 0);
    TemporaryScout.Objective.Sequence = 7;
    TemporaryScout.Objective.Kind = 4;
    TemporaryScout.Objective.Progress = 1;
    TestTrue(TEXT("scout may switch while a rich-deposit objective is active"), TemporaryScout.SetSpecialization(2, 60));
    TestEqual(TEXT("objective remains achievable after losing the only rich chance"), TemporaryScout.Objective.Kind, 3);
    TestTrue(TEXT("partially completed objective retains proportional progress"), TemporaryScout.Objective.Progress > 0 && TemporaryScout.Objective.IsValid());

    FProgress TrainedScout;
    TrainedScout.SetSpecialization(1, 0);
    TrainedScout.Objective.Sequence = 7;
    TrainedScout.Objective.Kind = 4;
    TrainedScout.Objective.Progress = 1;
    for (int32 I = 0; I < UpgradeCount; ++I)
        if (Upgrade(I).RichOreChance > 0.0f) { TrainedScout.Acquired[I] = true; break; }
    TestTrue(TEXT("trained scout can change specialization"), TrainedScout.SetSpecialization(2, 60));
    TestEqual(TEXT("rich objective remains when research supplies rich chance"), TrainedScout.Objective.Kind, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightProductionModuleTest, "SpaceFlight.Production.ModuleRecipesAndSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightProductionModuleTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    P.SetRefiningMode(1);
    P.AddResources(9);
    TestFalse(TEXT("ordinary ore is not a production output"), P.ProduceRareOre(EOreKind::Common, 10.0f));
    TestTrue(TEXT("module can produce blue ore"), P.ProduceRareOre(EOreKind::Blue, 10.0f));
    TestEqual(TEXT("one blue ore was produced"), P.BlueOre, int64(1));
    TestEqual(TEXT("three common ore became one blue"), P.Resources, int64(7));
    TestEqual(TEXT("module becomes ready in three seconds"), P.ProductionReadyAt, 13.0f);
    TestFalse(TEXT("shared module cannot produce during its cooldown"), P.ProduceRareOre(EOreKind::Black, 12.99f));
    TestEqual(TEXT("failed order does not spend ore"), P.Resources, int64(7));
    TestEqual(TEXT("cooldown cannot create black ore"), P.BlackOre, int64(0));

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 10, 20, true, 1);
    const FString Slot = FString::Printf(TEXT("SpaceFlightModuleTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("module state writes to an isolated slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("module state loads from disk"), Loaded);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    if (Loaded)
    {
        TestTrue(TEXT("module save restores"), Loaded->Restore(Restored, Time, Brightness, bAuto));
        TestEqual(TEXT("remaining cooldown survives restart"), Restored.ProductionReadyAt, 13.0f);
        TestFalse(TEXT("reload does not bypass the cooldown"), Restored.ProduceRareOre(EOreKind::Black, 12.99f));
        TestTrue(TEXT("black recipe works at the exact ready time"), Restored.ProduceRareOre(EOreKind::Black, 13.0f));
        TestEqual(TEXT("one black ore was produced"), Restored.BlackOre, int64(1));
        TestEqual(TEXT("ore accounting remains conserved"), Restored.Resources,
            Restored.TotalCollected - Restored.TotalSpent - Restored.OreConverted);
    }
    TestTrue(TEXT("isolated module test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    Save->ProductionReadyAt = 500.0f;
    TestFalse(TEXT("implausible future cooldown is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->ProductionSchema = 0;
    TestTrue(TEXT("legacy saves without a module restore"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("legacy module is ready"), Restored.ProductionReadyAt, 0.0f);

    FProgress Tagged;
    Tagged.SetRefiningMode(1);
    Tagged.AddResources(2);
    Tagged.AddOre(10, EOreKind::Blue);
    TestFalse(TEXT("recipes cannot consume colored ore as common ore"), Tagged.ProduceRareOre(EOreKind::Black, 0));
    TestEqual(TEXT("colored reserve is left intact"), Tagged.BlueOre, int64(10));
    FProgress Auto;
    Auto.AddOre(1, EOreKind::Black);
    Auto.AddResources(3);
    TestTrue(TEXT("produced blue ore participates in normal isotope synthesis"), Auto.ProduceRareOre(EOreKind::Blue, 0));
    TestEqual(TEXT("one of each colored ore creates one isotope"), Auto.Isotope, int64(1));
    TestEqual(TEXT("combined recipe and isotope accounting balances"), Auto.Resources,
        Auto.TotalCollected - Auto.TotalSpent - Auto.OreConverted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRefiningTest, "SpaceFlight.Production.RefiningPolicyAndSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRefiningTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    TestEqual(TEXT("existing automatic policy remains the default"), P.RefiningMode, 0);
    TestTrue(TEXT("player can stop refining"), P.SetRefiningMode(1));
    P.AddOre(20, EOreKind::Blue);
    P.AddOre(20, EOreKind::Black);
    TestEqual(TEXT("stopped refinery keeps paired ore"), P.Isotope, int64(0));
    TestTrue(TEXT("player can select half of each reserve"), P.SetRefiningMode(3));
    TestEqual(TEXT("half of each current reserve refines one to one"), P.Isotope, int64(10));
    TestEqual(TEXT("half blue remains"), P.BlueOre, int64(10));
    TestEqual(TEXT("half black remains"), P.BlackOre, int64(10));
    for (int32 I = 0; I < 10; ++I) P.RefineIsotope();
    TestEqual(TEXT("repeat evaluation cannot consume the same quota twice"), P.Isotope, int64(10));
    TestFalse(TEXT("reselecting the active policy cannot recreate the quota"), P.SetRefiningMode(3));
    P.AddOre(1, EOreKind::Blue);
    P.AddOre(1, EOreKind::Black);
    TestEqual(TEXT("fractional allowances do not round up"), P.Isotope, int64(10));

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 100, 20, true, 1);
    const FString Slot = FString::Printf(TEXT("SpaceFlightRefiningTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("refining state writes to an isolated slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("refining state loads from disk"), Loaded);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    if (Loaded)
    {
        TestTrue(TEXT("refining save restores"), Loaded->Restore(Restored, Time, Brightness, bAuto));
        TestEqual(TEXT("chosen refining mode persists"), Restored.RefiningMode, 3);
        Restored.AddOre(1, EOreKind::Blue);
        Restored.AddOre(1, EOreKind::Black);
        TestEqual(TEXT("fractional quotas survive a restart"), Restored.Isotope, int64(11));
        TestEqual(TEXT("refining conservation holds"), Restored.Resources,
            Restored.TotalCollected - Restored.TotalSpent - Restored.OreConverted);
    }
    TestTrue(TEXT("isolated refining test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    Save->RefiningMode = 99;
    TestFalse(TEXT("invalid refining mode is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->RefiningSchema = 0;
    TestTrue(TEXT("old saves without refining settings restore"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("old saves use automatic policy"), Restored.RefiningMode, 0);
    Save->RefiningSchema = 1;
    Save->RefiningMode = 3;
    Save->BlueRefiningQuota = MAX_int64;
    TestFalse(TEXT("quota cannot exceed remaining blue ore"), Save->Restore(Restored, Time, Brightness, bAuto));

    for (int32 Mode = 2; Mode <= 5; ++Mode)
    {
        FProgress Rate;
        Rate.SetRefiningMode(1);
        Rate.AddOre(20, EOreKind::Blue);
        Rate.AddOre(20, EOreKind::Black);
        Rate.SetRefiningMode(Mode);
        TestEqual(TEXT("each percentage applies separately to both reserves"), Rate.Isotope, int64((Mode - 1) * 5));
    }
    FProgress Resumed;
    Resumed.SetRefiningMode(1);
    Resumed.AddOre(10, EOreKind::Blue);
    Resumed.AddOre(10, EOreKind::Black);
    Resumed.SetRefiningMode(0);
    TestEqual(TEXT("returning to automatic mode uses available pairs"), Resumed.Isotope, int64(10));

    FProgress Spending;
    Spending.SetRefiningMode(1);
    Spending.AddOre(20, EOreKind::Blue);
    Spending.AddOre(20, EOreKind::Black);
    Spending.SetRefiningMode(2);
    TestEqual(TEXT("quarter setting converts only a quarter"), Spending.Isotope, int64(5));
    TestTrue(TEXT("regular upgrades remain purchasable"), Spending.Purchase(0));
    Spending.AddOre(4, EOreKind::Blue);
    Spending.AddOre(4, EOreKind::Black);
    TestEqual(TEXT("new quarter allowances create exactly one isotope"), Spending.Isotope, int64(6));
    TestTrue(TEXT("both reserves remain nonnegative"), Spending.BlueOre >= 0 && Spending.BlackOre >= 0);
    TestEqual(TEXT("purchase and refining preserve all ore"), Spending.Resources,
        Spending.TotalCollected - Spending.TotalSpent - Spending.OreConverted);
    FProgress Reserved;
    Reserved.SetRefiningMode(5);
    Reserved.AddOre(20, EOreKind::Blue);
    TestTrue(TEXT("ordinary purchases may consume ore reserved for refining"), Reserved.Purchase(0));
    Reserved.AddOre(20, EOreKind::Black);
    TestEqual(TEXT("spent ore cannot be refined later"), Reserved.Isotope, int64(8));
    TestEqual(TEXT("remaining blue is exhausted exactly once"), Reserved.BlueOre, int64(0));
    FProgress ManualChoice;
    ManualChoice.SetRefiningMode(1);
    ManualChoice.AddOre(20, EOreKind::Blue);
    ManualChoice.AddOre(20, EOreKind::Black);
    const FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    const TArray<FForecastRock> NoRocks;
    const FAutoUpgradeChoice Choice = ChooseAutoUpgrade(ManualChoice, Context, NoRocks);
    TestTrue(TEXT("autopilot still finds an affordable ordinary improvement"), ManualChoice.CanPurchase(Choice.Next));
    TestEqual(TEXT("autopilot forecast does not change player's refining setting"), ManualChoice.RefiningMode, 1);
    TestEqual(TEXT("autopilot forecast does not force conversion"), ManualChoice.Isotope, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSectorTest, "SpaceFlight.Sectors.OpeningGenerationAndSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSectorTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("new flight opens the first sector"), UnlockedSector(0), 0);
    TestEqual(TEXT("second sector needs three completed goals"), UnlockedSector(3), 1);
    TestEqual(TEXT("third sector needs eight completed goals"), UnlockedSector(8), 2);
    TestEqual(TEXT("cycling never selects a locked sector"), NextSector(0, 0), 0);
    TestEqual(TEXT("cycling selects the next open sector"), NextSector(0, 3), 1);
    TestEqual(TEXT("cycling returns to the starting sector"), NextSector(2, 8), 0);
    TestEqual(TEXT("original sector preserves fifteen slots"), SectorAsteroidCount(0), 15);
    TestTrue(TEXT("blue sector is less dense"), SectorAsteroidCount(1) < SectorAsteroidCount(0));
    TestTrue(TEXT("blue sector is slower"), SectorSpeedMultiplier(1) < 1.0f);
    TestTrue(TEXT("black sector is faster"), SectorSpeedMultiplier(2) > 1.0f);
    int32 Kinds[3][3] = {};
    for (int32 Sector = 0; Sector < 3; ++Sector)
        for (int32 Pass = 0; Pass < 300; ++Pass)
            for (int32 Index = 0; Index < SectorAsteroidCount(Sector); ++Index)
            {
                const EOreKind Kind = SectorOreKind(Sector, 12345, Index, Pass, 0);
                TestTrue(TEXT("kind is reproducible"), Kind == SectorOreKind(Sector, 12345, Index, Pass, 0));
                ++Kinds[Sector][static_cast<int32>(Kind)];
                const float X = ChooseAsteroidSpawn(SectorSeed(12345, Sector), Index, Pass, 0, 1280, 1, 15).X;
                const float SafeX = AvoidShip(X, 400, 640, 400, 1280, 1, 15);
                TestTrue(TEXT("all sectors avoid the ship"), FMath::Abs(SafeX - 640.0f) >= 48.0f);
            }
    for (int32 Sector = 0; Sector < 3; ++Sector)
        for (int32 Kind = 0; Kind < 3; ++Kind)
            TestTrue(TEXT("each sector contains every ore type"), Kinds[Sector][Kind] > 0);
    TestTrue(TEXT("blue sector favours blue ore"), Kinds[1][1] > Kinds[1][0] && Kinds[1][1] > Kinds[1][2]);
    TestTrue(TEXT("black sector favours black ore"), Kinds[2][2] > Kinds[2][0] && Kinds[2][2] > Kinds[2][1]);

    FProgress P;
    P.Objective.Sequence = 8;
    P.CurrentSector = 2;
    P.WorldSeed = -123456;
    P.bHasWorldSeed = true;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 100, 20, true, 1);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    TestTrue(TEXT("sector state restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("selected sector persists"), Restored.CurrentSector, 2);
    TestEqual(TEXT("world seed persists"), Restored.WorldSeed, -123456);
    TestTrue(TEXT("world seed presence persists"), Restored.bHasWorldSeed);
    const FString Slot = FString::Printf(TEXT("SpaceFlightSectorTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("sector state writes to an isolated slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("sector state loads from disk"), Loaded);
    if (Loaded)
    {
        TestTrue(TEXT("sector disk save validates"), Loaded->Restore(Restored, Time, Brightness, bAuto));
        TestEqual(TEXT("disk save preserves selected sector"), Restored.CurrentSector, 2);
        TestEqual(TEXT("disk save preserves world seed"), Restored.WorldSeed, -123456);
    }
    TestTrue(TEXT("isolated sector test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    Save->SectorIndex = 3;
    TestFalse(TEXT("invalid sector is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->SectorIndex = 2;
    Save->ObjectiveSequence = 0;
    TestFalse(TEXT("locked sector is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->SectorSchema = 0;
    TestTrue(TEXT("legacy save without sectors restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("legacy save starts in original sector"), Restored.CurrentSector, 0);
    TestFalse(TEXT("legacy save receives a new seed on launch"), Restored.bHasWorldSeed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightObjectivesTest, "SpaceFlight.Progress.FlightObjectives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightObjectivesTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    TestEqual(TEXT("first goal counts destroyed asteroids"), P.Objective.Target(), 10);
    for (int32 I = 0; I < 9; ++I)
        TestEqual(TEXT("no reward before completion"), P.Objective.Record(EOreKind::Common, 3, false, false), 0);
    TestEqual(TEXT("partial progress counts kills"), P.Objective.Progress, 9);
    const int32 Reward = P.Objective.Record(EOreKind::Common, 3, false, false);
    TestTrue(TEXT("completion yields a modest reward"), Reward > 0 && Reward <= 5);
    TestEqual(TEXT("completion advances exactly once"), P.Objective.Sequence, 1);
    TestEqual(TEXT("next goal starts empty"), P.Objective.Progress, 0);
    TestEqual(TEXT("wrong ore does not advance blue objective"), P.Objective.Record(EOreKind::Black, 4, false, false), 0);
    TestEqual(TEXT("wrong ore is ignored"), P.Objective.Progress, 0);
    P.Objective.Record(EOreKind::Blue, 7, false, false);
    TestEqual(TEXT("blue ore counts actual mined quantity"), P.Objective.Progress, 7);

    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 100, 20, true, 1);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    TestTrue(TEXT("objective state restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("objective sequence survives save"), Restored.Objective.Sequence, 1);
    TestEqual(TEXT("partial objective survives save"), Restored.Objective.Progress, 7);
    const FString Slot = FString::Printf(TEXT("SpaceFlightObjectiveTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("objective writes to an isolated disk slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestNotNull(TEXT("objective save loads from disk"), Loaded);
    if (Loaded)
    {
        TestTrue(TEXT("objective disk save validates"), Loaded->Restore(Restored, Time, Brightness, bAuto));
        TestEqual(TEXT("disk save preserves partial objective"), Restored.Objective.Progress, 7);
    }
    TestTrue(TEXT("isolated objective test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    Save->ObjectiveProgress = MAX_int32;
    TestFalse(TEXT("invalid objective is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->ObjectiveSchema = 0;
    TestTrue(TEXT("legacy saves start a valid first objective"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("legacy objective starts at zero"), Restored.Objective.Sequence, 0);

    FFlightObjective Rich;
    Rich.Sequence = 6;
    Rich.Kind = 2;
    Rich.Progress = Rich.Target() - 1;
    Rich.Record(EOreKind::Black, 1, false, true);
    TestEqual(TEXT("rich objective appears only when unlocked"), Rich.Kind, 4);
    Rich.Record(EOreKind::Blue, 20, false, true);
    TestEqual(TEXT("ordinary deposits do not count as rich"), Rich.Progress, 0);
    Rich.Record(EOreKind::Common, 1, true, true);
    TestEqual(TEXT("rich deposit advances once"), Rich.Progress, 1);
    FFlightObjective Locked;
    Locked.Sequence = 6;
    Locked.Kind = 2;
    Locked.Progress = Locked.Target() - 1;
    Locked.Record(EOreKind::Black, 1, false, false);
    TestEqual(TEXT("locked rich goal falls back to ordinary ore"), Locked.Kind, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSaveIntegrityTest, "SpaceFlight.Progress.SaveIntegrity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSaveIntegrityTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(FProgress(), 10, 20, true, 1);
    TArray<uint8> Legacy, Sealed, Payload;
    TestTrue(TEXT("legacy save serializes"), UGameplayStatics::SaveGameToMemory(Save, Legacy));
    TestEqual(TEXT("legacy save is accepted without claiming a checksum"), static_cast<int32>(UnwrapSaveData(Legacy, Payload)), static_cast<int32>(ESaveEnvelope::Legacy));
    TestTrue(TEXT("legacy payload is preserved"), Payload == Legacy);
    TestTrue(TEXT("valid payload can be sealed"), SealSaveData(Legacy, Sealed));
    TestEqual(TEXT("sealed save verifies"), static_cast<int32>(UnwrapSaveData(Sealed, Payload)), static_cast<int32>(ESaveEnvelope::Verified));
    TestTrue(TEXT("sealed payload is preserved"), Payload == Legacy);
    TArray<uint8> Damaged = Sealed;
    Damaged[Damaged.Num() / 2] ^= 0x01;
    TestEqual(TEXT("changed payload is rejected before deserialization"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged[12] ^= 0x01;
    TestEqual(TEXT("changed checksum is rejected"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged[8] ^= 0x01;
    TestEqual(TEXT("changed length is rejected"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged.Add(0);
    TestEqual(TEXT("unexpected trailing data is rejected"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged.SetNum(Damaged.Num() - 1);
    TestEqual(TEXT("truncated sealed data is rejected"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged[4] = 2;
    TestEqual(TEXT("future envelope is distinguished and must not be overwritten"), static_cast<int32>(UnwrapSaveData(Damaged, Payload)), static_cast<int32>(ESaveEnvelope::Unsupported));
    const FString Slot = FString::Printf(TEXT("SpaceFlightIntegrityTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestEqual(TEXT("nonexistent isolated slot is missing"), static_cast<int32>(USpaceFlightSaveGame::InspectSlotPresence(Slot)), static_cast<int32>(ESaveSlotPresence::Missing));
    TestTrue(TEXT("sealed bytes write to isolated slot"), UGameplayStatics::SaveDataToSlot(Sealed, Slot, 0));
    TestEqual(TEXT("existing isolated slot is present"), static_cast<int32>(USpaceFlightSaveGame::InspectSlotPresence(Slot)), static_cast<int32>(ESaveSlotPresence::Present));
    TestNotNull(TEXT("sealed disk save loads"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot));
    Damaged = Sealed; Damaged[Damaged.Num() / 2] ^= 0x01;
    TestTrue(TEXT("corrupt bytes write only to isolated test slot"), UGameplayStatics::SaveDataToSlot(Damaged, Slot, 0));
    ESaveEnvelope State = ESaveEnvelope::Verified;
    TestNull(TEXT("corrupt disk save is not deserialized"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot, &State));
    TestEqual(TEXT("corrupt disk save is identified"), static_cast<int32>(State), static_cast<int32>(ESaveEnvelope::Corrupt));
    Damaged = Sealed; Damaged[4] = 2;
    TestTrue(TEXT("future bytes write only to isolated test slot"), UGameplayStatics::SaveDataToSlot(Damaged, Slot, 0));
    TestNull(TEXT("future envelope is not deserialized"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot, &State));
    TestEqual(TEXT("future disk save is identifiable for write protection"), static_cast<int32>(State), static_cast<int32>(ESaveEnvelope::Unsupported));
    TestTrue(TEXT("legacy bytes write only to isolated test slot"), UGameplayStatics::SaveDataToSlot(Legacy, Slot, 0));
    TestNotNull(TEXT("legacy disk save still loads"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot, &State));
    TestEqual(TEXT("legacy disk save is identified"), static_cast<int32>(State), static_cast<int32>(ESaveEnvelope::Legacy));
    TestEqual(TEXT("a loaded A slot is never chosen as the next target"), NextSaveSlot(0), 1);
    TestEqual(TEXT("a loaded B slot is never chosen as the next target"), NextSaveSlot(1), 0);
    const FString Stage = Slot + TEXT("Stage");
    TestFalse(TEXT("failed staging verification does not commit"), USpaceFlightSaveGame::WriteVerifiedSlot(Slot, Stage, Sealed, true));
    TArray<uint8> Existing;
    TestTrue(TEXT("target remains readable after failed verification"), UGameplayStatics::LoadDataFromSlot(Existing, Slot, 0));
    TestTrue(TEXT("failed verification leaves the previous target unchanged"), Existing == Legacy);
    TestFalse(TEXT("failed promotion does not commit"), USpaceFlightSaveGame::WriteVerifiedSlot(Slot, Stage, Sealed, false, true));
    TestTrue(TEXT("failed promotion leaves the previous target readable"), UGameplayStatics::LoadDataFromSlot(Existing, Slot, 0));
    TestTrue(TEXT("failed promotion preserves the previous target bytes"), Existing == Legacy);
    TestTrue(TEXT("verified stage commits to target"), USpaceFlightSaveGame::WriteVerifiedSlot(Slot, Stage, Sealed));
    TestTrue(TEXT("committed bytes are the checked bytes"), UGameplayStatics::LoadDataFromSlot(Existing, Slot, 0) && Existing == Sealed);
    TArray<uint8> OtherClass = Legacy;
    const ANSICHAR Name[] = "SpaceFlightSaveGame";
    int32 NameOffset = INDEX_NONE;
    for (int32 I = 0; I + static_cast<int32>(sizeof(Name)) <= OtherClass.Num(); ++I)
        if (FMemory::Memcmp(OtherClass.GetData() + I, Name, sizeof(Name) - 1) == 0) { NameOffset = I; break; }
    TestTrue(TEXT("serialized save contains its class name"), NameOffset != INDEX_NONE);
    if (NameOffset != INDEX_NONE)
    {
        OtherClass[NameOffset + sizeof(Name) - 2] = 'X';
        TArray<uint8> OtherSealed;
        TestTrue(TEXT("other class has a valid envelope"), SealSaveData(OtherClass, OtherSealed));
        TestTrue(TEXT("other class is written to an isolated slot"), UGameplayStatics::SaveDataToSlot(OtherSealed, Slot, 0));
        TestNull(TEXT("unrecognized save class does not load"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot, &State));
        TestEqual(TEXT("unrecognized checked save blocks writes"), static_cast<int32>(State), static_cast<int32>(ESaveEnvelope::Unsupported));
    }
    TestTrue(TEXT("isolated slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
    TestNull(TEXT("missing slot cannot be read"), USpaceFlightSaveGame::LoadWithHeaderCheck(Slot, &State));
    TestEqual(TEXT("read failure is distinguished from damaged data"), static_cast<int32>(State), static_cast<int32>(ESaveEnvelope::Unreadable));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSaveRoundTripTest, "SpaceFlight.Progress.SaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSaveRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Original;
    Original.AddResources(Upgrade(0).Cost + 100);
    TestTrue(TEXT("sample radar purchase succeeds"), Original.Purchase(0));
    Original.AddOre(20, EOreKind::Blue);
    Original.AddOre(10, EOreKind::Black);
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(Original, 7321.5f, 30, false, 42);
    FProgress Restored;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = true;
    TestTrue(TEXT("valid state restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestTrue(TEXT("purchased nodes restore"), Restored.Has(0));
    TestEqual(TEXT("ore balance restores"), Restored.Resources, Original.Resources);
    TestEqual(TEXT("colored reserve restores"), Restored.BlueOre, Original.BlueOre);
    TestEqual(TEXT("isotope restores"), Restored.Isotope, Original.Isotope);
    TestEqual(TEXT("total spending restores"), Restored.TotalSpent, Original.TotalSpent);
    TestEqual(TEXT("elapsed play time restores"), Time, 7321.5f);
    TestEqual(TEXT("brightness restores"), Brightness, 30);
    TestFalse(TEXT("manual mode restores"), bAuto);

    const FString Slot = FString::Printf(TEXT("SpaceFlightSaveTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("state writes to an isolated test slot"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
    USpaceFlightSaveGame* Loaded = USpaceFlightSaveGame::LoadWithHeaderCheck(Slot);
    TestNotNull(TEXT("state loads from disk through the header check"), Loaded);
    if (Loaded) TestTrue(TEXT("loaded state validates"), Loaded->Restore(Restored, Time, Brightness, bAuto));
    TArray<uint8> ValidBytes;
    TestTrue(TEXT("valid save serializes to memory"), UGameplayStatics::SaveGameToMemory(Save, ValidBytes));
    TestTrue(TEXT("valid save header is accepted"), HasUsableSaveHeader(ValidBytes));
    const TArray<uint8> Truncated{ 0x47, 0x56, 0x41, 0x53 };
    TestFalse(TEXT("truncated versioned header is rejected"), HasUsableSaveHeader(Truncated));
    TArray<uint8> InvalidBytes;
    InvalidBytes.Init(0, 64);
    TestFalse(TEXT("unrecognized header is rejected"), HasUsableSaveHeader(InvalidBytes));
    const FString BadSlot = FString::Printf(TEXT("SpaceFlightBadHeaderTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("truncated input writes to an isolated slot"), UGameplayStatics::SaveDataToSlot(Truncated, BadSlot, 0));
    TestNull(TEXT("safe slot loader rejects truncated data without deserializing it"), USpaceFlightSaveGame::LoadWithHeaderCheck(BadSlot));
    TestTrue(TEXT("isolated bad-header slot is removed"), UGameplayStatics::DeleteGameInSlot(BadSlot, 0));
    TestTrue(TEXT("isolated test slot is removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));

    Save->Resources64 += 1;
    Restored.AddResources(5);
    const int64 Before = Restored.Resources;
    TestFalse(TEXT("corrupt resource accounting is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("invalid state cannot partially overwrite active progress"), Restored.Resources, Before);
    Save->Capture(Original, 100, 20, true, 43);
    Save->AcquiredIndices.Add(2);
    TestFalse(TEXT("missing prerequisites are rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->Capture(Original, 100, 20, true, 44);
    Save->AcquiredIndices.Add(0);
    TestFalse(TEXT("duplicate node indices are rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    USpaceFlightSaveGame* Backup = NewObject<USpaceFlightSaveGame>();
    Backup->Capture(Original, 95, 20, true, 41);
    TestTrue(TEXT("valid backup wins over a corrupt newer save"), USpaceFlightSaveGame::NewestValid(Save, Backup) == Backup);
    Save->Capture(Original, 100, 20, true, 44);
    TestTrue(TEXT("newest valid save wins"), USpaceFlightSaveGame::NewestValid(Save, Backup) == Save);
    TestFalse(TEXT("stress mode cannot discard progress after a save failure"), CanEnterStressMode(true, false, true));
    TestFalse(TEXT("stress mode cannot hide a blocked save"), CanEnterStressMode(true, true, false));
    TestTrue(TEXT("stress mode starts after a successful save"), CanEnterStressMode(true, false, false));
    TestTrue(TEXT("isolated no-save benchmarks can still run"), CanEnterStressMode(false, false, false));
    TestFalse(TEXT("blocked saves cannot be deleted by reset"), CanResetProgress(true, false, true));
    TestFalse(TEXT("running game cannot reset"), CanResetProgress(false, false, false));
    TestFalse(TEXT("diagnostic stress mode cannot reset"), CanResetProgress(true, true, false));
    TestTrue(TEXT("a paused game with healthy saves can reset"), CanResetProgress(true, false, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSoundPolicyTest, "SpaceFlight.Audio.EventPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSoundPolicyTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestTrue(TEXT("nonfatal damage has an impact sound"), SoundForImpact(false, 27) == ESoundEvent::Impact);
    TestTrue(TEXT("small destruction uses the light explosion"), SoundForImpact(true, 9) == ESoundEvent::ExplosionSmall);
    TestTrue(TEXT("medium destruction uses the medium explosion"), SoundForImpact(true, 18) == ESoundEvent::ExplosionMedium);
    TestTrue(TEXT("large destruction uses the heavy explosion"), SoundForImpact(true, 27) == ESoundEvent::ExplosionLarge);
    FSoundGate Gate;
    TestTrue(TEXT("first shot is audible"), Gate.Allow(ESoundEvent::Laser, 10.0));
    TestFalse(TEXT("rapid shots are limited"), Gate.Allow(ESoundEvent::Laser, 10.04));
    TestTrue(TEXT("a shot becomes audible after cooldown"), Gate.Allow(ESoundEvent::Laser, 10.8));
    TestTrue(TEXT("impact is independent from shot"), Gate.Allow(ESoundEvent::Impact, 10.8));
    TestFalse(TEXT("simultaneous explosions share the impact limit"), Gate.Allow(ESoundEvent::ExplosionLarge, 10.81));
    TestTrue(TEXT("player feedback remains audible"), Gate.Allow(ESoundEvent::TreeOpen, 10.81));
    TestTrue(TEXT("pause feedback is not blocked by combat"), Gate.Allow(ESoundEvent::Pause, 10.81));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSoundAssetTest, "SpaceFlight.Audio.ImportedAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSoundAssetTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 I = 0; I < static_cast<int32>(ESoundEvent::Count); ++I)
    {
        const ESoundEvent Event = static_cast<ESoundEvent>(I);
        const FString Path = SoundAssetPath(Event);
        USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Path);
        TestNotNull(*FString::Printf(TEXT("sound exists: %s"), *Path), Wave);
        if (!Wave) continue;
        TestTrue(*FString::Printf(TEXT("sound has a duration: %s"), *Path), Wave->Duration > 0.02f);
        if (Event == ESoundEvent::Engine) TestTrue(TEXT("engine wave is looped"), Wave->bLooping);
        else TestFalse(*FString::Printf(TEXT("effect does not loop: %s"), *Path), Wave->bLooping);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAudioVarietyTest, "SpaceFlight.Audio.VarietyAndRestraint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAudioVarietyTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FSoundGate Gate;
    TestTrue(TEXT("first laser can sound"), Gate.Allow(ESoundEvent::Laser, 20.0));
    TestFalse(TEXT("repeated laser within half a second is suppressed"), Gate.Allow(ESoundEvent::Laser, 20.5));
    TestTrue(TEXT("laser can return after its quieter cadence"), Gate.Allow(ESoundEvent::Laser, 20.8));
    TestTrue(TEXT("first isotope cue sounds"), Gate.Allow(ESoundEvent::Isotope, 20.0));
    TestFalse(TEXT("isotope cues do not repeat every second"), Gate.Allow(ESoundEvent::Isotope, 21.0));
    TestTrue(TEXT("manual upgrade remains immediately audible"), Gate.Allow(ESoundEvent::Upgrade, 20.5));
    TestTrue(TEXT("frequent combat is quieter than an upgrade"), EventGain(ESoundEvent::Laser) < EventGain(ESoundEvent::Upgrade));

    FSoundVariety Variety;
    for (ESoundEvent Event : { ESoundEvent::Laser, ESoundEvent::Impact, ESoundEvent::ExplosionSmall,
        ESoundEvent::ExplosionMedium, ESoundEvent::ExplosionLarge })
    {
        int32 Previous = INDEX_NONE;
        for (int32 I = 0; I < 12; ++I)
        {
            const int32 Variant = Variety.Next(Event, I);
            TestTrue(TEXT("combat variant is within the pool"), Variant >= 0 && Variant < 3);
            TestTrue(TEXT("adjacent combat variants never repeat"), Variant != Previous);
            Previous = Variant;
        }
        for (int32 Variant = 1; Variant < 3; ++Variant)
        {
            const FString Path = SoundVariantAssetPath(Event, Variant);
            TestNotNull(*FString::Printf(TEXT("combat variant is imported: %s"), *Path),
                LoadObject<USoundWave>(nullptr, *Path));
        }
    }
    TestEqual(TEXT("single-variant interface cue remains unchanged"), Variety.Next(ESoundEvent::Pause, 1), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightStarBrightnessTest, "SpaceFlight.Flight.StarBrightness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightStarBrightnessTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FStarBrightness Brightness;
    const FLinearColor Background(0.006f, 0.012f, 0.035f);
    const FLinearColor Star(0.13f, 0.16f, 0.20f);
    TestEqual(TEXT("default brightness is the preferred value"), Brightness.Value, 20);
    TestTrue(TEXT("default dims the star"), Brightness.Apply(Background, Star).R < Star.R);
    Brightness.Adjust(-1);
    TestEqual(TEXT("one key press decreases by ten"), Brightness.Value, 10);
    TestTrue(TEXT("decrease dims the star"), Brightness.Apply(Background, Star).R < Star.R);
    for (int32 I = 0; I < 30; ++I) Brightness.Adjust(-1);
    TestEqual(TEXT("minimum brightness is zero"), Brightness.Value, 0);
    TestTrue(TEXT("zero blends star into the unchanged background"), Brightness.Apply(Background, Star).Equals(Background));
    for (int32 I = 0; I < 10; ++I) Brightness.Adjust(1);
    TestTrue(TEXT("100 preserves the original star color"), Brightness.Apply(Background, Star).Equals(Star));
    for (int32 I = 0; I < 30; ++I) Brightness.Adjust(1);
    TestEqual(TEXT("maximum brightness is 200"), Brightness.Value, 200);
    TestTrue(TEXT("increase brightens the star"), Brightness.Apply(Background, Star).R > Star.R);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightBlueAsteroidTest, "SpaceFlight.Asteroids.BlueRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightBlueAsteroidTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestTrue(TEXT("new family is present among the permanent asteroids"), IsBlueAsteroid(2));
    TestTrue(TEXT("blue family includes small and medium slots"), IsBlueAsteroid(3) && IsBlueAsteroid(6));
    TestFalse(TEXT("crystal asteroid remains a separate family"), IsBlueAsteroid(0));
    TestFalse(TEXT("warm asteroid remains a separate family"), IsBlueAsteroid(1));
    const FBlueAsteroidMotion A = BlueAsteroidMotion(1234, 2, 0, 0);
    const FBlueAsteroidMotion Repeat = BlueAsteroidMotion(1234, 2, 0, 0);
    const FBlueAsteroidMotion B = BlueAsteroidMotion(1234, 3, 0, 0);
    const FBlueAsteroidMotion Respawn = BlueAsteroidMotion(1234, 2, 1, 0);
    TestEqual(TEXT("axis angle stays stable for the same asteroid"), A.AxisTilt, Repeat.AxisTilt);
    TestTrue(TEXT("different asteroids can have different tilted axes"),
        FMath::Abs(A.AxisTilt - B.AxisTilt) > 0.01f);
    TestTrue(TEXT("rotation has a nonzero speed"), FMath::Abs(A.AngularSpeed) > 0.01f);
    TestTrue(TEXT("a new pass can have a different axis"), FMath::Abs(A.AxisTilt - Respawn.AxisTilt) > 0.01f);
    TestTrue(TEXT("center of the asteroid is visible"), SampleBlueAsteroid(0, 0, 0, 0, 0).A > 0);
    TestEqual(TEXT("space outside the asteroid stays transparent"), SampleBlueAsteroid(1, 1, 0, 0, 0).A, static_cast<uint8>(0));
    int32 OppositeSidePixels = 0, SilhouetteChanges = 0, DamageChanges = 0, LoopChanges = 0;
    for (int32 Y = 0; Y < 32; ++Y) for (int32 X = 0; X < 32; ++X)
    {
        const float U = (X + 0.5f - 16.0f) / 16.0f;
        const float V = (Y + 0.5f - 16.0f) / 16.0f;
        const FColor Front = SampleBlueAsteroid(U, V, 0, 0, 0);
        const FColor Back = SampleBlueAsteroid(U, V, PI, 0, 0);
        const FColor Quarter = SampleBlueAsteroid(U, V, PI * 0.5f, 0, 0);
        const FColor Damaged = SampleBlueAsteroid(U, V, 0, 0, 2);
        const FColor Loop = SampleBlueAsteroid(U, V, 2 * PI, 0, 0);
        if (Front.A && Back.A && Front != Back) ++OppositeSidePixels;
        if ((Front.A > 0) != (Quarter.A > 0)) ++SilhouetteChanges;
        if (Front.A && Front != Damaged) ++DamageChanges;
        if (Front != Loop) ++LoopChanges;
    }
    TestTrue(TEXT("opposite side reveals different surface details"), OppositeSidePixels > 40);
    TestTrue(TEXT("3D turn changes the silhouette"), SilhouetteChanges > 15);
    TestTrue(TEXT("damage appears on the 3D surface"), DamageChanges > 3);
    TestTrue(TEXT("full rotation returns to the same side without a seam"), LoopChanges < 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightShipVisualsTest, "SpaceFlight.Flight.ShipAppearanceAndBank",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightShipVisualsTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress P;
    P.AddResources(5000);
    auto Visual = SpaceFlight::DescribeShipAppearance(P);
    TestTrue(TEXT("baseline ship has no installed equipment"), Visual.RadarLevel == 0 && Visual.CollectorLevel == 0 &&
        Visual.CoolingLevel == 0 && !Visual.bLeftLaser && !Visual.bRightLaser && !Visual.bEngineUpgrade);
    for (int32 I = 0; I < 3; ++I)
    {
        TestTrue(TEXT("radar stage purchased"), P.Purchase(I));
        TestEqual(TEXT("each radar stage adds an appearance level"), SpaceFlight::DescribeShipAppearance(P).RadarLevel, I + 1);
    }
    for (int32 I = 3; I < 5; ++I)
    {
        TestTrue(TEXT("collector stage purchased"), P.Purchase(I));
        TestEqual(TEXT("each collector stage adds an appearance level"), SpaceFlight::DescribeShipAppearance(P).CollectorLevel, I - 2);
    }
    TestTrue(TEXT("left laser purchased"), P.Purchase(5));
    Visual = SpaceFlight::DescribeShipAppearance(P);
    TestTrue(TEXT("left laser mount appears independently"), Visual.bLeftLaser && !Visual.bRightLaser);
    TestTrue(TEXT("right laser purchased"), P.Purchase(6));
    Visual = SpaceFlight::DescribeShipAppearance(P);
    TestTrue(TEXT("right laser mount appears"), Visual.bLeftLaser && Visual.bRightLaser);
    for (int32 I = 7; I < 9; ++I)
    {
        TestTrue(TEXT("cooling stage purchased"), P.Purchase(I));
        TestEqual(TEXT("each cooling stage adds an appearance level"), SpaceFlight::DescribeShipAppearance(P).CoolingLevel, I - 6);
    }
    TestTrue(TEXT("engine upgrade purchased"), P.Purchase(9));
    TestTrue(TEXT("engine upgrade changes exhaust appearance"), SpaceFlight::DescribeShipAppearance(P).bEngineUpgrade);

    TestEqual(TEXT("stationary ship targets zero bank"), SpaceFlight::ManeuverBankTarget(0.0f, 0.1f, 1280.0f, 1.0f), 0.0f);
    TestTrue(TEXT("right movement banks right"), SpaceFlight::ManeuverBankTarget(10.0f, 0.1f, 1280.0f, 1.0f) > 0.0f);
    TestTrue(TEXT("left movement banks left"), SpaceFlight::ManeuverBankTarget(-10.0f, 0.1f, 1280.0f, 1.0f) < 0.0f);
    TestEqual(TEXT("bank is limited"), SpaceFlight::ManeuverBankTarget(1000.0f, 0.1f, 1280.0f, 1.0f), 1.0f);
    TestEqual(TEXT("zero delta has no bank target"), SpaceFlight::ManeuverBankTarget(1.0f, 0.0f, 1280.0f, 1.0f), 0.0f);
    const FVector2D Pivot = SpaceFlight::ShipPoint(28, 29, 640, 100, 1, 1);
    TestTrue(TEXT("ship rotates around its center"), Pivot.Equals(FVector2D(640, 129)));
    TestTrue(TEXT("nose leans right while banking right"), SpaceFlight::ShipPoint(28, 4, 640, 100, 1, 1).X > 640.0);
    TestTrue(TEXT("nose leans left while banking left"), SpaceFlight::ShipPoint(28, 4, 640, 100, 1, -1).X < 640.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightVolleyTest, "SpaceFlight.Laser.MultipleDistinctTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightVolleyTest::RunTest(const FString& Parameters)
{
    const FVector2D Origin(640.0, 500.0);
    TArray<SpaceFlight::FLaserCandidate, TInlineAllocator<3>> Targets;
    auto Add = [&](int32 Id, double X, double Y, int32 Guns)
    {
        SpaceFlight::AddLaserCandidate(Targets, { Id, 0, 12, FVector2D(X, Y), FLinearColor::White },
            Origin, 720.0f, 220.0f, Guns);
    };
    Add(0, 540.0, 500.0, 3);
    Add(1, 690.0, 500.0, 3);
    Add(2, 640.0, 600.0, 3);
    Add(3, 660.0, 500.0, 3);
    Add(3, 660.0, 500.0, 3);
    Add(4, 861.0, 500.0, 3);
    Add(5, 640.0, -1.0, 3);
    TestEqual(TEXT("three lasers select three distinct in-range targets"), Targets.Num(), 3);
    if (Targets.Num() == 3)
    {
        TestEqual(TEXT("closest target is first"), Targets[0].Index, 3);
        TestEqual(TEXT("right target is second"), Targets[1].Index, 1);
        TestTrue(TEXT("third target is distinct"), Targets[2].Index == 0 || Targets[2].Index == 2);
    }
    Targets.Reset();
    Add(0, 540.0, 500.0, 1);
    Add(1, 690.0, 500.0, 1);
    TestEqual(TEXT("one laser selects only one target"), Targets.Num(), 1);
    if (Targets.Num() == 1) TestEqual(TEXT("single laser selects nearest"), Targets[0].Index, 1);
    Targets.Reset();
    Add(8, 860.0, 500.0, 2);
    Add(9, 420.0, 500.0, 2);
    TestEqual(TEXT("both sides at the boundary can be hit"), Targets.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightProgressTest, "SpaceFlight.Progress.PurchasesAndEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightProgressTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress P;
    TestEqual(TEXT("starting laser count"), P.Lasers(), 1);
    TestEqual(TEXT("starting range"), P.Range(), 220.0f);
    TestFalse(TEXT("cannot purchase without resources"), P.Purchase(0));
    P.AddResources(5000);
    TestFalse(TEXT("cannot bypass radar prerequisite"), P.Purchase(2));
    TestFalse(TEXT("cannot bypass wing prerequisite"), P.Purchase(6));
    TestFalse(TEXT("cannot bypass collector prerequisite"), P.Purchase(4));
    TestTrue(TEXT("first radar bought"), P.Purchase(0));
    TestEqual(TEXT("radar cost paid"), P.Resources, int64(4988));
    TestEqual(TEXT("first radar changes range"), P.Range(), 235.0f);
    TestFalse(TEXT("no duplicate purchases"), P.Purchase(0));
    TestEqual(TEXT("duplicate does not spend ore"), P.Resources, int64(4988));
    TestTrue(TEXT("second radar bought"), P.Purchase(1));
    TestTrue(TEXT("third radar bought"), P.Purchase(2));
    TestEqual(TEXT("first three radar stages increase range moderately"), P.Range(), 280.0f);
    TestTrue(TEXT("left wing laser bought"), P.Purchase(5));
    TestEqual(TEXT("two lasers"), P.Lasers(), 2);
    TestTrue(TEXT("right wing laser bought"), P.Purchase(6));
    TestEqual(TEXT("at most three lasers"), P.Lasers(), 3);
    TestTrue(TEXT("cooling first stage"), P.Purchase(7));
    TestTrue(TEXT("cooling second stage"), P.Purchase(8));
    TestTrue(TEXT("cooling reduces interval"), FMath::IsNearlyEqual(P.Cooldown(), 0.62f, 0.0001f));
    TestTrue(TEXT("collector first stage"), P.Purchase(3));
    TestTrue(TEXT("collector second stage"), P.Purchase(4));
    TestEqual(TEXT("collector increases large asteroid ore"), P.Yield(27), 7);
    TestTrue(TEXT("engine upgrade bought"), P.Purchase(9));
    TestTrue(TEXT("engine improves speed moderately"), FMath::IsNearlyEqual(P.SpeedMultiplier(), 1.08f));
    TestFalse(TEXT("invalid upgrade rejected"), P.Purchase(SpaceFlight::UpgradeCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpandedTreeTest, "SpaceFlight.Progress.ExpandedTree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpandedTreeTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("tree includes the ricochet upgrade"), UpgradeCount, 81);
    TestEqual(TEXT("normal game has fifteen asteroid slots"), NormalAsteroidCount, 15);
    FProgress Progress;
    Progress.AddResources(1000000);
    for (int32 I = 0; I < 10; ++I) TestTrue(TEXT("original upgrade remains available"), Progress.Purchase(I));
    TestTrue(TEXT("radar branch continues beyond its original final node"), Progress.Purchase(10));
    for (int32 I = 11; I < 20; ++I) TestTrue(TEXT("new normal upgrades are purchasable"), Progress.Purchase(I));
    TestEqual(TEXT("new laser upgrade increases shot damage"), Progress.LaserDamage(), 2);
    TestFalse(TEXT("micro upgrade requires isotope, not ordinary ore"), Progress.Purchase(20));
    Progress.AddOre(37, EOreKind::Blue);
    TestEqual(TEXT("blue ore alone cannot create isotope"), Progress.Isotope, int64(0));
    Progress.AddOre(35, EOreKind::Black);
    TestEqual(TEXT("one blue and one black ore form one isotope"), Progress.Isotope, int64(35));
    TestEqual(TEXT("unpaired blue ore remains available"), Progress.BlueOre, int64(2));
    TestEqual(TEXT("all paired black ore was consumed"), Progress.BlackOre, int64(0));
    TestEqual(TEXT("conversion consumes two ore per isotope"), Progress.OreConverted, int64(70));
    TestTrue(TEXT("isotope buys its own micro upgrade"), Progress.Purchase(20));
    TestEqual(TEXT("micro purchase consumes isotope"), Progress.Isotope, int64(0));
    TestEqual(TEXT("ore accounting includes conversion"), Progress.Resources,
        Progress.TotalCollected - Progress.TotalSpent - Progress.OreConverted);

    FProgress Tagged;
    Tagged.AddOre(Upgrade(0).Cost + 1, EOreKind::Blue);
    TestTrue(TEXT("ordinary upgrade may spend colored ore"), Tagged.Purchase(0));
    TestEqual(TEXT("spent colored ore is removed from its reserve"), Tagged.BlueOre, int64(1));
    Tagged.AddOre(3, EOreKind::Black);
    TestEqual(TEXT("spent ore cannot be converted twice"), Tagged.Isotope, int64(1));
    TestEqual(TEXT("unpaired black ore remains"), Tagged.BlackOre, int64(2));
    TestEqual(TEXT("accounting remains balanced"), Tagged.Resources,
        Tagged.TotalCollected - Tagged.TotalSpent - Tagged.OreConverted);

    FProgress Reserve;
    Reserve.AddResources(100000);
    Reserve.AddOre(1000, EOreKind::Blue);
    Reserve.AddOre(1000, EOreKind::Black);
    int32 EarlyMicroCost = 0;
    for (int32 I = 20; I < 25; ++I) EarlyMicroCost += Upgrade(I).Cost;
    TestEqual(TEXT("isotope reserve covers early micro upgrades without consuming all future ore"), Reserve.Isotope, int64(EarlyMicroCost));
    TestTrue(TEXT("extra colored ore remains available for ordinary upgrades"), Reserve.BlueOre > 0 && Reserve.BlackOre > 0);
    for (int32 I : {0, 1, 2, 10}) TestTrue(TEXT("radar branch can unlock its next micro"), Reserve.Purchase(I));
    TestEqual(TEXT("unlocking a later micro automatically refines retained ore"), Reserve.Isotope, int64(1000));
    TestEqual(TEXT("refining retained ore preserves accounting"), Reserve.Resources,
        Reserve.TotalCollected - Reserve.TotalSpent - Reserve.OreConverted);
    USpaceFlightSaveGame* ReserveSave = NewObject<USpaceFlightSaveGame>();
    ReserveSave->Capture(Reserve, 600, 20, true, 1);
    FProgress ReloadedReserve;
    float ReloadedTime = 0.0f;
    int32 ReloadedBrightness = 0;
    bool bReloadedAuto = false;
    TestTrue(TEXT("newly refined isotope is safe to save and restore"),
        ReserveSave->Restore(ReloadedReserve, ReloadedTime, ReloadedBrightness, bReloadedAuto));
    TestEqual(TEXT("restored isotope equals refined isotope"), ReloadedReserve.Isotope, Reserve.Isotope);
    TestEqual(TEXT("restored colored balance equals retained balance"), ReloadedReserve.BlueOre, Reserve.BlueOre);

    FProgress AllMicro;
    AllMicro.AddResources(100000000);
    AllMicro.AddOre(2000000, EOreKind::Blue);
    AllMicro.AddOre(2000000, EOreKind::Black);
    int32 AllMicroCosts = 0;
    for (int32 I = 0; I < UpgradeCount; ++I)
    {
        if (Upgrade(I).bMicro) AllMicroCosts += Upgrade(I).Cost;
        TestTrue(TEXT("every upgrade and its prerequisite are purchasable"), AllMicro.Purchase(I));
    }
    TestEqual(TEXT("all micro costs are deducted"), AllMicro.IsotopeSpent, int64(AllMicroCosts));
    TestEqual(TEXT("laser damage remains useful for three asteroid sizes"), AllMicro.LaserDamage(), 3);
    TestTrue(TEXT("collector micro upgrade improves yield"), AllMicro.Yield(9) >= 3);
    TestTrue(TEXT("maximum manual speed retains its previously comfortable value"), FMath::IsNearlyEqual(AllMicro.SpeedMultiplier(), 1.9f, .001f));
    TestTrue(TEXT("rare-ore upgrades affect blue asteroids"), AllMicro.Yield(9, EOreKind::Blue) > AllMicro.Yield(9, EOreKind::Common));
    TestEqual(TEXT("rare-ore upgrades affect blue and black equally"), AllMicro.Yield(9, EOreKind::Blue), AllMicro.Yield(9, EOreKind::Black));
    TestTrue(TEXT("prospecting creates a bounded chance of rich ore"), AllMicro.RichOreChance() > 0.0f && AllMicro.RichOreChance() <= 0.31f + KINDA_SMALL_NUMBER);
    TestEqual(TEXT("rich deposits add only the asteroid's base yield"), AllMicro.Yield(27, EOreKind::Blue, true) - AllMicro.Yield(27, EOreKind::Blue), 5);
    TestFalse(TEXT("zero chance never produces a rich deposit"), IsRichOre(123, 2, 4, 1, 0.0f));
    TestTrue(TEXT("full chance always produces a rich deposit"), IsRichOre(123, 2, 4, 1, 1.0f));
    TestEqual(TEXT("deposit outcome remains stable for the same asteroid"),
        IsRichOre(123, 2, 4, 1, .25f), IsRichOre(123, 2, 4, 1, .25f));
    int32 RichCount = 0;
    for (int32 I = 0; I < 10000; ++I) if (IsRichOre(123, I % 15, I / 15, I / 150, .25f)) ++RichCount;
    TestTrue(TEXT("rich deposits occur near their stated probability"), RichCount >= 2250 && RichCount <= 2750);
    for (int32 NodeIndex = 0; NodeIndex < UpgradeCount; ++NodeIndex)
    {
        FProgress Probe;
        Probe.AddResources(100000000);
        Probe.AddOre(2000000, EOreKind::Blue);
        Probe.AddOre(2000000, EOreKind::Black);
        TArray<int32> Parents;
        for (int32 Parent = Upgrade(NodeIndex).Parent; Parent != INDEX_NONE; Parent = Upgrade(Parent).Parent)
            Parents.Add(Parent);
        for (int32 I = Parents.Num() - 1; I >= 0; --I)
            TestTrue(TEXT("node prerequisite can be purchased"), Probe.Purchase(Parents[I]));
        const float BeforeRange = Probe.Range(), BeforeCooldown = Probe.Cooldown(), BeforeSpeed = Probe.SpeedMultiplier(), BeforeRich = Probe.RichOreChance(), BeforeStability = Probe.BankStability();
        const int32 BeforeLasers = Probe.Lasers(), BeforeDamage = Probe.LaserDamage(), BeforeRicochets = Probe.RicochetCount(), BeforeCommon = Probe.Yield(9), BeforeBlue = Probe.Yield(9, EOreKind::Blue),
            BeforeLarge = Probe.Yield(27), BeforeClean = Probe.Yield(9, EOreKind::Common, false, true);
        TestTrue(FString::Printf(TEXT("node %d can be purchased"), NodeIndex), Probe.Purchase(NodeIndex));
        TestTrue(FString::Printf(TEXT("node %d changes an actual gameplay value"), NodeIndex),
            !FMath::IsNearlyEqual(Probe.Range(), BeforeRange) || !FMath::IsNearlyEqual(Probe.Cooldown(), BeforeCooldown) ||
            !FMath::IsNearlyEqual(Probe.SpeedMultiplier(), BeforeSpeed) || !FMath::IsNearlyEqual(Probe.RichOreChance(), BeforeRich) ||
            !FMath::IsNearlyEqual(Probe.BankStability(), BeforeStability) || Probe.Yield(27) != BeforeLarge ||
            Probe.Yield(9, EOreKind::Common, false, true) != BeforeClean ||
            Probe.Lasers() != BeforeLasers || Probe.LaserDamage() != BeforeDamage || Probe.RicochetCount() != BeforeRicochets || Probe.Yield(9) != BeforeCommon ||
            Probe.Yield(9, EOreKind::Blue) != BeforeBlue);
    }
    const int64 IsotopeBefore = AllMicro.Isotope;
    AllMicro.AddOre(1, EOreKind::Blue);
    AllMicro.AddOre(1, EOreKind::Black);
    TestEqual(TEXT("ore is no longer converted after all micro upgrades"), AllMicro.Isotope, IsotopeBefore);
    FProgress Mixed;
    FRandomStream Random(4172);
    for (int32 I = 0; I < 300; ++I)
    {
        Mixed.AddOre(Random.RandRange(1, 5), static_cast<EOreKind>(Random.RandRange(0, 2)));
        const int32 Candidate = Random.RandRange(0, UpgradeCount - 1);
        if (Mixed.CanPurchase(Candidate)) Mixed.Purchase(Candidate);
        TestTrue(TEXT("colored reserves never exceed the ore balance"),
            Mixed.BlueOre >= 0 && Mixed.BlackOre >= 0 && Mixed.BlueOre + Mixed.BlackOre <= Mixed.Resources);
        TestEqual(TEXT("ore remains conserved across purchases and conversion"), Mixed.Resources,
            Mixed.TotalCollected - Mixed.TotalSpent - Mixed.OreConverted);
        TestEqual(TEXT("crafted isotope is held or spent exactly once"), Mixed.Isotope + Mixed.IsotopeSpent, Mixed.OreConverted / 2);
    }
    FAsteroidDurability Rock;
    Rock.Prepare(0, 0, 27);
    TestFalse(TEXT("two-damage shot does not destroy a three-hit asteroid"), Rock.Hit(2));
    TestTrue(TEXT("remaining durability is destroyed by the next shot"), Rock.Hit(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightResourceAccountingTest, "SpaceFlight.Progress.ResourceAccounting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightResourceAccountingTest::RunTest(const FString& Parameters)
{
    SpaceFlight::FProgress P;
    P.AddResources(2);
    P.AddResources(3);
    P.AddResources(8);
    P.AddResources(0);
    P.AddResources(-3);
    TestEqual(TEXT("ore received accumulates"), P.TotalCollected, int64(13));
    TestEqual(TEXT("ore balance accumulates"), P.Resources, int64(13));
    TestEqual(TEXT("nothing spent yet"), P.TotalSpent, int64(0));
    TestTrue(TEXT("purchase is affordable"), P.Purchase(0));
    TestEqual(TEXT("purchase reduces the balance"), P.Resources, int64(1));
    TestEqual(TEXT("purchase increases total spending"), P.TotalSpent, int64(12));
    TestEqual(TEXT("purchase does not change total collected"), P.TotalCollected, int64(13));
    TestFalse(TEXT("duplicate purchase is rejected"), P.Purchase(0));
    TestFalse(TEXT("unaffordable purchase is rejected"), P.Purchase(3));
    TestEqual(TEXT("failed purchases do not change spending"), P.TotalSpent, int64(12));
    P.AddResources(8);
    TestEqual(TEXT("balance equals collected minus spent"), P.Resources, P.TotalCollected - P.TotalSpent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAsteroidBalanceTest, "SpaceFlight.Asteroids.DurabilityAndEconomy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAsteroidBalanceTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    for (int32 Size : {9, 18, 27})
    {
        FAsteroidDurability Rock;
        Rock.Prepare(0, 0, Size);
        const int32 Hits = Size == 9 ? 1 : (Size == 18 ? 2 : 3);
        TestEqual(TEXT("size determines initial durability"), Rock.Remaining, Hits);
        for (int32 Hit = 1; Hit <= Hits; ++Hit)
        {
            TestEqual(TEXT("only the final hit destroys an asteroid"), Rock.Hit(), Hit == Hits);
            Rock.Prepare(0, 0, Size);
            TestEqual(TEXT("damage persists within one asteroid pass"), Rock.Remaining, Hits - Hit);
        }
        TestFalse(TEXT("destroyed asteroid cannot be destroyed twice"), Rock.Hit());
        Rock.Prepare(1, 0, Size);
        TestEqual(TEXT("a new pass restores durability"), Rock.Remaining, Hits);
        Rock.Hit();
        Rock.Prepare(1, 1, Size);
        TestEqual(TEXT("a replacement restores durability"), Rock.Remaining, Hits);
    }
    TestEqual(TEXT("small asteroid yields one ore"), P.Yield(9), 1);
    TestEqual(TEXT("medium asteroid rewards two shots"), P.Yield(18), 3);
    TestEqual(TEXT("large asteroid rewards three shots"), P.Yield(27), 5);
    P.AddResources(Upgrade(3).Cost);
    TestTrue(TEXT("collector can be purchased"), P.Purchase(3));
    TestEqual(TEXT("collector increases small reward"), P.Yield(9), 2);
    TestEqual(TEXT("collector improves medium reward"), P.Yield(18), 4);
    TestEqual(TEXT("collector improves large reward"), P.Yield(27), 6);

    const FForecastContext Context{1280, 720, 1, 640, 690, 690, 0, 10};
    const TArray<FForecastRock> FleetingLarge{{800, 710, 100, 14, 27, false}};
    TestEqual(TEXT("a large asteroid escaping before its third shot yields no forecast ore"),
        ForecastOre(FProgress(), Context, FleetingLarge), 0.0f);
    TArray<FForecastRock> DamagedLarge = FleetingLarge;
    DamagedLarge[0].HitsRemaining = 1;
    TestTrue(TEXT("forecast rewards a damaged asteroid destroyed before escape"),
        ForecastOre(FProgress(), Context, DamagedLarge) > 4.9f);
    FProgress ThreeLasers;
    ThreeLasers.AddResources(Upgrade(5).Cost + Upgrade(6).Cost);
    TestTrue(TEXT("first additional laser can be bought"), ThreeLasers.Purchase(5));
    TestTrue(TEXT("second additional laser can be bought"), ThreeLasers.Purchase(6));
    TestEqual(TEXT("a volley cannot hit the same asteroid more than once"),
        ForecastOre(ThreeLasers, Context, FleetingLarge), 0.0f);
    FProgress StrongLaser;
    StrongLaser.AddResources(Upgrade(5).Cost);
    TestTrue(TEXT("laser prerequisite is open"), StrongLaser.Purchase(5));
    StrongLaser.AddOre(Upgrade(22).Cost, EOreKind::Blue);
    StrongLaser.AddOre(Upgrade(22).Cost, EOreKind::Black);
    TArray<FForecastRock> TwoHitsLeft = FleetingLarge;
    TwoHitsLeft[0].HitsRemaining = 2;
    TestEqual(TEXT("one-damage laser cannot finish the target before escape"),
        ForecastOre(StrongLaser, Context, TwoHitsLeft), 0.0f);
    TestTrue(TEXT("laser micro upgrade can be purchased"), StrongLaser.Purchase(22));
    TestTrue(TEXT("forecast uses upgraded laser damage"), ForecastOre(StrongLaser, Context, TwoHitsLeft) > 4.9f);

    const FForecastContext TimingContext{1280, 720, 1, 640, 650, 650, 0, 10};
    const TArray<FForecastRock> TimingRocks{{800, 582, 100, 14, 27, false}};
    FProgress Cooled;
    Cooled.AddResources(Upgrade(7).Cost);
    TestTrue(TEXT("cooling prerequisite can be purchased"), Cooled.Purchase(7));
    TestEqual(TEXT("uncool laser cannot finish a near-escaping rock"), ForecastOre(FProgress(), TimingContext, TimingRocks), 0.0f);
    TestTrue(TEXT("forecast recognizes a small cooldown reduction before the asteroid escapes"),
        ForecastOre(Cooled, TimingContext, TimingRocks) > 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAutoUpgradeTest, "SpaceFlight.Progress.AutopilotForecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAutoUpgradeTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    FForecastContext Context{1280, 720, 1, 640, 500, 475, 0, 10};
    TArray<FForecastRock> Rocks;
    TestEqual(TEXT("no forecast opportunities means no speculative purchase"),
        ChooseAutoUpgrade(Progress, Context, Rocks).Next, INDEX_NONE);

    Rocks.Add({800, 475, 0, 13, 9, false});
    const auto Single = ChooseAutoUpgrade(Progress, Context, Rocks);
    TestEqual(TEXT("collector pays off on a single accessible rock"), Single.Next, 3);
    TestFalse(TEXT("the best upgrade may require saving first"), Progress.CanPurchase(Single.Next));
    TestEqual(TEXT("forecast does not spend resources"), Progress.Resources, int64(0));
    TestFalse(TEXT("forecast does not purchase upgrades"), Progress.Has(Single.Next));

    FProgress EnoughForCollector;
    EnoughForCollector.AddResources(Upgrade(3).Cost);
    TestEqual(TEXT("affordable upgrades retain forecast priority"),
        ChooseAutoUpgrade(EnoughForCollector, Context, Rocks).Next, 3);
    FProgress EnoughForRadar;
    EnoughForRadar.AddResources(Upgrade(0).Cost);
    const auto Affordable = ChooseAutoUpgrade(EnoughForRadar, Context, Rocks);
    TestEqual(TEXT("an unaffordable forecast winner does not block an affordable upgrade"), Affordable.Next, 0);
    TestTrue(TEXT("the fallback upgrade can actually be purchased"), EnoughForRadar.CanPurchase(Affordable.Next));

    FProgress LastLeaf;
    LastLeaf.AddResources(100000000);
    LastLeaf.AddOre(2000000, EOreKind::Blue);
    LastLeaf.AddOre(2000000, EOreKind::Black);
    for (int32 I = 0; I < UpgradeCount; ++I)
        if (I != 54) TestTrue(TEXT("prerequisites and other branches can be bought"), LastLeaf.Purchase(I));
    const TArray<FForecastRock> NoRocks;
    const auto Remaining = ChooseAutoUpgrade(LastLeaf, Context, NoRocks);
    TestEqual(TEXT("an affordable last leaf is chosen even with no predicted income"), Remaining.Next, 54);
    TestTrue(TEXT("last leaf is purchasable"), LastLeaf.CanPurchase(Remaining.Next));
    TestTrue(TEXT("purchasing last leaf spends resources"), LastLeaf.Purchase(Remaining.Next));
    TestEqual(TEXT("finished tree no longer has a choice"), ChooseAutoUpgrade(LastLeaf, Context, NoRocks).Next, INDEX_NONE);

    FProgress MicroOnly;
    MicroOnly.AddResources(Upgrade(0).Cost);
    TestTrue(TEXT("radar prerequisite is open"), MicroOnly.Purchase(0));
    MicroOnly.AddOre(Upgrade(20).Cost, EOreKind::Blue);
    MicroOnly.AddOre(Upgrade(20).Cost, EOreKind::Black);
    const auto MicroChoice = ChooseAutoUpgrade(MicroOnly, Context, NoRocks);
    TestEqual(TEXT("autopilot chooses an affordable micro upgrade with isotope"), MicroChoice.Next, 20);
    TestTrue(TEXT("autopilot micro choice can be purchased"), MicroOnly.Purchase(MicroChoice.Next));

    FProgress Midgame;
    for (int32 I : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 18, 20, 21, 23, 24 }) Midgame.Acquired[I] = true;
    Midgame.AddResources(600);
    Midgame.Isotope = 99;
    const auto Alternative = ChooseAutoUpgrade(Midgame, Context, NoRocks);
    TestTrue(TEXT("autopilot can choose a newly affordable side branch"), IsSideUpgrade(Alternative.Next) && Midgame.CanPurchase(Alternative.Next));
    for (int32 I : { 65, 68, 71, 74, 77, RicochetUpgrade }) Midgame.Acquired[I] = true;
    TestEqual(TEXT("midgame waits when neither currency reaches an open upgrade"),
        ChooseAutoUpgrade(Midgame, Context, NoRocks).Next, INDEX_NONE);
    FProgress EnoughOre = Midgame;
    EnoughOre.AddResources(300);
    const auto OreChoice = ChooseAutoUpgrade(EnoughOre, Context, NoRocks);
    TestEqual(TEXT("autopilot chooses the collector as soon as 900 ore is available"), OreChoice.Next, 12);
    TestTrue(TEXT("autopilot can spend ore on the selected collector"), EnoughOre.Purchase(OreChoice.Next));
    FProgress EnoughIsotope = Midgame;
    EnoughIsotope.Isotope = 250;
    const auto IsotopeChoice = ChooseAutoUpgrade(EnoughIsotope, Context, NoRocks);
    TestEqual(TEXT("autopilot selects the resonator once isotope is sufficient"), IsotopeChoice.Next, 22);
    TestTrue(TEXT("autopilot can spend isotope on the resonator"), EnoughIsotope.Purchase(IsotopeChoice.Next));

    Context.Time = 4.0f * PI;
    Rocks.Reset();
    Rocks.Add({1005, 475, 0, 13, 9, false});
    const auto Far = ChooseAutoUpgrade(Progress, Context, Rocks);
    TestEqual(TEXT("long-range opportunity selects the required first radar"), Far.Next, 0);
    TestTrue(TEXT("forecast considers a profitable locked radar descendant"), Far.Goal != INDEX_NONE && Upgrade(Far.Goal).Column == 0);

    Context = {1280, 720, 1, 640, 650, 650, 0, 10};
    Progress.AddResources(Upgrade(3).Cost + Upgrade(4).Cost);
    TestTrue(TEXT("collector first stage can be bought"), Progress.Purchase(3));
    TestTrue(TEXT("collector second stage can be bought"), Progress.Purchase(4));
    Rocks.Reset();
    for (float X : {450.0f, 475.0f, 805.0f, 830.0f}) Rocks.Add({X, 690, 100, 13, 9, false});
    const auto Fleeting = ChooseAutoUpgrade(Progress, Context, Rocks);
    TestEqual(TEXT("simultaneous fleeting targets favor an extra laser"), Fleeting.Next, 5);

    Progress.AddResources(100000000);
    Progress.AddOre(2000000, EOreKind::Blue);
    Progress.AddOre(2000000, EOreKind::Black);
    for (int32 I = 0; I < UpgradeCount; ++I) if (!Progress.Has(I)) TestTrue(TEXT("complete tree can be purchased"), Progress.Purchase(I));
    TestEqual(TEXT("complete tree has no automatic choice"), ChooseAutoUpgrade(Progress, Context, Rocks).Next, INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightCrystalGlowTest, "SpaceFlight.Asteroids.CrystalGlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightCrystalGlowTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    const auto First = CrystalGlow(0, 1234, 0, 0);
    const auto Repeat = CrystalGlow(0, 1234, 0, 0);
    TestEqual(TEXT("glow is deterministic per asteroid"), First.Strength, Repeat.Strength);
    TestEqual(TEXT("glow mask is brightest in the center"), CrystalGlowMask(0.0f, 0.0f), 1.0f);
    TestTrue(TEXT("glow fades continuously from center to rim"),
        CrystalGlowMask(0.25f, 0.0f) > CrystalGlowMask(0.5f, 0.0f) &&
        CrystalGlowMask(0.5f, 0.0f) > CrystalGlowMask(0.9f, 0.0f) && CrystalGlowMask(0.9f, 0.0f) > 0.0f);
    TestTrue(TEXT("glow is circular instead of square"),
        FMath::IsNearlyEqual(CrystalGlowMask(0.5f, 0), CrystalGlowMask(0.3f, 0.4f), 0.0001f) &&
        CrystalGlowMask(0.8f, 0.8f) == 0.0f);
    TestEqual(TEXT("glow fades to transparent at the edge"), CrystalGlowMask(1.0f, 0), 0.0f);
    float Minimum = 1, Maximum = 0;
    float Last = First.Strength;
    bool bDifferent = false;
    for (int32 I = 0; I <= 600; ++I)
    {
        const float Time = I * 0.01f;
        const auto Glow = CrystalGlow(Time, 1234, 0, 0);
        Minimum = FMath::Min(Minimum, Glow.Strength);
        Maximum = FMath::Max(Maximum, Glow.Strength);
        TestTrue(TEXT("pulse stays subdued and changes smoothly"), Glow.Strength >= 0.319f && Glow.Strength <= 0.701f &&
            FMath::Abs(Glow.Strength - Last) < 0.01f);
        TestTrue(TEXT("palette stays cool"), Glow.Color.B > Glow.Color.R && Glow.Color.G > Glow.Color.R);
        Last = Glow.Strength;
        bDifferent |= FMath::Abs(Glow.Strength - CrystalGlow(Time, 1234, 1, 0).Strength) > 0.05f;
    }
    TestTrue(TEXT("glow brightens and fades over a cycle"), Maximum - Minimum > 0.35f);
    TestTrue(TEXT("asteroids pulse independently"), bDifferent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAutopilotSpeedForecastTest, "SpaceFlight.Flight.AutopilotSpeedForecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAutopilotSpeedForecastTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FForecastContext Context{1280, 720, 1, 640, 500, 500, 0, 10};
    const TArray<FForecastRock> Rocks{{1120, 500, 0, 7, 9, false}};
    FProgress Base;
    const float BaseIncome = ForecastOre(Base, Context, Rocks);
    TestTrue(TEXT("forecast uses the slower original autopilot route"), BaseIncome < 0.9f);
    FProgress Boosted;
    Boosted.AddResources(Upgrade(9).Cost);
    TestTrue(TEXT("engine can be purchased"), Boosted.Purchase(9));
    TestTrue(TEXT("engine does not change established route speed"), FMath::IsNearlyEqual(ForecastOre(Boosted, Context, Rocks), BaseIncome, 0.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightClockTest, "SpaceFlight.Flight.TimerFormat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightClockTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("timer starts at zero"), SpaceFlight::FormatFlightTime(0.0f), FString(TEXT("00:00:00")));
    TestEqual(TEXT("timer does not count fractional seconds"), SpaceFlight::FormatFlightTime(59.9f), FString(TEXT("00:00:59")));
    TestEqual(TEXT("timer rolls into minutes"), SpaceFlight::FormatFlightTime(60.0f), FString(TEXT("00:01:00")));
    TestEqual(TEXT("timer rolls into hours"), SpaceFlight::FormatFlightTime(3661.0f), FString(TEXT("01:01:01")));
    TestEqual(TEXT("hours are not capped at one day"), SpaceFlight::FormatFlightTime(360000.0f), FString(TEXT("100:00:00")));
    TestEqual(TEXT("negative time clamps to zero"), SpaceFlight::FormatFlightTime(-1.0f), FString(TEXT("00:00:00")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTreeHitTest, "SpaceFlight.Progress.TreeHitTesting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTreeHitTest::RunTest(const FString& Parameters)
{
    int32 NormalByColumn[5] = {0, 0, 0, 0, 0};
    int32 MicroByColumn[5] = {0, 0, 0, 0, 0};
    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
    {
        const auto& Node = SpaceFlight::Upgrade(I);
        TestTrue(TEXT("node has a valid branch"), Node.Column >= 0 && Node.Column < 5);
        if (Node.Column >= 0 && Node.Column < 5)
            ++(Node.bMicro ? MicroByColumn[Node.Column] : NormalByColumn[Node.Column]);
        TestTrue(TEXT("node has a positive price"), Node.Cost > 0);
        TestTrue(TEXT("node has a real effect"), Node.RangeBonus > 0 || Node.LaserBonus > 0 || Node.CooldownReduction > 0 ||
            Node.YieldBonus > 0 || Node.SpeedBonus > 0 || Node.DamageBonus > 0 || Node.RareYieldBonus > 0 || Node.RichOreChance > 0 ||
            Node.LargeOreBonus > 0 || Node.FirstHitOreBonus > 0 || Node.StabilityBonus > 0 || Node.RicochetBonus > 0);
        if (Node.Parent >= 0)
        {
            TestTrue(TEXT("prerequisite precedes the node"), Node.Parent < I);
            const auto& Parent = SpaceFlight::Upgrade(Node.Parent);
            TestEqual(TEXT("prerequisite stays in the same column"), Node.Column, Parent.Column);
            if (!Node.bMicro)
            {
                TestEqual(TEXT("prerequisite is the previous tier"), Node.Tier, Parent.Tier + 1);
                TestTrue(TEXT("branch prices increase gradually"), Node.Cost > Parent.Cost);
            }
            else TestEqual(TEXT("micro improvement attaches at its parent's tier"), Node.Tier, Parent.Tier);
        }
    }
    for (int32 Column = 0; Column < 5; ++Column)
    {
        TestEqual(TEXT("laser branch also contains ricochet"), NormalByColumn[Column], Column == 2 ? 14 : 13);
        TestEqual(TEXT("each branch contains three micro upgrades"), MicroByColumn[Column], 3);
    }
    for (const FVector2D Viewport : { FVector2D(1280, 720), FVector2D(640, 360), FVector2D(1920, 1080), FVector2D(1024, 768) })
    {
        const float Scale = SpaceFlight::UpgradeTreeScale(Viewport.X, Viewport.Y) * SpaceFlight::DefaultTreeZoom;
        const FVector2D Ship = SpaceFlight::UpgradeTreeShipCenter(Viewport.X, Viewport.Y);
        TestTrue(TEXT("ship remains at the center of the viewport"),
            Ship.Equals(Viewport * 0.5f, KINDA_SMALL_NUMBER));
        TestEqual(TEXT("ship does not activate an upgrade"),
            SpaceFlight::UpgradeAtScreenPosition(Ship, Viewport.X, Viewport.Y), INDEX_NONE);
        const FVector2D Radar = SpaceFlight::UpgradeScreenCenter(0, Viewport.X, Viewport.Y);
        const FVector2D Collector = SpaceFlight::UpgradeScreenCenter(3, Viewport.X, Viewport.Y);
        const FVector2D Laser = SpaceFlight::UpgradeScreenCenter(5, Viewport.X, Viewport.Y);
        const FVector2D Cooling = SpaceFlight::UpgradeScreenCenter(7, Viewport.X, Viewport.Y);
        const FVector2D Engine = SpaceFlight::UpgradeScreenCenter(9, Viewport.X, Viewport.Y);
        TestTrue(TEXT("branches lead up, left, right and down"),
            Radar.Y < Ship.Y && FMath::IsNearlyEqual(Radar.X, Ship.X) &&
            Collector.X < Ship.X && FMath::IsNearlyEqual(Collector.Y, Ship.Y) &&
            Laser.X > Ship.X && FMath::IsNearlyEqual(Laser.Y, Ship.Y) &&
            Cooling.Y > Ship.Y && Engine.Y > Ship.Y && Cooling.X < Ship.X && Engine.X > Ship.X);
        for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
        {
            const FVector2D Node = SpaceFlight::UpgradeScreenCenter(I, Viewport.X, Viewport.Y);
            const FVector2D Half = SpaceFlight::UpgradeCardHalfSize(I) * Scale;
            const FVector2D PanToNode = -SpaceFlight::UpgradeTreePosition(I) * SpaceFlight::DefaultTreeZoom;
            const FVector2D PannedNode = SpaceFlight::UpgradeScreenCenter(I, Viewport.X, Viewport.Y,
                SpaceFlight::DefaultTreeZoom, PanToNode);
            TestEqual(FString::Printf(TEXT("node %d can be selected after panning at %.0fx%.0f"), I, Viewport.X, Viewport.Y),
                SpaceFlight::UpgradeAtScreenPosition(PannedNode, Viewport.X, Viewport.Y,
                    SpaceFlight::DefaultTreeZoom, PanToNode), I);
            TestTrue(TEXT("upgrade card does not overlap the ship"),
                FMath::Abs(Node.X - Ship.X) > Half.X + 72 * Scale || FMath::Abs(Node.Y - Ship.Y) > Half.Y + 30 * Scale);
            const auto& Definition = SpaceFlight::Upgrade(I);
            if (Definition.Parent >= 0)
            {
                const FVector2D Parent = SpaceFlight::UpgradeScreenCenter(Definition.Parent, Viewport.X, Viewport.Y);
                TestTrue(TEXT("each tier is farther from the ship than its prerequisite"),
                    FVector2D::Distance(Node, Ship) > FVector2D::Distance(Parent, Ship));
            }
            for (int32 J = I + 1; J < SpaceFlight::UpgradeCount; ++J)
            {
                const FVector2D Other = SpaceFlight::UpgradeScreenCenter(J, Viewport.X, Viewport.Y);
                const FVector2D OtherHalf = SpaceFlight::UpgradeCardHalfSize(J) * Scale;
                TestTrue(TEXT("clickable cards do not overlap"),
                    FMath::Abs(Node.X - Other.X) > Half.X + OtherHalf.X || FMath::Abs(Node.Y - Other.Y) > Half.Y + OtherHalf.Y);
            }
        }
        const float UIScale = SpaceFlight::UpgradeTreeScale(Viewport.X, Viewport.Y);
        const FVector2D Header = SpaceFlight::UpgradeTreeOrigin(Viewport.X, Viewport.Y) + FVector2D(640, 70) * UIScale;
        TestEqual(TEXT("header does not activate an upgrade"),
            SpaceFlight::UpgradeAtScreenPosition(Header, Viewport.X, Viewport.Y), INDEX_NONE);
        const FVector2D Pan(80, -45);
        const float Zoom = 0.9f;
        const FVector2D Moved = SpaceFlight::UpgradeScreenCenter(5, Viewport.X, Viewport.Y, Zoom, Pan);
        TestEqual(TEXT("hit testing follows zoom and pan"),
            SpaceFlight::UpgradeAtScreenPosition(Moved, Viewport.X, Viewport.Y, Zoom, Pan), 5);
        const FVector2D Anchor = SpaceFlight::UpgradeScreenCenter(3, Viewport.X, Viewport.Y);
        const FVector2D ZoomPan = SpaceFlight::PanForTreeZoom(Anchor, Viewport.X, Viewport.Y,
            FVector2D::ZeroVector, SpaceFlight::DefaultTreeZoom, Zoom);
        TestTrue(TEXT("zoom preserves the position beneath the cursor"),
            SpaceFlight::UpgradeScreenCenter(3, Viewport.X, Viewport.Y, Zoom, ZoomPan).Equals(Anchor, 0.01f));
        const FVector2D HiddenPan = (Header - Ship) / UIScale - SpaceFlight::UpgradeTreePosition(0) * Zoom;
        TestEqual(TEXT("cards hidden under the header cannot be clicked"),
            SpaceFlight::UpgradeAtScreenPosition(Header, Viewport.X, Viewport.Y, Zoom, HiddenPan), INDEX_NONE);
    }
    TestEqual(TEXT("zero viewport is ignored"), SpaceFlight::UpgradeAtScreenPosition(FVector2D::ZeroVector, 0, 0), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAvoidanceTest, "SpaceFlight.Flight.AvoidsAsteroids",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAvoidanceTest::RunTest(const FString& Parameters)
{
    for (float ShipX : { 32.0f, 100.0f, 640.0f, 1180.0f, 1248.0f })
    {
        for (float RockX : { 25.0f, 100.0f, 630.0f, 1190.0f, 1255.0f })
        {
            const float X = SpaceFlight::AvoidShip(RockX, 500.0f, ShipX, 500.0f, 1280.0f, 1.0f, 15.0f);
            TestTrue(TEXT("asteroid stays outside the ship at close range"), FMath::Abs(X - ShipX) >= 48.0f - KINDA_SMALL_NUMBER);
            TestTrue(TEXT("asteroid stays within screen bounds"), X >= 15.0f && X <= 1265.0f);
        }
    }
    TestEqual(TEXT("distant asteroid is not displaced"),
        SpaceFlight::AvoidShip(640.0f, 0.0f, 640.0f, 500.0f, 1280.0f, 1.0f, 15.0f), 640.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightLaserTest, "SpaceFlight.Laser.RangeAndSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightLaserTest::RunTest(const FString& Parameters)
{
    const FVector2D Origin(640.0, 500.0);
    const float RangeSquared = FMath::Square(SpaceFlight::LaserRange);
    for (int32 Side : { -1, 1 })
    {
        const FString Label = Side < 0 ? TEXT("left") : TEXT("right");
        float Nearest = RangeSquared;
        TestTrue(Label + TEXT(" inside range"), SpaceFlight::ConsiderLaserTarget(
            Origin, Origin + FVector2D(Side * 219.9, 0.0), 720.0f, Nearest));
        Nearest = RangeSquared;
        TestTrue(Label + TEXT(" exactly at range"), SpaceFlight::ConsiderLaserTarget(
            Origin, Origin + FVector2D(Side * 220.0, 0.0), 720.0f, Nearest));
        Nearest = RangeSquared;
        TestFalse(Label + TEXT(" outside range"), SpaceFlight::ConsiderLaserTarget(
            Origin, Origin + FVector2D(Side * 220.1, 0.0), 720.0f, Nearest));
        Nearest = RangeSquared;
        TestTrue(Label + TEXT(" diagonal target"), SpaceFlight::ConsiderLaserTarget(
            Origin, Origin + FVector2D(Side * 120.0, -120.0), 720.0f, Nearest));
        TestFalse(Label + TEXT(" farther target does not replace nearer"),
            SpaceFlight::ConsiderLaserTarget(Origin, Origin + FVector2D(Side * 200.0, 0.0), 720.0f, Nearest));
        TestTrue(Label + TEXT(" closer target replaces farther"),
            SpaceFlight::ConsiderLaserTarget(Origin, Origin + FVector2D(Side * 70.0, 0.0), 720.0f, Nearest));
    }
    float Nearest = RangeSquared;
    TestFalse(TEXT("off-screen targets are ignored"), SpaceFlight::ConsiderLaserTarget(
        FVector2D(640.0, 50.0), FVector2D(640.0, -1.0), 720.0f, Nearest));
    Nearest = RangeSquared;
    TestTrue(TEXT("left target accepted first"), SpaceFlight::ConsiderLaserTarget(
        Origin, Origin + FVector2D(-180.0, 0.0), 720.0f, Nearest));
    TestTrue(TEXT("closer right target replaces left"), SpaceFlight::ConsiderLaserTarget(
        Origin, Origin + FVector2D(90.0, 0.0), 720.0f, Nearest));
    Nearest = RangeSquared;
    TestTrue(TEXT("right target accepted first"), SpaceFlight::ConsiderLaserTarget(
        Origin, Origin + FVector2D(180.0, 0.0), 720.0f, Nearest));
    TestTrue(TEXT("closer left target replaces right"), SpaceFlight::ConsiderLaserTarget(
        Origin, Origin + FVector2D(-90.0, 0.0), 720.0f, Nearest));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSpawnTest, "SpaceFlight.Asteroids.RandomSafeSpawns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSpawnTest::RunTest(const FString& Parameters)
{
    int32 LeftCount = 0;
    int32 RightCount = 0;
    bool bAllSafe = true;
    bool bVariesAcrossPasses = false;
    bool bVariesAcrossSessions = false;
    bool bVariesAcrossReplacements = false;
    bool bDeterministic = true;
    for (uint32 Session = 1; Session <= 32; ++Session)
    {
        for (int32 Index = 0; Index < 7; ++Index)
        {
            for (int32 Pass = 0; Pass < 32; ++Pass)
            {
                constexpr float Width = 1280.0f;
                constexpr float Pixel = 1.0f;
                const float Radius = (3.0f + Index * 2.0f) * Pixel;
                const auto Spawn = SpaceFlight::ChooseAsteroidSpawn(Session, Index, Pass, 0, Width, Pixel, Radius);
                const auto Repeat = SpaceFlight::ChooseAsteroidSpawn(Session, Index, Pass, 0, Width, Pixel, Radius);
                const auto NextPass = SpaceFlight::ChooseAsteroidSpawn(Session, Index, Pass + 1, 0, Width, Pixel, Radius);
                const auto NextSession = SpaceFlight::ChooseAsteroidSpawn(Session + 1, Index, Pass, 0, Width, Pixel, Radius);
                const auto Replacement = SpaceFlight::ChooseAsteroidSpawn(Session, Index, Pass, 1, Width, Pixel, Radius);
                bDeterministic &= Spawn.X == Repeat.X && Spawn.bLeft == Repeat.bLeft;
                bVariesAcrossPasses |= Spawn.X != NextPass.X;
                bVariesAcrossSessions |= Spawn.X != NextSession.X;
                bVariesAcrossReplacements |= Spawn.X != Replacement.X;
                bAllSafe &= Spawn.X - Radius >= 0.0f && Spawn.X + Radius <= Width;
                bAllSafe &= FMath::Abs(Spawn.X - Width * 0.5f) > 28.0f * Pixel + Radius;
                bAllSafe &= Spawn.bLeft ? Spawn.X < Width * 0.5f : Spawn.X > Width * 0.5f;
                if (Spawn.bLeft) ++LeftCount; else ++RightCount;
            }
        }
    }
    TestTrue(TEXT("all asteroid bounds stay on screen and clear of ship"), bAllSafe);
    TestTrue(TEXT("same inputs give a stable position"), bDeterministic);
    TestTrue(TEXT("positions vary across passes"), bVariesAcrossPasses);
    TestTrue(TEXT("positions vary across sessions"), bVariesAcrossSessions);
    TestTrue(TEXT("destroyed asteroid replacements get new positions"), bVariesAcrossReplacements);
    TestTrue(TEXT("both sides receive substantial numbers of asteroids"), LeftCount > 1000 && RightCount > 1000);
    AddInfo(FString::Printf(TEXT("Spawn sample: %d left, %d right"), LeftCount, RightCount));
    return true;
}
#endif
