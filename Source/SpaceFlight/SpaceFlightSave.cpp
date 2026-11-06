#include "SpaceFlightSave.h"
#include "SpaceFlightSectors.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

USpaceFlightSaveGame* USpaceFlightSaveGame::LoadWithHeaderCheck(const FString& SlotName, SpaceFlight::ESaveEnvelope* OutEnvelope)
{
    if (OutEnvelope) *OutEnvelope = SpaceFlight::ESaveEnvelope::Unreadable;
    TArray<uint8> Data, Payload;
    if (!UGameplayStatics::LoadDataFromSlot(Data, SlotName, 0)) return nullptr;
    const auto Envelope = SpaceFlight::UnwrapSaveData(Data, Payload);
    if (OutEnvelope) *OutEnvelope = Envelope;
    if (Envelope != SpaceFlight::ESaveEnvelope::Legacy && Envelope != SpaceFlight::ESaveEnvelope::Verified) return nullptr;
    USpaceFlightSaveGame* Result = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Payload));
    if (!Result && OutEnvelope) *OutEnvelope = SpaceFlight::ESaveEnvelope::Unsupported;
    return Result;
}

SpaceFlight::ESaveSlotPresence USpaceFlightSaveGame::InspectSlotPresence(const FString& SlotName)
{
#if PLATFORM_WINDOWS
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav")));
    const DWORD Attributes = ::GetFileAttributesW(*Path);
    if (Attributes != INVALID_FILE_ATTRIBUTES)
        return (Attributes & FILE_ATTRIBUTE_DIRECTORY) ? SpaceFlight::ESaveSlotPresence::Unreadable : SpaceFlight::ESaveSlotPresence::Present;
    const DWORD Error = ::GetLastError();
    return (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND) ? SpaceFlight::ESaveSlotPresence::Missing : SpaceFlight::ESaveSlotPresence::Unreadable;
#else
    return UGameplayStatics::DoesSaveGameExist(SlotName, 0) ? SpaceFlight::ESaveSlotPresence::Present : SpaceFlight::ESaveSlotPresence::Missing;
#endif
}

bool USpaceFlightSaveGame::WriteVerifiedSlot(const FString& SlotName, const FString& StageName, const TArray<uint8>& SealedData, bool bSimulateReadFailure, bool bSimulateMoveFailure)
{
    TArray<uint8> ReadBack;
    const bool bVerified = UGameplayStatics::SaveDataToSlot(SealedData, StageName, 0) && !bSimulateReadFailure &&
        UGameplayStatics::LoadDataFromSlot(ReadBack, StageName, 0) && ReadBack == SealedData;
    if (!bVerified)
    {
        UGameplayStatics::DeleteGameInSlot(StageName, 0);
        return false;
    }
    // Desktop Unreal's generic save system stores slots in Saved/SaveGames.
    // The active slot is never the destination; failed promotion leaves it intact.
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames"));
    const FString Target = Directory / (SlotName + TEXT(".sav"));
    const FString Stage = Directory / (StageName + TEXT(".sav"));
    const FString Source = bSimulateMoveFailure ? Stage + TEXT(".missing") : Stage;
#if PLATFORM_WINDOWS
    // Replace in one operation: unlike IFileManager::Move(Replace=true), this
    // does not delete the old backup before attempting the rename.
    const bool bMoved = !!::MoveFileExW(*Source, *Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool bMoved = FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Target, *Source);
#endif
    if (!bMoved) UGameplayStatics::DeleteGameInSlot(StageName, 0);
    return bMoved;
}

void USpaceFlightSaveGame::CaptureEvent(const SpaceFlight::FMineralEvent& Event)
{
    EventSchema = 1;
    EventPhase = static_cast<int32>(Event.Phase);
    EventKind = static_cast<int32>(Event.Kind);
    EventRemaining = Event.Remaining;
    EventAnchor = Event.Anchor;
    EventClaimed = static_cast<int32>(Event.Claimed);
    EventClaims = Event.Claims;
    EventNumber = Event.EventNumber;
    EventRandomSeed = Event.Random.GetCurrentSeed();
}

