#include "SpaceFlightLogic.h"
#include "BlueAsteroidArt.h"
#include "SpaceFlightSectors.h"
#include "SpaceFlightEvents.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
    // This is an accelerated model, not a rendering or hardware benchmark. It
    // uses the same spawn, targeting, damage, economy and autopilot functions
    // as the game; 0.1 s steps and 2 s decisions intentionally approximate it.
    struct FBalanceRock
    {
        float Speed = 0.0f;
        float InitialTravel = 0.0f;
        float ResetTime = -1.0f;
        int32 Generation = 0;
        SpaceFlight::FAsteroidDurability Durability;
    };

    struct FBalanceSnapshot
    {
        int32 Purchased = 0;
        int64 Collected = 0;
        int64 Isotope = 0;
        int32 Lasers = 0;
        int32 Damage = 0;
        int32 SmallYield = 0;
        float Range = 0;
        float Speed = 0;
        int32 EventBonus = 0;
        int32 SidePurchased = 0;
        int32 SideMask = 0;
        int64 Spent = 0;
        float LongestGap = 0;
        float FirstSideTime = -1;
        float HalfTreeTime = -1;
        int64 Reserve = 0;
        int32 AffordableDecisions = 0;
        int32 MissedAffordableDecisions = 0;
        int32 WaitingDecisions = 0;
        float MaxMissedAffordableSeconds = 0.0f;
        int32 ObjectivesCompleted = 0;
        int32 ObjectiveRewards = 0;
    };

    TArray<FBalanceSnapshot> SimulateProgress(uint32 Seed, TConstArrayView<int32> Checkpoints, int32 Sector = 0, int32 RefiningMode = 0, bool bUseModule = false, int32 Specialization = 0, bool bEvents = false, bool bAllowSideBranches = true, bool bTrace = false, bool bScriptedManual = false, bool bFineStep = false, float ForecastHorizon = 12.0f, bool bLegacyGoals = false)
    {
        using namespace SpaceFlight;
        constexpr float Width = 1280.0f, Height = 720.0f, Pixel = 1.0f, Unit = 4.0f;
        const float Step = bFineStep ? .025f : .1f;
        const int32 StepsPerSecond = bFineStep ? 40 : 10;
        FBalanceRock Rocks[NormalAsteroidCount];
        for (int32 I = 0; I < NormalAsteroidCount; ++I)
        {
            FRandomStream Random(4627 + I * 719);
            Rocks[I].Speed = Unit * Random.FRandRange(9.0f, 21.0f) * SectorSpeedMultiplier(Sector);
            Rocks[I].InitialTravel = Random.FRand() * Height;
        }
        FProgress Progress;
        FMineralEvent Event;
        Event.Initialize(Seed);
        int32 EventBonus = 0;
        float LastPurchaseTime = 0.0f, LongestPurchaseGap = 0.0f, FirstSideTime = -1.0f, HalfTreeTime = -1.0f;
        int32 PurchaseCount = 0, CorePurchaseCount = 0, Destroyed = 0, LargeDestroyed = 0;
        int32 AffordableDecisions = 0, MissedAffordableDecisions = 0, WaitingDecisions = 0;
        int32 ConsecutiveMissedDecisions = 0, MaxConsecutiveMissedDecisions = 0;
        int32 ObjectiveRewards = 0;
        float Core33Time = -1.0f;
        if (Specialization != 0) Progress.SetSpecialization(Specialization, 0);
        Progress.SetRefiningMode(RefiningMode);
        Progress.Objective.Sequence = Sector == 0 ? 0 : (Sector == 1 ? 3 : 8);
        Progress.Objective.Kind = Progress.Objective.Sequence % 4;
        Progress.CurrentSector = Sector;
        float ShipX = Width * .5f, Bank = 0, LastShot = -100.0f;
        TArray<FBalanceSnapshot> Results;
        if (Checkpoints.IsEmpty()) return Results;
        const int32 EndStep = Checkpoints.Last() * StepsPerSecond;
        int32 CheckpointIndex = 0;
        for (int32 Tick = 0; Tick <= EndStep; ++Tick)
        {
            const float Time = Tick * Step;
            if (bEvents && Tick > 0) Event.Advance(Step, false, SectorAsteroidCount(Sector));
            const float Before = ShipX;
            const float Destination = Width * (0.5f + 0.27f * FMath::Sin(Time * .25f));
            if (bScriptedManual)
                ShipX += (FMath::Fmod(Time, 8.0f) < 4.0f ? 1.0f : -1.0f) * Step * Width * .30f * Progress.SpeedMultiplier();
            else
                ShipX = FMath::FInterpConstantTo(ShipX, Destination, Step, Width * .30f * Progress.SpeedMultiplier() * .55f);
            ShipX = FMath::Clamp(ShipX, 32.0f * Pixel, Width - 32.0f * Pixel);
            Bank = FMath::FInterpTo(Bank, ShipManeuverBank(ShipX - Before, Step, Width, Progress), Step, 5.5f);
            const float Top = Height * .69f - 29.0f * Pixel + FMath::Sin(Time * 1.4f) * Unit / 3.0f;
            const FVector2D Origin = ShipPoint(28, 4, ShipX, Top, Pixel, Bank);
            const bool bEvaluate = Tick % (2 * StepsPerSecond) == 0;
            TArray<FLaserCandidate, TInlineAllocator<3>> Targets;
            TArray<FLaserCandidate, TInlineAllocator<32>> RicochetCandidates;
            const bool bProtectFirstHitBonus = Progress.Has(RicochetUpgrade) && Progress.FirstHitOreBonus() > 0;
            const int32 EquippedDamage = bProtectFirstHitBonus ? Progress.LaserDamage() : 0;
            TArray<FForecastRock, TInlineAllocator<MaxForecastRocks>> Forecast;
            for (int32 I = 0; I < SectorAsteroidCount(Sector); ++I)
            {
                FBalanceRock& Rock = Rocks[I];
                const int32 Size = AsteroidSizeForIndex(I);
                const float Radius = (Size * .5f + 1.0f) * Pixel;
                const float Travel = Rock.ResetTime >= 0 ? (Time - Rock.ResetTime) * Rock.Speed : Rock.InitialTravel + Time * Rock.Speed;
                const int32 Pass = FMath::FloorToInt(Travel / Height);
                const float Y = FMath::Fmod(Travel, Height);
                Rock.Durability.Prepare(Pass, Rock.Generation, Size);
                const float BaseX = ChooseAsteroidSpawn(SectorSeed(Seed, Sector), I, Pass, Rock.Generation, Width, Pixel, Radius).X;
                const float X = AvoidShip(BaseX, Y, ShipX, Top + 29.0f * Pixel, Width, Pixel, Radius);
                const EOreKind Kind = SectorOreKind(Sector, Seed, I, Pass, Rock.Generation);
                const bool bMarked = bEvents && Event.IsMarked(I, SectorAsteroidCount(Sector));
                const bool bDamaged = !Rock.Durability.IsUndamaged(Size);
                const FLaserCandidate Candidate{ I, Pass, Size, FVector2D(X, Y), FLinearColor::White, 0, Kind, bMarked, bDamaged,
                    RicochetWouldSpoilFirstHitBonus(Size, EquippedDamage, bDamaged, bProtectFirstHitBonus) };
                AddLaserCandidate(Targets, Candidate,
                    Origin, Height, Progress.Range(), Progress.Lasers());
                if (Progress.Has(RicochetUpgrade) && Y >= 0 && Y <= Height && FVector2D::DistSquared(Origin, Candidate.Position) <= FMath::Square(Progress.Range()))
                    RicochetCandidates.Add(Candidate);
                if (bEvaluate)
                    Forecast.Add({ BaseX, Y, Rock.Speed, Radius, Size, true, Rock.Durability.Remaining, Kind, bMarked, Rock.Durability.RemainingTenths });
            }
            if (bEvaluate)
            {
                Forecast.Sort([&](const FForecastRock& A, const FForecastRock& B) {
                    return FVector2D::DistSquared(FVector2D(A.BaseX, A.Y), Origin) <
                        FVector2D::DistSquared(FVector2D(B.BaseX, B.Y), Origin);
                });
                const FForecastContext Context{ Width, Height, Pixel, ShipX, Top + 29 * Pixel,
                    static_cast<float>(Origin.Y), Time, Time - LastShot };
                // Distinguish legitimate waiting from a chooser that ignores an
                // upgrade already affordable in the simulated policy's tree.
                bool bAnyAffordable = false;
                for (int32 I = 0; I < UpgradeCount; ++I)
                    if ((bAllowSideBranches || !IsSideUpgrade(I)) && Progress.CanPurchase(I))
                    { bAnyAffordable = true; break; }
                const FAutoUpgradeChoice Choice = ChooseAutoUpgrade(Progress, Context, Forecast, bAllowSideBranches, ForecastHorizon);
                const bool bChosenAffordable = Progress.CanPurchase(Choice.Next) &&
                    (bAllowSideBranches || !IsSideUpgrade(Choice.Next));
                const bool bPurchased = bChosenAffordable && Progress.Purchase(Choice.Next);
                if (bAnyAffordable)
                {
                    ++AffordableDecisions;
                    if (!bPurchased)
                    {
                        ++MissedAffordableDecisions;
                        ++ConsecutiveMissedDecisions;
                        MaxConsecutiveMissedDecisions = FMath::Max(MaxConsecutiveMissedDecisions, ConsecutiveMissedDecisions);
                    }
                    else ConsecutiveMissedDecisions = 0;
                }
                else { ++WaitingDecisions; ConsecutiveMissedDecisions = 0; }
                if (bPurchased)
                {
                    if (bTrace) UE_LOG(LogTemp, Display, TEXT("PROGRESSION_PURCHASE seed=%u side=%d sector=%d refining=%d time=%.0f index=%d cost=%d gap=%.0f"),
                        Seed, bAllowSideBranches ? 1 : 0, Sector, RefiningMode, Time, Choice.Next, Upgrade(Choice.Next).Cost, Time - LastPurchaseTime);
                    LongestPurchaseGap = FMath::Max(LongestPurchaseGap, Time - LastPurchaseTime);
                    LastPurchaseTime = Time;
                    ++PurchaseCount;
                    if (!IsSideUpgrade(Choice.Next) && ++CorePurchaseCount == 33) Core33Time = Time;
                    if (FirstSideTime < 0 && IsSideUpgrade(Choice.Next)) FirstSideTime = Time;
                    const int32 TotalNodes = bAllowSideBranches ? UpgradeCount : OriginalUpgradeCount;
                    if (HalfTreeTime < 0 && PurchaseCount >= (TotalNodes + 1) / 2) HalfTreeTime = Time;
                }
            }
            if (!Targets.IsEmpty() && Time - LastShot >= Progress.Cooldown())
            {
                LastShot = Time;
                struct FModelShot { FLaserCandidate Target; bool bReflected; };
                TArray<FModelShot, TInlineAllocator<6>> Shots;
                TArray<int32, TInlineAllocator<6>> Reserved;
                for (const FLaserCandidate& Target : Targets) { Shots.Add({ Target, false }); Reserved.Add(Target.Index); }
                if (Progress.Has(RicochetUpgrade))
                    for (const FLaserCandidate& Target : Targets)
                    {
                        const int32 Index = SelectRicochetTarget(RicochetCandidates, Reserved, Target.Position, Origin,
                            FVector2D(ShipX, Top + 29 * Pixel), 29 * Pixel, Progress.Range(), Height, Progress.TargetingMode);
                        if (Index == INDEX_NONE) continue;
                        for (const FLaserCandidate& Candidate : RicochetCandidates)
                            if (Candidate.Index == Index) { Shots.Add({ Candidate, true }); Reserved.Add(Index); break; }
                    }
                for (const FModelShot& Shot : Shots)
                {
                    const FLaserCandidate& Target = Shot.Target;
                    FBalanceRock& Rock = Rocks[Target.Index];
                    const bool bFirstHit = Rock.Durability.IsUndamaged(Target.Size);
                    const bool bHitObjective = Progress.Objective.Kind == 5;
                    const int32 HitReward = Progress.ObjectiveReward(Progress.Objective.RecordHit(Progress.RichOreChance() > 0.0f));
                    if (bLegacyGoals && Progress.Objective.Kind == 5) Progress.Objective.Kind = 1;
                    if (!(Shot.bReflected ? Rock.Durability.HitTenths(Progress.LaserDamage() * 4) : Rock.Durability.Hit(Progress.LaserDamage())))
                    {
                        Progress.AddResources(HitReward);
                        ObjectiveRewards += HitReward;
                        continue;
                    }
                    ++Destroyed;
                    if (AsteroidHitPoints(Target.Size) == 3) ++LargeDestroyed;
                    const bool bRich = IsRichOre(SectorSeed(Seed, Sector), Target.Index, Target.Pass, Rock.Generation, Progress.RichOreChance());
                    Rock.ResetTime = Time;
                    ++Rock.Generation;
                    const int32 Bonus = bEvents ? ScaledEventBonus(Event.ClaimBonus(Target.Index, SectorAsteroidCount(Sector)), Progress) : 0;
                    EventBonus += Bonus;
                    const int32 Mined = Progress.Yield(Target.Size, Target.OreKind, bRich, bFirstHit) + Bonus;
                    // Accepted ore cannot exceed the int32 per-asteroid yield.
                    const int32 AcceptedOre = static_cast<int32>(Progress.AddOre(Mined, Target.OreKind));
                    const int32 GoalReward = bHitObjective ? HitReward : Progress.ObjectiveReward(
                        Progress.Objective.Record(Target.OreKind, AcceptedOre, bRich, Progress.RichOreChance() > 0.0f));
                    if (bLegacyGoals && Progress.Objective.Kind == 5) Progress.Objective.Kind = 1;
                    Progress.AddResources(GoalReward);
                    ObjectiveRewards += GoalReward;
                }
            }
            if (bUseModule && Time >= Progress.ProductionReadyAt)
            {
                const EOreKind Scarcer = Progress.BlueOre <= Progress.BlackOre ? EOreKind::Blue : EOreKind::Black;
                Progress.ProduceRareOre(Scarcer, Time);
            }
            if (CheckpointIndex < Checkpoints.Num() && Tick == Checkpoints[CheckpointIndex] * StepsPerSecond)
            {
                if (bTrace) UE_LOG(LogTemp, Display, TEXT("PROGRESSION_MEASURE seed=%u side=%d sector=%d refining=%d scripted_manual=%d seconds=%d destroyed=%d large=%d core33=%.0f"),
                    Seed, bAllowSideBranches ? 1 : 0, Sector, RefiningMode, bScriptedManual ? 1 : 0, Checkpoints[CheckpointIndex], Destroyed, LargeDestroyed, Core33Time);
                int32 Purchased = 0, SidePurchased = 0, SideMask = 0;
                for (int32 I = 0; I < UpgradeCount; ++I) if (Progress.Has(I))
                {
                    ++Purchased;
                    if (IsSideUpgrade(I)) { ++SidePurchased; SideMask |= 1 << Upgrade(I).Column; }
                }
                Results.Add({ Purchased, Progress.TotalCollected, Progress.Isotope, Progress.Lasers(), Progress.LaserDamage(),
                    Progress.Yield(9, EOreKind::Common, false, true), Progress.Range(), Progress.SpeedMultiplier(), EventBonus,
                    SidePurchased, SideMask, Progress.TotalSpent, FMath::Max(LongestPurchaseGap, Time - LastPurchaseTime), FirstSideTime, HalfTreeTime,
                    Progress.Resources, AffordableDecisions, MissedAffordableDecisions, WaitingDecisions, MaxConsecutiveMissedDecisions * 2.0f,
                    Progress.Objective.Sequence, ObjectiveRewards });
                UE_LOG(LogTemp, Display, TEXT("BALANCE_SIM seed=%u sector=%d refining=%d module=%d spec=%d seconds=%d purchased=%d total=%d ore=%lld isotope=%lld lasers=%d damage=%d small_yield=%d range=%.0f speed=%.2f"),
                    Seed, Sector, RefiningMode, bUseModule ? 1 : 0, Specialization, Checkpoints[CheckpointIndex], Purchased, UpgradeCount, Progress.TotalCollected, Progress.Isotope,
                    Progress.Lasers(), Progress.LaserDamage(), Progress.Yield(9, EOreKind::Common, false, true), Progress.Range(), Progress.SpeedMultiplier());
                ++CheckpointIndex;
            }
        }
        return Results;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSideBranchBalanceTest, "SpaceFlight.SideBranches.Balance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSideBranchBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 1800, 7200, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Original = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, false, false, true);
        const auto Expanded = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, false, true, true);
        if (!TestEqual(TEXT("both models contain all checkpoints"), Expanded.Num(), 4) || Original.Num() != 4) return false;
        for (int32 I = 0; I < Checkpoints.Num(); ++I)
        {
            UE_LOG(LogTemp, Display, TEXT("SIDE_BALANCE seed=%u seconds=%d original=%d expanded=%d side=%d mask=%d ore_original=%lld ore=%lld spent=%lld gap_original=%.0f gap=%.0f first_side=%.0f half_original=%.0f half=%.0f"),
                Seed, Checkpoints[I], Original[I].Purchased, Expanded[I].Purchased, Expanded[I].SidePurchased,
                Expanded[I].SideMask, Original[I].Collected, Expanded[I].Collected, Expanded[I].Spent,
                Original[I].LongestGap, Expanded[I].LongestGap, Expanded[I].FirstSideTime, Original[I].HalfTreeTime, Expanded[I].HalfTreeTime);
        }
        TestTrue(TEXT("new choices do not finish half the tree in five minutes"), Expanded[0].Purchased * 2 < SpaceFlight::UpgradeCount);
        TestTrue(TEXT("two-hour progress remains below half the expanded tree"), Expanded[2].Purchased * 2 < SpaceFlight::UpgradeCount);
        TestTrue(TEXT("autopilot purchases side upgrades during a two-hour session"), Expanded[2].SidePurchased > 0);
        TestTrue(TEXT("about half the larger tree is accessible in several hours"), Expanded[3].Purchased * 2 >= SpaceFlight::UpgradeCount);
        TestTrue(TEXT("six-hour progress leaves substantial late development"), Expanded[3].Purchased <= 55);
        TestTrue(TEXT("the old core remains substantially reachable"), Expanded[3].Purchased - Expanded[3].SidePurchased >= 20);
    }
    return true;
}

