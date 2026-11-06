#pragma once

#include "CoreMinimal.h"

namespace SpaceFlight
{
    struct FHudRect
    {
        float X = 0, Y = 0, Width = 0, Height = 0;
        float Area() const { return Width * Height; }
        bool Fits(float ViewWidth, float ViewHeight) const
        {
            return X >= 0 && Y >= 0 && Width > 0 && Height > 0 &&
                X + Width <= ViewWidth + 0.01f && Y + Height <= ViewHeight + 0.01f;
        }
        bool Intersects(const FHudRect& Other) const
        {
            return X < Other.X + Other.Width && X + Width > Other.X &&
                Y < Other.Y + Other.Height && Y + Height > Other.Y;
        }
    };

    struct FCompactHudLayout
    {
        bool bValid = false;
        float Scale = 0;
        FHudRect Status, Goal, Clock, Resources, TrackedUpgrade, LeftPopup, RightPopup;
    };

    inline bool UsePortraitHud(float Width, float Height)
    {
        // Keep the stacked layout across the square boundary so a slight
        // resize does not suddenly shrink all labels to desktop scale.
        return FMath::IsFinite(Width) && FMath::IsFinite(Height) && Width > 0 && Height > 0 && Width / Height < 1.1f;
    }

    struct FPortraitHudOptions
    {
        bool bDetailed = false, bPopup = false, bPaused = false, bReset = false;
        bool bNotice = false, bEvent = false, bStress = false, bWarning = false;
        bool bTracked = false;
    };

    struct FPortraitHudLayout
    {
        bool bValid = false;
        float Scale = 0;
        FHudRect Status, Clock, Goal, TrackedUpgrade, Resources, Details, Popup, Pause, Reset, Notice, Event, Stress, Warning, Brightness;
    };

    // In portrait, stack short full-width rows and reserve independent slots
    // for transient messages. Landscape keeps its original compact geometry.
    inline FPortraitHudLayout MakePortraitHudLayout(float Width, float Height, const FPortraitHudOptions& Options)
    {
        FPortraitHudLayout Out;
        if (!UsePortraitHud(Width, Height)) return Out;
        // Reserve an extra row only when a player-selected upgrade is tracked.
        // The larger height budget keeps transient rows and bottom warnings apart.
        const float S = FMath::Min3(Width / 568.0f, (Height - 30.0f) / (Options.bTracked ? 678.0f : 620.0f), 1.0f);
        if (S <= 0.0f) return Out;
        Out.bValid = true; Out.Scale = S;
        const float M = 12 * S, Gap = 6 * S, Full = Width - 2 * M;
        float Y = M;
        const float ClockWidth = 124 * S;
        Out.Status = { M, Y, Full - ClockWidth - Gap, 48 * S };
        Out.Clock = { Width - M - ClockWidth, Y, ClockWidth, 48 * S };
        Y += 54 * S;
        Out.Goal = { M, Y, Full, 32 * S }; Y += 38 * S;
        if (Options.bTracked) { Out.TrackedUpgrade = { M, Y, Full, 46 * S }; Y += 52 * S; }
        Out.Resources = { M, Y, Full, 50 * S }; Y += 56 * S;
        if (Options.bDetailed) { Out.Details = { M, Y, Full, 170 * S }; Y += 176 * S; }
        if (Options.bPopup) { Out.Popup = { M, Y, Full, 96 * S }; Y += 102 * S; }
        if (Options.bPaused) { Out.Pause = { M, Y, Full, 30 * S }; Y += 36 * S; }
        if (Options.bPaused && Options.bReset) { Out.Reset = { M, Y, Full, 28 * S }; Y += 34 * S; }
        if (!Options.bPaused && Options.bNotice) { Out.Notice = { M, Y, Full, 30 * S }; Y += 36 * S; }
        if (Options.bEvent) { Out.Event = { M, Y, Full, 30 * S }; }
        // Brightness is rendered at fixed pixel coordinates by DrawHUD.
        Out.Brightness = { 12.0f, Height - 23.0f, 28.0f, 18.0f };
        const float Bottom = Height - 30.0f;
        if (Options.bWarning) Out.Warning = { M, Bottom - 28 * S, Full, 26 * S };
        if (Options.bStress) Out.Stress = { M, Bottom - (Options.bWarning ? 60 : 28) * S, Full, 26 * S };
        return Out;
    }

    inline FCompactHudLayout MakeCompactHudLayout(float Width, float Height, bool bTracked = false)
    {
        FCompactHudLayout Out;
        if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width <= 0 || Height <= 0) return Out;
        const float BaseScale = FMath::Min(Width / 1280.0f, Height / 720.0f);
        // Give small windows a modest readability boost while retaining the
        // reference scale at 1280x720 and above.
        const float S = BaseScale < 1.0f ? FMath::Min(1.0f, BaseScale * 1.15f) : BaseScale;
        Out.bValid = S > 0;
        Out.Scale = S;
        Out.Status = { 18 * S, 16 * S, 348 * S, 48 * S };
        Out.Goal = { 18 * S, 70 * S, 348 * S, 34 * S };
        Out.Clock = { Width * .5f - 75 * S, 16 * S, 150 * S, 40 * S };
        Out.Resources = { Width - 18 * S - 390 * S, 16 * S, 390 * S, 56 * S };
        if (bTracked) Out.TrackedUpgrade = { 18 * S, 112 * S, 348 * S, 46 * S };
        Out.LeftPopup = { 18 * S, (bTracked ? 164.0f : 112.0f) * S, 348 * S, 96 * S };
        Out.RightPopup = { Out.Resources.X, 80 * S, 390 * S, 96 * S };
        return Out;
    }

    inline FHudRect RefiningPopupRect(const FCompactHudLayout& Layout, float Width)
    {
        FHudRect Popup = Layout.RightPopup;
        if (Width < 900.0f)
        {
            const float RightEdge = Popup.X + Popup.Width;
            const float LeftLimit = Layout.Goal.X + Layout.Goal.Width + 12.0f * Layout.Scale;
            Popup.Width = FMath::Max(Popup.Width, FMath::Min(600.0f * Layout.Scale, RightEdge - LeftLimit));
            Popup.X = RightEdge - Popup.Width;
            Popup.Height = 110.0f * Layout.Scale;
        }
        return Popup;
    }

    inline FHudRect BelowIfOverlapping(FHudRect Rect, const FHudRect& Obstacle, float Gap)
    {
        if (Rect.Intersects(Obstacle)) Rect.Y = Obstacle.Y + Obstacle.Height + Gap;
        return Rect;
    }
}