bool USpaceFlightSaveGame::RestoreEvent(SpaceFlight::FMineralEvent& OutEvent) const
{
    using namespace SpaceFlight;
    if (EventSchema == 0) return true;
    if (EventSchema != 1 || EventPhase < 0 || EventPhase > 2 || EventKind < 0 || EventKind > 2 ||
        !FMath::IsFinite(EventRemaining) || EventRemaining <= 0.0 ||
        EventRemaining > (EventPhase == 0 ? 300.0 : EventPhase == 1 ? 8.0 : 35.0) ||
        EventAnchor < 0 || EventAnchor >= 32 || EventNumber < 0 || EventNumber == MAX_int32 ||
        EventClaims < 0 || EventClaims > (EventKind == 0 ? 4 : 1) ||
        FMath::CountBits(static_cast<uint32>(EventClaimed)) != EventClaims ||
        (EventPhase == 1 && EventClaims != 0)) return false;
    FMineralEvent Candidate;
    Candidate.Phase = static_cast<EMineralPhase>(EventPhase);
    Candidate.Kind = static_cast<ERareEventKind>(EventKind);
    Candidate.Remaining = EventRemaining;
    Candidate.Anchor = EventAnchor;
    Candidate.Claimed = static_cast<uint32>(EventClaimed);
    Candidate.Claims = EventClaims;
    Candidate.EventNumber = EventNumber;
    Candidate.Random.Initialize(EventRandomSeed);
    OutEvent = Candidate;
    return true;
}

void USpaceFlightSaveGame::Capture(const SpaceFlight::FProgress& Progress, float Time, int32 Brightness, bool bAuto, int64 NewRevision)
{
    Version = SpaceFlight::CurrentSaveVersion;
    Revision = NewRevision;
    AutoPurchasePolicy = Progress.bAutoPurchasesEnabled ? 1 : 0;
    TrackedUpgradeIndex = Progress.TrackedUpgrade;
    SurpriseDelivered = Progress.Surprise.bDelivered ? 1 : 0;
    SurpriseNotBefore = Progress.Surprise.NotBeforeSeconds;
    SectorReadyAt = Progress.SectorReadyAt;
    // Do not accidentally turn an invalid in-memory state into a valid one
    // when restoring the derived state from its serialized fields.
    ExpeditionRoute = Progress.Expedition.IsValid() ? Progress.Expedition.Route : -2;
    ExpeditionTarget = Progress.Expedition.Target;
    ExpeditionProgress = Progress.Expedition.Progress;
    Resources64 = Progress.Resources;
    TotalCollected64 = Progress.TotalCollected;
    TotalSpent64 = Progress.TotalSpent;
    BlueOre64 = Progress.BlueOre;
    BlackOre64 = Progress.BlackOre;
    Isotope64 = Progress.Isotope;
    OreConverted64 = Progress.OreConverted;
    IsotopeSpent64 = Progress.IsotopeSpent;
    ElapsedSeconds = Time;
    StarBrightness = Brightness;
    bAutopilot = bAuto;
    JournalSchema = 1;
    JournalFlags = static_cast<int32>(Progress.DiscoveryBits);
    ObjectiveSchema = 2;
    ObjectiveSequence = Progress.Objective.Sequence;
    ObjectiveKind = Progress.Objective.Kind;
    ObjectiveProgress = Progress.Objective.Progress;
    SectorSchema = 1;
    SectorIndex = Progress.CurrentSector;
    WorldSeed = Progress.WorldSeed;
    bHasWorldSeed = Progress.bHasWorldSeed;
    RefiningSchema = 1;
    RefiningMode = Progress.RefiningMode;
    BlueRefiningQuota = Progress.BlueRefiningQuota;
    BlackRefiningQuota = Progress.BlackRefiningQuota;
    ProductionSchema = 1;
    ProductionReadyAt = Progress.ProductionReadyAt;
    SpecializationSchema = 1;
    Specialization = Progress.Specialization;
    SpecializationReadyAt = Progress.SpecializationReadyAt;
    TargetingSchema = 1;
    TargetingMode = Progress.TargetingMode;
    AcquiredIndices.Reset();
    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
        if (Progress.Has(I)) AcquiredIndices.Add(I);
}

