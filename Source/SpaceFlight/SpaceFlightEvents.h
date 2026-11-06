#pragma once

#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    enum class EMineralPhase : uint8 { Waiting, Warning, Active };
    enum class ERareEventKind : uint8 { MineralFlow, UnstableCrystal, LargeDeposit };
    inline EDiscovery DiscoveryForEvent(ERareEventKind Kind)
    {
        switch (Kind)
        {
        case ERareEventKind::UnstableCrystal: return EDiscovery::UnstableCrystal;
        case ERareEventKind::LargeDeposit: return EDiscovery::LargeDeposit;
        default: return EDiscovery::MineralFlow;
        }
    }
    enum EMineralTransition : uint8 { NoTransition = 0, WarningStarted = 1, FlowStarted = 2, FlowEnded = 4 };

    // Keep early encounters familiar and bound late rewards even in long saves.
    inline int32 ScaledEventBonus(int32 BaseBonus, const FProgress& Progress)
    {
        const int64 Multiplier = 1 + FMath::Clamp<int64>(Progress.TotalCollected / 100000, 0, 4);
        return static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(FMath::Max(0, BaseBonus)) * Multiplier));
    }

    struct FMineralEvent
    {
        EMineralPhase Phase = EMineralPhase::Waiting;
        ERareEventKind Kind = ERareEventKind::MineralFlow;
        double Remaining = 0.0;
        int32 Anchor = 0;
        uint32 Claimed = 0;
        int32 Claims = 0;
        int32 EventNumber = 0;
        FRandomStream Random;

        void Initialize(uint32 Seed)
        {
            Random.Initialize(static_cast<int32>(Seed ^ 0x6e4d92b1u));
            Phase = EMineralPhase::Waiting;
            Kind = ERareEventKind::MineralFlow;
            Remaining = Random.FRandRange(150.0f, 210.0f);
            Anchor = 0;
            Claimed = 0;
            Claims = 0;
            EventNumber = 0;
        }

        uint8 Advance(double Delta, bool bBlocked, int32 Count)
        {
            if (bBlocked || !FMath::IsFinite(Delta) || Delta <= 0.0 || Count <= 0) return NoTransition;
            uint8 Transitions = NoTransition;
            while (Delta >= Remaining)
            {
                Delta -= Remaining;
                if (Phase == EMineralPhase::Waiting)
                {
                    Phase = EMineralPhase::Warning;
                    Remaining = 8.0;
                    Kind = EventNumber++ == 0 ? ERareEventKind::MineralFlow :
                        static_cast<ERareEventKind>(Random.RandRange(0, 2));
                    Anchor = Random.RandRange(0, Count - 1);
                    if (Kind == ERareEventKind::LargeDeposit)
                    {
                        TArray<int32, TInlineAllocator<16>> Eligible;
                        for (int32 I = 0; I < FMath::Min(Count, 32); ++I)
                            if (AsteroidSizeForIndex(I) == 27) Eligible.Add(I);
                        if (Eligible.IsEmpty()) Kind = ERareEventKind::MineralFlow;
                        else Anchor = Eligible[Random.RandRange(0, Eligible.Num() - 1)];
                    }
                    Claimed = 0;
                    Claims = 0;
                    Transitions |= WarningStarted;
                }
                else if (Phase == EMineralPhase::Warning)
                {
                    Phase = EMineralPhase::Active;
                    Remaining = 35.0;
                    Transitions |= FlowStarted;
                }
                else
                {
                    Phase = EMineralPhase::Waiting;
                    Remaining = Random.FRandRange(180.0f, 300.0f);
                    Transitions |= FlowEnded;
                }
            }
            Remaining -= Delta;
            return Transitions;
        }

        bool IsMarked(int32 Index, int32 Count) const
        {
            const int32 Limit = Kind == ERareEventKind::MineralFlow ? 4 : 1;
            if (Phase != EMineralPhase::Active || Claims >= Limit || Index < 0 || Index >= Count || Count <= 0 || Index >= 32) return false;
            if (Kind == ERareEventKind::LargeDeposit)
            {
                for (int32 Offset = 0; Offset < Count; ++Offset)
                {
                    const int32 Candidate = (Anchor % Count + Offset) % Count;
                    if (Candidate < 32 && AsteroidSizeForIndex(Candidate) == 27) return Index == Candidate;
                }
                return false;
            }
            return ((Index - Anchor % Count + Count) % Count) < FMath::Min(Limit, Count) && (Claimed & (1u << Index)) == 0;
        }

        int32 CurrentBonus() const
        {
            if (Phase != EMineralPhase::Active) return 0;
            if (Kind == ERareEventKind::LargeDeposit) return 9;
            return Kind == ERareEventKind::MineralFlow ? 2 : (Remaining > 20.0 ? 6 : 3);
        }

        int32 ClaimBonus(int32 Index, int32 Count)
        {
            if (!IsMarked(Index, Count)) return 0;
            Claimed |= 1u << Index;
            ++Claims;
            return CurrentBonus();
        }
    };
}
