#pragma once

#include "CoreMinimal.h"
#include "SpaceFlightLocalization.h"
#include "SpaceFlightExpedition.h"
#include "SpaceFlightSectors.h"

namespace SpaceFlight
{
    inline FString ExpeditionSectorHint(const FExpedition& Expedition, int32 CurrentSector, bool bStressMode)
    {
        if (!bStressMode && Expedition.IsValid() && Expedition.State == EExpeditionState::Active &&
            Expedition.Route > 0 && CurrentSector >= 0 && CurrentSector < SectorCount && Expedition.Route != CurrentSector)
        {
            return Expedition.Route == 1 ? SpaceFlight::Localize(TEXT("РЕЙС: НУЖНА СИНЯЯ РОССЫПЬ")) : SpaceFlight::Localize(TEXT("РЕЙС: НУЖЕН АЛМАЗНЫЙ ПОТОК"));
        }
        return FString();
    }

    inline const TCHAR* ExpeditionActionWord(EExpeditionState State, bool bStressMode = false)
    {
        return State == EExpeditionState::Ready && !bStressMode ? SpaceFlight::Localize(TEXT("НАГРАДА")) : SpaceFlight::Localize(TEXT("РЕЙС"));
    }

    inline bool CanChangeSectorNow(bool bPaused, bool bStressMode, float Now, float ReadyAt)
    {
        return !bPaused && !bStressMode && Now >= ReadyAt;
    }

    inline FString SectorStatusLabel(bool bAutopilot, int32 CurrentSector, int32 CompletedObjectives, bool bCanChangeSector = true)
    {
        const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("РУБЕЖ")), SpaceFlight::Localize(TEXT("СИНЯЯ РОССЫПЬ")), SpaceFlight::Localize(TEXT("АЛМАЗНЫЙ ПОТОК")) };
        return FString::Printf(TEXT("%s · %s%s"), bAutopilot ? SpaceFlight::Localize(TEXT("АВТО")) : SpaceFlight::Localize(TEXT("РУЧНОЙ")),
            bCanChangeSector && UnlockedSector(CompletedObjectives) > 0 ? SpaceFlight::Localize(TEXT("C — СЕКТОР: ")) : TEXT(""),
            Names[FMath::Clamp(CurrentSector, 0, SectorCount - 1)]);
    }

    inline const TCHAR* PauseActionLabel(bool bRefiningSelection, bool bProductionSelection, bool bSpecializationSelection)
    {
        return bRefiningSelection || bProductionSelection || bSpecializationSelection ?
            SpaceFlight::Localize(TEXT("ПАУЗА  ·  ESC — ОТМЕНА")) : SpaceFlight::Localize(TEXT("ПАУЗА  ·  ESC — ПРОДОЛЖИТЬ"));
    }

    inline const TCHAR* EscapeActionShort(bool bPaused, bool bRefiningSelection, bool bProductionSelection, bool bSpecializationSelection)
    {
        if (bRefiningSelection || bProductionSelection || bSpecializationSelection) return SpaceFlight::Localize(TEXT("ОТМЕНА"));
        return bPaused ? SpaceFlight::Localize(TEXT("ПРОДОЛЖИТЬ")) : SpaceFlight::Localize(TEXT("ПАУЗА"));
    }
}
