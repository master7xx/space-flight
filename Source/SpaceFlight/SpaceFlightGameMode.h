#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMutex.h"
#include "SpaceFlightLogic.h"
#include "SpaceFlightAudio.h"
#include "SpaceFlightTreeInput.h"
#include "SpaceFlightShield.h"
#include "SpaceFlightEvents.h"
#include "SpaceFlightGameMode.generated.h"

class UTexture2D;
class USoundBase;
class UAudioComponent;

struct FSpaceDustBurst
{
    FVector2D Position;
    FLinearColor Color;
    float StartTime;
    int32 Seed;
    int32 Strength;
    int32 BonusOre = 0;
};

struct FSpaceImpact
{
    FVector2D Position;
    FLinearColor Color;
    float StartTime;
    float Radius;
    int32 Seed;
};

struct FSpaceBeam
{
    FVector2D Target;
    int32 WeaponIndex;
    FVector2D ReflectionOrigin = FVector2D::ZeroVector;
    bool bReflected = false;
};

UCLASS()
class SPACEFLIGHT_API ASpaceFlightHUD : public AHUD
{
    GENERATED_BODY()
public:
    ASpaceFlightHUD();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void DrawHUD() override;
    void StartAsteroidStress();
    void CancelAutopilot();
    void ResumeAutopilot();
    void ToggleTree();
    void ToggleJournal();
    void ToggleAutoPurchases();
    void PurchaseUpgrade(int32 Index, bool bAutomatic = false);
    void BeginTreePointer(SpaceFlight::ETreePointerButton Button, FVector2D Point);
    void EndTreePointer(SpaceFlight::ETreePointerButton Button, FVector2D Point);
    void CancelTreePointer();
    void ZoomTree(float Direction, FVector2D Cursor);
    void ResetTreeView();
    void FocusNextAvailableUpgrade();
    void ToggleTrackedUpgrade();
    void OnPauseChanged(bool bPaused);
    void AdjustStarBrightness(int32 Direction);
    void CycleSector();
    void ToggleHUDDetails();
    void CycleLanguage();
    void CycleTargetingMode();
    void SelectRefiningMode();
    void ApplyRefiningMode();
    bool CancelRefiningSelection();
    void SelectProductionRecipe();
    bool ApplyProductionRecipe();
    bool CancelProductionSelection();
    void SelectSpecialization();
    bool ApplySpecialization();
    bool CancelSpecializationSelection();
    void SelectExpedition();
    bool ApplyExpedition();
    bool CancelExpeditionSelection();
    void SaveProgress();
    void RequestProgressReset();
private:
    void LoadProgress();
    int32 ResolveTreeHoveredUpgrade();
    float TotalFlightSeconds() const;
    bool bSaveEnabled = true;
    bool bSaveBlocked = false;
    bool bSaveSessionLocked = false;
    bool bSaveWriteFailed = false;
    bool bDetailedHUD = false;
    FString SaveSlotBase = TEXT("SpaceFlightProgress");
    int64 SaveRevision = 0;
    int32 ActiveSaveSlot = INDEX_NONE;
    TUniquePtr<UE::FPlatformSystemWideMutex> SaveMutex;
    float SavedFlightSeconds = 0.0f;
    float SessionStartTime = 0.0f;
    float NextSaveTime = 0.0f;
    float ResetConfirmUntil = 0.0f;
    bool bRefiningSelection = false;
    int32 SelectedRefiningMode = 0;
    bool bProductionSelection = false;
    SpaceFlight::EOreKind SelectedProductionKind = SpaceFlight::EOreKind::Blue;
    bool bSpecializationSelection = false;
    int32 SelectedSpecialization = 0;
    bool bExpeditionSelection = false;
    int32 SelectedExpeditionRoute = 0;
    FString ExpeditionError;
    FString ExpeditionTitle() const;
    FString ExpeditionDetail() const;
    FString ExpeditionRewardDetail() const;
    FString ExpeditionHint() const;
    void PlayEventSound(SpaceFlight::ESoundEvent Event, bool bInterface = false);
#if WITH_DEV_AUTOMATION_TESTS
    void RunLegacyMigrationFixture();
    void RunFutureSaveCheck(const FString& Phase);
    bool bFutureSaveEndPlayCheck = false;
    void RunExpeditionSaveCheck(const FString& Phase);
    void TickAudioCheck();
    void TickSaveResetCheck();
    void TickStressSaveCheck();
    void TickSectorCheck();
    void TickRefiningCheck();
    void TickProductionCheck();
    void TickSpecializationCheck();
    void TickHUDCheck();
    void TickJournalCheck();
    void TickTrackedInputCheck();
    void TickManualInputCheck();
    void TickExpeditionInputCheck();
    void TickTargetingInputCheck();
    void TickLanguageInputCheck();
    int32 LanguageInputStep = 0;
    uint64 LanguageInputLastFrame = MAX_uint64;
    void FinishSurpriseCheck(bool bSuccess);
    bool bAudioCheck = false;
    bool bAudioCheckRecorded = false;
    double AudioCheckStarted = 0.0;
    int32 AudioCheckStep = 0;
    bool bSaveResetCheck = false;
    int32 SaveResetCheckStep = 0;
    double SaveResetCheckStarted = 0.0;
    bool bStressSaveCheck = false;
    int32 StressSaveCheckStep = 0;
    double StressSaveCheckStarted = 0.0;
    bool bSectorCheck = false;
    int32 SectorCheckStep = 0;
    double SectorCheckStarted = 0.0;
    bool bRefiningCheck = false;
    int32 RefiningCheckStep = 0;
    double RefiningCheckStarted = 0.0;
    bool bProductionCheck = false;
    int32 ProductionCheckStep = 0;
    double ProductionCheckStarted = 0.0;
    bool bSpecializationCheck = false;
    int32 SpecializationCheckStep = 0;
    double SpecializationCheckStarted = 0.0;
    bool bHUDCheck = false;
    int32 HUDCheckStep = 0;
    double HUDCheckStarted = 0.0;
    bool bJournalCheck = false;
    int32 JournalCheckStep = 0;
    double JournalCheckStarted = 0.0;
    bool bTrackedInputCheck = false;
    bool bTrackedInputRunningPassed = false;
    bool bTrackedInputKeyDown = false;
    int32 TrackedInputStep = 0;
    int64 TrackedInputInitialOre = 0;
    uint64 TrackedInputLastFrame = MAX_uint64;
    double TrackedInputStarted = 0.0;
    bool bManualInputCheck = false;
    int32 ManualInputStep = 0;
    int32 ManualInputWaitFrames = 0;
    int32 ManualInputKeyIndex = 0;
    uint64 ManualInputLastFrame = MAX_uint64;
    double ManualInputStarted = 0.0;
    float ManualInputStartX = 0.0f;
    bool bExpeditionInputCheck = false;
    int32 ExpeditionInputStep = 0;
    uint64 ExpeditionInputLastFrame = MAX_uint64;
    double ExpeditionInputStarted = 0.0;
    int64 ExpeditionInputBeforeReward = 0;
    int64 ExpeditionInputBeforeTotal = 0;
    int64 ExpeditionInputBeforeBlack = 0;
    int32 ExpeditionInputReward = 0;
    bool bTargetingInputCheck = false;
    int32 TargetingInputStep = 0;
    uint64 TargetingInputLastFrame = MAX_uint64;
    double TargetingInputStarted = 0.0;
    bool bFreshCheckpointPending = false;
    float FreshCheckpointSeconds = 0.0f;
    bool bFrameProfile = false;
    double FrameProfileStart = 0.0;
    double FrameProfileLast = 0.0;
    TArray<double> FrameProfileSamples;
    TArray<double> AutoProfileSamples;
#endif
    SpaceFlight::FSoundGate SoundGate;
    SpaceFlight::FSoundVariety SoundVariety;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USoundBase>> EventSounds;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USoundBase>> CombatSoundVariants;
    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> EngineAudio;
    SpaceFlight::FStarBrightness StarBrightness;
    float LastLaserTime = -100.0f;
    float NextAutoUpgradeEvaluation = 0.0f;
    TArray<FSpaceBeam> LaserBeams;
    SpaceFlight::FProgress Progress;
    float ShipX = -1.0f;
    float ShipBank = 0.0f;
    float UpgradeFlashStart = -100.0f;
    float ViewportWidth = 0.0f;
    float ViewportHeight = 0.0f;
    bool bAutopilot = true;
    bool bTreeOpen = false;
    bool bJournalOpen = false;
    float TreeZoom = SpaceFlight::DefaultTreeZoom;
    FVector2D TreePan = FVector2D::ZeroVector;
    SpaceFlight::FTreePointerGesture TreePointer;
    int32 TreeNavigationCursor = INDEX_NONE;
    int32 TreeKeyboardFocus = INDEX_NONE;
    bool bTreeKeyboardFocusActive = false;
    bool bTreeFocusHasMousePosition = false;
    FVector2D TreeFocusMousePosition = FVector2D::ZeroVector;
    SpaceFlight::FShieldTimeline Shield;
    SpaceFlight::FMineralEvent MineralEvent;
    bool bHasRestoredEvent = false;
    double PauseStartedAt = 0.0;
    FString UpgradeNotice;
    float NoticeUntil = 0.0f;
    void DrawFlightHUD(float W, float H, float Elapsed, bool bPaused);
    void DrawCompactFlightHUD(float W, float H, float Elapsed, bool bPaused);
    void DrawUpgradeTree(float W, float H);
    void DrawDiscoveryJournal(float W, float H);
    void DrawSurprise(float W, float H);
    void ReplaySurprise();
    bool bSurprisePlaying = false;
    bool bSurpriseReplay = false;
    bool bSurpriseSeenThisSession = false;
    bool bSurpriseCommitPending = false;
    float SurpriseAge = 0.0f;
    double SurpriseLastRealTime = 0.0;
    double SurpriseRetryAt = 0.0;
#if WITH_DEV_AUTOMATION_TESTS
    FString SurpriseCheckPhase;
    double SurpriseCheckStarted = 0.0;
    double SurpriseCheckFinishAt = 0.0;
    bool bSurpriseCheckFinished = false;
    int32 SurpriseCheckStep = 0;
    double SurpriseCheckStepAt = 0.0;
    float SurpriseCheckAge = 0.0f;
    float SurpriseCheckShipX = 0.0f;
    bool bSurpriseCheckManual = false;
#endif
    TArray<float> BaseResetTimes;
    TArray<int32> BaseGenerations;
    TArray<SpaceFlight::FAsteroidDurability> AsteroidDurability;
    uint32 SpawnSeed = 0;
    bool bSpawnSeedInitialized = false;
    TArray<FSpaceDustBurst> DustBursts;
    TArray<FSpaceImpact> Impacts;
    bool bStressMode = false;
    bool bBenchmark = false;
    bool bFpsCheck = false;
    double FpsCheckStart = 0.0;
    int32 FpsCheckFrames = 0;
    bool bFlightTrace = false;
    double FlightTraceStart = 0.0;
    int32 FlightTraceSecond = -1;
    int32 CrystalGlowTraceSecond = -1;
    int32 BlueAsteroidTraceSecond = -1;
    int32 LastLoggedStressCount = -1;
    double StressStartTime = 0.0;
    int32 BenchmarkCheckpoint = 100;
    double BenchmarkWindowStart = 0.0;
    double BenchmarkFrameTotal = 0.0;
    double BenchmarkWorstFrame = 0.0;
    double BenchmarkLastFrameTime = 0.0;
    double BenchmarkRecentFps = 0.0;
    double BenchmarkRecentWorstFrame = 0.0;
    int32 BenchmarkFrames = 0;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> StressRockTextures;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> BlueAsteroidAtlases;
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> CrystalGlowTexture;
    UPROPERTY()
    TObjectPtr<UTexture2D> SkillAtlas;
    UPROPERTY()
    TObjectPtr<UTexture2D> TalentAtlas;
    UPROPERTY()
    TObjectPtr<UTexture2D> ExtraTalentAtlas;
    UPROPERTY()
    TObjectPtr<UTexture2D> HudFrame;
    void CreateStressRockTextures();
    void CreateCrystalGlowTexture();
    void CreateBlueAsteroidAtlases();
};

