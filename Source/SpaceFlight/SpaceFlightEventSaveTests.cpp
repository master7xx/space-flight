#include "SpaceFlightSave.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightEventPersistenceTest, "SpaceFlight.Events.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightEventPersistenceTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FMineralEvent Event;
    Event.Initialize(1024);
    Event.Advance(Event.Remaining, false, 15);
    Event.Advance(Event.Remaining, false, 15);
    TestEqual(TEXT("event reward is granted once"), Event.ClaimBonus(Event.Anchor, 15), 2);
    Event.Advance(3.5, false, 15);
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(FProgress(), 20, 20, true, 1);
    Save->CaptureEvent(Event);
    TArray<uint8> Bytes;
    TestTrue(TEXT("event snapshot serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
    USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("event snapshot deserializes"), Loaded)) return false;
    FMineralEvent Restored;
    TestTrue(TEXT("active event restores"), Loaded->RestoreEvent(Restored));
    TestEqual(TEXT("phase restored"), static_cast<int32>(Restored.Phase), static_cast<int32>(Event.Phase));
    TestEqual(TEXT("remaining time restored"), Restored.Remaining, Event.Remaining);
    TestEqual(TEXT("random stream restored"), Restored.Random.GetCurrentSeed(), Event.Random.GetCurrentSeed());
    TestEqual(TEXT("claim mask restored"), Restored.Claimed, Event.Claimed);
    TestEqual(TEXT("claim count restored"), Restored.Claims, Event.Claims);
    TestEqual(TEXT("already claimed asteroid cannot pay twice"), Restored.ClaimBonus(Event.Anchor, 15), 0);
    Event.Advance(Event.Remaining + 1.0, false, 15);
    Restored.Advance(Restored.Remaining + 1.0, false, 15);
    TestEqual(TEXT("future schedule follows same stream"), Restored.Remaining, Event.Remaining);
    const double Previous = Restored.Remaining;
    Restored.Advance(10, true, 15);
    TestEqual(TEXT("blocked event remains paused"), Restored.Remaining, Previous);
    FProgress Progress; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    Loaded->EventClaims = 4;
    TestFalse(TEXT("inconsistent claim state rejects entire save"), Loaded->Restore(Progress, Time, Brightness, bAuto));
    Loaded->EventSchema = 2;
    TestFalse(TEXT("unknown encounter schema is not interpreted"), Loaded->Restore(Progress, Time, Brightness, bAuto));
    Loaded->EventSchema = 0;
    TestTrue(TEXT("legacy save without event state loads"), Loaded->Restore(Progress, Time, Brightness, bAuto));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightHitObjectiveTest, "SpaceFlight.Progress.HitObjective",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightHitObjectiveTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    P.Objective.Sequence = 4;
    P.Objective.Kind = 0;
    P.Objective.Progress = P.Objective.Target() - 1;
    TestTrue(TEXT("preceding objective completes"), P.Objective.Record(EOreKind::Common, 1, false, false) > 0);
    TestEqual(TEXT("hit objective appears in sequence"), P.Objective.Kind, 5);
    TestEqual(TEXT("hit objective starts at zero"), P.Objective.Progress, 0);
    const int32 Target = P.Objective.Target();
    for (int32 I = 0; I < Target - 1; ++I)
        TestEqual(TEXT("incomplete hit does not pay"), P.Objective.RecordHit(false), 0);
    TestEqual(TEXT("each hit counts once"), P.Objective.Progress, Target - 1);
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 100, 20, true, 1);
    FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("partial hit objective restores"), Save->Restore(Restored, Time, Brightness, bAuto));
    TestEqual(TEXT("partial hit count restored"), Restored.Objective.Progress, Target - 1);
    TestTrue(TEXT("last hit gives a reward"), Restored.Objective.RecordHit(false) > 0);
    TestEqual(TEXT("next objective starts clean"), Restored.Objective.Progress, 0);
    TestEqual(TEXT("extra hit does not repeat reward"), Restored.Objective.RecordHit(false), 0);
    Save->ObjectiveSchema = 1;
    TestFalse(TEXT("old objective schema cannot contain the new kind"), Save->Restore(Restored, Time, Brightness, bAuto));
    return true;
}
#endif
