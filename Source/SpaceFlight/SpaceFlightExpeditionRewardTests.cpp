#include "SpaceFlightLogic.h"
#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
    bool PrepareReadyExpedition(SpaceFlight::FProgress& Progress, int32 Route)
    {
        using namespace SpaceFlight;
        Progress.Objective.Sequence = 8;
        Progress.CurrentSector = Route;
        return Progress.Expedition.Start(Route, 100, 2) &&
            Progress.Expedition.Record(Route, Route, 100, false) == 100 &&
            Progress.Expedition.State == EExpeditionState::Ready;
    }

    bool RoundTrip(SpaceFlight::FProgress& Original, SpaceFlight::FProgress& Restored)
    {
        USpaceFlightSaveGame* Snapshot = NewObject<USpaceFlightSaveGame>();
        Snapshot->Capture(Original, 0.0f, 20, true, 1);
        TArray<uint8> Bytes;
        if (!UGameplayStatics::SaveGameToMemory(Snapshot, Bytes)) return false;
        const auto* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
        if (!Loaded) return false;
        float Time = 0.0f;
        int32 Brightness = 0;
        bool bAutopilot = false;
        return Loaded->Restore(Restored, Time, Brightness, bAutopilot);
    }

    void CompareRewardState(FAutomationTestBase& Test, const SpaceFlight::FProgress& Actual,
                            const SpaceFlight::FProgress& Expected)
    {
        Test.TestEqual(TEXT("ore balance"), Actual.Resources, Expected.Resources);
        Test.TestEqual(TEXT("total collected"), Actual.TotalCollected, Expected.TotalCollected);
        Test.TestEqual(TEXT("total spent"), Actual.TotalSpent, Expected.TotalSpent);
        Test.TestEqual(TEXT("blue balance"), Actual.BlueOre, Expected.BlueOre);
        Test.TestEqual(TEXT("black balance"), Actual.BlackOre, Expected.BlackOre);
        Test.TestEqual(TEXT("isotope balance"), Actual.Isotope, Expected.Isotope);
        Test.TestEqual(TEXT("ore converted"), Actual.OreConverted, Expected.OreConverted);
        Test.TestEqual(TEXT("isotope spent"), Actual.IsotopeSpent, Expected.IsotopeSpent);
        Test.TestEqual(TEXT("discovery bits"), static_cast<int64>(Actual.DiscoveryBits), static_cast<int64>(Expected.DiscoveryBits));
        Test.TestEqual(TEXT("blue refinery quota"), Actual.BlueRefiningQuota, Expected.BlueRefiningQuota);
        Test.TestEqual(TEXT("black refinery quota"), Actual.BlackRefiningQuota, Expected.BlackRefiningQuota);
        Test.TestEqual(TEXT("refinery mode"), Actual.RefiningMode, Expected.RefiningMode);
        Test.TestTrue(TEXT("expedition state"), Actual.Expedition.State == Expected.Expedition.State);
        Test.TestEqual(TEXT("expedition route"), Actual.Expedition.Route, Expected.Expedition.Route);
        Test.TestEqual(TEXT("expedition target"), Actual.Expedition.Target, Expected.Expedition.Target);
        Test.TestEqual(TEXT("expedition progress"), Actual.Expedition.Progress, Expected.Expedition.Progress);
        Test.TestEqual(TEXT("objective progress"), Actual.Objective.Progress, Expected.Objective.Progress);
        Test.TestEqual(TEXT("objective sequence"), Actual.Objective.Sequence, Expected.Objective.Sequence);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardOreCodeTest,
    "SpaceFlight.Progress.ExpeditionReward.OreCode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardOreCodeTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("free route rewards common ore"), FExpedition::RewardOreCode(0), 0);
    TestEqual(TEXT("blue route rewards black ore"), FExpedition::RewardOreCode(1), 2);
    TestEqual(TEXT("black route rewards blue ore"), FExpedition::RewardOreCode(2), 1);
    for (const int32 Route : { MIN_int32, -1, 3, MAX_int32 })
        TestEqual(TEXT("invalid route has no reward ore"), FExpedition::RewardOreCode(Route), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardRateTest,
    "SpaceFlight.Progress.ExpeditionReward.RouteRates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardRateTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("invalid route has no reward"), FExpedition::RewardAmount(-1, 100), 0);
    TestEqual(TEXT("target below minimum has no reward"), FExpedition::RewardAmount(1, 99), 0);
    TestEqual(TEXT("target above maximum has no reward"), FExpedition::RewardAmount(1, 10001), 0);
    for (const int32 Route : { 0, 1, 2 })
        for (const int32 Target : { 100, 101, 109, 199, 10000 })
        {
            FExpedition E;
            TestTrue(TEXT("valid route starts"), E.Start(Route, Target, 2));
            TestEqual(TEXT("credit completes target"), E.Record(Route, Route, Target, false), Target);
            const int32 Expected = Target / (Route == 0 ? 20 : 10);
            TestEqual(FString::Printf(TEXT("route %d target %d awards correct rounded rate"), Route, Target), E.Claim(), Expected);
            TestEqual(TEXT("cannot claim twice"), E.Claim(), 0);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardRoutesTest,
    "SpaceFlight.Progress.ExpeditionReward.Routes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardRoutesTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    {
        FProgress Progress;
        const FProgress Idle = Progress;
        TestEqual(TEXT("idle expedition pays nothing"), Progress.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Progress, Idle);
        TestTrue(TEXT("start incomplete route"), Progress.Expedition.Start(1, 100, 2));
        TestEqual(TEXT("partially advance route"), Progress.Expedition.Record(1, 1, 30, false), 30);
        const FProgress Active = Progress;
        TestEqual(TEXT("incomplete expedition pays nothing"), Progress.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Progress, Active);
    }
    for (int32 Route = 0; Route <= 2; ++Route)
    {
        FProgress Progress;
        TestTrue(TEXT("disable refining"), Progress.SetRefiningMode(1));
        TestEqual(TEXT("add mined ore"), Progress.AddOre(100, static_cast<EOreKind>(Route)), int64(100));
        if (!TestTrue(TEXT("prepare ready expedition"), PrepareReadyExpedition(Progress, Route))) return false;

        const int32 Reward = Route == 0 ? 5 : 10;
        TestEqual(TEXT("claim uses the route's rate"), Progress.ClaimExpeditionReward(), Reward);
        TestEqual(TEXT("claim adds reward once to total ore"), Progress.Resources, int64(100 + Reward));
        TestEqual(TEXT("claim adds reward once to lifetime collected"), Progress.TotalCollected, int64(100 + Reward));
        TestEqual(TEXT("blue reward and mined ore retain their types"), Progress.BlueOre,
            int64(Route == 1 ? 100 : Route == 2 ? Reward : 0));
        TestEqual(TEXT("black reward and mined ore retain their types"), Progress.BlackOre,
            int64(Route == 2 ? 100 : Route == 1 ? Reward : 0));
        TestEqual(TEXT("no isotope is produced when refining is disabled"), Progress.Isotope, int64(0));
        TestEqual(TEXT("no ore is converted when refining is disabled"), Progress.OreConverted, int64(0));
        TestTrue(TEXT("successful claim resets expedition"), Progress.Expedition.State == EExpeditionState::Idle && Progress.Expedition.IsValid());

        const FProgress AfterClaim = Progress;
        TestEqual(TEXT("second claim gives nothing"), Progress.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Progress, AfterClaim);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardRefiningTest,
    "SpaceFlight.Progress.ExpeditionReward.AutomaticRefining",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardRefiningTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Route = 1; Route <= 2; ++Route)
    {
        FProgress Progress;
        TestEqual(TEXT("automatic refining is default"), Progress.RefiningMode, 0);
        TestEqual(TEXT("mine a full route of one color"), Progress.AddOre(100, static_cast<EOreKind>(Route)), int64(100));
        if (!TestTrue(TEXT("prepare ready colored expedition"), PrepareReadyExpedition(Progress, Route))) return false;

        TestEqual(TEXT("claim adds ten opposite-color ore"), Progress.ClaimExpeditionReward(), 10);
        TestEqual(TEXT("ten opposite-color ore make ten isotope"), Progress.Isotope, int64(10));
        TestEqual(TEXT("isotope consumes ore at one-to-one ratio"), Progress.OreConverted, int64(20));
        TestEqual(TEXT("remaining total ore accounts for conversion"), Progress.Resources, int64(90));
        TestEqual(TEXT("remaining blue ore is correct"), Progress.BlueOre, int64(Route == 1 ? 90 : 0));
        TestEqual(TEXT("remaining black ore is correct"), Progress.BlackOre, int64(Route == 2 ? 90 : 0));
        TestEqual(TEXT("collected ore includes reward without double counting"), Progress.TotalCollected, int64(110));
        TestTrue(TEXT("claim resets expedition"), Progress.Expedition.State == EExpeditionState::Idle && Progress.Expedition.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardLimitTest,
    "SpaceFlight.Progress.ExpeditionReward.AtomicLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardLimitTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Route = 0; Route <= 2; ++Route)
    {
        FProgress Progress;
        TestTrue(TEXT("disable refining"), Progress.SetRefiningMode(1));
        TestEqual(TEXT("leave room for less than reward"), Progress.AddResources(ResourceAccountingLimit - 4), ResourceAccountingLimit - 4);
        if (!TestTrue(TEXT("prepare ready expedition"), PrepareReadyExpedition(Progress, Route))) return false;
        const FProgress Before = Progress;

        TestEqual(TEXT("partial reward is refused"), Progress.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Progress, Before);
        TestTrue(TEXT("refused reward remains claimable"), Progress.Expedition.State == EExpeditionState::Ready);

        FProgress Restored;
        if (!TestTrue(TEXT("failed claim snapshot round trips"), RoundTrip(Progress, Restored))) return false;
        CompareRewardState(*this, Restored, Before);

        FProgress Exact;
        TestTrue(TEXT("disable refining for exact limit"), Exact.SetRefiningMode(1));
        const int32 Reward = Route == 0 ? 5 : 10;
        TestEqual(TEXT("leave exactly enough room"), Exact.AddResources(ResourceAccountingLimit - Reward), ResourceAccountingLimit - Reward);
        if (!TestTrue(TEXT("prepare exact-limit expedition"), PrepareReadyExpedition(Exact, Route))) return false;
        TestEqual(TEXT("reward fitting exactly is accepted"), Exact.ClaimExpeditionReward(), Reward);
        TestEqual(TEXT("exact claim reaches accounting limit"), Exact.TotalCollected, ResourceAccountingLimit);
        TestTrue(TEXT("exact claim resets expedition"), Exact.Expedition.State == EExpeditionState::Idle && Exact.Expedition.IsValid());
    }

    // A partial colored reward would also trigger automatic refining. The
    // rejected claim must roll back isotope, quotas and discovery as well.
    for (int32 Route = 1; Route <= 2; ++Route)
    {
        FProgress Progress;
        TestEqual(TEXT("seed matching color"), Progress.AddOre(100, static_cast<EOreKind>(Route)), int64(100));
        TestEqual(TEXT("leave room for less than reward"), Progress.AddResources(ResourceAccountingLimit - 104), ResourceAccountingLimit - 104);
        if (!TestTrue(TEXT("prepare ready colored expedition"), PrepareReadyExpedition(Progress, Route))) return false;
        const FProgress Before = Progress;
        TestEqual(TEXT("partial auto-refining reward is refused"), Progress.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Progress, Before);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardQuotaTest,
    "SpaceFlight.Progress.ExpeditionReward.RefiningQuotas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardQuotaTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Route : { 1, 2 })
    {
        for (int32 Mode = 2; Mode <= 5; ++Mode)
        {
            FProgress Progress;
            TestEqual(TEXT("start with matching ore"), Progress.AddOre(100, static_cast<EOreKind>(Route)), int64(100));
            TestTrue(TEXT("set quota mode"), Progress.SetRefiningMode(Mode));
            if (!TestTrue(TEXT("prepare colored expedition"), PrepareReadyExpedition(Progress, Route))) return false;
            const int32 Percent = (Mode - 1) * 25;
            const int64 Pairs = (10 * Percent) / 100;
            TestEqual(TEXT("claim awards ten opposite-color ore"), Progress.ClaimExpeditionReward(), 10);
            TestEqual(TEXT("quota limits the number of pairs"), Progress.Isotope, Pairs);
            TestEqual(TEXT("conversion uses one ore of each color"), Progress.OreConverted, 2 * Pairs);
            TestEqual(TEXT("resource accounting includes converted pairs"), Progress.Resources, int64(110) - 2 * Pairs);
            const int64 MatchedQuota = 100 * Percent - Pairs * 100;
            const int64 OppositeQuota = 10 * Percent - Pairs * 100;
            TestEqual(TEXT("blue quota keeps fractional remainder"), Progress.BlueRefiningQuota, Route == 1 ? MatchedQuota : OppositeQuota);
            TestEqual(TEXT("black quota keeps fractional remainder"), Progress.BlackRefiningQuota, Route == 2 ? MatchedQuota : OppositeQuota);
            FProgress Restored;
            if (!TestTrue(TEXT("quota state round trips"), RoundTrip(Progress, Restored))) return false;
            CompareRewardState(*this, Restored, Progress);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionRewardSaveTest,
    "SpaceFlight.Progress.ExpeditionReward.SaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionRewardSaveTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Route = 0; Route <= 2; ++Route)
    {
        FProgress Before;
        TestTrue(TEXT("disable refining"), Before.SetRefiningMode(1));
        TestEqual(TEXT("add mined ore"), Before.AddOre(100, static_cast<EOreKind>(Route)), int64(100));
        if (!TestTrue(TEXT("prepare ready expedition"), PrepareReadyExpedition(Before, Route))) return false;
        FProgress Restored;
        if (!TestTrue(TEXT("ready expedition snapshot round trips"), RoundTrip(Before, Restored))) return false;
        CompareRewardState(*this, Restored, Before);

        TestEqual(TEXT("restored ready route pays once"), Restored.ClaimExpeditionReward(), Route == 0 ? 5 : 10);
        const FProgress AfterClaim = Restored;
        FProgress Reloaded;
        if (!TestTrue(TEXT("claimed snapshot round trips"), RoundTrip(Restored, Reloaded))) return false;
        CompareRewardState(*this, Reloaded, AfterClaim);
        TestEqual(TEXT("reloaded claim cannot be repeated"), Reloaded.ClaimExpeditionReward(), 0);
        CompareRewardState(*this, Reloaded, AfterClaim);
    }
    return true;
}
#endif