// A diagnostic of the actual purchase policy, not a player-satisfaction test.
// Both rates use the model's two-second decision interval; a miss is an
// evaluation at which some allowed node is affordable but no purchase succeeds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAffordablePurchaseBalanceTest, "SpaceFlight.SideBranches.AffordablePurchaseBalance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAffordablePurchaseBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> LongCheckpoints{ 300, 7200, 21600 };
    const TArray<int32> ShortCheckpoints{ 300, 7200 };
    for (int32 Sector : { 0, 1, 2 })
        for (int32 Refining : { 0, 1 })
            for (uint32 Seed : { 1024u, 7331u, 204810u })
            {
                // A six-hour autopilot model with default refining plus a
                // two-hour scenario with voluntary refining disabled.
                const TArray<int32>& Checkpoints = Refining == 0 ? LongCheckpoints : ShortCheckpoints;
                const auto Results = SimulateProgress(Seed, Checkpoints, Sector, Refining);
                if (!TestEqual(TEXT("affordability scenario has all checkpoints"), Results.Num(), Checkpoints.Num())) return false;
                for (int32 I = 0; I < Results.Num(); ++I)
                {
                    const auto& S = Results[I];
                    UE_LOG(LogTemp, Display, TEXT("AFFORDABILITY_BALANCE seed=%u sector=%d refining=%d seconds=%d purchases=%d reserve=%lld isotope=%lld affordable=%d waiting=%d missed=%d max_missed_seconds=%.0f"),
                        Seed, Sector, Refining, Checkpoints[I], S.Purchased, S.Reserve, S.Isotope,
                        S.AffordableDecisions, S.WaitingDecisions, S.MissedAffordableDecisions, S.MaxMissedAffordableSeconds);
                    TestEqual(TEXT("an affordable upgrade always results in a purchase at a policy decision"), S.MissedAffordableDecisions, 0);
                    TestTrue(TEXT("resources remain nonnegative"), S.Reserve >= 0 && S.Isotope >= 0);
                }
                TestTrue(TEXT("autopilot actually exercises affordable purchases"), Results.Last().AffordableDecisions > 0);
            }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightEventBalanceTest, "SpaceFlight.Events.Balance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightEventBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 7200, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Baseline = SimulateProgress(Seed, Checkpoints);
        const auto Events = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, true);
        TestEqual(TEXT("baseline and event checkpoints exist"), Events.Num(), Baseline.Num());
        if (Events.Num() != 3 || Baseline.Num() != 3) return false;
        for (int32 I = 0; I < 3; ++I)
        {
            UE_LOG(LogTemp, Display, TEXT("EVENT_BALANCE seed=%u seconds=%d baseline_purchases=%d event_purchases=%d baseline_ore=%lld event_ore=%lld direct_bonus=%d"),
                Seed, Checkpoints[I], Baseline[I].Purchased, Events[I].Purchased, Baseline[I].Collected, Events[I].Collected, Events[I].EventBonus);
            TestTrue(TEXT("event rewards remain bounded in the model"), Events[I].EventBonus <= 45 * (1 + Checkpoints[I] / 180));
            TestTrue(TEXT("events do not accelerate the tree dramatically"), Events[I].Purchased <= Baseline[I].Purchased + 3);
        }
        TestTrue(TEXT("events award some ore during a long session"), Events[2].EventBonus > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSpecializationBalanceTest, "SpaceFlight.Specialization.Balance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSpecializationBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 7200, 21600 };
    for (int32 Specialization : { 1, 2, 3 })
        for (uint32 Seed : { 1024u, 7331u, 204810u })
        {
            const auto Results = SimulateProgress(Seed, Checkpoints, 0, 0, Specialization == 3, Specialization);
            TestEqual(TEXT("all specialization checkpoints exist"), Results.Num(), 3);
            if (Results.Num() != 3) continue;
            TestTrue(TEXT("specialization leaves early upgrades attainable"), Results[0].Purchased > 0);
            TestTrue(TEXT("specialization keeps two-hour progression gradual"), Results[1].Purchased * 2 < SpaceFlight::UpgradeCount);
            TestTrue(TEXT("specialization reaches half the tree in several hours"), Results[2].Purchased * 2 >= SpaceFlight::UpgradeCount);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightModuleBalanceTest, "SpaceFlight.Production.ModuleBalance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightModuleBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 7200, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Results = SimulateProgress(Seed, Checkpoints, 0, 0, true);
        TestEqual(TEXT("all module balance checkpoints exist"), Results.Num(), 3);
        if (Results.Num() != 3) continue;
        TestTrue(TEXT("regular use does not nearly finish the tree in five minutes"), Results[0].Purchased * 2 < SpaceFlight::UpgradeCount);
        TestTrue(TEXT("regular use keeps two-hour progression gradual"), Results[1].Purchased * 2 < SpaceFlight::UpgradeCount);
        TestTrue(TEXT("regular use reaches half the tree within several hours"), Results[2].Purchased * 2 >= SpaceFlight::UpgradeCount);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRefiningBalanceTest, "SpaceFlight.Production.Balance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRefiningBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 7200, 21600 };
    for (int32 Mode : { 1, 2, 3, 5 })
        for (uint32 Seed : { 1024u, 7331u })
        {
            const auto Results = SimulateProgress(Seed, Checkpoints, 0, Mode, false, 0, false, true, Seed == 1024u);
            TestEqual(TEXT("all production balance checkpoints exist"), Results.Num(), 3);
            if (Results.Num() != 3) continue;
            TestTrue(TEXT("production setting leaves early improvements attainable"), Results[0].Purchased > 0);
            TestTrue(TEXT("production setting keeps early progression gradual"), Results[1].Purchased * 2 < SpaceFlight::UpgradeCount);
            TestTrue(TEXT("ordinary upgrades remain attainable with any setting"), Results[2].Purchased >= 20);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSectorBalanceTest, "SpaceFlight.Sectors.Balance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSectorBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 7200, 21600 };
    for (int32 Sector : { 1, 2 })
        for (uint32 Seed : { 1024u, 7331u, 204810u })
        {
            const auto Results = SimulateProgress(Seed, Checkpoints, Sector, 0, false, 0, false, true, Seed == 1024u);
            TestEqual(TEXT("all sector checkpoints exist"), Results.Num(), 3);
            if (Results.Num() != 3) continue;
            TestTrue(TEXT("sector offers early improvements"), Results[0].Purchased > 0);
            TestTrue(TEXT("sector supports continued progress"), Results[1].Purchased >= 15);
            TestTrue(TEXT("sector does not open half the tree within two hours"), Results[1].Purchased * 2 < SpaceFlight::UpgradeCount);
            TestTrue(TEXT("sector can reach half the tree within several hours"), Results[2].Purchased * 2 >= SpaceFlight::UpgradeCount);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightHitObjectiveBalanceTest, "SpaceFlight.Balance.HitObjectiveComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightHitObjectiveBalanceTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{300, 7200, 21600};
    for (uint32 Seed : {1024u, 7331u, 204810u})
    {
        const auto Current = SimulateProgress(Seed, Checkpoints);
        const auto Before = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, false, true, false, false, false, 12.0f, true);
        if (!TestEqual(TEXT("new objective checkpoints exist"), Current.Num(), Checkpoints.Num()) ||
            !TestEqual(TEXT("legacy objective checkpoints exist"), Before.Num(), Checkpoints.Num())) return false;
        for (int32 I = 0; I < Checkpoints.Num(); ++I)
        {
            UE_LOG(LogTemp, Display, TEXT("HIT_GOAL_COMPARE seed=%u seconds=%d before_purchased=%d after_purchased=%d before_ore=%lld after_ore=%lld before_goals=%d after_goals=%d before_rewards=%d after_rewards=%d"),
                Seed, Checkpoints[I], Before[I].Purchased, Current[I].Purchased, Before[I].Collected, Current[I].Collected,
                Before[I].ObjectivesCompleted, Current[I].ObjectivesCompleted, Before[I].ObjectiveRewards, Current[I].ObjectiveRewards);
            TestTrue(TEXT("both objective schedules remain achievable"), Current[I].ObjectivesCompleted > 0 && Before[I].ObjectivesCompleted > 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightScriptedManualBalanceTest, "SpaceFlight.SideBranches.ScriptedManual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightScriptedManualBalanceTest::RunTest(const FString& Parameters)
{
    // Fixed left/right inputs isolate movement; both scenarios still use the
    // same automatic purchase policy. This is not a human playtest.
    const TArray<int32> Checkpoints{ 300, 1800, 7200, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Results = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, false, true, true, true);
        if (!TestEqual(TEXT("scripted manual simulation has all checkpoints"), Results.Num(), Checkpoints.Num())) return false;
        TestTrue(TEXT("scripted manual movement retains progress"), Results.Last().Purchased > Results[1].Purchased);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightBalanceTimingResolutionTest, "SpaceFlight.SideBranches.TimingResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightBalanceTimingResolutionTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 1800, 7200 };
    const auto Coarse = SimulateProgress(1024, Checkpoints);
    const auto Fine = SimulateProgress(1024, Checkpoints, 0, 0, false, 0, false, true, false, false, true);
    if (!TestEqual(TEXT("coarse timing checkpoints"), Coarse.Num(), Checkpoints.Num()) ||
        !TestEqual(TEXT("fine timing checkpoints"), Fine.Num(), Checkpoints.Num())) return false;
    for (int32 I = 0; I < Checkpoints.Num(); ++I)
        UE_LOG(LogTemp, Display, TEXT("TIMING_RESOLUTION seconds=%d coarse=%d fine=%d coarse_ore=%lld fine_ore=%lld"),
            Checkpoints[I], Coarse[I].Purchased, Fine[I].Purchased, Coarse[I].Collected, Fine[I].Collected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightForecastHorizonTest, "SpaceFlight.SideBranches.ForecastHorizon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightForecastHorizonTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 1800, 7200, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Short = SimulateProgress(Seed, Checkpoints);
        const auto Long = SimulateProgress(Seed, Checkpoints, 0, 0, false, 0, false, true, false, false, false, 18.0f);
        if (!TestEqual(TEXT("short forecast checkpoints"), Short.Num(), Checkpoints.Num()) ||
            !TestEqual(TEXT("long forecast checkpoints"), Long.Num(), Checkpoints.Num())) return false;
        for (int32 I = 0; I < Checkpoints.Num(); ++I)
            UE_LOG(LogTemp, Display, TEXT("HORIZON_COMPARE seed=%u seconds=%d short=%d long=%d short_ore=%lld long_ore=%lld"),
                Seed, Checkpoints[I], Short[I].Purchased, Long[I].Purchased, Short[I].Collected, Long[I].Collected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightEarlyPacingTest, "SpaceFlight.Balance.EarlyPacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightEarlyPacingTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 60, 300 };
    const auto Results = SimulateProgress(1024, Checkpoints);
    TestEqual(TEXT("both checkpoints are produced"), Results.Num(), 2);
    if (Results.Num() != 2) return false;
    TestTrue(TEXT("the beginning contains achievable improvements"), Results[1].Purchased > 0);
    TestTrue(TEXT("five minutes does not nearly complete the tree"), Results[1].Purchased * 2 < SpaceFlight::UpgradeCount);
    TestTrue(TEXT("five minute combat power stays below its final damage"), Results[1].Damage < 3);
    TestTrue(TEXT("five minute range and yield remain moderate"), Results[1].Range <= 320.0f && Results[1].SmallYield <= 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightLongPacingTest, "SpaceFlightLong.Balance.Pacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightLongPacingTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 300, 1800, 3600, 7200, 14400, 21600 };
    for (uint32 Seed : { 1024u, 7331u, 204810u })
    {
        const auto Results = SimulateProgress(Seed, Checkpoints);
        TestEqual(TEXT("all long-session checkpoints are produced"), Results.Num(), Checkpoints.Num());
        if (Results.Num() != Checkpoints.Num()) continue;
        TestTrue(TEXT("two hours do not yet finish half the tree"), Results[3].Purchased * 2 < SpaceFlight::UpgradeCount);
        TestTrue(TEXT("progress continues through several hours"), Results[4].Purchased > Results[2].Purchased);
        TestTrue(TEXT("about half the tree is reachable within several hours"), Results[5].Purchased * 2 >= SpaceFlight::UpgradeCount);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightMarathonPacingTest, "BalanceMarathon.Progress.LongHorizon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightMarathonPacingTest::RunTest(const FString& Parameters)
{
    const TArray<int32> Checkpoints{ 7200, 21600, 43200, 86400 };
    const auto Results = SimulateProgress(1024, Checkpoints);
    TestEqual(TEXT("all marathon checkpoints are produced"), Results.Num(), Checkpoints.Num());
    if (Results.Num() == Checkpoints.Num())
    {
        TestTrue(TEXT("late development continues after the midpoint"), Results[3].Purchased > Results[1].Purchased);
        TestTrue(TEXT("later upgrades remain attainable in the long session model"), Results[3].Purchased >= 45);
    }
    return true;
}
#endif
