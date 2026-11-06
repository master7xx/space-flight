#pragma once

#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    enum class EUpgradeNavigationState : uint8
    {
        Available,
        NeedsResources,
        NoneAffordable,
        Complete
    };

    enum class EUpgradeCurrency : uint8 { Ore, Isotope };

    struct FUpgradeNavigationResult
    {
        EUpgradeNavigationState State = EUpgradeNavigationState::NoneAffordable;
        int32 Index = INDEX_NONE;
        int32 AvailableCount = 0;
        int32 EligibleCount = 0;
        int64 MissingAmount = 0;
        EUpgradeCurrency Currency = EUpgradeCurrency::Ore;
    };

    inline bool IsUpgradeEligible(const FProgress& Progress, int32 Index)
    {
        if (Index < 0 || Index >= UpgradeCount || Progress.Has(Index)) return false;
        const auto& Node = Upgrade(Index);
        return Node.Parent < 0 || Progress.Has(Node.Parent);
    }

    // Called on demand, not every frame. The cursor is an upgrade index, not
    // a list position, so the next result stays predictable if resources or
    // prerequisites change between key presses. Invalid cursors start at 0.
    // If nothing can be purchased, cycle through the currently unlocked
    // unowned nodes without ranking ore and isotope against one another.
    inline FUpgradeNavigationResult NextAffordableUpgrade(const FProgress& Progress, int32 AfterIndex = INDEX_NONE)
    {
        FUpgradeNavigationResult Result;
        int32 OwnedCount = 0;
        for (int32 Index = 0; Index < UpgradeCount; ++Index)
        {
            OwnedCount += Progress.Has(Index) ? 1 : 0;
            Result.AvailableCount += Progress.CanPurchase(Index) ? 1 : 0;
            if (IsUpgradeEligible(Progress, Index)) ++Result.EligibleCount;
        }
        if (OwnedCount == UpgradeCount)
        {
            Result.State = EUpgradeNavigationState::Complete;
            return Result;
        }
        if (Result.EligibleCount == 0) return Result;

        const int32 Start = AfterIndex >= 0 && AfterIndex < UpgradeCount ? (AfterIndex + 1) % UpgradeCount : 0;
        for (int32 Offset = 0; Offset < UpgradeCount; ++Offset)
        {
            const int32 Index = (Start + Offset) % UpgradeCount;
            const auto& Node = Upgrade(Index);
            const bool bEligible = IsUpgradeEligible(Progress, Index);
            if (bEligible && (Result.AvailableCount == 0 || Progress.CanPurchase(Index)))
            {
                Result.State = Result.AvailableCount > 0 ? EUpgradeNavigationState::Available : EUpgradeNavigationState::NeedsResources;
                Result.Index = Index;
                Result.Currency = Node.bMicro ? EUpgradeCurrency::Isotope : EUpgradeCurrency::Ore;
                const int64 Balance = FMath::Max<int64>(0, Node.bMicro ? Progress.Isotope : Progress.Resources);
                Result.MissingAmount = FMath::Max<int64>(0, static_cast<int64>(Node.Cost) - Balance);
                break;
            }
        }
        return Result;
    }
}
