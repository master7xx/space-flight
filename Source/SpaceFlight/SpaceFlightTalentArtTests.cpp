#include "SpaceFlightTalentArt.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExtraTalentAtlasTest,
    "SpaceFlight.Tree.ExtraTalentAtlas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExtraTalentAtlasTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    const int32 ExtraUpgrades[] = { 29, 35, 41, 47, 54, 65, 70, 71, 66, 56, 60, 30, 62, 79, 31, 33 };
    for (int32 Cell = 0; Cell < UE_ARRAY_COUNT(ExtraUpgrades); ++Cell)
    {
        const int32 Index = ExtraUpgrades[Cell];
        const auto Extra = ResolveTalentIcon(Index, true, true, true);
        const auto Regular = ResolveTalentIcon(Index, false, true, true);
        const auto Legacy = ResolveTalentIcon(Index, false, false, true);
        TestTrue(FString::Printf(TEXT("upgrade %d uses distinct art"), Index), Extra.Source == ETalentAtlasSource::Extra);
        TestEqual(TEXT("each extra upgrade has its own atlas cell"), Extra.Index, Cell);
        TestTrue(TEXT("missing extra atlas falls back to original"), Regular.Source == ETalentAtlasSource::Talent);
        TestEqual(TEXT("fallback retains original mapping"), Regular.Index, TalentIconIndex(Index));
        TestTrue(TEXT("missing both talent atlases falls back to legacy"), Legacy.Source == ETalentAtlasSource::Legacy);
        TestTrue(TEXT("extra art works when other atlases are missing"), ResolveTalentIcon(Index, true, false, false).Source == ETalentAtlasSource::Extra);
        TestTrue(TEXT("missing all atlases draws no art"), ResolveTalentIcon(Index, false, false, false).Source == ETalentAtlasSource::None);
    }
    TestTrue(TEXT("ordinary upgrade retains original atlas"), ResolveTalentIcon(0, true, true, true).Source == ETalentAtlasSource::Talent);
    TestTrue(TEXT("ordinary upgrade falls back to legacy"), ResolveTalentIcon(0, true, false, true).Source == ETalentAtlasSource::Legacy);
    TestTrue(TEXT("ordinary upgrade without an assigned atlas draws no art"), ResolveTalentIcon(0, true, false, false).Source == ETalentAtlasSource::None);
    TestTrue(TEXT("invalid index remains rejected"), ResolveTalentIcon(-1, true, true, true).Source == ETalentAtlasSource::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTalentIconMappingTest,
    "SpaceFlight.Tree.TalentIconMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTalentIconMappingTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("negative upgrade index is rejected"), TalentIconIndex(INDEX_NONE), INDEX_NONE);
    TestEqual(TEXT("index past the tree is rejected"), TalentIconIndex(UpgradeCount), INDEX_NONE);
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
        TestTrue(FString::Printf(TEXT("upgrade %d maps within the talent atlas"), Index),
            TalentIconIndex(Index) >= 0 && TalentIconIndex(Index) < TalentAtlasCellCount);
    TestEqual(TEXT("radar uses antenna"), TalentIconIndex(0), 0);
    TestEqual(TEXT("small sensor uses optical sensor"), TalentIconIndex(20), 1);
    TestEqual(TEXT("resonator uses resonance icon"), TalentIconIndex(22), 14);
    TestEqual(TEXT("vein analysis uses vein analyser"), TalentIconIndex(58), 13);
    TestEqual(TEXT("crusher uses crusher"), TalentIconIndex(68), 3);
    TestEqual(TEXT("precision optics uses lens"), TalentIconIndex(72), 5);
    TestEqual(TEXT("stabilizer uses gyroscope"), TalentIconIndex(77), 9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTalentAtlasFallbackTest,
    "SpaceFlight.Tree.TalentAtlasFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTalentAtlasFallbackTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (int32 Index = 0; Index < UpgradeCount; ++Index)
    {
        const auto NewArt = ResolveTalentIcon(Index, true, true);
        TestTrue(TEXT("new atlas is preferred when present"), NewArt.Source == ETalentAtlasSource::Talent);
        TestEqual(TEXT("new atlas selects the semantic icon"), NewArt.Index, TalentIconIndex(Index));
        const auto Fallback = ResolveTalentIcon(Index, false, true);
        TestTrue(TEXT("old atlas is used if new art is unavailable"), Fallback.Source == ETalentAtlasSource::Legacy);
        TestEqual(TEXT("old atlas retains the original branch icon"), Fallback.Index, FMath::Clamp(Upgrade(Index).Column, 0, 4));
        TestTrue(TEXT("missing atlases draw no icon"), ResolveTalentIcon(Index, false, false).Source == ETalentAtlasSource::None);
    }
    TestTrue(TEXT("invalid negative index never selects an atlas"), ResolveTalentIcon(INDEX_NONE, true, true).Source == ETalentAtlasSource::None);
    TestTrue(TEXT("out-of-range index never selects an atlas"), ResolveTalentIcon(UpgradeCount, true, true).Source == ETalentAtlasSource::None);
    const auto NewShip = ResolveShipIcon(true, true);
    TestTrue(TEXT("ship uses new atlas"), NewShip.Source == ETalentAtlasSource::Talent);
    TestEqual(TEXT("ship uses its own illustration"), NewShip.Index, 15);
    const auto LegacyShip = ResolveShipIcon(false, true);
    TestTrue(TEXT("ship falls back to legacy atlas"), LegacyShip.Source == ETalentAtlasSource::Legacy);
    TestEqual(TEXT("legacy ship illustration remains available"), LegacyShip.Index, 5);
    TestTrue(TEXT("missing atlases draw no ship art"), ResolveShipIcon(false, false).Source == ETalentAtlasSource::None);
    return true;
}
#endif
