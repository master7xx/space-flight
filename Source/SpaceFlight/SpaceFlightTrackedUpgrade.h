#pragma once

#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    enum class ETrackedUpgradeStatus : uint8 { None, Prerequisite, NeedResources, Ready };
    enum class ETrackedUpgradeCurrency : uint8 { Ore, Isotope };

    struct FTrackedUpgradeInfo
    {
        ETrackedUpgradeStatus Status = ETrackedUpgradeStatus::None;
        int32 Index = INDEX_NONE;
        ETrackedUpgradeCurrency Currency = ETrackedUpgradeCurrency::Ore;
        int64 Balance = 0;
        int64 Cost = 0;
        int64 Missing = 0;
        int32 ParentIndex = INDEX_NONE;
    };

    // This is a live view of the player's current funds, not a reservation or
    // a forecast. In particular, coloured ore is not already-made isotope and
    // the cost does not include any unpurchased prerequisites.
    inline FTrackedUpgradeInfo DescribeTrackedUpgrade(const FProgress& Progress)
    {
        FTrackedUpgradeInfo Info;
        const int32 Index = Progress.TrackedUpgrade;
        if (Index < 0 || Index >= UpgradeCount || Progress.Has(Index)) return Info;

        const FUpgradeDefinition& Node = Upgrade(Index);
        Info.Index = Index;
        Info.Currency = Node.bMicro ? ETrackedUpgradeCurrency::Isotope : ETrackedUpgradeCurrency::Ore;
        Info.Balance = FMath::Max<int64>(0, Node.bMicro ? Progress.Isotope : Progress.Resources);
        Info.Cost = Node.Cost;
        Info.Missing = FMath::Max<int64>(0, Info.Cost - Info.Balance);
        Info.ParentIndex = Node.Parent;
        if (Node.Parent >= 0 && !Progress.Has(Node.Parent))
            Info.Status = ETrackedUpgradeStatus::Prerequisite;
        else
            Info.Status = Progress.CanPurchase(Index) ? ETrackedUpgradeStatus::Ready : ETrackedUpgradeStatus::NeedResources;
        return Info;
    }
}
