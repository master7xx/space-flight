#pragma once

#include "CoreMinimal.h"
#include "SpaceFlightLogic.h"

namespace SpaceFlight
{
    enum class ESoundEvent : uint8
    {
        Engine, Launch, Laser, Impact, ExplosionSmall, ExplosionMedium, ExplosionLarge,
        Isotope, Upgrade, Denied, TreeOpen, TreeClose, ModeOn, ModeOff,
        Pause, Resume, UiTick, Stress, Count
    };

    inline const TCHAR* SoundName(ESoundEvent Event)
    {
        static const TCHAR* Names[] = {
            TEXT("engine"), TEXT("launch"), TEXT("laser"), TEXT("impact"), TEXT("explosion_small"),
            TEXT("explosion_medium"), TEXT("explosion_large"), TEXT("isotope"), TEXT("upgrade"),
            TEXT("denied"), TEXT("tree_open"), TEXT("tree_close"), TEXT("mode_on"), TEXT("mode_off"),
            TEXT("pause"), TEXT("resume"), TEXT("ui_tick"), TEXT("stress")
        };
        static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(ESoundEvent::Count), "Every event needs a sound");
        const int32 Index = static_cast<int32>(Event);
        return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("");
    }

    inline FString SoundAssetPath(ESoundEvent Event)
    {
        return FString::Printf(TEXT("/Game/Audio/S_%s.S_%s"), SoundName(Event), SoundName(Event));
    }

    inline int32 CombatSoundSlot(ESoundEvent Event)
    {
        switch (Event)
        {
            case ESoundEvent::Laser: return 0;
            case ESoundEvent::Impact: return 1;
            case ESoundEvent::ExplosionSmall: return 2;
            case ESoundEvent::ExplosionMedium: return 3;
            case ESoundEvent::ExplosionLarge: return 4;
            default: return INDEX_NONE;
        }
    }

    inline FString SoundVariantAssetPath(ESoundEvent Event, int32 Variant)
    {
        if (Variant <= 0) return SoundAssetPath(Event);
        return FString::Printf(TEXT("/Game/Audio/S_%s_%d.S_%s_%d"), SoundName(Event), Variant + 1, SoundName(Event), Variant + 1);
    }

    inline float EventGain(ESoundEvent Event)
    {
        if (Event == ESoundEvent::Laser) return 0.32f;
        if (Event == ESoundEvent::Impact) return 0.38f;
        if (Event == ESoundEvent::ExplosionSmall || Event == ESoundEvent::ExplosionMedium || Event == ESoundEvent::ExplosionLarge) return 0.48f;
        if (Event == ESoundEvent::Isotope) return 0.5f;
        return 1.0f;
    }

    struct FSoundVariety
    {
        int32 Last[5] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
        int32 Next(ESoundEvent Event, int32 RandomValue)
        {
            const int32 Slot = CombatSoundSlot(Event);
            if (Slot == INDEX_NONE) return 0;
            const uint32 Random = static_cast<uint32>(RandomValue);
            const int32 Variant = Last[Slot] == INDEX_NONE ? static_cast<int32>(Random % 3) :
                (Last[Slot] + 1 + static_cast<int32>(Random % 2)) % 3;
            Last[Slot] = Variant;
            return Variant;
        }
    };

    inline ESoundEvent SoundForImpact(bool bDestroyed, int32 Size)
    {
        if (!bDestroyed) return ESoundEvent::Impact;
        const int32 Strength = AsteroidHitPoints(Size);
        return Strength == 1 ? ESoundEvent::ExplosionSmall :
            (Strength == 2 ? ESoundEvent::ExplosionMedium : ESoundEvent::ExplosionLarge);
    }

    struct FSoundGate
    {
        double LastLaser = -1e9;
        double LastImpact = -1e9;
        double LastIsotope = -1e9;
        double LastUiTick = -1e9;
        bool Allow(ESoundEvent Event, double Now)
        {
            double* Last = nullptr;
            double Interval = 0.0;
            if (Event == ESoundEvent::Laser) { Last = &LastLaser; Interval = 0.7; }
            else if (Event == ESoundEvent::Impact || Event == ESoundEvent::ExplosionSmall ||
                Event == ESoundEvent::ExplosionMedium || Event == ESoundEvent::ExplosionLarge)
                { Last = &LastImpact; Interval = 0.65; }
            else if (Event == ESoundEvent::Isotope) { Last = &LastIsotope; Interval = 3.0; }
            else if (Event == ESoundEvent::UiTick) { Last = &LastUiTick; Interval = 0.08; }
            if (!Last) return true;
            if (Now >= *Last && Now - *Last < Interval) return false;
            *Last = Now;
            return true;
        }
    };
}
