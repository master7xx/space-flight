#include "SpaceFlightTrackedUpgrade.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradeOreTest,
    "SpaceFlight.Progress.TrackedUpgrade.OreAndPrerequisites",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradeOreTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    TestTrue(TEXT("no selected goal has no status"), DescribeTrackedUpgrade(Progress).Status == ETrackedUpgradeStatus::None);
    for (int32 Invalid : { -2, UpgradeCount, MAX_int32 })
    {
        Progress.TrackedUpgrade = Invalid;
        const auto Info = DescribeTrackedUpgrade(Progress);
        TestTrue(TEXT("invalid index is not presented as a goal"), Info.Status == ETrackedUpgradeStatus::None);
        TestEqual(TEXT("invalid index has no result index"), Info.Index, INDEX_NONE);
        TestEqual(TEXT("read-only description does not change an invalid selection"), Progress.TrackedUpgrade, Invalid);
    }

    Progress.TrackedUpgrade = 3; // First collector: 16 ore, no prerequisite.
    Progress.AddResources(15);
    auto Info = DescribeTrackedUpgrade(Progress);
    TestTrue(TEXT("an unlocked collector needs its missing ore"), Info.Status == ETrackedUpgradeStatus::NeedResources);
    TestEqual(TEXT("collector is the tracked node"), Info.Index, 3);
    TestTrue(TEXT("collector uses the overall ore balance"), Info.Currency == ETrackedUpgradeCurrency::Ore);
    TestEqual(TEXT("collector balance is current ore"), Info.Balance, int64(15));
    TestEqual(TEXT("collector cost is the selected node's cost"), Info.Cost, int64(Upgrade(3).Cost));
    TestEqual(TEXT("collector is one ore short"), Info.Missing, int64(1));
    TestEqual(TEXT("first collector has no parent"), Info.ParentIndex, INDEX_NONE);
    Progress.AddResources(1);
    Info = DescribeTrackedUpgrade(Progress);
    TestTrue(TEXT("collector is ready at the exact threshold"), Info.Status == ETrackedUpgradeStatus::Ready);
    TestEqual(TEXT("ready collector has no missing ore"), Info.Missing, int64(0));

    TestTrue(TEXT("a different affordable purchase succeeds"), Progress.Purchase(0));
    TestEqual(TEXT("buying another upgrade retains the tracked collector"), Progress.TrackedUpgrade, 3);
    Info = DescribeTrackedUpgrade(Progress);
    TestTrue(TEXT("spending ore can make a tracked goal unaffordable again"), Info.Status == ETrackedUpgradeStatus::NeedResources);
    TestEqual(TEXT("the description reflects ore spent elsewhere"), Info.Balance, int64(4));
    TestEqual(TEXT("the remaining cost is recomputed"), Info.Missing, int64(12));
    TestFalse(TEXT("unaffordable purchase is rejected"), Progress.Purchase(3));
    TestEqual(TEXT("failed purchase leaves tracking intact"), Progress.TrackedUpgrade, 3);
    Progress.AddResources(12);
    TestTrue(TEXT("tracked collector can be purchased"), Progress.Purchase(3));
    TestEqual(TEXT("successful purchase clears tracking"), Progress.TrackedUpgrade, INDEX_NONE);
    TestTrue(TEXT("completed tracking has no status"), DescribeTrackedUpgrade(Progress).Status == ETrackedUpgradeStatus::None);
    Progress.TrackedUpgrade = 3; // Defensive read of stale/corrupt in-memory state.
    TestTrue(TEXT("already owned target is never presented as pending"), DescribeTrackedUpgrade(Progress).Status == ETrackedUpgradeStatus::None);

    FProgress Locked;
    Locked.TrackedUpgrade = 1; // Radar II requires Radar I.
    Locked.AddResources(1000);
    Info = DescribeTrackedUpgrade(Locked);
    TestTrue(TEXT("missing prerequisite takes priority over sufficient funds"), Info.Status == ETrackedUpgradeStatus::Prerequisite);
    TestEqual(TEXT("locked goal reports its direct prerequisite"), Info.ParentIndex, 0);
    TestEqual(TEXT("locked goal still reports the actual balance"), Info.Balance, int64(1000));
    TestEqual(TEXT("sufficient funds do not create a false deficit"), Info.Missing, int64(0));
    TestFalse(TEXT("a locked target cannot be bought"), Locked.Purchase(1));
    TestEqual(TEXT("failed locked purchase retains tracking"), Locked.TrackedUpgrade, 1);
    TestTrue(TEXT("the prerequisite can be purchased"), Locked.Purchase(0));
    TestEqual(TEXT("buying the prerequisite does not clear the target"), Locked.TrackedUpgrade, 1);
    TestTrue(TEXT("target becomes ready once its prerequisite is owned"), DescribeTrackedUpgrade(Locked).Status == ETrackedUpgradeStatus::Ready);
    TestTrue(TEXT("target can then be purchased"), Locked.Purchase(1));
    TestEqual(TEXT("target clears only after its own purchase"), Locked.TrackedUpgrade, INDEX_NONE);

    FProgress Large;
    Large.TrackedUpgrade = 9;
    Large.AddResources(ResourceAccountingLimit);
    Info = DescribeTrackedUpgrade(Large);
    TestTrue(TEXT("a very large balance remains affordable"), Info.Status == ETrackedUpgradeStatus::Ready);
    TestEqual(TEXT("a 64-bit balance is preserved without narrowing"), Info.Balance, ResourceAccountingLimit);
    TestEqual(TEXT("large balances do not wrap the deficit"), Info.Missing, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradeIsotopeTest,
    "SpaceFlight.Progress.TrackedUpgrade.IsotopeAndReadOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradeIsotopeTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    TestTrue(TEXT("refining can be turned off"), Progress.SetRefiningMode(1));
    Progress.AddResources(Upgrade(0).Cost);
    TestTrue(TEXT("micro-upgrade prerequisite can be bought"), Progress.Purchase(0));
    Progress.TrackedUpgrade = 20;
    Progress.AddOre(50, EOreKind::Blue);
    Progress.AddOre(50, EOreKind::Black);
    auto Info = DescribeTrackedUpgrade(Progress);
    TestTrue(TEXT("micro-upgrade uses isotope"), Info.Currency == ETrackedUpgradeCurrency::Isotope);
    TestTrue(TEXT("unconverted coloured ore does not satisfy an isotope target"), Info.Status == ETrackedUpgradeStatus::NeedResources);
    TestEqual(TEXT("isotope balance stays zero while refining is off"), Info.Balance, int64(0));
    TestEqual(TEXT("the full isotope price is missing"), Info.Missing, int64(Upgrade(20).Cost));
    TestEqual(TEXT("tracked micro-upgrade keeps its parent index"), Info.ParentIndex, 0);
    TestEqual(TEXT("colour ore remains in the ore balance"), Progress.Resources, int64(100));

    TestTrue(TEXT("refining can be turned back to auto"), Progress.SetRefiningMode(0));
    Info = DescribeTrackedUpgrade(Progress);
    TestEqual(TEXT("automatic conversion creates 50 isotope"), Info.Balance, int64(50));
    TestTrue(TEXT("isotope target becomes ready after conversion"), Info.Status == ETrackedUpgradeStatus::Ready);
    TestEqual(TEXT("conversion consumes both colours from ore"), Progress.Resources, int64(0));
    TestTrue(TEXT("tracked micro-upgrade can be purchased"), Progress.Purchase(20));
    TestEqual(TEXT("successful micro purchase clears tracking"), Progress.TrackedUpgrade, INDEX_NONE);
    TestEqual(TEXT("isotope purchase spends the selected price"), Progress.IsotopeSpent, int64(Upgrade(20).Cost));

    FProgress Fractional;
    Fractional.AddResources(Upgrade(0).Cost);
    TestTrue(TEXT("fractional case opens radar parent"), Fractional.Purchase(0));
    TestTrue(TEXT("25 percent refining can be selected"), Fractional.SetRefiningMode(2));
    Fractional.TrackedUpgrade = 20;
    Fractional.AddOre(9, EOreKind::Blue);
    Fractional.AddOre(9, EOreKind::Black);
    const FProgress Before = Fractional;
    Info = DescribeTrackedUpgrade(Fractional);
    TestEqual(TEXT("fractional quota creates only whole isotope pairs"), Info.Balance, int64(2));
    TestEqual(TEXT("deficit excludes unconverted fractional quotas"), Info.Missing, int64(Upgrade(20).Cost - 2));
    TestTrue(TEXT("fractional target still needs resources"), Info.Status == ETrackedUpgradeStatus::NeedResources);
    for (int32 Repeat = 0; Repeat < 3; ++Repeat) DescribeTrackedUpgrade(Fractional);
    TestEqual(TEXT("description leaves tracking unchanged"), Fractional.TrackedUpgrade, Before.TrackedUpgrade);
    TestEqual(TEXT("description leaves total ore unchanged"), Fractional.Resources, Before.Resources);
    TestEqual(TEXT("description leaves blue ore unchanged"), Fractional.BlueOre, Before.BlueOre);
    TestEqual(TEXT("description leaves black ore unchanged"), Fractional.BlackOre, Before.BlackOre);
    TestEqual(TEXT("description leaves isotope unchanged"), Fractional.Isotope, Before.Isotope);
    TestEqual(TEXT("description leaves collected ore unchanged"), Fractional.TotalCollected, Before.TotalCollected);
    TestEqual(TEXT("description leaves spent ore unchanged"), Fractional.TotalSpent, Before.TotalSpent);
    TestEqual(TEXT("description leaves converted ore unchanged"), Fractional.OreConverted, Before.OreConverted);
    TestEqual(TEXT("description leaves blue quota unchanged"), Fractional.BlueRefiningQuota, Before.BlueRefiningQuota);
    TestEqual(TEXT("description leaves black quota unchanged"), Fractional.BlackRefiningQuota, Before.BlackRefiningQuota);
    TestEqual(TEXT("description leaves refinery mode unchanged"), Fractional.RefiningMode, Before.RefiningMode);
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        TestEqual(TEXT("description never purchases an upgrade"), Fractional.Has(Index), Before.Has(Index));
    return true;
}
#endif
