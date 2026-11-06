#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Misc/Crc.h"
#include "SpaceFlightLogic.h"
#include "SpaceFlightEvents.h"
#include "SpaceFlightSave.generated.h"

namespace SpaceFlight
{
    constexpr int32 CurrentSaveVersion = 7;
    // This project has always used versioned Unreal saves, whose magic is
    // GVAS. Reject obviously truncated or unrelated files before the engine
    // attempts to deserialize their contents.
    inline bool HasUsableSaveHeader(TConstArrayView<uint8> Data)
    {
        return Data.Num() >= 64 && Data[0] == 0x47 && Data[1] == 0x56 && Data[2] == 0x41 && Data[3] == 0x53;
    }

    enum class ESaveEnvelope : uint8 { Legacy, Verified, Corrupt, Unsupported, Unreadable };
    enum class ESaveSlotPresence : uint8 { Missing, Present, Unreadable };
    inline int32 NextSaveSlot(int32 ActiveSlot) { return ActiveSlot == 0 ? 1 : 0; }
    constexpr int32 SaveEnvelopeHeaderSize = 16;
    constexpr int32 MaxSavePayloadBytes = 64 * 1024 * 1024;
    inline void AppendSaveUint32(TArray<uint8>& Data, uint32 Value)
    {
        for (int32 Shift = 0; Shift < 32; Shift += 8) Data.Add(static_cast<uint8>(Value >> Shift));
    }
    inline uint32 ReadSaveUint32(TConstArrayView<uint8> Data, int32 Offset)
    {
        return static_cast<uint32>(Data[Offset]) | (static_cast<uint32>(Data[Offset + 1]) << 8) |
            (static_cast<uint32>(Data[Offset + 2]) << 16) | (static_cast<uint32>(Data[Offset + 3]) << 24);
    }
    inline bool SealSaveData(TConstArrayView<uint8> Payload, TArray<uint8>& OutData)
    {
        OutData.Reset();
        if (!HasUsableSaveHeader(Payload) || Payload.Num() > MaxSavePayloadBytes) return false;
        OutData.Reserve(SaveEnvelopeHeaderSize + Payload.Num());
        OutData.Append({ 'S', 'F', 'S', 'V' });
        AppendSaveUint32(OutData, 1);
        AppendSaveUint32(OutData, static_cast<uint32>(Payload.Num()));
        AppendSaveUint32(OutData, FCrc::MemCrc32(Payload.GetData(), Payload.Num()));
        OutData.Append(Payload.GetData(), Payload.Num());
        return true;
    }
    inline ESaveEnvelope UnwrapSaveData(TConstArrayView<uint8> Data, TArray<uint8>& OutPayload)
    {
        OutPayload.Reset();
        if (HasUsableSaveHeader(Data))
        {
            if (Data.Num() > MaxSavePayloadBytes) return ESaveEnvelope::Corrupt;
            OutPayload.Append(Data.GetData(), Data.Num());
            return ESaveEnvelope::Legacy;
        }
        if (Data.Num() < 8 || Data[0] != 'S' || Data[1] != 'F' || Data[2] != 'S' || Data[3] != 'V') return ESaveEnvelope::Corrupt;
        if (ReadSaveUint32(Data, 4) != 1) return ESaveEnvelope::Unsupported;
        if (Data.Num() < SaveEnvelopeHeaderSize) return ESaveEnvelope::Corrupt;
        const uint32 Size = ReadSaveUint32(Data, 8);
        if (Size < 64 || Size > MaxSavePayloadBytes || static_cast<int64>(Size) != Data.Num() - SaveEnvelopeHeaderSize) return ESaveEnvelope::Corrupt;
        const uint8* Payload = Data.GetData() + SaveEnvelopeHeaderSize;
        if (FCrc::MemCrc32(Payload, Size) != ReadSaveUint32(Data, 12) || !HasUsableSaveHeader(MakeArrayView(Payload, static_cast<int32>(Size)))) return ESaveEnvelope::Corrupt;
        OutPayload.Append(Payload, Size);
        return ESaveEnvelope::Verified;
    }
    inline bool CanEnterStressMode(bool bSaveEnabled, bool bSaveBlocked, bool bSaveWriteFailed)
    {
        return !bSaveEnabled || (!bSaveBlocked && !bSaveWriteFailed);
    }

    inline bool CanResetProgress(bool bPaused, bool bStressMode, bool bSaveBlocked)
    {
        return bPaused && !bStressMode && !bSaveBlocked;
    }
}

