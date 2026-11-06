#pragma once

#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    // New atlas uses four columns and four rows. Cells 11 and 15 are reserved
    // for the isotope emblem and ship, not assigned as upgrade effects.
    constexpr int32 TalentAtlasColumns = 4;
    constexpr int32 TalentAtlasRows = 4;
    constexpr int32 TalentAtlasCellCount = TalentAtlasColumns * TalentAtlasRows;

    // Deliberately keyed by the existing stable upgrade ID: this is visual
    // metadata only and never changes definitions, economy, or save data.
    inline int32 TalentIconIndex(int32 UpgradeIndex)
    {
        static constexpr uint8 Icons[] = {
             0,  0,  0,  2,  2,  4,  4,  6,  6,  8,
            12, 12,  2, 13,  4,  5,  6,  6,  8,  8,
             1,  2, 14,  7,  8, 12, 12, 12, 12, 12,
            10,  2, 13, 10, 13,  2, 14, 13,  5, 14,
             2,  5,  6,  6,  6,  6,  6,  6,  8,  8,
             8,  8,  8,  8,  8,  1,  1, 10, 13,  5,
            14,  7,  7,  8,  8, 12, 13, 12,  3,  3,
             3,  4,  5,  5,  6,  7,  6,  9,  9,  9,  5
        };
        static_assert(UE_ARRAY_COUNT(Icons) == UpgradeCount, "Every upgrade must have a talent icon");
        return UpgradeIndex >= 0 && UpgradeIndex < UpgradeCount ? Icons[UpgradeIndex] : INDEX_NONE;
    }

    enum class ETalentAtlasSource : uint8 { None, Talent, Legacy, Extra };
    struct FTalentIconSelection
    {
        ETalentAtlasSource Source = ETalentAtlasSource::None;
        int32 Index = INDEX_NONE;
    };

    inline FTalentIconSelection ResolveTalentIcon(int32 UpgradeIndex, bool bHasTalent, bool bHasLegacy)
    {
        const int32 Icon = TalentIconIndex(UpgradeIndex);
        if (Icon == INDEX_NONE) return {};
        if (bHasTalent) return { ETalentAtlasSource::Talent, Icon };
        if (bHasLegacy) return { ETalentAtlasSource::Legacy, FMath::Clamp(Upgrade(UpgradeIndex).Column, 0, 4) };
        return {};
    }

    inline FTalentIconSelection ResolveTalentIcon(int32 UpgradeIndex, bool bHasExtra, bool bHasTalent, bool bHasLegacy)
    {
        // This atlas is additive: stable upgrade IDs select individual art,
        // and any missing texture falls back to the original atlas and HUD art.
        int32 ExtraIndex = INDEX_NONE;
        switch (UpgradeIndex)
        {
            case 29: ExtraIndex = 0; break;   // Final radar
            case 35: ExtraIndex = 1; break;   // Final collector
            case 41: ExtraIndex = 2; break;   // Horizon lens
            case 47: ExtraIndex = 3; break;   // Final cooling
            case 54: ExtraIndex = 4; break;   // Final engine
            case 65: ExtraIndex = 5; break;   // Spectral antenna
            case 70: ExtraIndex = 6; break;   // Deep processing
            case 71: ExtraIndex = 7; break;   // Clean cut
            case 66: ExtraIndex = 8; break;   // Ore map
            case 56: ExtraIndex = 9; break;   // Quantum sensor
            case 60: ExtraIndex = 10; break;  // Quantum pulse
            case 30: ExtraIndex = 11; break;  // Spectral collection
            case 62: ExtraIndex = 12; break;  // Cryo loop III
            case 79: ExtraIndex = 13; break;  // Stabilizer III
            case 31: ExtraIndex = 14; break;  // Collector VI
            case 33: ExtraIndex = 15; break;  // Deep analysis
            default: break;
        }
        if (bHasExtra && ExtraIndex != INDEX_NONE) return { ETalentAtlasSource::Extra, ExtraIndex };
        return ResolveTalentIcon(UpgradeIndex, bHasTalent, bHasLegacy);
    }

    inline FTalentIconSelection ResolveShipIcon(bool bHasTalent, bool bHasLegacy)
    {
        if (bHasTalent) return { ETalentAtlasSource::Talent, 15 };
        if (bHasLegacy) return { ETalentAtlasSource::Legacy, 5 };
        return {};
    }
}
