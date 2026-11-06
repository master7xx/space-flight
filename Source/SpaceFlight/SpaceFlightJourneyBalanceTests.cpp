#include "SpaceFlightLogic.h"
#include "SpaceFlightSectors.h"
#include "SpaceFlightEvents.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
    using namespace SpaceFlight;

    // Diagnostic model, independent of the older fixed-sector balance model.
    // It deliberately shares game rules, but not game frame scheduling or UI.
    enum class EJourneyStyle : uint8 { Auto, CoreLaser, ManualSide };
    struct FJourneyRock
    {
        float Speed = 0.0f;
        float InitialTravel = 0.0f;
        float ResetTime = -1.0f;
        int32 Generation = 0;
        FAsteroidDurability Durability;
    };
    struct FJourneySnapshot
    {
        int32 Seconds = 0, Sector = 0, Goals = 0, Purchased = 0, Side = 0, Micro = 0;
        int32 Hits = 0, Destroyed = 0, EventBonus = 0, MissedAuto = 0;
        int64 Resources = 0, Blue = 0, Black = 0, Isotope = 0;
        int64 Collected = 0, Spent = 0, Converted = 0, IsotopeSpent = 0;
        int64 MinedOre = 0, ExpeditionCredited = 0, ExpeditionReward = 0;
        int64 MinedBlue = 0, MinedBlack = 0;
        int32 RicochetHits = 0;
        int32 ExpeditionClaims = 0;
        float HalfTreeTime = -1, FirstSector1 = -1, FirstSector2 = -1, LongestGap = 0;
        uint32 PurchaseHash = 0;
    };
    struct FJourneyResult
    {
        TArray<FJourneySnapshot> Checkpoints;
        int32 Transitions = 0, InvalidTransitions = 0, InvalidPurchases = 0;
        int32 AccountingFailures = 0, MissedAuto = 0, ExpeditionFailures = 0, DuplicateClaimFailures = 0;
    };

    int32 PreferredPurchase(const FProgress& Progress, EJourneyStyle Style)
    {
        int32 Best = INDEX_NONE;
        for (int32 I = 0; I < UpgradeCount; ++I)
        {
            if (!Progress.CanPurchase(I)) continue;
            const bool Preferred = Style == EJourneyStyle::CoreLaser
                ? !IsSideUpgrade(I) && Upgrade(I).Column == 2 : IsSideUpgrade(I);
            const bool BestPreferred = Best != INDEX_NONE && (Style == EJourneyStyle::CoreLaser
                ? !IsSideUpgrade(Best) && Upgrade(Best).Column == 2 : IsSideUpgrade(Best));
            if (Best == INDEX_NONE || (Preferred && !BestPreferred) ||
                (Preferred == BestPreferred && Upgrade(I).Cost < Upgrade(Best).Cost)) Best = I;
        }
        return Best;
    }

    FJourneyResult SimulateJourney(uint32 Seed, EJourneyStyle Style, bool bExplore, bool bFineStep = false,
        int32 EndSeconds = 21600, bool bRunExpeditions = false,
        int32 FixedSector = INDEX_NONE, int32 ExpeditionRoute = 0, bool bExperimentalLegacyColourReward = false,
        int32 TargetingMode = 0, int32 RicochetScenario = 0)
    {
        constexpr float Width = 1280.0f, Height = 720.0f, Pixel = 1.0f, Unit = 4.0f;
        const float Step = bFineStep ? .025f : .1f;
        const int32 StepsPerSecond = bFineStep ? 40 : 10;
        const int32 Checkpoints[] = { 300, 1800, 7200, 21600 };
        FJourneyResult Result;
        FProgress Progress;
        Progress.TargetingMode = TargetingMode;
        if (RicochetScenario > 0)
        {
            // Paired diagnostic loadouts, not purchases or a new-player run.
            Progress.Acquired[5] = true;
            Progress.Acquired[71] = true;
            if (RicochetScenario == 3 || RicochetScenario == 4 || RicochetScenario == 6)
            { Progress.Acquired[6] = true; Progress.Acquired[14] = true; Progress.Acquired[22] = true; }
            if (RicochetScenario == 2 || RicochetScenario == 4 || RicochetScenario >= 5) Progress.Acquired[RicochetUpgrade] = true;
        }
        // Diagnostic scenario only: a coloured sector is unlocked at the
        // start, with no retroactive rewards for those earlier objectives.
        if (FixedSector >= 0 && FixedSector < SectorCount)
        {
            Progress.CurrentSector = FixedSector;
            Progress.Objective.Sequence = FixedSector == 1 ? 3 : (FixedSector == 2 ? 8 : 0);
        }
        FMineralEvent Event;
        Event.Initialize(Seed);
        FJourneyRock Rocks[NormalAsteroidCount];
        auto ResetRocks = [&](int32 Sector)
        {
            for (int32 I = 0; I < NormalAsteroidCount; ++I)
            {
                Rocks[I] = FJourneyRock{};
                FRandomStream Random(4627 + I * 719);
                Rocks[I].Speed = Unit * Random.FRandRange(9.0f, 21.0f) * SectorSpeedMultiplier(Sector);
                Rocks[I].InitialTravel = Random.FRand() * Height;
            }
        };
        ResetRocks(Progress.CurrentSector);
        float ShipX = Width * .5f, Bank = 0, LastShot = -100.0f;
        float NextDecision = 0, NextSectorChange = 0, LastPurchase = 0, LongestGap = 0;
        float HalfTreeTime = -1, FirstSector1 = -1, FirstSector2 = -1;
        int32 Purchased = 0, Hits = 0, Destroyed = 0, EventBonus = 0, Checkpoint = 0;
        int64 MinedOre = 0, ExpeditionCredited = 0, ExpeditionReward = 0;
        int64 MinedBlue = 0, MinedBlack = 0;
        int32 RicochetHits = 0;
        int32 ExpeditionClaims = 0;
        uint32 PurchaseHash = 0;
        for (int32 Tick = 0; Tick <= EndSeconds * StepsPerSecond; ++Tick)
        {
            const float Time = Tick * Step;
            // Approximation: a player starts the chosen route immediately and
            // confirms it after a volley. Real UI input and saving are not simulated.
            if (bRunExpeditions && Progress.Expedition.State == EExpeditionState::Idle)
            {
                const int32 Target = static_cast<int32>(FMath::Clamp<int64>(100 + Progress.TotalCollected / 20, 100, 10000));
                if (!Progress.Expedition.Start(ExpeditionRoute, Target, UnlockedSector(Progress.Objective.Sequence))) ++Result.ExpeditionFailures;
            }
            if (bExplore && UnlockedSector(Progress.Objective.Sequence) > Progress.CurrentSector && Time >= NextSectorChange)
            {
                const int32 Old = Progress.CurrentSector;
                const int32 Next = NextSector(Old, Progress.Objective.Sequence);
                if (Next != Old + 1 || Next > UnlockedSector(Progress.Objective.Sequence)) ++Result.InvalidTransitions;
                Progress.CurrentSector = Next;
                NextSectorChange = Time + 12.0f;
                if (Next == 1 && FirstSector1 < 0) FirstSector1 = Time;
                if (Next == 2 && FirstSector2 < 0) FirstSector2 = Time;
                ++Result.Transitions;
                ResetRocks(Next);
                NextDecision = Time;
            }
            const int32 Sector = Progress.CurrentSector;
            const int32 Count = SectorAsteroidCount(Sector);
            if (Tick > 0) Event.Advance(Step, false, Count);
            const float PreviousX = ShipX;
            if (Style == EJourneyStyle::ManualSide)
                ShipX += (FMath::Fmod(Time, 8.0f) < 4.0f ? 1.0f : -1.0f) * Step * Width * .30f * Progress.SpeedMultiplier();
            else
            {
                const float Destination = Width * (0.5f + 0.27f * FMath::Sin(Time * .25f));
                ShipX = FMath::FInterpConstantTo(ShipX, Destination, Step, Width * .30f * Progress.SpeedMultiplier() * .55f);
            }
            ShipX = FMath::Clamp(ShipX, 32.0f * Pixel, Width - 32.0f * Pixel);
            Bank = FMath::FInterpTo(Bank, ShipManeuverBank(ShipX - PreviousX, Step, Width, Progress), Step, 5.5f);
            const float Top = Height * .69f - 29.0f * Pixel + FMath::Sin(Time * 1.4f) * Unit / 3.0f;
            const FVector2D Origin = ShipPoint(28, 4, ShipX, Top, Pixel, Bank);
            const bool bEvaluate = RicochetScenario == 0 && Time >= NextDecision;
            TArray<FLaserCandidate, TInlineAllocator<3>> Targets;
            TArray<FLaserCandidate, TInlineAllocator<32>> RicochetCandidates;
            const bool bProtectFirstHitBonus = Progress.Has(RicochetUpgrade) && Progress.FirstHitOreBonus() > 0;
            const int32 EquippedDamage = bProtectFirstHitBonus ? Progress.LaserDamage() : 0;
            TArray<FForecastRock, TInlineAllocator<MaxForecastRocks>> Forecast;
            for (int32 I = 0; I < Count; ++I)
            {
                FJourneyRock& Rock = Rocks[I];
                const int32 Size = AsteroidSizeForIndex(I);
                const float Radius = (Size * .5f + 1.0f) * Pixel;
                const float Travel = Rock.ResetTime >= 0 ? (Time - Rock.ResetTime) * Rock.Speed : Rock.InitialTravel + Time * Rock.Speed;
                const int32 Pass = FMath::FloorToInt(Travel / Height);
                const float Y = FMath::Fmod(Travel, Height);
                Rock.Durability.Prepare(Pass, Rock.Generation, Size);
                const float BaseX = ChooseAsteroidSpawn(SectorSeed(Seed, Sector), I, Pass, Rock.Generation, Width, Pixel, Radius).X;
                const float X = AvoidShip(BaseX, Y, ShipX, Top + 29.0f * Pixel, Width, Pixel, Radius);
                const EOreKind Kind = SectorOreKind(Sector, Seed, I, Pass, Rock.Generation);
                const bool bMarked = Event.IsMarked(I, Count);
                const bool bDamaged = !Rock.Durability.IsUndamaged(Size);
                const FLaserCandidate Candidate{ I, Pass, Size, FVector2D(X, Y), FLinearColor::White, 0, Kind, bMarked, bDamaged,
                    RicochetWouldSpoilFirstHitBonus(Size, EquippedDamage, bDamaged, bProtectFirstHitBonus) };
                AddLaserCandidate(Targets, Candidate, Origin, Height, Progress.Range(), Progress.Lasers(), Progress.TargetingMode);
                if (Progress.Has(RicochetUpgrade) && Y >= 0 && Y <= Height && FVector2D::DistSquared(Origin, Candidate.Position) <= FMath::Square(Progress.Range()))
                    RicochetCandidates.Add(Candidate);
                if (bEvaluate) Forecast.Add({ BaseX, Y, Rock.Speed, Radius, Size, true, Rock.Durability.Remaining, Kind, bMarked, Rock.Durability.RemainingTenths });
            }
            if (bEvaluate)
            {
                NextDecision = Time + (Style == EJourneyStyle::ManualSide ? 10.0f : 1.0f);
                int32 Choice = INDEX_NONE;
                bool bAnyAffordable = false;
                for (int32 I = 0; I < UpgradeCount; ++I) bAnyAffordable |= Progress.CanPurchase(I);
                if (Style == EJourneyStyle::Auto)
                {
                    Forecast.Sort([&](const FForecastRock& A, const FForecastRock& B) {
                        return FVector2D::DistSquared(FVector2D(A.BaseX, A.Y), Origin) <
                            FVector2D::DistSquared(FVector2D(B.BaseX, B.Y), Origin);
                    });
                    const FForecastContext Context{ Width, Height, Pixel, ShipX, Top + 29 * Pixel,
                        static_cast<float>(Origin.Y), Time, Time - LastShot };
                    Choice = ChooseAutoUpgrade(Progress, Context, Forecast).Next;
                }
                else Choice = PreferredPurchase(Progress, Style);
                const bool bCanPurchase = Progress.CanPurchase(Choice);
                if (Style == EJourneyStyle::Auto && bAnyAffordable && !bCanPurchase) ++Result.MissedAuto;
                if (bCanPurchase)
                {
                    const int32 Parent = Upgrade(Choice).Parent;
                    if (Parent >= 0 && !Progress.Has(Parent)) ++Result.InvalidPurchases;
                    if (!Progress.Purchase(Choice)) ++Result.InvalidPurchases;
                    else
                    {
                        PurchaseHash = HashCombineFast(PurchaseHash, GetTypeHash(Choice));
                        LongestGap = FMath::Max(LongestGap, Time - LastPurchase);
                        LastPurchase = Time;
                        if (++Purchased * 2 >= UpgradeCount && HalfTreeTime < 0) HalfTreeTime = Time;
                    }
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
                            FVector2D(ShipX, Top + 29 * Pixel), 29 * Pixel, Progress.Range(), Height, Progress.TargetingMode,
                            RicochetScenario >= 5 ? 160.0f : RicochetRange);
                        if (Index == INDEX_NONE) continue;
                        for (const FLaserCandidate& Candidate : RicochetCandidates)
                            if (Candidate.Index == Index) { Shots.Add({ Candidate, true }); Reserved.Add(Index); break; }
                    }
                for (const FModelShot& Shot : Shots)
                {
                    const FLaserCandidate& Target = Shot.Target;
                    if (Shot.bReflected) ++RicochetHits;
                    ++Hits;
                    FJourneyRock& Rock = Rocks[Target.Index];
                    const bool bFirstHit = Rock.Durability.IsUndamaged(Target.Size);
                    const bool bHitObjective = Progress.Objective.Kind == 5;
                    const int32 HitReward = Progress.ObjectiveReward(Progress.Objective.RecordHit(Progress.RichOreChance() > 0));
                    const bool bDestroyed = Shot.bReflected ? Rock.Durability.HitTenths(Progress.LaserDamage() * 4) : Rock.Durability.Hit(Progress.LaserDamage());
                    if (!bDestroyed) { Progress.AddResources(HitReward); continue; }
                    ++Destroyed;
                    const bool bRich = IsRichOre(SectorSeed(Seed, Sector), Target.Index, Target.Pass, Rock.Generation, Progress.RichOreChance());
                    Rock.ResetTime = Time;
                    ++Rock.Generation;
                    const int32 Bonus = ScaledEventBonus(Event.ClaimBonus(Target.Index, Count), Progress);
                    EventBonus += Bonus;
                    const int32 Mined = Progress.Yield(Target.Size, Target.OreKind, bRich, bFirstHit) + Bonus;
                    const int32 AcceptedOre = static_cast<int32>(Progress.AddOre(Mined, Target.OreKind));
                    MinedOre += AcceptedOre;
                    if (Target.OreKind == EOreKind::Blue) MinedBlue += AcceptedOre;
                    if (Target.OreKind == EOreKind::Black) MinedBlack += AcceptedOre;
                    if (bRunExpeditions)
                        ExpeditionCredited += Progress.Expedition.Record(Sector, static_cast<int32>(Target.OreKind), AcceptedOre, false);
                    const int32 GoalReward = bHitObjective ? HitReward : Progress.ObjectiveReward(
                        Progress.Objective.Record(Target.OreKind, AcceptedOre, bRich, Progress.RichOreChance() > 0));
                    Progress.AddResources(GoalReward);
                }
                if (bRunExpeditions && Progress.Expedition.State == EExpeditionState::Ready)
                {
                    const bool bLegacy = bExperimentalLegacyColourReward && ExpeditionRoute != 0;
                    const int32 Expected = bLegacy ? Progress.Expedition.Target / 20 :
                        FExpedition::RewardAmount(Progress.Expedition.Route, Progress.Expedition.Target);
                    const bool bHasRoom = Progress.TotalCollected >= 0 &&
                        Progress.TotalCollected <= ResourceAccountingLimit - Expected;
                    int32 Reward = 0;
                    if (bLegacy && bHasRoom)
                    {
                        // Diagnostic old-rate policy; the production claim now
                        // uses 10% for coloured routes. No persistence here.
                        Progress.Expedition.Claim();
                        Reward = static_cast<int32>(Progress.AddOre(Expected,
                            static_cast<EOreKind>(FExpedition::RewardOreCode(ExpeditionRoute))));
                    }
                    else if (!bLegacy) Reward = Progress.ClaimExpeditionReward();
                    if (!bHasRoom)
                    {
                        if (Reward != 0 || Progress.Expedition.State != EExpeditionState::Ready)
                            ++Result.ExpeditionFailures;
                    }
                    else if (Reward != Expected || Progress.Expedition.State != EExpeditionState::Idle)
                        ++Result.ExpeditionFailures;
                    else
                    {
                        ExpeditionReward += Reward;
                        ++ExpeditionClaims;
                        const int64 Resources = Progress.Resources, Collected = Progress.TotalCollected;
                        const int64 Blue = Progress.BlueOre, Black = Progress.BlackOre, Isotope = Progress.Isotope;
                        const int64 Converted = Progress.OreConverted;
                        if (Progress.ClaimExpeditionReward() != 0 || Progress.Resources != Resources ||
                            Progress.TotalCollected != Collected || Progress.BlueOre != Blue ||
                            Progress.BlackOre != Black || Progress.Isotope != Isotope || Progress.OreConverted != Converted)
                            ++Result.DuplicateClaimFailures;
                    }
                }
            }
            if (!Progress.Expedition.IsValidForUnlockedSector(UnlockedSector(Progress.Objective.Sequence))) ++Result.ExpeditionFailures;
            if (Progress.Resources < 0 || Progress.BlueOre < 0 || Progress.BlackOre < 0 || Progress.CommonOre() < 0 ||
                Progress.Isotope < 0 || Progress.Resources != Progress.TotalCollected - Progress.TotalSpent - Progress.OreConverted ||
                Progress.OreConverted != 2 * (Progress.Isotope + Progress.IsotopeSpent)) ++Result.AccountingFailures;
            if (Checkpoint < UE_ARRAY_COUNT(Checkpoints) && Tick == Checkpoints[Checkpoint] * StepsPerSecond)
            {
                FJourneySnapshot S;
                S.Seconds = Checkpoints[Checkpoint++]; S.Sector = Sector; S.Goals = Progress.Objective.Sequence;
                S.Purchased = Purchased; S.Hits = Hits; S.Destroyed = Destroyed; S.EventBonus = EventBonus;
                S.MissedAuto = Result.MissedAuto; S.Resources = Progress.Resources; S.Blue = Progress.BlueOre;
                S.Black = Progress.BlackOre; S.Isotope = Progress.Isotope; S.Collected = Progress.TotalCollected;
                S.Spent = Progress.TotalSpent; S.Converted = Progress.OreConverted; S.IsotopeSpent = Progress.IsotopeSpent;
                S.MinedOre = MinedOre; S.ExpeditionCredited = ExpeditionCredited;
                S.MinedBlue = MinedBlue; S.MinedBlack = MinedBlack;
                S.RicochetHits = RicochetHits;
                S.ExpeditionReward = ExpeditionReward; S.ExpeditionClaims = ExpeditionClaims;
                S.HalfTreeTime = HalfTreeTime; S.FirstSector1 = FirstSector1; S.FirstSector2 = FirstSector2;
                S.LongestGap = FMath::Max(LongestGap, Time - LastPurchase); S.PurchaseHash = PurchaseHash;
                for (int32 I = 0; I < UpgradeCount; ++I) if (Progress.Has(I))
                { if (IsSideUpgrade(I)) ++S.Side; if (Upgrade(I).bMicro) ++S.Micro; }
                Result.Checkpoints.Add(S);
            }
        }
        return Result;
    }

    void LogJourney(uint32 Seed, EJourneyStyle Style, bool bExplore, bool bFine, const FJourneyResult& Result)
    {
        for (const FJourneySnapshot& S : Result.Checkpoints)
            UE_LOG(LogTemp, Display, TEXT("JOURNEY_BALANCE seed=%u style=%d explore=%d fine=%d seconds=%d sector=%d goals=%d purchased=%d side=%d micro=%d half=%.1f sector1=%.1f sector2=%.1f gap=%.1f hits=%d destroyed=%d event_bonus=%d ore=%lld blue=%lld black=%lld isotope=%lld collected=%lld spent=%lld converted=%lld isotope_spent=%lld missed_auto=%d purchase_hash=%u"),
                Seed, static_cast<int32>(Style), bExplore ? 1 : 0, bFine ? 1 : 0, S.Seconds, S.Sector, S.Goals,
                S.Purchased, S.Side, S.Micro, S.HalfTreeTime, S.FirstSector1, S.FirstSector2, S.LongestGap,
                S.Hits, S.Destroyed, S.EventBonus, S.Resources, S.Blue, S.Black, S.Isotope, S.Collected,
                S.Spent, S.Converted, S.IsotopeSpent, S.MissedAuto, S.PurchaseHash);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRicochetBalanceTest, "SpaceFlight.Balance.RicochetComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRicochetBalanceTest::RunTest(const FString& Parameters)
{
    // Paired fixed loadouts for 1800 simulated seconds: no upgrades are bought,
    // sector is pre-unlocked, and matching seeds do not imply matching later
    // trajectories after the two policies start destroying different rocks.
    for (uint32 Seed : { 1024u, 7331u, 204810u })
        for (int32 Sector : { 1, 2 })
            for (int32 Scenario = 1; Scenario <= 6; ++Scenario)
            {
                const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                    1800, false, Sector, 0, false, 0, Scenario);
                const FString Context = FString::Printf(TEXT("seed=%u sector=%d scenario=%d"), Seed, Sector, Scenario);
                TestEqual(Context + TEXT(": checkpoints"), Result.Checkpoints.Num(), 2);
                TestEqual(Context + TEXT(": accounting"), Result.AccountingFailures, 0);
                TestEqual(Context + TEXT(": purchases remain frozen"), Result.InvalidPurchases, 0);
                if (Result.Checkpoints.Num() != 2) continue;
                for (const FJourneySnapshot& S : Result.Checkpoints)
                {
                    TestEqual(Context + TEXT(": no purchases"), S.Purchased, 0);
                    UE_LOG(LogTemp, Display, TEXT("RICOCHET_BALANCE seed=%u sector=%d scenario=%d seconds=%d mined=%lld blue=%lld black=%lld hits=%d reflected=%d destroyed=%d event_bonus=%d goals=%d"),
                        Seed, Sector, Scenario, S.Seconds, S.MinedOre, S.MinedBlue, S.MinedBlack,
                        S.Hits, S.RicochetHits, S.Destroyed, S.EventBonus, S.Goals);
                }
            }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionShortBalanceTest, "SpaceFlight.Balance.ExpeditionShort",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionShortBalanceTest::RunTest(const FString& Parameters)
{
    // Fixed-step model only: optimistic free-route use is not a real player's
    // actions and does not measure UI time, persistence, or subjective pacing.
    for (const uint32 Seed : { 1024u, 7331u, 204810u })
    {
        for (const bool bExpeditions : { false, true })
        {
            const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, true, false, 300, bExpeditions);
            TestEqual(TEXT("five-minute checkpoint exists"), Result.Checkpoints.Num(), 1);
            TestEqual(TEXT("resource accounting remains valid"), Result.AccountingFailures, 0);
            TestEqual(TEXT("expedition state remains valid"), Result.ExpeditionFailures, 0);
            TestEqual(TEXT("all purchases respect prerequisites"), Result.InvalidPurchases, 0);
            TestEqual(TEXT("sector transitions respect unlocks"), Result.InvalidTransitions, 0);
            TestEqual(TEXT("autopilot does not miss affordable nodes"), Result.MissedAuto, 0);
            if (Result.Checkpoints.Num() != 1) continue;
            const FJourneySnapshot& S = Result.Checkpoints[0];
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_BALANCE_SHORT seed=%u expeditions=%d seconds=%d mined=%lld collected=%lld direct_bonus=%lld credited=%lld claims=%d purchases=%d"),
                Seed, bExpeditions ? 1 : 0, S.Seconds, S.MinedOre, S.Collected,
                S.ExpeditionReward, S.ExpeditionCredited, S.ExpeditionClaims, S.Purchased);
            TestTrue(TEXT("only accepted mining can count toward expeditions"), S.ExpeditionCredited <= S.MinedOre);
            TestTrue(TEXT("direct rewards do not exceed five percent of credited mining"), S.ExpeditionReward * 20 <= S.ExpeditionCredited);
            if (bExpeditions) TestTrue(TEXT("the scenario claims at least one expedition"), S.ExpeditionClaims > 0);
            else
            {
                TestEqual(TEXT("disabled policy gives no expedition bonus"), S.ExpeditionReward, int64(0));
                TestEqual(TEXT("disabled policy gives no expedition credit"), S.ExpeditionCredited, int64(0));
                TestEqual(TEXT("disabled policy makes no claims"), S.ExpeditionClaims, 0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTargetingBalanceTest, "SpaceFlight.Balance.TargetingShort",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTargetingBalanceTest::RunTest(const FString& Parameters)
{
    // Five simulated minutes, fixed unlocked sector and autopilot; this is a
    // diagnostic comparison, not a human playtest or a six-hour stress test.
    for (uint32 Seed : { 1024u, 7331u, 204810u })
        for (int32 Sector : { 1, 2 })
            for (int32 Mode = 0; Mode <= 3; ++Mode)
            {
                const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                    300, false, Sector, 0, false, Mode);
                const FString Context = FString::Printf(TEXT("seed=%u sector=%d mode=%d"), Seed, Sector, Mode);
                TestEqual(Context + TEXT(": checkpoint"), Result.Checkpoints.Num(), 1);
                TestEqual(Context + TEXT(": accounting"), Result.AccountingFailures, 0);
                TestEqual(Context + TEXT(": no missed purchases"), Result.MissedAuto, 0);
                if (Result.Checkpoints.IsEmpty()) continue;
                const FJourneySnapshot& S = Result.Checkpoints[0];
                TestTrue(Context + TEXT(": mined color is within total"),
                    S.MinedBlue >= 0 && S.MinedBlack >= 0 && S.MinedBlue + S.MinedBlack <= S.MinedOre);
                UE_LOG(LogTemp, Display, TEXT("TARGETING_BALANCE seed=%u sector=%d mode=%d seconds=%d purchases=%d mined=%lld blue=%lld black=%lld event_bonus=%d"),
                    Seed, Sector, Mode, S.Seconds, S.Purchased, S.MinedOre, S.MinedBlue, S.MinedBlack, S.EventBonus);
            }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSectorComparisonTest, "SpaceFlight.Balance.ExpeditionSectorComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSectorComparisonTest::RunTest(const FString& Parameters)
{
    // An accelerated five-minute model, not a human playtest: the chosen
    // sector is unlocked at time zero; a player instantly starts and claims
    // the selected route, and the neutral profile and automatic purchases are
    // used throughout. The same seeds do not guarantee identical ore streams:
    // differing rewards may change purchases, lasers and later asteroid hits.
    for (const uint32 Seed : { 1024u, 7331u, 204810u })
    {
        for (const int32 Sector : { 1, 2 })
        {
            for (const int32 Route : { 0, Sector })
            {
                const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                    300, true, Sector, Route);
                const FString Context = FString::Printf(TEXT("seed=%u sector=%d route=%d"), Seed, Sector, Route);
                TestEqual(Context + TEXT(": one five-minute checkpoint"), Result.Checkpoints.Num(), 1);
                TestEqual(Context + TEXT(": resource conservation"), Result.AccountingFailures, 0);
                TestEqual(Context + TEXT(": route and reward states"), Result.ExpeditionFailures, 0);
                TestEqual(Context + TEXT(": duplicate claim does nothing"), Result.DuplicateClaimFailures, 0);
                TestEqual(Context + TEXT(": purchases respect prerequisites"), Result.InvalidPurchases, 0);
                TestEqual(Context + TEXT(": no unexpected sector transitions"), Result.Transitions, 0);
                TestEqual(Context + TEXT(": no missed affordable auto purchase"), Result.MissedAuto, 0);
                if (Result.Checkpoints.Num() != 1) continue;
                const FJourneySnapshot& S = Result.Checkpoints[0];
                TestEqual(Context + TEXT(": sector remains fixed"), S.Sector, Sector);
                TestTrue(Context + TEXT(": only accepted mining is credited"),
                    S.ExpeditionCredited >= 0 && S.ExpeditionCredited <= S.MinedOre);
                TestTrue(Context + TEXT(": rewards are nonnegative and within the route rate"),
                    S.ExpeditionReward >= 0 && S.ExpeditionReward * (Route == 0 ? 20 : 10) <= S.ExpeditionCredited);
                TestTrue(Context + TEXT(": selected route is exercised"), S.ExpeditionClaims > 0);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_SECTOR_COMPARISON seed=%u sector=%d route=%d seconds=%d purchases=%d ore=%lld blue=%lld black=%lld isotope=%lld isotope_spent=%lld collected=%lld spent=%lld mined=%lld credited=%lld claims=%d reward=%lld missed_auto=%d accounting_failures=%d duplicate_claim_failures=%d"),
                    Seed, Sector, Route, S.Seconds, S.Purchased, S.Resources, S.Blue, S.Black,
                    S.Isotope, S.IsotopeSpent, S.Collected, S.Spent, S.MinedOre, S.ExpeditionCredited,
                    S.ExpeditionClaims, S.ExpeditionReward, Result.MissedAuto, Result.AccountingFailures,
                    Result.DuplicateClaimFailures);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSectorTwoHourTest, "SpaceFlight.Balance.ExpeditionSectorTwoHour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSectorTwoHourTest::RunTest(const FString& Parameters)
{
    // This is an accelerated model of 7200 game seconds, not a two-hour
    // wall-clock test or a measure of player enjoyment. Each sector is open
    // from the start, with neutral specialization, autopurchases, and instant
    // route start/claim. Different rewards can change subsequent purchases
    // and asteroid hits, so paired runs need not follow identical trajectories.
    constexpr uint32 Seed = 1024u;
    constexpr int32 ExpectedCount = 3;
    const int32 ExpectedSeconds[ExpectedCount] = { 300, 1800, 7200 };
    for (const int32 Sector : { 1, 2 })
    {
        for (const int32 Route : { 0, Sector })
        {
            const FString Context = FString::Printf(TEXT("seed=%u sector=%d route=%d"), Seed, Sector, Route);
            const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                7200, true, Sector, Route);
            TestEqual(Context + TEXT(": three checkpoints"), Result.Checkpoints.Num(), ExpectedCount);
            TestEqual(Context + TEXT(": resource conservation throughout"), Result.AccountingFailures, 0);
            TestEqual(Context + TEXT(": route and reward states"), Result.ExpeditionFailures, 0);
            TestEqual(Context + TEXT(": duplicate claim does nothing"), Result.DuplicateClaimFailures, 0);
            TestEqual(Context + TEXT(": purchases respect prerequisites"), Result.InvalidPurchases, 0);
            TestEqual(Context + TEXT(": no unexpected sector transitions"), Result.Transitions, 0);
            TestEqual(Context + TEXT(": no invalid sector transitions"), Result.InvalidTransitions, 0);
            TestEqual(Context + TEXT(": no missed affordable auto purchase"), Result.MissedAuto, 0);
            int32 PreviousPurchases = 0, PreviousClaims = 0;
            int64 PreviousReward = 0, PreviousCollected = 0, PreviousMined = 0;
            for (int32 I = 0; I < Result.Checkpoints.Num(); ++I)
            {
                const FJourneySnapshot& S = Result.Checkpoints[I];
                if (I < ExpectedCount)
                    TestEqual(Context + TEXT(": checkpoint time"), S.Seconds, ExpectedSeconds[I]);
                TestEqual(Context + TEXT(": sector remains fixed"), S.Sector, Sector);
                TestTrue(Context + TEXT(": credited mining is valid"),
                    S.MinedOre >= 0 && S.MinedOre <= S.Collected && S.ExpeditionCredited >= 0 && S.ExpeditionCredited <= S.MinedOre);
                TestTrue(Context + TEXT(": reward is nonnegative and within the route rate"),
                    S.ExpeditionReward >= 0 && S.ExpeditionReward * (Route == 0 ? 20 : 10) <= S.ExpeditionCredited);
                TestTrue(Context + TEXT(": balances remain valid"),
                    S.Resources >= 0 && S.Blue >= 0 && S.Black >= 0 && S.Isotope >= 0 &&
                    S.Blue <= S.Resources && S.Black <= S.Resources - S.Blue &&
                    S.Resources == S.Collected - S.Spent - S.Converted &&
                    S.Converted == 2 * (S.Isotope + S.IsotopeSpent));
                TestTrue(Context + TEXT(": progress counters do not go backwards"),
                    S.Purchased >= PreviousPurchases && S.ExpeditionClaims >= PreviousClaims &&
                    S.ExpeditionReward >= PreviousReward && S.Collected >= PreviousCollected && S.MinedOre >= PreviousMined);
                TestTrue(Context + TEXT(": gap including unfinished wait fits in elapsed flight"),
                    S.LongestGap >= 0 && S.LongestGap <= S.Seconds);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_SECTOR_TWO_HOUR seed=%u sector=%d route=%d seconds=%d purchases=%d ore=%lld isotope=%lld isotope_spent=%lld mined=%lld credited=%lld claims=%d reward=%lld gap_including_open=%.1f missed_auto=%d accounting_failures=%d duplicate_claim_failures=%d"),
                    Seed, Sector, Route, S.Seconds, S.Purchased, S.Resources, S.Isotope, S.IsotopeSpent,
                    S.MinedOre, S.ExpeditionCredited, S.ExpeditionClaims, S.ExpeditionReward, S.LongestGap,
                    S.MissedAuto, Result.AccountingFailures, Result.DuplicateClaimFailures);
                PreviousPurchases = S.Purchased; PreviousClaims = S.ExpeditionClaims;
                PreviousReward = S.ExpeditionReward; PreviousCollected = S.Collected; PreviousMined = S.MinedOre;
            }
            if (Result.Checkpoints.Num() == ExpectedCount)
                TestTrue(Context + TEXT(": route completes at least once"), Result.Checkpoints.Last().ExpeditionClaims > 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionCandidateComparisonTest, "SpaceFlight.Balance.ExpeditionCandidateComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionCandidateComparisonTest::RunTest(const FString& Parameters)
{
    // Fixed-sector, instant-start/claim, one seed; 7200 game seconds are
    // simulated, not played. Policy 1 approximates the former 5% coloured
    // payout; policy 2 uses the actual 10% production claim.
    constexpr uint32 Seed = 1024u;
    for (int32 Sector : { 1, 2 })
    {
        for (int32 Policy = 0; Policy < 3; ++Policy)
        {
            const int32 Route = Policy == 0 ? 0 : Sector;
            const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                7200, true, Sector, Route, Policy == 1);
            const FString Context = FString::Printf(TEXT("sector=%d policy=%d"), Sector, Policy);
            TestEqual(Context + TEXT(": checkpoints"), Result.Checkpoints.Num(), 3);
            TestEqual(Context + TEXT(": accounting"), Result.AccountingFailures, 0);
            TestEqual(Context + TEXT(": expedition rules"), Result.ExpeditionFailures, 0);
            TestEqual(Context + TEXT(": no duplicate claims"), Result.DuplicateClaimFailures, 0);
            TestEqual(Context + TEXT(": no missed affordable upgrades"), Result.MissedAuto, 0);
            for (const FJourneySnapshot& S : Result.Checkpoints)
            {
                TestTrue(Context + TEXT(": direct rewards stay within policy bound"),
                    S.ExpeditionReward >= 0 && S.ExpeditionReward * (Policy == 2 ? 10 : 20) <= S.ExpeditionCredited);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_CANDIDATE seed=%u sector=%d policy=%d seconds=%d purchases=%d ore=%lld isotope=%lld isotope_spent=%lld mined=%lld credited=%lld claims=%d reward=%lld gap=%.1f"),
                    Seed, Sector, Policy, S.Seconds, S.Purchased, S.Resources, S.Isotope, S.IsotopeSpent,
                    S.MinedOre, S.ExpeditionCredited, S.ExpeditionClaims, S.ExpeditionReward, S.LongestGap);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionCandidateSeedsTest, "SpaceFlight.Balance.ExpeditionCandidateSeeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionCandidateSeedsTest::RunTest(const FString& Parameters)
{
    // Three seeded scenarios through 30 simulated minutes; a fixed sector is
    // open from time zero, and start/claim are instantaneous.
    for (uint32 Seed : { 1024u, 7331u, 204810u })
        for (int32 Sector : { 1, 2 })
            for (int32 Policy = 0; Policy < 3; ++Policy)
            {
                const int32 Route = Policy == 0 ? 0 : Sector;
                const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, false, false,
                    1800, true, Sector, Route, Policy == 1);
                const FString Context = FString::Printf(TEXT("seed=%u sector=%d policy=%d"), Seed, Sector, Policy);
                TestEqual(Context + TEXT(": checkpoints"), Result.Checkpoints.Num(), 2);
                TestEqual(Context + TEXT(": accounting"), Result.AccountingFailures, 0);
                TestEqual(Context + TEXT(": expedition"), Result.ExpeditionFailures, 0);
                TestEqual(Context + TEXT(": missed auto purchases"), Result.MissedAuto, 0);
                if (Result.Checkpoints.Num() != 2) continue;
                const FJourneySnapshot& S = Result.Checkpoints.Last();
                TestTrue(Context + TEXT(": reward bound"), S.ExpeditionReward >= 0 &&
                    S.ExpeditionReward * (Policy == 2 ? 10 : 20) <= S.ExpeditionCredited);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_CANDIDATE_SEEDS seed=%u sector=%d policy=%d seconds=%d purchases=%d ore=%lld isotope=%lld isotope_spent=%lld goals=%d claims=%d reward=%lld gap=%.1f"),
                    Seed, Sector, Policy, S.Seconds, S.Purchased, S.Resources, S.Isotope,
                    S.IsotopeSpent, S.Goals, S.ExpeditionClaims, S.ExpeditionReward, S.LongestGap);
            }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionTwoHourBalanceTest, "SpaceFlight.Balance.ExpeditionTwoHour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionTwoHourBalanceTest::RunTest(const FString& Parameters)
{
    // Accelerated fixed-step simulation, not two hours of wall-clock play.
    // Both modes share seed, movement, sector transitions and purchase policy.
    // Expedition mode assumes immediate start and collection of free routes;
    // UI interaction, save/reload and the player's subjective experience are
    // deliberately outside this model.
    for (const uint32 Seed : { 1024u, 7331u, 204810u })
    {
        for (const bool bExpeditions : { false, true })
        {
            const FJourneyResult Result = SimulateJourney(Seed, EJourneyStyle::Auto, true, false, 7200, bExpeditions);
            TestEqual(TEXT("three checkpoints exist through two hours"), Result.Checkpoints.Num(), 3);
            TestEqual(TEXT("resource accounting remains valid"), Result.AccountingFailures, 0);
            TestEqual(TEXT("expedition state remains valid"), Result.ExpeditionFailures, 0);
            TestEqual(TEXT("purchases respect prerequisites"), Result.InvalidPurchases, 0);
            TestEqual(TEXT("sector transitions respect unlocks"), Result.InvalidTransitions, 0);
            TestEqual(TEXT("autopilot does not miss affordable nodes"), Result.MissedAuto, 0);
            if (Result.Checkpoints.Num() != 3) continue;
            const FJourneySnapshot& S = Result.Checkpoints.Last();
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_BALANCE_2H seed=%u expeditions=%d seconds=%d purchases=%d mined=%lld collected=%lld direct_bonus=%lld credited=%lld claims=%d longest_gap_seconds=%.1f"),
                Seed, bExpeditions ? 1 : 0, S.Seconds, S.Purchased, S.MinedOre, S.Collected,
                S.ExpeditionReward, S.ExpeditionCredited, S.ExpeditionClaims, S.LongestGap);
            TestEqual(TEXT("last checkpoint is two hours"), S.Seconds, 7200);
            TestTrue(TEXT("accepted mining is nonnegative and included in collected ore"), S.MinedOre >= 0 && S.MinedOre <= S.Collected);
            TestTrue(TEXT("only accepted mining counts for expeditions"), S.ExpeditionCredited >= 0 && S.ExpeditionCredited <= S.MinedOre);
            TestTrue(TEXT("direct expedition rewards are nonnegative and bounded"),
                S.ExpeditionReward >= 0 && S.ExpeditionReward * 20 <= S.ExpeditionCredited);
            TestTrue(TEXT("longest gap includes at most the elapsed flight"), S.LongestGap >= 0 && S.LongestGap <= S.Seconds);
            if (bExpeditions) TestTrue(TEXT("enabled scenario has claimed an expedition"), S.ExpeditionClaims > 0);
            else
            {
                TestEqual(TEXT("disabled policy has no expedition bonus"), S.ExpeditionReward, int64(0));
                TestEqual(TEXT("disabled policy has no expedition credit"), S.ExpeditionCredited, int64(0));
                TestEqual(TEXT("disabled policy has no claims"), S.ExpeditionClaims, 0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightJourneyBalanceTest, "SpaceFlightLong.Balance.MultiSectorJourney",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightJourneyBalanceTest::RunTest(const FString& Parameters)
{
    for (uint32 Seed : { 1024u, 7331u, 204810u })
        for (EJourneyStyle Style : { EJourneyStyle::Auto, EJourneyStyle::CoreLaser, EJourneyStyle::ManualSide })
            for (bool bExplore : { false, true })
            {
                const FJourneyResult Result = SimulateJourney(Seed, Style, bExplore);
                LogJourney(Seed, Style, bExplore, false, Result);
                TestEqual(TEXT("all journey checkpoints exist"), Result.Checkpoints.Num(), 4);
                TestEqual(TEXT("resource accounting remains valid"), Result.AccountingFailures, 0);
                TestEqual(TEXT("purchases respect parent and affordability rules"), Result.InvalidPurchases, 0);
                TestEqual(TEXT("transitions respect sector unlock order"), Result.InvalidTransitions, 0);
                TestEqual(TEXT("automatic chooser does not miss affordable nodes"), Result.MissedAuto, 0);
                if (bExplore) TestEqual(TEXT("exploration reaches both unlocked sectors"), Result.Transitions, 2);
                else TestEqual(TEXT("baseline stays in the first sector"), Result.Transitions, 0);
                if (bExplore && Result.Checkpoints.Num() == 4)
                {
                    const FJourneySnapshot& Last = Result.Checkpoints.Last();
                    TestTrue(TEXT("first transition precedes second by the sector cooldown"),
                        Last.FirstSector1 >= 0 && Last.FirstSector2 >= Last.FirstSector1 + 12.0f);
                }
            }
    // Compare deterministic outputs and report time-step sensitivity separately;
    // neither comparison imposes an uncalibrated economy threshold.
    const auto First = SimulateJourney(1024u, EJourneyStyle::Auto, true, false, 1800);
    const auto Repeat = SimulateJourney(1024u, EJourneyStyle::Auto, true, false, 1800);
    const auto Fine = SimulateJourney(1024u, EJourneyStyle::Auto, true, true, 1800);
    LogJourney(1024u, EJourneyStyle::Auto, true, true, Fine);
    TestEqual(TEXT("fine-resolution comparison has checkpoints"), Fine.Checkpoints.Num(), 2);
    TestEqual(TEXT("fine-resolution accounting remains valid"), Fine.AccountingFailures, 0);
    TestEqual(TEXT("repeat has same checkpoint count"), First.Checkpoints.Num(), Repeat.Checkpoints.Num());
    if (First.Checkpoints.Num() == Repeat.Checkpoints.Num())
        for (int32 I = 0; I < First.Checkpoints.Num(); ++I)
        {
            TestEqual(TEXT("repeated purchase order is deterministic"), First.Checkpoints[I].PurchaseHash, Repeat.Checkpoints[I].PurchaseHash);
            TestEqual(TEXT("repeated collected ore is deterministic"), First.Checkpoints[I].Collected, Repeat.Checkpoints[I].Collected);
            TestEqual(TEXT("repeated sector is deterministic"), First.Checkpoints[I].Sector, Repeat.Checkpoints[I].Sector);
            TestEqual(TEXT("repeated destruction count is deterministic"), First.Checkpoints[I].Destroyed, Repeat.Checkpoints[I].Destroyed);
            TestEqual(TEXT("repeated event bonus is deterministic"), First.Checkpoints[I].EventBonus, Repeat.Checkpoints[I].EventBonus);
            TestEqual(TEXT("repeated isotope balance is deterministic"), First.Checkpoints[I].Isotope, Repeat.Checkpoints[I].Isotope);
        }
    return true;
}
#endif
