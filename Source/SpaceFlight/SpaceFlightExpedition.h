#pragma once

#include "CoreMinimal.h"

namespace SpaceFlight
{
    enum class EExpeditionState : uint8 { Idle, Active, Ready };

    // Standalone state: the caller records only ore actually accepted into inventory
    // and is responsible for applying the reward returned by Claim().
    struct FExpedition
    {
        static constexpr int32 MinTarget = 100;
        static constexpr int32 MaxTarget = 10000;

        // Ore codes match EOreKind: free -> common, blue route -> black,
        // diamond route -> blue. Invalid routes do not receive a reward.
        static int32 RewardOreCode(int32 Route)
        {
            switch (Route)
            {
            case 0: return 0;
            case 1: return 2;
            case 2: return 1;
            default: return INDEX_NONE;
            }
        }

        static int32 RewardAmount(int32 Route, int32 Target)
        {
            if (RewardOreCode(Route) == INDEX_NONE || Target < MinTarget || Target > MaxTarget) return 0;
            return Target / (Route == 0 ? 20 : 10);
        }

        EExpeditionState State = EExpeditionState::Idle;
        int32 Route = INDEX_NONE;
        int32 Target = 0;
        int32 Progress = 0;

        bool IsValid() const
        {
            if (State == EExpeditionState::Idle)
            {
                return Route == INDEX_NONE && Target == 0 && Progress == 0;
            }
            if (Route < 0 || Route > 2 || Target < MinTarget || Target > MaxTarget || Progress < 0)
            {
                return false;
            }
            if (State == EExpeditionState::Active)
            {
                return Progress < Target;
            }
            return State == EExpeditionState::Ready && Progress == Target;
        }

        bool IsValidForUnlockedSector(int32 UnlockedSector) const
        {
            return UnlockedSector >= 0 && UnlockedSector <= 2 && IsValid()
                && (State == EExpeditionState::Idle || Route <= UnlockedSector);
        }

        bool Start(int32 NewRoute, int32 NewTarget, int32 UnlockedSector)
        {
            if (!IsValid() || State != EExpeditionState::Idle || UnlockedSector < 0 || UnlockedSector > 2
                || NewRoute < 0 || NewRoute > 2 || NewRoute > UnlockedSector
                || NewTarget < MinTarget || NewTarget > MaxTarget)
            {
                return false;
            }
            State = EExpeditionState::Active;
            Route = NewRoute;
            Target = NewTarget;
            Progress = 0;
            return true;
        }

        // OreKind is the integer value of EOreKind: Common=0, Blue=1, Black=2.
        // Returns the portion credited to this expedition, capped by its target.
        int32 Record(int32 Sector, int32 OreKind, int64 AcceptedOre, bool bStressMode)
        {
            if (!IsValid() || State != EExpeditionState::Active || bStressMode
                || Sector < 0 || Sector > 2 || OreKind < 0 || OreKind > 2 || AcceptedOre <= 0
                || (Route == 1 && (Sector != 1 || OreKind != 1))
                || (Route == 2 && (Sector != 2 || OreKind != 2)))
            {
                return 0;
            }
            const int32 Remaining = Target - Progress;
            const int32 Credited = AcceptedOre >= Remaining ? Remaining : static_cast<int32>(AcceptedOre);
            Progress += Credited;
            if (Progress == Target)
            {
                State = EExpeditionState::Ready;
            }
            return Credited;
        }

        // A zero result means no reward was claimed. The minimum valid reward is 5.
        int32 Claim()
        {
            if (!IsValid() || State != EExpeditionState::Ready)
            {
                return 0;
            }
            const int32 Reward = RewardAmount(Route, Target);
            *this = FExpedition{};
            return Reward;
        }

        bool Cancel()
        {
            if (!IsValid() || State == EExpeditionState::Idle)
            {
                return false;
            }
            *this = FExpedition{};
            return true;
        }
    };
}
