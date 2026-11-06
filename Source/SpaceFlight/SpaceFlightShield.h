#pragma once

#include "CoreMinimal.h"

namespace SpaceFlight
{
struct FShieldPulse
{
    double Start = -10.0;
    float Duration = 1.0f;
    float Angle = 0.0f;
    int32 Pattern = 0;
};

struct FShieldTimeline
{
    FShieldPulse Current;
    double NextStart = 0.0;
    FRandomStream Random;

    void Initialize(uint32 Seed, double Now)
    {
        Random.Initialize(static_cast<int32>(Seed ^ 0x52b1c683u));
        Current = FShieldPulse();
        NextStart = Now + Random.FRandRange(2.0f, 8.0f);
    }

    bool Update(double Now)
    {
        if (!FMath::IsFinite(Now)) return false;
        bool bChanged = false;
        while (Now >= NextStart)
        {
            Current.Start = NextStart;
            Current.Pattern = Random.RandRange(0, 2);
            Current.Duration = Random.FRandRange(.85f, 1.35f);
            Current.Angle = Random.FRandRange(0.0f, 2.0f * PI);
            NextStart = Current.Start + Random.FRandRange(2.0f, 8.0f);
            bChanged = true;
        }
        return bChanged;
    }
};

inline float ShieldEnvelope(const FShieldPulse& Pulse, double Now)
{
    if (Pulse.Duration <= 0.0f || Now <= Pulse.Start || Now >= Pulse.Start + Pulse.Duration) return 0.0f;
    const float Wave = FMath::Sin(PI * static_cast<float>((Now - Pulse.Start) / Pulse.Duration));
    return Wave * Wave;
}

struct FShieldSegment
{
    FVector2D A;
    FVector2D B;
};

inline bool ClipShieldLine(FVector2D A, FVector2D B, FVector2D& OutA, FVector2D& OutB)
{
    const FVector2D D = B - A;
    const double AA = D.SizeSquared();
    if (AA < 1.e-10) return false;
    const double BB = 2.0 * FVector2D::DotProduct(A, D);
    const double CC = A.SizeSquared() - 1.0;
    const double Discriminant = BB * BB - 4.0 * AA * CC;
    if (Discriminant < 0.0) return false;
    const double Root = FMath::Sqrt(Discriminant);
    const double T0 = FMath::Max(0.0, (-BB - Root) / (2.0 * AA));
    const double T1 = FMath::Min(1.0, (-BB + Root) / (2.0 * AA));
    if (T0 >= T1) return false;
    OutA = A + D * T0;
    OutB = A + D * T1;
    return true;
}

inline TArray<FShieldSegment> BuildShieldSegments()
{
    TArray<FShieldSegment> Result;
    constexpr float Radius = .22f;
    for (int32 Q = -5; Q <= 5; ++Q)
    {
        for (int32 R = -5; R <= 5; ++R)
        {
            const FVector2D Center(1.5f * Radius * Q, FMath::Sqrt(3.0f) * Radius * (R + Q * .5f));
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                const float AngleA = Edge * PI / 3.0f;
                const float AngleB = (Edge + 1) * PI / 3.0f;
                FVector2D A, B;
                if (ClipShieldLine(Center + Radius * FVector2D(FMath::Cos(AngleA), FMath::Sin(AngleA)),
                    Center + Radius * FVector2D(FMath::Cos(AngleB), FMath::Sin(AngleB)), A, B))
                    Result.Add({ A, B });
            }
        }
    }
    return Result;
}

inline float ShieldSegmentStrength(const FShieldPulse& Pulse, const FShieldSegment& Segment, int32 Index, double Now)
{
    const float Envelope = ShieldEnvelope(Pulse, Now);
    if (Envelope <= 0.0f) return 0.0f;
    const float Progress = static_cast<float>((Now - Pulse.Start) / Pulse.Duration);
    const FVector2D Mid = (Segment.A + Segment.B) * .5;
    float Local = 0.0f;
    if (Pulse.Pattern == 0)
    {
        const float Angle = FMath::Atan2(Mid.Y, Mid.X);
        const float Difference = FMath::Abs(FMath::UnwindRadians(Angle - Pulse.Angle - Progress * 2.0f * PI));
        Local = FMath::Clamp(1.0f - Difference / .9f, 0.0f, 1.0f);
    }
    else if (Pulse.Pattern == 1)
    {
        const FVector2D Origin(FMath::Cos(Pulse.Angle) * .4f, FMath::Sin(Pulse.Angle) * .4f);
        const float Distance = static_cast<float>((Mid - Origin).Size());
        Local = FMath::Clamp(1.0f - FMath::Abs(Distance - Progress * 1.6f) / .32f, 0.0f, 1.0f);
    }
    else
    {
        const uint32 Hash = static_cast<uint32>(Index + 1) * 2654435761u;
        const float Offset = static_cast<float>((Hash >> 16) & 255) / 255.0f * .5f;
        if ((Hash & 3u) != 0)
            Local = FMath::Sin(PI * FMath::Clamp((Progress - Offset) * 2.0f, 0.0f, 1.0f));
    }
    return FMath::Clamp(Envelope * Local * Local, 0.0f, 1.0f);
}
}
