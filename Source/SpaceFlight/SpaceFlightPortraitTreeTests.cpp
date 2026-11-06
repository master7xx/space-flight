#include "SpaceFlightLogic.h"
#include "SpaceFlightTreeInput.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightPortraitTreeTest, "SpaceFlight.Tree.PortraitLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightPortraitTreeTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    constexpr float W = 512, H = 1024;
    TestTrue(TEXT("portrait uses the dedicated layout"), IsPortraitUpgradeLayout(W, H));
    TestTrue(TEXT("square windows keep the readable tree"), IsPortraitUpgradeLayout(512, 512));
    TestEqual(TEXT("portrait uses a legible card scale"), UpgradeTreeScale(W, H), 1.0f);
    TestTrue(TEXT("ship remains at window center"), UpgradeTreeShipCenter(W, H).Equals(FVector2D(256, 512), .01f));
    const FVector2D Min = UpgradeTreeCanvasMin(W, H), Max = UpgradeTreeCanvasMax(W, H);
    TestTrue(TEXT("portrait canvas uses most of the height"), Max.Y - Min.Y > H * .7f);
    TestFalse(TEXT("header cannot initiate a left drag"), IsTreeCanvasPoint(FVector2D(256, 80), W, H));
    TestFalse(TEXT("footer cannot initiate a left drag"), IsTreeCanvasPoint(FVector2D(256, 950), W, H));
    for (int32 I = 0; I < UpgradeCount; ++I)
    {
        const FVector2D Pan = -UpgradeTreePosition(I) * DefaultTreeZoom;
        const FVector2D Node = UpgradeScreenCenter(I, W, H, DefaultTreeZoom, Pan);
        TestEqual(FString::Printf(TEXT("portrait node %d selectable after panning"), I),
            UpgradeAtScreenPosition(Node, W, H, DefaultTreeZoom, Pan), I);
    }
    const FVector2D Cursor(300, 420);
    const FVector2D Pan(10, -15);
    const float OldZoom = .85f, NewZoom = 1.2f;
    const FVector2D ZoomPan = PanForTreeZoom(Cursor, W, H, Pan, OldZoom, NewZoom);
    const FVector2D Before = (Cursor - UpgradeTreeShipCenter(W, H, Pan)) / (UpgradeTreeScale(W, H) * OldZoom);
    const FVector2D After = (Cursor - UpgradeTreeShipCenter(W, H, ZoomPan)) / (UpgradeTreeScale(W, H) * NewZoom);
    TestTrue(TEXT("zoom retains the canvas point under the cursor"), Before.Equals(After, .01f));
    FTreePointerGesture Gesture;
    FVector2D DragPan = FVector2D::ZeroVector;
    // A partially clipped side-branch card covers (26, 140) at this zoom;
    // dragging it is intentionally a card gesture, not a canvas pan.
    const FVector2D Start(480, 160);
    TestEqual(TEXT("portrait drag starts on empty canvas"),
        UpgradeAtScreenPosition(Start, W, H, DefaultTreeZoom, DragPan), INDEX_NONE);
    TestTrue(TEXT("portrait canvas accepts a left drag"), Gesture.Begin(ETreePointerButton::Left, Start, W, H, DefaultTreeZoom, DragPan));
    Gesture.Update(Start + FVector2D(30, 40), W, H, DragPan);
    TestTrue(TEXT("portrait canvas follows drag"), DragPan.Equals(FVector2D(30, 40), .01f));
    Gesture.End(ETreePointerButton::Left, Start + FVector2D(30, 40), W, H, DefaultTreeZoom, DragPan);
    TestFalse(TEXT("landscape retains the original layout"), IsPortraitUpgradeLayout(1280, 720));
    TestEqual(TEXT("landscape reference scale stays unchanged"), UpgradeTreeScale(1280, 720), 1.0f);
    return true;
}
#endif
