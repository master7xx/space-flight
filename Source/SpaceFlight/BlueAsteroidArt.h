#pragma once

#include "CoreMinimal.h"

namespace SpaceFlight
{
    constexpr int32 BlueAsteroidVariants = 3;
    constexpr int32 BlueAsteroidDamageStates = 3;
    constexpr int32 BlueAsteroidFrames = 128;
    constexpr int32 BlueAsteroidFrameSize = 32;
    constexpr int32 BlueAsteroidAtlasColumns = 16;
    struct FBlueAsteroidMotion
    {
        float AxisTilt = 0;
        float Phase = 0;
        float AngularSpeed = 0;
        int32 Variant = 0;
    };
    inline FBlueAsteroidMotion BlueAsteroidMotion(uint32 Seed, int32 Index, int32 Pass, int32 Generation)
    {
        uint32 Key = HashCombineFast(Seed, GetTypeHash(Index));
        Key = HashCombineFast(Key, GetTypeHash(Pass));
        Key = HashCombineFast(Key, GetTypeHash(Generation));
        FRandomStream Random(static_cast<int32>(Key));
        FBlueAsteroidMotion Motion;
        Motion.AxisTilt = Random.FRandRange(-PI, PI);
        Motion.Phase = Random.FRandRange(0.0f, 2.0f * PI);
        Motion.AngularSpeed = Random.FRandRange(0.45f, 0.95f) * (Random.FRand() < 0.5f ? -1.0f : 1.0f);
        Motion.Variant = Random.RandRange(0, 2);
        return Motion;
    }
    inline bool IsBlueAsteroid(int32 Index)
    {
        const int32 Slot = Index % 7;
        return Slot == 2 || Slot == 3 || Slot == 6;
    }
    inline FColor SampleBlueAsteroid(float X, float Y, float Angle, int32 Variant, int32 Damage)
    {
        // Orthographic ray against an irregular ellipsoid, rotated about the
        // horizontal axis in the image plane. The opposite hemisphere becomes
        // visible after half a turn; the caller may tilt that axis on screen.
        const float C = FMath::Cos(Angle), S = FMath::Sin(Angle);
        const float AxisX = 0.80f + 0.025f * Variant;
        const float AxisY = 0.86f - 0.025f * Variant;
        const float AxisZ = 0.62f + 0.025f * Variant;
        const float InvY = 1.0f / FMath::Square(AxisY);
        const float InvZ = 1.0f / FMath::Square(AxisZ);
        const float A = S * S * InvY + C * C * InvZ;
        const float B = 2.0f * Y * S * C * (InvY - InvZ);
        float SurfaceRadius = 0.98f;
        FVector Normal = FVector::ZeroVector;
        for (int32 Iteration = 0; Iteration < 3; ++Iteration)
        {
            const float Q = FMath::Square(X / AxisX) + Y * Y * (C * C * InvY + S * S * InvZ) - SurfaceRadius * SurfaceRadius;
            const float Discriminant = B * B - 4.0f * A * Q;
            if (Discriminant < 0.0f) return FColor::Transparent;
            const float Depth = (-B + FMath::Sqrt(Discriminant)) / (2.0f * A);
            Normal = FVector(X / AxisX, (C * Y + S * Depth) / AxisY, (-S * Y + C * Depth) / AxisZ).GetSafeNormal();
            const float N1 = FMath::Sin(4.7f * Normal.X + 2.5f * Normal.Y + 3.2f * Normal.Z + Variant * 1.7f);
            const float N2 = FMath::Cos(3.4f * Normal.X - 4.1f * Normal.Y + 2.0f * Normal.Z - Variant * 0.8f);
            SurfaceRadius = 0.91f + 0.065f * N1 + 0.035f * N2;
        }
        const FVector ObjectNormal(Normal.X / AxisX, Normal.Y / AxisY, Normal.Z / AxisZ);
        const FVector WorldNormal(ObjectNormal.X, C * ObjectNormal.Y - S * ObjectNormal.Z,
            S * ObjectNormal.Y + C * ObjectNormal.Z);
        static const FVector Light = FVector(-0.35f, -0.45f, 0.82f).GetSafeNormal();
        const float Diffuse = FMath::Max(0.0f, FVector::DotProduct(WorldNormal.GetSafeNormal(), Light));
        const float Pattern = FMath::Sin(12.0f * Normal.X + 8.0f * Normal.Y - 7.0f * Normal.Z + Variant * 1.3f)
            * FMath::Cos(7.0f * Normal.X - 13.0f * Normal.Y + 9.0f * Normal.Z);
        const float Shade = (0.52f + 0.54f * Diffuse) * (0.88f + 0.15f * Pattern);
        FLinearColor Color = FLinearColor(0.055f + 0.01f * Variant, 0.20f + 0.015f * Variant, 0.32f + 0.015f * Variant) * Shade;
        if (Pattern > 0.68f) Color += FLinearColor(0.025f, 0.08f, 0.10f);

        // Craters are attached to distinct directions on the body, including
        // the far hemisphere, and appear or disappear as the rock turns.
        const float VariantAngle = Variant * 0.8f;
        const float VC = FMath::Cos(VariantAngle), VS = FMath::Sin(VariantAngle);
        const FVector Surface(VC * Normal.X - VS * Normal.Z, Normal.Y, VS * Normal.X + VC * Normal.Z);
        static const FVector CraterA = FVector(0.28f, -0.30f, 0.91f).GetSafeNormal();
        static const FVector CraterB = FVector(-0.48f, 0.28f, -0.83f).GetSafeNormal();
        static const FVector CraterC = FVector(0.78f, 0.49f, 0.38f).GetSafeNormal();
        const float Crater = FMath::Max3(FVector::DotProduct(Surface, CraterA),
            FVector::DotProduct(Surface, CraterB), FVector::DotProduct(Surface, CraterC));
        if (Crater > 0.976f) Color *= 0.42f;
        else if (Crater > 0.948f) Color += FLinearColor(0.018f, 0.045f, 0.055f);

        // Damage is fixed to the object-space surface, so it turns out of view
        // together with that side instead of sticking to the screen.
        if (Damage > 0 && Normal.Z > 0.2f)
        {
            const bool MainCrack = FMath::Abs(Normal.Y - 0.20f * Normal.X + 0.05f) < 0.07f && FMath::Abs(Normal.X) < 0.70f;
            const bool Branch = FMath::Abs(Normal.X + 0.45f * Normal.Y + 0.10f) < 0.07f && Normal.Y > -0.15f && Normal.Y < 0.60f;
            const bool Extra = Damage > 1 && FMath::Abs(Normal.Y + 0.6f * Normal.X - 0.28f) < 0.075f && FMath::Abs(Normal.X) < 0.65f;
            if (MainCrack || Branch || Extra) Color *= 0.30f;
        }
        Color.A = 1.0f;
        return Color.ToFColorSRGB();
    }
}