UCLASS()
class SPACEFLIGHT_API ASpaceFlightPlayerController : public APlayerController
{
    GENERATED_BODY()
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    void QuitFlight();
    void StartStress();
    void InterruptAutopilot();
    void StartAutopilot();
    void TogglePause();
    void ToggleTree();
    void ToggleJournal();
    void ToggleAutoPurchases();
    void TreePointerDown(SpaceFlight::ETreePointerButton Button);
    void TreePointerUp(SpaceFlight::ETreePointerButton Button);
    void TreeLeftDown();
    void TreeLeftUp();
    void TreeMiddleDown();
    void TreeMiddleUp();
    void TreeRightDown();
    void TreeRightUp();
    void ZoomTreeIn();
    void ZoomTreeOut();
    void ResetTreeView();
    void FocusNextAvailableUpgrade();
    void ToggleTrackedUpgrade();
    void ResetProgress();
    void IncreaseStarBrightness();
    void DecreaseStarBrightness();
    void CycleSector();
    void ToggleHUDDetails();
    void CycleLanguage();
    void CycleTargetingMode();
    void SelectRefiningMode();
    void ApplyRefiningMode();
    void SelectProductionRecipe();
    void SelectSpecialization();
    void SelectExpedition();
};

UCLASS()
class SPACEFLIGHT_API ASpaceFlightGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ASpaceFlightGameMode();
    virtual void StartPlay() override;
};
