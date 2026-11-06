#include "SpaceFlightLogic.h"
#include "SpaceFlightSectors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
    using namespace SpaceFlight;

    struct FRouteComparison
    {
        FProgress Progress;
        int32 Route = 0;
        int32 Claims = 0;
        int64 Mined = 0;
        int64 Credited = 0;
        int64 Reward = 0;
        bool bValid = true;
        bool bAccountingValid = true;
        bool bRepeatSafe = true;
        bool bRewardTypeValid = true;

        explicit FRouteComparison(int32 InRoute, int32 Sector) : Route(InRoute)
        {
            Progress.CurrentSector = Sector;
            Progress.Objective.Sequence = 8;
        }

        void Feed(int32 Sector, EOreKind Kind, int32 Amount)
        {
            // This is a fixed-target, immediate-confirmation model. It is not
            // the real UI, the variable target policy, or a human playtest.
            if (Progress.Expedition.State == EExpeditionState::Idle &&
                !Progress.Expedition.Start(Route, 100, 2)) bValid = false;
            const int64 Accepted = Progress.AddOre(Amount, Kind);
            Mined += Accepted;
            Credited += Progress.Expedition.Record(Sector, static_cast<int32>(Kind), Accepted, false);
            if (Progress.Expedition.State == EExpeditionState::Ready)
            {
                const int64 CollectedBefore = Progress.TotalCollected;
                const int64 CommonBefore = Progress.CommonOre();
                const int64 IsotopeBefore = Progress.Isotope;
                const int32 Payout = Progress.ClaimExpeditionReward();
                bValid &= Payout == (Route == 0 ? 5 : 10) && Progress.Expedition.State == EExpeditionState::Idle;
                bRewardTypeValid &= Progress.TotalCollected == CollectedBefore + Payout;
                if (Route == 0)
                    bRewardTypeValid &= Progress.CommonOre() == CommonBefore + Payout && Progress.Isotope == IsotopeBefore;
                else
                    bRewardTypeValid &= Progress.CommonOre() == CommonBefore;
                Reward += Payout;
                ++Claims;
                const int64 AfterClaim = Progress.TotalCollected;
                bRepeatSafe &= Progress.ClaimExpeditionReward() == 0 && Progress.TotalCollected == AfterClaim;
            }
            bValid &= Progress.Expedition.IsValidForUnlockedSector(2);
            bAccountingValid &= Progress.Resources >= 0 && Progress.BlueOre >= 0 && Progress.BlackOre >= 0 &&
                Progress.CommonOre() >= 0 && Progress.Isotope >= 0 &&
                Progress.Resources == Progress.TotalCollected - Progress.TotalSpent - Progress.OreConverted &&
                Progress.OreConverted == 2 * (Progress.Isotope + Progress.IsotopeSpent);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRouteComparisonTest,
    "SpaceFlight.Balance.ExpeditionRouteComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRouteComparisonTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    // Feed the same 300 synthetic mining events to a free route and the
    // corresponding coloured route in each sector. These are events, not
    // 300 seconds of measured player behaviour or a full economy simulation.
    for (const uint32 Seed : { 1024u, 7331u, 204810u })
    {
        for (int32 Sector = 1; Sector <= 2; ++Sector)
        {
            FRouteComparison Free(0, Sector);
            FRouteComparison Colour(Sector, Sector);
            FRandomStream AmountRandom(static_cast<int32>(Seed));
            const int32 Count = SectorAsteroidCount(Sector);
            for (int32 Event = 0; Event < 300; ++Event)
            {
                const EOreKind Kind = SectorOreKind(Sector, Seed, Event % Count, Event / Count, 0);
                const int32 Amount = AmountRandom.RandRange(1, 4);
                Free.Feed(Sector, Kind, Amount);
                Colour.Feed(Sector, Kind, Amount);
            }
            const FString Context = FString::Printf(TEXT("seed=%u sector=%d"), Seed, Sector);
            for (const FRouteComparison* Case : { &Free, &Colour })
            {
                TestTrue(Context + TEXT(": route state and reward are valid"), Case->bValid);
                TestTrue(Context + TEXT(": resource conservation"), Case->bAccountingValid);
                TestTrue(Context + TEXT(": repeated claim has no effect"), Case->bRepeatSafe);
                TestTrue(Context + TEXT(": payout uses the correct resource category"), Case->bRewardTypeValid);
                TestTrue(Context + TEXT(": credited ore cannot exceed accepted mining"), Case->Credited <= Case->Mined);
                TestTrue(Context + TEXT(": total reward stays within the route rate"),
                    Case->Reward >= 0 && Case->Reward * (Case->Route == 0 ? 20 : 10) <= Case->Credited);
                TestTrue(Context + TEXT(": at least one route completes"), Case->Claims > 0);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_ROUTE_COMPARISON seed=%u sector=%d route=%d events=300 mined=%lld credited=%lld claims=%d reward=%lld ore=%lld blue=%lld black=%lld isotope=%lld"),
                    Seed, Sector, Case->Route, Case->Mined, Case->Credited, Case->Claims, Case->Reward,
                    Case->Progress.Resources, Case->Progress.BlueOre, Case->Progress.BlackOre, Case->Progress.Isotope);
            }
            TestEqual(Context + TEXT(": both policies receive the same mining stream"), Free.Mined, Colour.Mined);
            TestTrue(Context + TEXT(": free route completes at least as many fixed targets"), Free.Claims >= Colour.Claims);
        }
    }

    // Identical accepted ore and delayed claims: the colour-specific payout
    // completes a pair, while a free-route payout remains ordinary ore.
    for (int32 ColourRoute : { 1, 2 })
    {
        FProgress Free, Colour;
        Free.CurrentSector = Colour.CurrentSector = ColourRoute;
        Free.Objective.Sequence = Colour.Objective.Sequence = 8;
        const EOreKind Matched = ColourRoute == 1 ? EOreKind::Blue : EOreKind::Black;
        const EOreKind Opposite = ColourRoute == 1 ? EOreKind::Black : EOreKind::Blue;
        TestTrue(TEXT("free route starts for pairing comparison"), Free.Expedition.Start(0, 100, 2));
        TestTrue(TEXT("colour route starts for pairing comparison"), Colour.Expedition.Start(ColourRoute, 100, 2));
        for (FProgress* Progress : { &Free, &Colour })
        {
            const int64 Accepted = Progress->AddOre(105, Matched);
            TestEqual(TEXT("matching ore completes either route"), Progress->Expedition.Record(ColourRoute, static_cast<int32>(Matched), Accepted, false), 100);
            TestEqual(TEXT("opposite ore is accepted before the delayed claim"), Progress->AddOre(100, Opposite), int64(100));
        }
        TestEqual(TEXT("both policies have identical ore before payout"), Free.Resources, Colour.Resources);
        TestEqual(TEXT("both policies have identical isotope before payout"), Free.Isotope, Colour.Isotope);
        const int64 InitialIsotope = Free.Isotope;
        const int64 InitialCommon = Free.CommonOre();
        TestEqual(TEXT("free route pays five ordinary ore"), Free.ClaimExpeditionReward(), 5);
        TestEqual(TEXT("colour route pays ten units of reward ore"), Colour.ClaimExpeditionReward(), 10);
        TestEqual(TEXT("ordinary reward does not form isotope"), Free.Isotope, InitialIsotope);
        TestEqual(TEXT("ordinary reward increases ordinary ore"), Free.CommonOre(), InitialCommon + 5);
        TestEqual(TEXT("colour reward forms five isotopes when opposite ore is present"), Colour.Isotope, InitialIsotope + 5);
        TestEqual(TEXT("colour reward does not create ordinary ore"), Colour.CommonOre(), InitialCommon);
        TestEqual(TEXT("colour route gives five more gross ore"), Colour.TotalCollected, Free.TotalCollected + 5);
    }
    return true;
}
#endif
