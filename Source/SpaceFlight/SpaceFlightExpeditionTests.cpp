#include "SpaceFlightExpedition.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "SpaceFlightLogic.h"
#include "Misc/AutomationTest.h"

static_assert(static_cast<int32>(SpaceFlight::EOreKind::Common) == 0, "Expedition ore codes must match EOreKind");
static_assert(static_cast<int32>(SpaceFlight::EOreKind::Blue) == 1, "Expedition ore codes must match EOreKind");
static_assert(static_cast<int32>(SpaceFlight::EOreKind::Black) == 2, "Expedition ore codes must match EOreKind");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionStartTest,
    "SpaceFlight.Progress.Expedition.Start",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionStartTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FExpedition Expedition;
    TestTrue(TEXT("initial state is valid"), Expedition.IsValid());
    TestFalse(TEXT("negative route is rejected"), Expedition.Start(-1, 100, 2));
    TestFalse(TEXT("unknown route is rejected"), Expedition.Start(3, 100, 2));
    TestFalse(TEXT("target below minimum is rejected"), Expedition.Start(0, 99, 2));
    TestFalse(TEXT("target above maximum is rejected"), Expedition.Start(0, 10001, 2));
    TestFalse(TEXT("invalid unlocked sector is rejected"), Expedition.Start(0, 100, 3));
    TestFalse(TEXT("locked route is rejected"), Expedition.Start(1, 100, 0));
    TestTrue(TEXT("rejected starts leave an idle expedition"), Expedition.IsValid() && Expedition.State == EExpeditionState::Idle);
    TestTrue(TEXT("minimum target is accepted"), Expedition.Start(0, 100, 0));
    TestFalse(TEXT("an active expedition cannot be replaced"), Expedition.Start(0, 10000, 2));
    TestEqual(TEXT("original target stays fixed"), Expedition.Target, 100);
    TestTrue(TEXT("active expedition can be cancelled"), Expedition.Cancel());
    TestTrue(TEXT("maximum target is accepted"), Expedition.Start(2, 10000, 2));
    TestFalse(TEXT("route cannot be restored with an inaccessible sector"), Expedition.IsValidForUnlockedSector(1));
    TestTrue(TEXT("route is valid when sector is unlocked"), Expedition.IsValidForUnlockedSector(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionProgressTest,
    "SpaceFlight.Progress.Expedition.Progress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionProgressTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FExpedition Expedition;
    TestTrue(TEXT("blue expedition starts"), Expedition.Start(1, 101, 2));
    TestEqual(TEXT("stress mode gives no progress"), Expedition.Record(1, 1, 50, true), 0);
    TestEqual(TEXT("wrong sector gives no progress"), Expedition.Record(0, 1, 50, false), 0);
    TestEqual(TEXT("wrong ore gives no progress"), Expedition.Record(1, 2, 50, false), 0);
    TestEqual(TEXT("invalid ore is rejected"), Expedition.Record(1, 3, 50, false), 0);
    TestEqual(TEXT("negative ore quantity is rejected"), Expedition.Record(1, 1, -1, false), 0);
    TestEqual(TEXT("matching accepted ore advances progress"), Expedition.Record(1, 1, 100, false), 100);
    TestEqual(TEXT("claim before completion pays nothing"), Expedition.Claim(), 0);
    TestEqual(TEXT("oversized input saturates at target"), Expedition.Record(1, 1, TNumericLimits<int64>::Max(), false), 1);
    TestTrue(TEXT("completed expedition is ready"), Expedition.State == EExpeditionState::Ready && Expedition.IsValid());
    TestEqual(TEXT("ready expedition does not record again"), Expedition.Record(1, 1, 1, false), 0);
    TestEqual(TEXT("coloured reward is floor of ten percent of target"), Expedition.Claim(), 10);
    TestEqual(TEXT("reward cannot be claimed twice"), Expedition.Claim(), 0);
    TestTrue(TEXT("claim resets to idle"), Expedition.State == EExpeditionState::Idle && Expedition.IsValid());

    TestTrue(TEXT("black expedition starts"), Expedition.Start(2, 100, 2));
    TestEqual(TEXT("black ore in blue sector is ignored"), Expedition.Record(1, 2, 100, false), 0);
    TestEqual(TEXT("blue ore in black sector is ignored"), Expedition.Record(2, 1, 100, false), 0);
    TestEqual(TEXT("black ore in black sector counts"), Expedition.Record(2, 2, 100, false), 100);
    TestTrue(TEXT("ready expedition can be cancelled"), Expedition.Cancel());
    TestEqual(TEXT("cancelled expedition pays nothing"), Expedition.Claim(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionValidationTest,
    "SpaceFlight.Progress.Expedition.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionValidationTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FExpedition Expedition;
    TestTrue(TEXT("general expedition starts"), Expedition.Start(0, 100, 0));
    TestEqual(TEXT("unknown sector is rejected"), Expedition.Record(3, 0, 10, false), 0);
    TestEqual(TEXT("negative ore kind is rejected"), Expedition.Record(0, -1, 10, false), 0);
    TestEqual(TEXT("ordinary ore in first sector counts"), Expedition.Record(0, 0, 30, false), 30);
    TestEqual(TEXT("blue ore in second sector counts"), Expedition.Record(1, 1, 30, false), 30);
    TestEqual(TEXT("black ore in third sector counts"), Expedition.Record(2, 2, 40, false), 40);
    TestEqual(TEXT("general expedition reward"), Expedition.Claim(), 5);
    TestFalse(TEXT("cancelling idle is a no-op"), Expedition.Cancel());

    Expedition.State = EExpeditionState::Active;
    Expedition.Route = 1;
    Expedition.Target = 100;
    Expedition.Progress = 100;
    TestFalse(TEXT("an active expedition at target is invalid"), Expedition.IsValid());
    TestEqual(TEXT("invalid saved state cannot accrue ore"), Expedition.Record(1, 1, 10, false), 0);
    TestEqual(TEXT("invalid saved state cannot claim"), Expedition.Claim(), 0);
    return true;
}
#endif
