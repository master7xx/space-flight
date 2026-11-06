#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SpaceFlightHudLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightAdditionalViewportTest, "SpaceFlight.HUD.AdditionalViewportLayouts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightAdditionalViewportTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    // Portrait, short widescreen, 4:3, reference, 16:9, and ultrawide.
    for (const FVector2D Viewport : { FVector2D(512, 1024), FVector2D(640, 360), FVector2D(800, 600),
        FVector2D(1280, 720), FVector2D(1920, 1080), FVector2D(3440, 1440) })
    {
        const FCompactHudLayout Layout = MakeCompactHudLayout(Viewport.X, Viewport.Y);
        TestTrue(TEXT("valid viewport"), Layout.bValid);
        const FHudRect Panels[] = { Layout.Status, Layout.Goal, Layout.Clock, Layout.Resources };
        for (int32 I = 0; I < UE_ARRAY_COUNT(Panels); ++I)
        {
            TestTrue(TEXT("permanent panel stays onscreen"), Panels[I].Fits(Viewport.X, Viewport.Y));
            for (int32 J = I + 1; J < UE_ARRAY_COUNT(Panels); ++J)
                TestFalse(TEXT("permanent panels remain distinct"), Panels[I].Intersects(Panels[J]));
        }
        TestTrue(TEXT("left popup fits"), Layout.LeftPopup.Fits(Viewport.X, Viewport.Y));
        TestTrue(TEXT("right popup fits"), Layout.RightPopup.Fits(Viewport.X, Viewport.Y));
        TestFalse(TEXT("left popup clears objective"), Layout.LeftPopup.Intersects(Layout.Goal));
        TestFalse(TEXT("right popup clears resources"), Layout.RightPopup.Intersects(Layout.Resources));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightTrackedUpgradeLayoutTest, "SpaceFlight.HUD.TrackedUpgradeLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightTrackedUpgradeLayoutTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    for (const FVector2D Viewport : { FVector2D(640, 360), FVector2D(800, 600),
        FVector2D(1280, 720), FVector2D(1920, 1080) })
    {
        const FCompactHudLayout Base = MakeCompactHudLayout(Viewport.X, Viewport.Y);
        const FCompactHudLayout ExplicitOff = MakeCompactHudLayout(Viewport.X, Viewport.Y, false);
        const FCompactHudLayout Tracked = MakeCompactHudLayout(Viewport.X, Viewport.Y, true);
        const float S = Tracked.Scale;
        const FString Ctx = FString::Printf(TEXT("%.0fx%.0f"), Viewport.X, Viewport.Y);
        TestEqual(Ctx + TEXT(": tracking is absent by default"), Base.TrackedUpgrade.Height, 0.0f);
        TestTrue(Ctx + TEXT(": explicit off preserves left popup"),
            FMath::IsNearlyEqual(Base.LeftPopup.Y, ExplicitOff.LeftPopup.Y));
        TestTrue(Ctx + TEXT(": tracked panel fits"), Tracked.TrackedUpgrade.Fits(Viewport.X, Viewport.Y));
        TestTrue(Ctx + TEXT(": tracked panel reserves two readable lines"),
            FMath::IsNearlyEqual(Tracked.TrackedUpgrade.Height, 46.0f * S));
        TestTrue(Ctx + TEXT(": tracked panel follows the flight goal"),
            Tracked.TrackedUpgrade.Y >= Tracked.Goal.Y + Tracked.Goal.Height);
        for (const FHudRect& Rect : { Tracked.Status, Tracked.Clock, Tracked.Goal, Tracked.Resources,
            Tracked.LeftPopup, Tracked.RightPopup, RefiningPopupRect(Tracked, Viewport.X) })
            TestFalse(Ctx + TEXT(": tracked panel does not cover another panel"), Tracked.TrackedUpgrade.Intersects(Rect));
        TestTrue(Ctx + TEXT(": left popup remains on screen"), Tracked.LeftPopup.Fits(Viewport.X, Viewport.Y));
        TestTrue(Ctx + TEXT(": left popup begins after the tracked panel"),
            Tracked.LeftPopup.Y >= Tracked.TrackedUpgrade.Y + Tracked.TrackedUpgrade.Height);
        const FHudRect Pause{ Viewport.X * .5f - 125 * S, Tracked.Clock.Y + 48 * S, 250 * S, 28 * S };
        const FHudRect Reset{ Viewport.X * .5f - 145 * S, Pause.Y + 34 * S, 290 * S, 27 * S };
        const float Gap = Tracked.Resources.X - (Tracked.Status.X + Tracked.Status.Width) - 16 * S;
        const float NoticeWidth = FMath::Min(380 * S, Gap);
        const FHudRect Notice{ (Tracked.Status.X + Tracked.Status.Width + Tracked.Resources.X - NoticeWidth) * .5f,
            Tracked.Resources.Y + Tracked.Resources.Height + 8 * S, NoticeWidth, 28 * S };
        for (const FHudRect& Rect : { Pause, Reset, Notice })
            TestFalse(Ctx + TEXT(": tracked panel does not cover flight messages"), Tracked.TrackedUpgrade.Intersects(Rect));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightRefiningOverlayLayoutTest, "SpaceFlight.HUD.RefiningOverlayLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightRefiningOverlayLayoutTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    bool bSawPauseOverlap = false, bSawResetOverlap = false, bSawNoticeOverlap = false;
    for (const FVector2D Viewport : { FVector2D(640, 360), FVector2D(800, 600), FVector2D(1280, 720) })
    {
        const FCompactHudLayout Layout = MakeCompactHudLayout(Viewport.X, Viewport.Y);
        const float S = Layout.Scale;
        const FHudRect Popup = RefiningPopupRect(Layout, Viewport.X);
        const FString Context = FString::Printf(TEXT("%.0fx%.0f"), Viewport.X, Viewport.Y);
        TestTrue(Context + TEXT(": refining popup fits"), Popup.Fits(Viewport.X, Viewport.Y));
        for (const FHudRect& Permanent : { Layout.Status, Layout.Goal, Layout.Clock, Layout.Resources })
            TestFalse(Context + TEXT(": refining popup clears permanent panels"), Popup.Intersects(Permanent));
        TestTrue(Context + TEXT(": right edge stays anchored"),
            FMath::IsNearlyEqual(Popup.X + Popup.Width, Layout.RightPopup.X + Layout.RightPopup.Width));
        TestTrue(Context + TEXT(": popup is never narrower"), Popup.Width + KINDA_SMALL_NUMBER >= Layout.RightPopup.Width);

        const FHudRect Pause{ Viewport.X * .5f - 125 * S, Layout.Clock.Y + 48 * S, 250 * S, 28 * S };
        const FHudRect Reset{ Viewport.X * .5f - 145 * S, Pause.Y + 34 * S, 290 * S, 27 * S };
        const float NoticeGap = Layout.Resources.X - (Layout.Status.X + Layout.Status.Width) - 16 * S;
        const float NoticeWidth = FMath::Min(380 * S, NoticeGap);
        const FHudRect Notice{ (Layout.Status.X + Layout.Status.Width + Layout.Resources.X - NoticeWidth) * .5f,
            Layout.Resources.Y + Layout.Resources.Height + 8 * S, NoticeWidth, 28 * S };
        const FHudRect Overlays[] = { Pause, Reset, Notice };
        bool* SawOverlap[] = { &bSawPauseOverlap, &bSawResetOverlap, &bSawNoticeOverlap };
        for (int32 I = 0; I < UE_ARRAY_COUNT(Overlays); ++I)
        {
            const FHudRect& Before = Overlays[I];
            const bool bOverlaps = Before.Intersects(Popup);
            *SawOverlap[I] |= bOverlaps;
            const FHudRect After = BelowIfOverlapping(Before, Popup, 8 * S);
            TestFalse(Context + TEXT(": transient overlay clears popup"), After.Intersects(Popup));
            TestTrue(Context + TEXT(": transient overlay fits"), After.Fits(Viewport.X, Viewport.Y));
            TestTrue(Context + TEXT(": overlay size and X remain unchanged"),
                FMath::IsNearlyEqual(After.X, Before.X) && FMath::IsNearlyEqual(After.Width, Before.Width) &&
                FMath::IsNearlyEqual(After.Height, Before.Height));
            const float ExpectedY = bOverlaps ? Popup.Y + Popup.Height + 8 * S : Before.Y;
            TestTrue(Context + TEXT(": overlay moves only when necessary"), FMath::IsNearlyEqual(After.Y, ExpectedY));
        }
        const FHudRect MovedPause = BelowIfOverlapping(Pause, Popup, 8 * S);
        const FHudRect MovedReset = BelowIfOverlapping(BelowIfOverlapping(Reset, Popup, 8 * S), MovedPause, 8 * S);
        TestFalse(Context + TEXT(": pause and reset remain separate"), MovedPause.Intersects(MovedReset));
        TestFalse(Context + TEXT(": moved reset still clears popup"), MovedReset.Intersects(Popup));
        TestTrue(Context + TEXT(": moved pause and reset fit"),
            MovedPause.Fits(Viewport.X, Viewport.Y) && MovedReset.Fits(Viewport.X, Viewport.Y));
    }
    TestTrue(TEXT("test covers real pause overlap"), bSawPauseOverlap);
    TestTrue(TEXT("test covers real reset overlap"), bSawResetOverlap);
    TestTrue(TEXT("test covers real notice overlap"), bSawNoticeOverlap);
    return true;
}

#endif
