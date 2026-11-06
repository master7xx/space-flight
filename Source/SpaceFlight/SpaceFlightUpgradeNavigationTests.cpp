#include "SpaceFlightUpgradeNavigation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightUpgradeNavigationTest, "SpaceFlight.Tree.AvailableNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightUpgradeNavigationTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress Progress;
    auto Result = NextAffordableUpgrade(Progress);
    TestTrue(TEXT("empty resources show an unlocked goal"), Result.State == EUpgradeNavigationState::NeedsResources);
    TestEqual(TEXT("first unlocked goal is the initial radar"), Result.Index, 0);
    TestEqual(TEXT("empty result has zero available"), Result.AvailableCount, 0);
    TestEqual(TEXT("radar needs twelve ore"), Result.MissingAmount, int64(12));
    TestTrue(TEXT("radar uses ore"), Result.Currency == EUpgradeCurrency::Ore);
    TestTrue(TEXT("first radar is eligible"), IsUpgradeEligible(Progress, 0));
    TestFalse(TEXT("locked child is not eligible"), IsUpgradeEligible(Progress, 1));
    TestFalse(TEXT("negative index is not eligible"), IsUpgradeEligible(Progress, -1));
    TestFalse(TEXT("out of range is not eligible"), IsUpgradeEligible(Progress, UpgradeCount));
    TestEqual(TEXT("unaffordable goals cycle without selecting locked children"), NextAffordableUpgrade(Progress, 0).Index, 3);
    TestEqual(TEXT("unaffordable goals wrap deterministically"), NextAffordableUpgrade(Progress, 9).Index, 0);

    Progress.AddResources(12);
    Result = NextAffordableUpgrade(Progress);
    TestTrue(TEXT("the first radar is available"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("only the first radar can be bought"), Result.Index, 0);
    TestEqual(TEXT("one available upgrade is counted"), Result.AvailableCount, 1);
    TestEqual(TEXT("available node has no resource deficit"), Result.MissingAmount, int64(0));
    TestEqual(TEXT("one item cycles back to itself"), NextAffordableUpgrade(Progress, 0).Index, 0);

    Progress.AddResources(988);
    const FProgress Before = Progress;
    TestEqual(TEXT("cycling starts with first available index"), NextAffordableUpgrade(Progress).Index, 0);
    TestEqual(TEXT("next available branch follows in index order"), NextAffordableUpgrade(Progress, 0).Index, 3);
    TestEqual(TEXT("last available branch wraps around"), NextAffordableUpgrade(Progress, 9).Index, 0);
    TestEqual(TEXT("invalid positive cursor starts over"), NextAffordableUpgrade(Progress, MAX_int32).Index, 0);
    TestEqual(TEXT("invalid negative cursor starts over"), NextAffordableUpgrade(Progress, -2).Index, 0);
    TestEqual(TEXT("navigation does not spend ore"), Progress.Resources, Before.Resources);
    TestEqual(TEXT("navigation does not spend isotope"), Progress.Isotope, Before.Isotope);
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        TestTrue(TEXT("navigation does not purchase a node"), Progress.Has(Index) == Before.Has(Index));

    TestTrue(TEXT("purchase makes the cursor's old node unavailable"), Progress.Purchase(0));
    TestEqual(TEXT("next scan includes a newly unlocked node"), NextAffordableUpgrade(Progress, 0).Index, 1);
    Progress.TotalSpent += Progress.Resources;
    Progress.Resources = 0;
    Result = NextAffordableUpgrade(Progress, 1);
    TestTrue(TEXT("resource loss switches to unlocked goals"), Result.State == EUpgradeNavigationState::NeedsResources);
    TestEqual(TEXT("scan continues past previous cursor"), Result.Index, 3);
    TestEqual(TEXT("collector deficit uses current stock"), Result.MissingAmount, int64(16));

    FProgress Micro;
    Micro.AddResources(Upgrade(0).Cost);
    TestTrue(TEXT("radar prerequisite can be bought"), Micro.Purchase(0));
    Micro.AddOre(Upgrade(20).Cost, EOreKind::Blue);
    Micro.AddOre(Upgrade(20).Cost, EOreKind::Black);
    TestEqual(TEXT("available isotope upgrade is included"), NextAffordableUpgrade(Micro).Index, 20);
    TestFalse(TEXT("micro purchase is not performed"), Micro.Has(20));

    FProgress MicroGoal;
    MicroGoal.SetRefiningMode(1);
    MicroGoal.AddResources(Upgrade(0).Cost);
    TestTrue(TEXT("micro goal prerequisite opens"), MicroGoal.Purchase(0));
    MicroGoal.AddOre(1, EOreKind::Blue);
    MicroGoal.AddOre(1, EOreKind::Black);
    Result = NextAffordableUpgrade(MicroGoal, 19);
    TestTrue(TEXT("micro goal is offered despite disabled refining"), Result.State == EUpgradeNavigationState::NeedsResources);
    TestEqual(TEXT("unaffordable scan can reach micro upgrade"), Result.Index, 20);
    TestTrue(TEXT("micro deficit explicitly uses isotope"), Result.Currency == EUpgradeCurrency::Isotope);
    TestEqual(TEXT("ore is not silently treated as isotope"), Result.MissingAmount, int64(Upgrade(20).Cost));
    const FProgress MicroBefore = MicroGoal;
    NextAffordableUpgrade(MicroGoal, 19);
    TestEqual(TEXT("inspection does not change isotope"), MicroGoal.Isotope, MicroBefore.Isotope);
    TestEqual(TEXT("inspection does not spend blue ore"), MicroGoal.BlueOre, MicroBefore.BlueOre);
    TestEqual(TEXT("inspection does not spend black ore"), MicroGoal.BlackOre, MicroBefore.BlackOre);
    TestEqual(TEXT("inspection preserves refinery mode"), MicroGoal.RefiningMode, MicroBefore.RefiningMode);
    TestTrue(TEXT("refining can be enabled separately"), MicroGoal.SetRefiningMode(0));
    Result = NextAffordableUpgrade(MicroGoal, 19);
    TestEqual(TEXT("isotope deficit is refreshed after conversion"), Result.MissingAmount, int64(Upgrade(20).Cost - 1));

    // Fractional quotas must not be mistaken for already produced isotope.
    for (const int32 Mode : { 2, 3 })
    {
        FProgress Partial;
        for (int32 Index = 0; Index < UpgradeCount; ++Index)
            if (Index != 20) Partial.Acquired[Index] = true;
        TestTrue(TEXT("partial refinery mode can be enabled"), Partial.SetRefiningMode(Mode));
        const int32 OrePerPair = Mode == 2 ? 4 : 2;
        Partial.AddOre(OrePerPair, EOreKind::Blue);
        Partial.AddOre(OrePerPair, EOreKind::Black);
        TestEqual(TEXT("partial refining creates one isotope"), Partial.Isotope, int64(1));
        Partial.AddOre(1, EOreKind::Blue);
        Partial.AddOre(1, EOreKind::Black);
        Result = NextAffordableUpgrade(Partial, 19);
        TestTrue(TEXT("fractional quotas leave the micro goal unaffordable"), Result.State == EUpgradeNavigationState::NeedsResources);
        TestEqual(TEXT("partial refining keeps the micro goal eligible"), Result.Index, 20);
        TestTrue(TEXT("partial refining reports isotope currency"), Result.Currency == EUpgradeCurrency::Isotope);
        TestEqual(TEXT("fractional ore quotas do not reduce isotope deficit"), Result.MissingAmount, int64(Upgrade(20).Cost - 1));
        Partial.AddOre(OrePerPair - 1, EOreKind::Blue);
        Partial.AddOre(OrePerPair - 1, EOreKind::Black);
        Result = NextAffordableUpgrade(Partial, 19);
        TestEqual(TEXT("carried quotas eventually produce another isotope"), Partial.Isotope, int64(2));
        TestEqual(TEXT("deficit updates after quota conversion"), Result.MissingAmount, int64(Upgrade(20).Cost - 2));
    }

    FProgress OrePriority;
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        if (Index != 3 && Index != 20) OrePriority.Acquired[Index] = true;
    OrePriority.AddResources(Upgrade(3).Cost);
    Result = NextAffordableUpgrade(OrePriority, 19);
    TestTrue(TEXT("affordable ore goal takes priority over locked-by-isotope goal"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("ore goal is selected even when scan starts at micro goal"), Result.Index, 3);
    TestTrue(TEXT("selected ordinary goal costs ore"), Result.Currency == EUpgradeCurrency::Ore);
    TestEqual(TEXT("one ore goal is affordable"), Result.AvailableCount, 1);

    FProgress IsotopePriority;
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        if (Index != 3 && Index != 20) IsotopePriority.Acquired[Index] = true;
    IsotopePriority.AddOre(Upgrade(20).Cost, EOreKind::Blue);
    IsotopePriority.AddOre(Upgrade(20).Cost, EOreKind::Black);
    Result = NextAffordableUpgrade(IsotopePriority, 2);
    TestTrue(TEXT("affordable isotope goal takes priority over locked-by-ore goal"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("micro goal is selected even when scan starts at ore goal"), Result.Index, 20);
    TestTrue(TEXT("selected micro goal costs isotope"), Result.Currency == EUpgradeCurrency::Isotope);
    TestEqual(TEXT("one isotope goal is affordable"), Result.AvailableCount, 1);

    FProgress IsotopeTransition;
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        if (Index != 20) IsotopeTransition.Acquired[Index] = true;
    TestTrue(TEXT("quarter-rate refinery mode can be enabled"), IsotopeTransition.SetRefiningMode(2));
    IsotopeTransition.AddOre((Upgrade(20).Cost - 1) * 4, EOreKind::Blue);
    IsotopeTransition.AddOre((Upgrade(20).Cost - 1) * 4, EOreKind::Black);
    Result = NextAffordableUpgrade(IsotopeTransition, 19);
    TestTrue(TEXT("micro goal needs resources one isotope below threshold"), Result.State == EUpgradeNavigationState::NeedsResources);
    TestEqual(TEXT("micro goal is one isotope short"), Result.MissingAmount, int64(1));
    IsotopeTransition.AddOre(4, EOreKind::Blue);
    IsotopeTransition.AddOre(4, EOreKind::Black);
    Result = NextAffordableUpgrade(IsotopeTransition, 19);
    TestTrue(TEXT("micro goal becomes available at the isotope threshold"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("available micro goal is selected"), Result.Index, 20);
    TestTrue(TEXT("threshold still uses isotope currency"), Result.Currency == EUpgradeCurrency::Isotope);
    TestEqual(TEXT("micro deficit is zero at threshold"), Result.MissingAmount, int64(0));

    FProgress Transition;
    Transition.AddResources(11);
    Result = NextAffordableUpgrade(Transition);
    TestEqual(TEXT("one ore is missing before threshold"), Result.MissingAmount, int64(1));
    Transition.AddResources(1);
    Result = NextAffordableUpgrade(Transition);
    TestTrue(TEXT("affordable mode returns after reaching threshold"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("same newly affordable radar is selected"), Result.Index, 0);

    FProgress Expensive;
    for (int32 Index = 0; Index < UpgradeCount; ++Index) if (Index != 54) Expensive.Acquired[Index] = true;
    Expensive.AddResources(3);
    Result = NextAffordableUpgrade(Expensive, 54);
    TestTrue(TEXT("unaffordable last leaf remains reachable"), Result.State == EUpgradeNavigationState::NeedsResources);
    TestEqual(TEXT("last leaf is the only unlocked candidate"), Result.Index, 54);
    TestEqual(TEXT("last leaf deficit uses int64"), Result.MissingAmount, int64(Upgrade(54).Cost) - 3);
    Expensive.AddResources(5000000000LL);
    Result = NextAffordableUpgrade(Expensive, 54);
    TestTrue(TEXT("large balances switch back to affordable mode"), Result.State == EUpgradeNavigationState::Available);
    TestEqual(TEXT("large balance never wraps deficit"), Result.MissingAmount, int64(0));

    FProgress Last;
    Last.AddResources(5000000000LL);
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        if (Index != 54) Last.Acquired[Index] = true;
    Result = NextAffordableUpgrade(Last, 54);
    TestEqual(TEXT("a last leaf is found with 64-bit funds"), Result.Index, 54);
    TestEqual(TEXT("only the last leaf is available"), Result.AvailableCount, 1);
    Last.Acquired[54] = true;
    Result = NextAffordableUpgrade(Last, 54);
    TestTrue(TEXT("a complete tree is distinct from insufficient funds"), Result.State == EUpgradeNavigationState::Complete);
    TestEqual(TEXT("a complete tree has no target"), Result.Index, INDEX_NONE);
    TestEqual(TEXT("a complete tree has zero available"), Result.AvailableCount, 0);
    return true;
}
#endif