UCLASS()
class SPACEFLIGHT_API USpaceFlightSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 1;
    UPROPERTY() int64 Revision = 0;
    // Version 3 requires an explicit 0/1; -1 detects a missing property.
    UPROPERTY() int32 AutoPurchasePolicy = -1;
    // Version 4 requires an explicit target; -2 detects a missing property,
    // while INDEX_NONE (-1) means no tracked upgrade.
    UPROPERTY() int32 TrackedUpgradeIndex = -2;
    // Version 5 requires both fields; negative defaults detect missing properties.
    UPROPERTY() int32 SurpriseDelivered = -1;
    UPROPERTY() float SurpriseNotBefore = -1.0f;
    // V6 requires these fields; negative sentinels distinguish missing
    // properties from a valid zero deadline and an idle expedition.
    UPROPERTY() float SectorReadyAt = -1.0f;
    UPROPERTY() int32 ExpeditionRoute = -2;
    UPROPERTY() int32 ExpeditionTarget = -1;
    UPROPERTY() int32 ExpeditionProgress = -1;
    UPROPERTY() int32 Resources = 0;
    UPROPERTY() int32 TotalCollected = 0;
    UPROPERTY() int32 TotalSpent = 0;
    UPROPERTY() int32 BlueOre = 0;
    UPROPERTY() int32 BlackOre = 0;
    UPROPERTY() int32 Isotope = 0;
    UPROPERTY() int32 OreConverted = 0;
    UPROPERTY() int32 IsotopeSpent = 0;
    // Version 2 retains the original fields for legacy deserialization.
    // Negative defaults detect missing mandatory v2 fields.
    UPROPERTY() int64 Resources64 = -1;
    UPROPERTY() int64 TotalCollected64 = -1;
    UPROPERTY() int64 TotalSpent64 = -1;
    UPROPERTY() int64 BlueOre64 = -1;
    UPROPERTY() int64 BlackOre64 = -1;
    UPROPERTY() int64 Isotope64 = -1;
    UPROPERTY() int64 OreConverted64 = -1;
    UPROPERTY() int64 IsotopeSpent64 = -1;
    UPROPERTY() float ElapsedSeconds = 0.0f;
    UPROPERTY() int32 StarBrightness = 20;
    UPROPERTY() bool bAutopilot = true;
    UPROPERTY() TArray<int32> AcquiredIndices;
    // Zero is the pre-objective save format; existing saves start at the first goal.
    UPROPERTY() int32 ObjectiveSchema = 0;
    UPROPERTY() int32 ObjectiveSequence = 0;
    UPROPERTY() int32 ObjectiveKind = 0;
    UPROPERTY() int32 ObjectiveProgress = 0;
    UPROPERTY() int32 SectorSchema = 0;
    UPROPERTY() int32 SectorIndex = 0;
    UPROPERTY() int32 WorldSeed = 0;
    UPROPERTY() bool bHasWorldSeed = false;
    UPROPERTY() int32 RefiningSchema = 0;
    UPROPERTY() int32 RefiningMode = 0;
    UPROPERTY() int64 BlueRefiningQuota = 0;
    UPROPERTY() int64 BlackRefiningQuota = 0;
    UPROPERTY() int32 ProductionSchema = 0;
    UPROPERTY() float ProductionReadyAt = 0.0f;
    UPROPERTY() int32 SpecializationSchema = 0;
    UPROPERTY() int32 Specialization = 0;
    UPROPERTY() float SpecializationReadyAt = 0.0f;
    // Optional in V6 and earlier saves; schema zero means nearest targets.
    UPROPERTY() int32 TargetingSchema = 0;
    UPROPERTY() int32 TargetingMode = 0;
    // Absent in older saves: compact HUD is the default.
    UPROPERTY() bool bDetailedHUD = false;
    // Zero marks saves written before the discovery journal existed.
    UPROPERTY() int32 JournalSchema = 0;
    UPROPERTY() int32 JournalFlags = 0;
    // Zero means the save predates persistent encounters.
    UPROPERTY() int32 EventSchema = 0;
    UPROPERTY() int32 EventPhase = 0;
    UPROPERTY() int32 EventKind = 0;
    UPROPERTY() double EventRemaining = 0.0;
    UPROPERTY() int32 EventAnchor = 0;
    UPROPERTY() int32 EventClaimed = 0;
    UPROPERTY() int32 EventClaims = 0;
    UPROPERTY() int32 EventNumber = 0;
    UPROPERTY() int32 EventRandomSeed = 0;

    void Capture(const SpaceFlight::FProgress& Progress, float Time, int32 Brightness, bool bAuto, int64 NewRevision);
    void CaptureEvent(const SpaceFlight::FMineralEvent& Event);
    bool RestoreEvent(SpaceFlight::FMineralEvent& OutEvent) const;
    bool Restore(SpaceFlight::FProgress& OutProgress, float& OutTime, int32& OutBrightness, bool& bOutAuto) const;
    static const USpaceFlightSaveGame* NewestValid(const USpaceFlightSaveGame* A, const USpaceFlightSaveGame* B);
    static SpaceFlight::ESaveSlotPresence InspectSlotPresence(const FString& SlotName);
    static USpaceFlightSaveGame* LoadWithHeaderCheck(const FString& SlotName, SpaceFlight::ESaveEnvelope* OutEnvelope = nullptr);
    static bool WriteVerifiedSlot(const FString& SlotName, const FString& StageName, const TArray<uint8>& SealedData, bool bSimulateReadFailure = false, bool bSimulateMoveFailure = false);
};
