#include "SpaceFlightHudActions.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionSectorHintTest,
    "SpaceFlight.HUD.ExpeditionSectorHint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionSectorHintTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FExpedition Expedition;
    TestTrue(TEXT("idle route has no sector hint"), ExpeditionSectorHint(Expedition, 0, false).IsEmpty());
    TestTrue(TEXT("free route starts"), Expedition.Start(0, 100, 2));
    for (int32 Sector = 0; Sector < 3; ++Sector)
        TestTrue(TEXT("free route works in every sector"), ExpeditionSectorHint(Expedition, Sector, false).IsEmpty());
    TestTrue(TEXT("free route can be cancelled"), Expedition.Cancel());
    for (int32 Route = 1; Route <= 2; ++Route)
    {
        TestTrue(TEXT("colored route starts"), Expedition.Start(Route, 100, 2));
        const FString Expected = Route == 1 ? TEXT("РЕЙС: НУЖНА СИНЯЯ РОССЫПЬ") : TEXT("РЕЙС: НУЖЕН АЛМАЗНЫЙ ПОТОК");
        for (int32 Sector = 0; Sector < 3; ++Sector)
        {
            const FString Hint = ExpeditionSectorHint(Expedition, Sector, false);
            TestEqual(TEXT("wrong sector names the route; matching sector has no warning"), Hint,
                Sector == Route ? FString() : Expected);
            TestTrue(TEXT("stress mode hides route guidance"), ExpeditionSectorHint(Expedition, Sector, true).IsEmpty());
        }
        TestEqual(TEXT("wrong sector cannot advance the route"), Expedition.Record(0, Route, 10, false), 0);
        TestEqual(TEXT("matching ore in matching sector advances the route"), Expedition.Record(Route, Route, 100, false), 100);
        TestTrue(TEXT("completed route shows reward rather than sector hint"), ExpeditionSectorHint(Expedition, 0, false).IsEmpty());
        TestTrue(TEXT("completed route can be claimed"), Expedition.Claim() > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightExpeditionActionLabelTest,
    "SpaceFlight.HUD.ExpeditionActionLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightExpeditionActionLabelTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestEqual(TEXT("idle expedition opens the route selection"), FString(ExpeditionActionWord(EExpeditionState::Idle)), FString(TEXT("РЕЙС")));
    TestEqual(TEXT("active expedition opens its progress"), FString(ExpeditionActionWord(EExpeditionState::Active)), FString(TEXT("РЕЙС")));
    TestEqual(TEXT("completed expedition shows its unclaimed reward"), FString(ExpeditionActionWord(EExpeditionState::Ready)), FString(TEXT("НАГРАДА")));
    TestEqual(TEXT("stress mode does not advertise a disabled claim"), FString(ExpeditionActionWord(EExpeditionState::Ready, true)), FString(TEXT("РЕЙС")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightSectorStatusLabelTest,
    "SpaceFlight.HUD.SectorActionLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightSectorStatusLabelTest::RunTest(const FString& Parameters)
{
    const TCHAR* Names[] = { TEXT("РУБЕЖ"), TEXT("СИНЯЯ РОССЫПЬ"), TEXT("АЛМАЗНЫЙ ПОТОК") };
    for (bool bAutopilot : { false, true })
    {
        for (int32 Objectives : { 0, 1, 2, 3, 8 })
        {
            const FString Label = SpaceFlight::SectorStatusLabel(bAutopilot, 0, Objectives);
            const FString Context = FString::Printf(TEXT("autopilot=%d objectives=%d"), bAutopilot, Objectives);
            TestTrue(Context + TEXT(": flight mode is visible"),
                Label.Contains(bAutopilot ? TEXT("АВТО") : TEXT("РУЧНОЙ")));
            TestTrue(Context + TEXT(": current sector is visible"), Label.Contains(Names[0]));
            TestEqual(Context + TEXT(": sector key is shown only when a sector is unlocked"),
                Label.Contains(TEXT("C")), Objectives >= 3);
            if (Objectives >= 3)
                TestTrue(Context + TEXT(": key is identified as a sector action"), Label.Contains(TEXT("C — СЕКТОР:")));
        }
        for (int32 Sector : { 1, 2 })
        {
            const int32 Objectives = Sector == 1 ? 3 : 8;
            const FString Label = SpaceFlight::SectorStatusLabel(bAutopilot, Sector, Objectives);
            const FString Context = FString::Printf(TEXT("autopilot=%d sector=%d"), bAutopilot, Sector);
            TestTrue(Context + TEXT(": flight mode is visible"),
                Label.Contains(bAutopilot ? TEXT("АВТО") : TEXT("РУЧНОЙ")));
            TestTrue(Context + TEXT(": current sector is visible"), Label.Contains(Names[Sector]));
            TestTrue(Context + TEXT(": sector key is visible"), Label.Contains(TEXT("C")));
        }
    }
    TestFalse(TEXT("unavailable sector action is hidden while paused or during stress mode"),
        SpaceFlight::SectorStatusLabel(true, 2, 8, false).Contains(TEXT("C")));
    TestFalse(TEXT("sector change is unavailable before cooldown expires"), SpaceFlight::CanChangeSectorNow(false, false, 11.99f, 12.0f));
    TestTrue(TEXT("sector change becomes available at cooldown expiry"), SpaceFlight::CanChangeSectorNow(false, false, 12.0f, 12.0f));
    TestFalse(TEXT("pause blocks an otherwise ready sector change"), SpaceFlight::CanChangeSectorNow(true, false, 12.0f, 12.0f));
    TestFalse(TEXT("stress mode blocks an otherwise ready sector change"), SpaceFlight::CanChangeSectorNow(false, true, 12.0f, 12.0f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightPauseActionLabelTest,
    "SpaceFlight.HUD.PauseActionLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightPauseActionLabelTest::RunTest(const FString& Parameters)
{
    for (int32 Mask = 0; Mask < 8; ++Mask)
    {
        const bool bRefining = (Mask & 1) != 0;
        const bool bProduction = (Mask & 2) != 0;
        const bool bSpecialization = (Mask & 4) != 0;
        const FString Label = SpaceFlight::PauseActionLabel(bRefining, bProduction, bSpecialization);
        const FString Context = FString::Printf(TEXT("selection mask=%d"), Mask);
        TestTrue(Context + TEXT(": Escape key is shown"), Label.Contains(TEXT("ESC")));
        TestEqual(Context + TEXT(": cancellation matches an open selection"), Label.Contains(TEXT("ОТМЕНА")), Mask != 0);
        TestEqual(Context + TEXT(": resume is shown only with no selection"), Label.Contains(TEXT("ПРОДОЛЖИТЬ")), Mask == 0);
        for (bool bPaused : { false, true })
        {
            const FString Short = SpaceFlight::EscapeActionShort(bPaused, bRefining, bProduction, bSpecialization);
            const TCHAR* Expected = Mask != 0 ? TEXT("ОТМЕНА") : bPaused ? TEXT("ПРОДОЛЖИТЬ") : TEXT("ПАУЗА");
            TestEqual(Context + TEXT(": control guide matches the next Escape action"), Short, FString(Expected));
        }
    }
    return true;
}
#endif
