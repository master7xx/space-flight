#pragma once

#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    enum class ETreePointerButton : uint8 { Left, Middle, Right };

    inline bool IsTreeCanvasPoint(FVector2D Point, float Width, float Height)
    {
        if (Width <= 0 || Height <= 0) return false;
        const FVector2D Min = UpgradeTreeCanvasMin(Width, Height);
        const FVector2D Max = UpgradeTreeCanvasMax(Width, Height);
        return Point.X >= Min.X && Point.X <= Max.X && Point.Y >= Min.Y && Point.Y <= Max.Y;
    }

    struct FTreePointerGesture
    {
        bool IsActive() const { return bActive; }
        ETreePointerButton Button() const { return ActiveButton; }
        void Cancel() { bActive = false; bMoved = false; PressedCard = INDEX_NONE; }

        bool Begin(ETreePointerButton NewButton, FVector2D Point, float Width, float Height, float Zoom, FVector2D Pan)
        {
            if (bActive || Width <= 0 || Height <= 0 || Zoom <= 0 ||
                Point.X < 0 || Point.Y < 0 || Point.X > Width || Point.Y > Height) return false;
            const int32 Card = UpgradeAtScreenPosition(Point, Width, Height, Zoom, Pan);
            if (NewButton == ETreePointerButton::Left && Card == INDEX_NONE && !IsTreeCanvasPoint(Point, Width, Height)) return false;
            ActiveButton = NewButton;
            Start = Last = Point;
            PressedCard = NewButton == ETreePointerButton::Left ? Card : INDEX_NONE;
            bMoved = false;
            bActive = true;
            return true;
        }

        void Update(FVector2D Point, float Width, float Height, FVector2D& Pan)
        {
            if (!bActive || Width <= 0 || Height <= 0) return;
            const float S = UpgradeTreeScale(Width, Height);
            if (S <= 0) return;
            if (FVector2D::DistSquared(Start, Point) > FMath::Square(4.0f * S)) bMoved = true;
            if (bMoved && PressedCard == INDEX_NONE)
            {
                Pan += (Point - Last) / S;
                Last = Point;
            }
        }

        int32 End(ETreePointerButton ReleasedButton, FVector2D Point, float Width, float Height, float Zoom, FVector2D& Pan)
        {
            if (!bActive || ReleasedButton != ActiveButton) return INDEX_NONE;
            Update(Point, Width, Height, Pan);
            const int32 Clicked = !bMoved && PressedCard != INDEX_NONE &&
                UpgradeAtScreenPosition(Point, Width, Height, Zoom, Pan) == PressedCard ? PressedCard : INDEX_NONE;
            Cancel();
            return Clicked;
        }

    private:
        bool bActive = false;
        bool bMoved = false;
        ETreePointerButton ActiveButton = ETreePointerButton::Left;
        int32 PressedCard = INDEX_NONE;
        FVector2D Start = FVector2D::ZeroVector;
        FVector2D Last = FVector2D::ZeroVector;
    };
}
