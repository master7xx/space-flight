#pragma once

#include "SpaceFlightLogic.h"
#include "BlueAsteroidArt.h"

namespace SpaceFlight
{
    constexpr int32 SectorCount = 3;
    inline int32 UnlockedSector(int32 CompletedGoals) { return CompletedGoals >= 8 ? 2 : (CompletedGoals >= 3 ? 1 : 0); }
    inline int32 NextSector(int32 Current, int32 CompletedGoals)
    {
        const int32 Last = UnlockedSector(CompletedGoals);
        return Current >= 0 && Current <= Last ? (Current + 1) % (Last + 1) : 0;
    }
    inline int32 SectorAsteroidCount(int32 Sector) { return Sector == 1 ? 12 : NormalAsteroidCount; }
    inline float SectorSpeedMultiplier(int32 Sector) { return Sector == 1 ? 0.82f : (Sector == 2 ? 1.18f : 1.0f); }
    inline uint32 SectorSeed(uint32 WorldSeed, int32 Sector)
    {
        return Sector == 0 ? WorldSeed : HashCombineFast(WorldSeed, GetTypeHash(Sector));
    }
    inline EOreKind SectorOreKind(int32 Sector, uint32 WorldSeed, int32 Index, int32 Pass, int32 Generation)
    {
        if (Sector == 0)
            return IsBlueAsteroid(Index) ? EOreKind::Blue : (Index % 4 == 0 ? EOreKind::Black : EOreKind::Common);
        // Reserve one slot for each kind so flight goals cannot be stranded.
        if (Index % 12 == 0) return EOreKind::Black;
        if (Index % 12 == 1) return EOreKind::Blue;
        if (Index % 12 == 2) return EOreKind::Common;
        uint32 Seed = HashCombineFast(SectorSeed(WorldSeed, Sector), GetTypeHash(Index));
        Seed = HashCombineFast(Seed, GetTypeHash(Pass));
        Seed = HashCombineFast(Seed, GetTypeHash(Generation));
        FRandomStream Random(static_cast<int32>(Seed));
        const int32 Roll = Random.RandRange(0, 99);
        if (Roll < 60) return Sector == 1 ? EOreKind::Blue : EOreKind::Black;
        if (Roll < 80) return Sector == 1 ? EOreKind::Black : EOreKind::Blue;
        return EOreKind::Common;
    }
}
