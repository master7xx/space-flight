#pragma once

#include "CoreMinimal.h"

namespace SpaceFlight
{
    constexpr float SurpriseMinimumFlightSeconds = 1800.0f;
    constexpr int32 SurpriseMinimumObjectives = 8;
    constexpr float SurpriseLegacyDelaySeconds = 120.0f;

    struct FSurpriseProgress
    {
        bool bDelivered = false;
        float NotBeforeSeconds = SurpriseMinimumFlightSeconds;
    };

    inline FSurpriseProgress MigrateSurpriseProgress(float ElapsedSeconds)
    {
        FSurpriseProgress Result;
        Result.NotBeforeSeconds = FMath::Max(SurpriseMinimumFlightSeconds, ElapsedSeconds + SurpriseLegacyDelaySeconds);
        return Result;
    }

    inline bool CanPresentSurprise(const FSurpriseProgress& Surprise, float ElapsedSeconds,
        int32 CompletedObjectives, bool bBlocked)
    {
        return !bBlocked && !Surprise.bDelivered && FMath::IsFinite(ElapsedSeconds) &&
            FMath::IsFinite(Surprise.NotBeforeSeconds) &&
            Surprise.NotBeforeSeconds >= SurpriseMinimumFlightSeconds &&
            ElapsedSeconds >= Surprise.NotBeforeSeconds && CompletedObjectives >= SurpriseMinimumObjectives;
    }
}