bool USpaceFlightSaveGame::Restore(SpaceFlight::FProgress& OutProgress, float& OutTime, int32& OutBrightness, bool& bOutAuto) const
{
    using namespace SpaceFlight;
    FMineralEvent EventCandidate;
    if (!RestoreEvent(EventCandidate)) return false;
    if ((Version < 1 || Version > CurrentSaveVersion) || Revision < 0 ||
        (Version >= 3 && AutoPurchasePolicy != 0 && AutoPurchasePolicy != 1) ||
        (Version >= 4 && (TrackedUpgradeIndex < INDEX_NONE || TrackedUpgradeIndex >= UpgradeCount ||
            (Version < 7 && TrackedUpgradeIndex >= RicochetUpgrade))) ||
        (Version >= 5 && ((SurpriseDelivered != 0 && SurpriseDelivered != 1) ||
            !FMath::IsFinite(SurpriseNotBefore) || SurpriseNotBefore < SurpriseMinimumFlightSeconds)) ||
        (Version >= 6 && (!FMath::IsFinite(SectorReadyAt) || SectorReadyAt < 0.0f ||
            SectorReadyAt > ElapsedSeconds + 12.0f + 0.01f))) return false;
    const int64 ReadResources = Version == 1 ? Resources : Resources64;
    const int64 ReadCollected = Version == 1 ? TotalCollected : TotalCollected64;
    const int64 ReadSpent = Version == 1 ? TotalSpent : TotalSpent64;
    const int64 ReadBlue = Version == 1 ? BlueOre : BlueOre64;
    const int64 ReadBlack = Version == 1 ? BlackOre : BlackOre64;
    const int64 ReadIsotope = Version == 1 ? Isotope : Isotope64;
    const int64 ReadConverted = Version == 1 ? OreConverted : OreConverted64;
    const int64 ReadIsotopeSpent = Version == 1 ? IsotopeSpent : IsotopeSpent64;
    const auto IsResourceValid = [](int64 Value) { return Value >= 0 && Value <= ResourceAccountingLimit; };
    if (!IsResourceValid(ReadResources) || !IsResourceValid(ReadCollected) || !IsResourceValid(ReadSpent) ||
        !IsResourceValid(ReadBlue) || !IsResourceValid(ReadBlack) || !IsResourceValid(ReadIsotope) ||
        !IsResourceValid(ReadConverted) || !IsResourceValid(ReadIsotopeSpent) ||
        !FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 0 || StarBrightness < 0 || StarBrightness > 200 || StarBrightness % 10 != 0 ||
        ReadBlue > ReadResources || ReadBlack > ReadResources - ReadBlue || ReadConverted % 2 != 0 ||
        ReadSpent > ReadCollected || ReadConverted > ReadCollected - ReadSpent ||
        ReadResources != ReadCollected - ReadSpent - ReadConverted ||
        ReadIsotope > ReadConverted / 2 || ReadIsotopeSpent != ReadConverted / 2 - ReadIsotope || AcquiredIndices.Num() > UpgradeCount ||
        ObjectiveSchema < 0 || ObjectiveSchema > 2 || SectorSchema < 0 || SectorSchema > 1 ||
        RefiningSchema < 0 || RefiningSchema > 1 || ProductionSchema < 0 || ProductionSchema > 1 ||
        SpecializationSchema < 0 || SpecializationSchema > 1 || TargetingSchema < 0 || TargetingSchema > 1 ||
        (TargetingSchema == 1 && (TargetingMode < 0 || TargetingMode > 3)) || JournalSchema < 0 || JournalSchema > 1 ||
        (JournalSchema == 1 && (JournalFlags < 0 || (static_cast<uint32>(JournalFlags) & ~KnownDiscoveryBits) != 0)))
        return false;

    if (Version >= 5)
    {
        // A v5 threshold is either the original threshold or a legacy
        // migration delay. Use the same float arithmetic as migration so
        // very large finite flight times round consistently.
        const float LatestNotBefore = FMath::Max(SurpriseMinimumFlightSeconds, ElapsedSeconds + SurpriseLegacyDelaySeconds);
        if (!FMath::IsFinite(LatestNotBefore) || static_cast<double>(SurpriseNotBefore) > static_cast<double>(LatestNotBefore) + 0.01)
            return false;
    }

    FProgress Candidate;
    if (TargetingSchema == 1) Candidate.TargetingMode = TargetingMode;
    if (Version >= 5)
    {
        Candidate.Surprise.bDelivered = SurpriseDelivered == 1;
        Candidate.Surprise.NotBeforeSeconds = SurpriseNotBefore;
    }
    else Candidate.Surprise = MigrateSurpriseProgress(ElapsedSeconds);
    Candidate.bAutoPurchasesEnabled = Version < 3 || AutoPurchasePolicy == 1;
    Candidate.Resources = ReadResources;
    Candidate.TotalCollected = ReadCollected;
    Candidate.TotalSpent = ReadSpent;
    Candidate.BlueOre = ReadBlue;
    Candidate.BlackOre = ReadBlack;
    Candidate.Isotope = ReadIsotope;
    Candidate.OreConverted = ReadConverted;
    Candidate.IsotopeSpent = ReadIsotopeSpent;
    if (JournalSchema == 1) Candidate.DiscoveryBits = static_cast<uint32>(JournalFlags);
    else
    {
        // Legacy saves contain no encounter history. Only infer discoveries
        // directly proven by an extant balance or by actual isotope conversion.
        if (ReadBlue > 0 || ReadConverted > 0) Candidate.RecordDiscovery(EDiscovery::BlueOre);
        if (ReadBlack > 0 || ReadConverted > 0) Candidate.RecordDiscovery(EDiscovery::BlackOre);
        if (ReadConverted > 0) Candidate.RecordDiscovery(EDiscovery::Isotope);
    }
    if (SpecializationSchema == 1)
    {
        if (Specialization < 0 || Specialization > 3 || !FMath::IsFinite(SpecializationReadyAt) ||
            SpecializationReadyAt < 0.0f || SpecializationReadyAt > ElapsedSeconds + SpecializationCooldownSeconds + 0.01f) return false;
        Candidate.Specialization = Specialization;
        Candidate.SpecializationReadyAt = SpecializationReadyAt;
    }
    if (ProductionSchema == 1)
    {
        if (!FMath::IsFinite(ProductionReadyAt) || ProductionReadyAt < 0.0f ||
            ProductionReadyAt > ElapsedSeconds + ProductionCooldownSeconds + 0.01f) return false;
        Candidate.ProductionReadyAt = ProductionReadyAt;
    }
    if (RefiningSchema == 1)
    {
        if (RefiningMode < 0 || RefiningMode > 5 || BlueRefiningQuota < 0 || BlackRefiningQuota < 0 ||
            BlueRefiningQuota > ReadBlue * 100 || BlackRefiningQuota > ReadBlack * 100 ||
            (RefiningMode < 2 && (BlueRefiningQuota != 0 || BlackRefiningQuota != 0))) return false;
        Candidate.RefiningMode = RefiningMode;
        Candidate.BlueRefiningQuota = BlueRefiningQuota;
        Candidate.BlackRefiningQuota = BlackRefiningQuota;
    }
    if (ObjectiveSchema == 1 || ObjectiveSchema == 2)
    {
        if (ObjectiveSchema == 1 && ObjectiveKind > 4) return false;
        Candidate.Objective.Sequence = ObjectiveSequence;
        Candidate.Objective.Kind = ObjectiveKind;
        Candidate.Objective.Progress = ObjectiveProgress;
        if (!Candidate.Objective.IsValid()) return false;
    }
    if (SectorSchema == 1)
    {
        if (SectorIndex < 0 || SectorIndex >= SectorCount || SectorIndex > UnlockedSector(Candidate.Objective.Sequence)) return false;
        Candidate.CurrentSector = SectorIndex;
        Candidate.WorldSeed = WorldSeed;
        Candidate.bHasWorldSeed = bHasWorldSeed;
    }
    if (Version >= 6)
    {
        Candidate.SectorReadyAt = SectorReadyAt;
        Candidate.Expedition.Route = ExpeditionRoute;
        Candidate.Expedition.Target = ExpeditionTarget;
        Candidate.Expedition.Progress = ExpeditionProgress;
        Candidate.Expedition.State = ExpeditionRoute == INDEX_NONE ? EExpeditionState::Idle :
            (ExpeditionProgress == ExpeditionTarget ? EExpeditionState::Ready : EExpeditionState::Active);
        if (!Candidate.Expedition.IsValidForUnlockedSector(UnlockedSector(Candidate.Objective.Sequence))) return false;
    }
    for (const int32 Index : AcquiredIndices)
    {
        if (Index < 0 || Index >= UpgradeCount || (Version < 7 && Index >= RicochetUpgrade) || Candidate.Has(Index)) return false;
        Candidate.Acquired[Index] = true;
    }
    for (const int32 Index : AcquiredIndices)
    {
        const int32 Parent = Upgrade(Index).Parent;
        if (Parent >= 0 && !Candidate.Has(Parent)) return false;
    }
    if (Version >= 4)
    {
        if (TrackedUpgradeIndex != INDEX_NONE && Candidate.Has(TrackedUpgradeIndex)) return false;
        Candidate.TrackedUpgrade = TrackedUpgradeIndex;
    }
    OutProgress = MoveTemp(Candidate);
    OutTime = ElapsedSeconds;
    OutBrightness = StarBrightness;
    bOutAuto = bAutopilot;
    return true;
}

const USpaceFlightSaveGame* USpaceFlightSaveGame::NewestValid(const USpaceFlightSaveGame* A, const USpaceFlightSaveGame* B)
{
    SpaceFlight::FProgress Temp;
    float Time = 0;
    int32 Brightness = 0;
    bool bAuto = false;
    const bool bAValid = A && A->Restore(Temp, Time, Brightness, bAuto);
    const bool bBValid = B && B->Restore(Temp, Time, Brightness, bAuto);
    if (bAValid && bBValid) return A->Revision >= B->Revision ? A : B;
    return bAValid ? A : (bBValid ? B : nullptr);
}
