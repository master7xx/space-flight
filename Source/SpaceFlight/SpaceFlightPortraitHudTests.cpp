#include "SpaceFlightHudLayout.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightPortraitHudLayoutTest, "SpaceFlight.HUD.PortraitLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightPortraitHudLayoutTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    TestFalse(TEXT("landscape retains the existing HUD"), UsePortraitHud(1280, 720));
    TestTrue(TEXT("512x1024 selects portrait"), UsePortraitHud(512, 1024));
    TestTrue(TEXT("near-square narrow windows retain the readable layout"), UsePortraitHud(512, 600));
    TestTrue(TEXT("square windows retain the readable layout"), UsePortraitHud(512, 512));
    TestTrue(TEXT("slightly wide square windows retain the readable layout"), UsePortraitHud(512, 480));
    const auto Baseline = MakePortraitHudLayout(512, 1024, {});
    TestTrue(TEXT("portrait text receives substantially more scale"), Baseline.Scale >= .89f);
    TestTrue(TEXT("clock and status share the first row"), FMath::IsNearlyEqual(Baseline.Clock.Y, Baseline.Status.Y));
    TestTrue(TEXT("goal follows status"), Baseline.Goal.Y >= Baseline.Status.Y + Baseline.Status.Height);
    TestTrue(TEXT("resources follow goal"), Baseline.Resources.Y >= Baseline.Goal.Y + Baseline.Goal.Height);
    for (const FVector2D Viewport : { FVector2D(512,1024), FVector2D(400,800), FVector2D(360,800),
        FVector2D(640,900), FVector2D(240,302), FVector2D(512,600), FVector2D(240,241), FVector2D(512,512), FVector2D(512,480) })
    {
        for (int32 Mask = 0; Mask < 256; ++Mask)
        {
            FPortraitHudOptions Options;
            Options.bDetailed = (Mask & 1) != 0;
            Options.bPopup = (Mask & 2) != 0;
            Options.bPaused = (Mask & 4) != 0;
            Options.bReset = Options.bPaused && (Mask & 8) != 0;
            Options.bNotice = (Mask & 16) != 0;
            Options.bEvent = (Mask & 32) != 0;
            Options.bWarning = (Mask & 64) != 0;
            Options.bTracked = (Mask & 128) != 0;
            // Check the otherwise rare simultaneous stress+warning case as well.
            Options.bStress = (Mask & 8) != 0;
            const auto L = MakePortraitHudLayout(Viewport.X, Viewport.Y, Options);
            TestTrue(TEXT("portrait layout is valid"), L.bValid);
            TArray<FHudRect, TInlineAllocator<16>> Rects{ L.Status, L.Clock, L.Goal, L.Resources, L.Brightness };
            if (Options.bTracked)
            {
                Rects.Add(L.TrackedUpgrade);
                TestTrue(TEXT("tracked row follows flight objective"), L.TrackedUpgrade.Y >= L.Goal.Y + L.Goal.Height);
                TestTrue(TEXT("tracked row reserves two readable lines"), FMath::IsNearlyEqual(L.TrackedUpgrade.Height, 46.0f * L.Scale));
                TestTrue(TEXT("resources follow tracked row"), L.Resources.Y >= L.TrackedUpgrade.Y + L.TrackedUpgrade.Height);
            }
            else TestEqual(TEXT("no tracking leaves the optional row absent"), L.TrackedUpgrade.Height, 0.0f);
            if (Options.bDetailed) Rects.Add(L.Details);
            if (Options.bPopup) Rects.Add(L.Popup);
            if (Options.bPaused) Rects.Add(L.Pause);
            if (Options.bPaused && Options.bReset) Rects.Add(L.Reset);
            if (!Options.bPaused && Options.bNotice) Rects.Add(L.Notice);
            if (Options.bEvent) Rects.Add(L.Event);
            if (Options.bStress) Rects.Add(L.Stress);
            if (Options.bWarning) Rects.Add(L.Warning);
            for (int32 I = 0; I < Rects.Num(); ++I)
            {
                TestTrue(FString::Printf(TEXT("portrait block %d fits at %.0fx%.0f mask=%d"), I, Viewport.X, Viewport.Y, Mask),
                    Rects[I].Fits(Viewport.X, Viewport.Y));
                for (int32 J = I + 1; J < Rects.Num(); ++J)
                    TestFalse(FString::Printf(TEXT("portrait blocks %d/%d remain separate at %.0fx%.0f mask=%d"), I, J, Viewport.X, Viewport.Y, Mask),
                        Rects[I].Intersects(Rects[J]));
            }
        }
    }
    return true;
}
#endif
