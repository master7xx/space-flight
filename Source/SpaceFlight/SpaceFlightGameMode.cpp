#include "SpaceFlightGameMode.h"
#include "SpaceFlightLogic.h"
#include "SpaceFlightSave.h"
#include "SpaceFlightSectors.h"
#include "SpaceFlightHudLayout.h"
#include "SpaceFlightHudActions.h"
#include "SpaceFlightUpgradeNavigation.h"
#include "SpaceFlightTrackedUpgrade.h"
#include "SpaceFlightTalentArt.h"
#include "BlueAsteroidArt.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "Components/AudioComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundBase.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AudioMixerBlueprintLibrary.h"
#include "CoreGlobals.h"
#include "InputKeyEventArgs.h"
#include "Misc/DefaultValueHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#endif

namespace
{
    const TCHAR* TargetingModeName(int32 Mode)
    {
        const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("БЛИЖАЙШИЕ")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА")), SpaceFlight::Localize(TEXT("СОБЫТИЯ")) };
        return Mode >= 0 && Mode < UE_ARRAY_COUNT(Names) ? Names[Mode] : Names[0];
    }
    const TCHAR* SpecializationName(int32 Mode)
    {
        const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("НЕТ")), SpaceFlight::Localize(TEXT("РАЗВЕДКА")), SpaceFlight::Localize(TEXT("ТОЧНАЯ ДОБЫЧА")), SpaceFlight::Localize(TEXT("ЛОГИСТИКА")) };
        return Mode >= 0 && Mode < UE_ARRAY_COUNT(Names) ? Names[Mode] : TEXT("?");
    }
    const TCHAR* SpecializationDescription(int32 Mode)
    {
        const TCHAR* Descriptions[] = { SpaceFlight::Localize(TEXT("БОНУС НЕ ВЫБРАН")), SpaceFlight::Localize(TEXT("БОГАТАЯ ЗАЛЕЖЬ: +5 П.П. К ШАНСУ")),
            SpaceFlight::Localize(TEXT("+1 РУДА ПРИ УНИЧТОЖЕНИИ ПЕРВЫМ ВЫСТРЕЛОМ")), SpaceFlight::Localize(TEXT("ФАБРИКАТОР: 2 РУДЫ  •  ЦЕЛИ: +1 РУДА")) };
        return Mode >= 0 && Mode < UE_ARRAY_COUNT(Descriptions) ? Descriptions[Mode] : TEXT("");
    }
    const TCHAR* RefiningModeName(int32 Mode)
    {
        const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("АВТО")), SpaceFlight::Localize(TEXT("ВЫКЛ.")), TEXT("25%"), TEXT("50%"), TEXT("75%"), TEXT("100%") };
        return Mode >= 0 && Mode < UE_ARRAY_COUNT(Names) ? Names[Mode] : TEXT("?");
    }
    FString RefiningModeDescription(int32 Mode)
    {
        if (Mode == 0) return SpaceFlight::Localize(TEXT("РЕЗЕРВ: ДОСТУПНЫЕ И ПЕРВЫЕ МИКРОУЛУЧШЕНИЯ"));
        if (Mode == 1) return SpaceFlight::Localize(TEXT("СИНТЕЗ ОСТАНОВИТСЯ, ИЗОТОП СОХРАНИТСЯ"));
        return SF_LOCAL_PRINTF(TEXT("%s КАЖДОГО ЦВЕТА · ПО ПОТРЕБНОСТИ"), TEXT("%s EACH COLOR AS REQUIRED"), TEXT("%s CADA COLOR SEGÚN SE REQUIERA"), RefiningModeName(Mode));
    }
    const TCHAR* RefiningApplyHint(int32 Current, int32 Selected)
    {
        if (Current == Selected) return SpaceFlight::Localize(TEXT("ЭТОТ РЕЖИМ УЖЕ ПРИМЕНЁН"));
        if (Selected == 1) return SpaceFlight::Localize(TEXT("ВЫКЛЮЧЕНИЕ НЕ РАСХОДУЕТ РУДУ"));
        return SpaceFlight::Localize(TEXT("ПРИМЕНЕНИЕ МОЖЕТ СРАЗУ РАСХОДОВАТЬ РУДУ"));
    }
    FString UpgradeStatusText(const SpaceFlight::FProgress& Progress, int32 Index)
    {
        const auto& Node = SpaceFlight::Upgrade(Index);
        if (Progress.Has(Index)) return SpaceFlight::Localize(TEXT("ИЗУЧЕНО"));
        if (Node.Parent >= 0 && !Progress.Has(Node.Parent))
            return SF_LOCAL_PRINTF(TEXT("НУЖНО: %s"), TEXT("REQUIRES: %s"), TEXT("REQUIERE: %s"), SpaceFlight::Upgrade(Node.Parent).Name);
        if (Progress.CanPurchase(Index)) return SpaceFlight::Localize(TEXT("МОЖНО ОТКРЫТЬ"));
        const int64 Balance = Node.bMicro ? Progress.Isotope : Progress.Resources;
        const int64 Missing = FMath::Max<int64>(0, static_cast<int64>(Node.Cost) - Balance);
        const TCHAR* Currency = Node.bMicro ? SpaceFlight::Localize(TEXT("ИЗОТОПА")) : SpaceFlight::Localize(TEXT("РУДЫ"));
        FString Status = SF_LOCAL_PRINTF(TEXT("НЕ ХВАТАЕТ: %lld %s"), TEXT("NEED: %lld %s"), TEXT("FALTAN: %lld %s"), Missing, Currency);
        if (Node.bMicro && Progress.RefiningMode == 1) Status += SpaceFlight::Localize(TEXT("  ·  ПЕРЕРАБОТКА ВЫКЛ."));
        return Status;
    }
    FString TrackedUpgradeDetail(const SpaceFlight::FProgress& Progress, const SpaceFlight::FTrackedUpgradeInfo& Info)
    {
        const TCHAR* Currency = Info.Currency == SpaceFlight::ETrackedUpgradeCurrency::Isotope ? SpaceFlight::Localize(TEXT("ИЗОТОПА")) : SpaceFlight::Localize(TEXT("РУДЫ"));
        const FString Balance = FString::Printf(TEXT("%lld / %lld %s"), Info.Balance, Info.Cost, Currency);
        if (Info.Status == SpaceFlight::ETrackedUpgradeStatus::Prerequisite)
            return SF_LOCAL_PRINTF(TEXT("%s  ·  НУЖНО: %s"), TEXT("%s NEEDED: %s"), TEXT("%s NECESARIO: %s"), *Balance, SpaceFlight::Upgrade(Info.ParentIndex).Name);
        if (Info.Status == SpaceFlight::ETrackedUpgradeStatus::Ready) return Balance + SpaceFlight::Localize(TEXT("  ·  МОЖНО ОТКРЫТЬ"));
        if (Info.Currency == SpaceFlight::ETrackedUpgradeCurrency::Isotope && Progress.RefiningMode == 1)
            return Balance + SpaceFlight::Localize(TEXT("  ·  ПЕРЕРАБОТКА ВЫКЛ."));
        return Balance;
    }
    const FLinearColor BlackRockDust(0.08f, 0.12f, 0.17f);
    const FLinearColor BlueRockDust(0.08f, 0.27f, 0.42f);
    // Sources are normalized to -24 dBFS RMS; this gain targets a comfortable
    // session mix while retaining headroom for simultaneous effects.
    constexpr float MasterSoundGain = 1.75f;
    constexpr float EngineSoundGain = 0.16f;

    FLinearColor CrystalRockColor(float X, float Y, float Distance, float Boundary, float Shade,
        float Glow, const FLinearColor& GlowColor)
    {
        // Cool rim lighting preserves a nearly black silhouette against space.
        FLinearColor Color = FLinearColor(0.012f, 0.018f, 0.029f) * (Shade / 0.25f);
        if (Distance > Boundary - 0.13f)
            Color = FLinearColor(0.065f, 0.095f, 0.13f);

        // Two faceted diamonds live in rock-local coordinates, so they rotate with it.
        const float Diamond1 = FMath::Abs(X + 0.24f) + FMath::Abs(Y + 0.15f);
        const float Diamond2 = FMath::Abs(X - 0.32f) + FMath::Abs(Y - 0.28f);
        if (Diamond1 < 0.21f || Diamond2 < 0.15f)
        {
            const float Diamond = FMath::Min(Diamond1, Diamond2);
            Color = (Diamond < 0.075f ? FLinearColor(0.80f, 0.97f, 1.0f) : GlowColor) * (0.62f + Glow * 0.55f);
        }
        Color.A = 1.0f;
        return Color;
    }
}

ASpaceFlightGameMode::ASpaceFlightGameMode()
{
    HUDClass = ASpaceFlightHUD::StaticClass();
    PlayerControllerClass = ASpaceFlightPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}

ASpaceFlightHUD::ASpaceFlightHUD()
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> Atlas(TEXT("/Game/UI/T_SkillAtlas.T_SkillAtlas"));
    if (Atlas.Succeeded()) SkillAtlas = Atlas.Object;
    static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Talent(TEXT("/Game/UI/T_TalentAtlas.T_TalentAtlas"), LOAD_Quiet | LOAD_NoWarn);
    TalentAtlas = Talent.Get();
    static ConstructorHelpers::FObjectFinderOptional<UTexture2D> ExtraTalent(TEXT("/Game/UI/T_TalentAtlasExtra.T_TalentAtlasExtra"), LOAD_Quiet | LOAD_NoWarn);
    ExtraTalentAtlas = ExtraTalent.Get();
    static ConstructorHelpers::FObjectFinder<UTexture2D> Frame(TEXT("/Game/UI/T_HudFrame.T_HudFrame"));
    if (Frame.Succeeded()) HudFrame = Frame.Object;
    EventSounds.SetNum(static_cast<int32>(SpaceFlight::ESoundEvent::Count));
#define LOAD_FLIGHT_SOUND(Event, Name) \
    { static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/Audio/S_" Name ".S_" Name)); \
      EventSounds[static_cast<int32>(SpaceFlight::ESoundEvent::Event)] = Sound.Object; }
    LOAD_FLIGHT_SOUND(Engine, "engine");
    LOAD_FLIGHT_SOUND(Launch, "launch");
    LOAD_FLIGHT_SOUND(Laser, "laser");
    LOAD_FLIGHT_SOUND(Impact, "impact");
    LOAD_FLIGHT_SOUND(ExplosionSmall, "explosion_small");
    LOAD_FLIGHT_SOUND(ExplosionMedium, "explosion_medium");
    LOAD_FLIGHT_SOUND(ExplosionLarge, "explosion_large");
    LOAD_FLIGHT_SOUND(Isotope, "isotope");
    LOAD_FLIGHT_SOUND(Upgrade, "upgrade");
    LOAD_FLIGHT_SOUND(Denied, "denied");
    LOAD_FLIGHT_SOUND(TreeOpen, "tree_open");
    LOAD_FLIGHT_SOUND(TreeClose, "tree_close");
    LOAD_FLIGHT_SOUND(ModeOn, "mode_on");
    LOAD_FLIGHT_SOUND(ModeOff, "mode_off");
    LOAD_FLIGHT_SOUND(Pause, "pause");
    LOAD_FLIGHT_SOUND(Resume, "resume");
    LOAD_FLIGHT_SOUND(UiTick, "ui_tick");
    LOAD_FLIGHT_SOUND(Stress, "stress");
#undef LOAD_FLIGHT_SOUND
    CombatSoundVariants.SetNum(10);
#define LOAD_COMBAT_VARIANT(Event, Name, Variant) \
    { static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/Audio/S_" Name "_" #Variant ".S_" Name "_" #Variant)); \
      CombatSoundVariants[SpaceFlight::CombatSoundSlot(SpaceFlight::ESoundEvent::Event) * 2 + (Variant - 2)] = Sound.Object; }
    LOAD_COMBAT_VARIANT(Laser, "laser", 2);
    LOAD_COMBAT_VARIANT(Laser, "laser", 3);
    LOAD_COMBAT_VARIANT(Impact, "impact", 2);
    LOAD_COMBAT_VARIANT(Impact, "impact", 3);
    LOAD_COMBAT_VARIANT(ExplosionSmall, "explosion_small", 2);
    LOAD_COMBAT_VARIANT(ExplosionSmall, "explosion_small", 3);
    LOAD_COMBAT_VARIANT(ExplosionMedium, "explosion_medium", 2);
    LOAD_COMBAT_VARIANT(ExplosionMedium, "explosion_medium", 3);
    LOAD_COMBAT_VARIANT(ExplosionLarge, "explosion_large", 2);
    LOAD_COMBAT_VARIANT(ExplosionLarge, "explosion_large", 3);
#undef LOAD_COMBAT_VARIANT
}

void ASpaceFlightHUD::BeginPlay()
{
    Super::BeginPlay();
    bSaveEnabled = !FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAudioCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceStressBenchmark")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceShowTree")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceShowJournal")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualInputCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionInputCheck"));
    int32 LanguageIndex = 0;
    FString SavedLanguage;
    if (bSaveEnabled && FFileHelper::LoadFileToString(SavedLanguage, *(FPaths::ProjectSavedDir() / TEXT("SpaceFlightLanguage.txt"))))
    {
        SavedLanguage.TrimStartAndEndInline();
        LanguageIndex = SavedLanguage == TEXT("en") ? 1 : SavedLanguage == TEXT("es") ? 2 : 0;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (!bSaveEnabled) FParse::Value(FCommandLine::Get(), TEXT("SpaceLanguage="), LanguageIndex);
#endif
    SpaceFlight::SetLanguage(static_cast<SpaceFlight::ELanguage>(LanguageIndex));
#if WITH_DEV_AUTOMATION_TESTS
    const bool bTargetingInputRequested = FParse::Param(FCommandLine::Get(), TEXT("SpaceTargetingInputCheck"));
    const bool bTargetingInputAllowed = bTargetingInputRequested && FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave"));
    if (bTargetingInputRequested && !bTargetingInputAllowed)
    {
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("TARGETING_INPUT_CHECK failure=unsafe_configuration"));
    }
    const bool bExpeditionInputRequested = FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionInputCheck"));
    const bool bExpeditionInputAllowed = bExpeditionInputRequested && FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualInputCheck")) && !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck"));
    if (bExpeditionInputRequested && !bExpeditionInputAllowed)
        UE_LOG(LogTemp, Error, TEXT("EXPEDITION_INPUT_CHECK failure=unsafe_configuration"));
    const bool bManualInputRequested = FParse::Param(FCommandLine::Get(), TEXT("SpaceManualInputCheck"));
    const bool bManualInputAllowed = bManualInputRequested && FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck"));
    if (bManualInputRequested && !bManualInputAllowed)
        UE_LOG(LogTemp, Error, TEXT("MANUAL_INPUT_CHECK failure=unsafe_configuration"));
    const bool bTrackedInputRequested = FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedInputCheck"));
    const bool bTrackedInputAllowed = bTrackedInputRequested && !bSaveEnabled;
    if (bTrackedInputRequested && !bTrackedInputAllowed)
    {
        // Refuse the test and disable writes before even attempting to load a
        // real slot when the required isolated launch was omitted.
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("TRACKED_INPUT_CHECK running=0 paused=0 failure=save_enabled"));
    }
#endif
    FString TestSaveId;
    const bool bTestSlotRequested = FParse::Value(FCommandLine::Get(), TEXT("SpaceSaveTestId="), TestSaveId) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveTestId"));
#if WITH_DEV_AUTOMATION_TESTS
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceJournalPreview")))
    {
        Progress.AddOre(100, SpaceFlight::EOreKind::Common);
        Progress.AddOre(60, SpaceFlight::EOreKind::Blue);
        Progress.AddOre(40, SpaceFlight::EOreKind::Black);
        for (int32 Index : { 0, 1, 3, 5, 7, 9, 20, 21 }) Progress.Acquired[Index] = true;
        Progress.RecordDiscovery(SpaceFlight::EDiscovery::MineralFlow);
        Progress.RecordDiscovery(SpaceFlight::EDiscovery::UnstableCrystal);
        Progress.Objective.Sequence = 8;
        Progress.Objective.Kind = 0;
        Progress.Objective.Progress = 0;
    }
    FGuid ParsedTestId;
    bool bVerifiedTestSaveId = false;
    if (bTestSlotRequested)
    {
        bVerifiedTestSaveId = FGuid::ParseExact(TestSaveId, EGuidFormats::Digits, ParsedTestId);
        if (bVerifiedTestSaveId) SaveSlotBase = TEXT("SpaceFlightTest_") + TestSaveId;
        else { bSaveEnabled = false; UE_LOG(LogTemp, Warning, TEXT("Invalid isolated save ID; all save writes are disabled.")); }
    }
    FString LegacyFixtureArgument;
    const bool bLegacyFixtureHasValue = FParse::Value(FCommandLine::Get(), TEXT("SpaceLegacyMigrationFixture="), LegacyFixtureArgument);
    const bool bLegacyFixtureRequested = bLegacyFixtureHasValue || FParse::Param(FCommandLine::Get(), TEXT("SpaceLegacyMigrationFixture"));
    FString ExpeditionSavePhase;
    const bool bExpeditionSaveRequested = FParse::Value(FCommandLine::Get(), TEXT("SpaceExpeditionSaveCheck="), ExpeditionSavePhase) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionSaveCheck"));
    const bool bValidExpeditionSavePhase = ExpeditionSavePhase == TEXT("active") || ExpeditionSavePhase == TEXT("ready") ||
        ExpeditionSavePhase == TEXT("failed") || ExpeditionSavePhase == TEXT("claim") || ExpeditionSavePhase == TEXT("verify");
    FString ExpeditionGuardArgument;
    const bool bOtherCheckpoint = FParse::Value(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint="), ExpeditionGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint"));
    const bool bOtherSurprise = FParse::Value(FCommandLine::Get(), TEXT("SpaceSurpriseCheck="), ExpeditionGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceSurpriseCheck"));
    const bool bExpeditionSaveAllowed = bExpeditionSaveRequested && bVerifiedTestSaveId && bSaveEnabled && bValidExpeditionSavePhase &&
        (FParse::Param(FCommandLine::Get(), TEXT("SpaceFailStageVerification")) == (ExpeditionSavePhase == TEXT("failed"))) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveResetCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceStressSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSectorCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceRefiningCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceProductionCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSpecializationCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveOnceAndPause")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchaseSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceEventPersistenceFixture")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoFlightCheck")) && !bOtherCheckpoint && !bOtherSurprise && !bLegacyFixtureRequested;
    if (bExpeditionSaveRequested && !bExpeditionSaveAllowed)
    {
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("EXPEDITION_SAVE_CHECK_ERROR invalid_configuration"));
    }
    FString SurpriseCheckArgument;
    const bool bSurpriseCheckRequested = FParse::Value(FCommandLine::Get(), TEXT("SpaceSurpriseCheck="), SurpriseCheckArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceSurpriseCheck"));
    const bool bSurpriseCheckAllowed = bSurpriseCheckRequested && bVerifiedTestSaveId && bSaveEnabled &&
        (SurpriseCheckArgument == TEXT("baseline") || SurpriseCheckArgument == TEXT("play") || SurpriseCheckArgument == TEXT("replay")) && !bLegacyFixtureRequested;
    if (bSurpriseCheckRequested && !bSurpriseCheckAllowed)
    {
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR invalid_configuration"));
    }
    FString CheckpointGuardArgument;
    const bool bCheckpointRequested = FParse::Value(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint"));
    FString FutureSavePhase;
    const bool bFutureSaveRequested = FParse::Value(FCommandLine::Get(), TEXT("SpaceFutureSaveCheck="), FutureSavePhase) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceFutureSaveCheck"));
    // This fixture is allowed to write only an explicitly isolated namespace.
    // Conflicting fixtures must be rejected before loading any slot.
    const bool bFutureSaveAllowed = bFutureSaveRequested && bVerifiedTestSaveId && bSaveEnabled &&
        (FutureSavePhase == TEXT("write") || FutureSavePhase == TEXT("verify")) &&
        !bExpeditionSaveRequested && !bSurpriseCheckRequested && !bCheckpointRequested && !bLegacyFixtureRequested &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceFailStageVerification")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveResetCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceStressSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSectorCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceRefiningCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceProductionCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSpecializationCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveOnceAndPause")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchaseSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceEventPersistenceFixture")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoFlightCheck"));
    if (bFutureSaveRequested && !bFutureSaveAllowed)
    {
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("FUTURE_SAVE_CHECK_ERROR invalid_configuration"));
    }
    const bool bLegacyFixtureAllowed = bLegacyFixtureRequested && !bLegacyFixtureHasValue && bVerifiedTestSaveId && bSaveEnabled &&
        !bExpeditionSaveRequested && !bSurpriseCheckRequested && !bCheckpointRequested && !bFutureSaveRequested &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceFailStageVerification")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveResetCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceStressSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSectorCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceRefiningCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceProductionCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSpecializationCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveOnceAndPause")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchaseSaveCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceEventPersistenceFixture")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoFlightCheck"));
    if (bLegacyFixtureRequested && !bLegacyFixtureAllowed)
    {
        bSaveEnabled = false;
        UE_LOG(LogTemp, Error, TEXT("LEGACY_MIGRATION_FIXTURE_ERROR invalid_configuration"));
    }
    // A mistakenly supplied test fixture must not even load the player's slot.
    if (bCheckpointRequested && !bVerifiedTestSaveId) bSaveEnabled = false;
    bSaveResetCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveResetCheck"));
    SaveResetCheckStarted = FPlatformTime::Seconds();
    bStressSaveCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceStressSaveCheck"));
    StressSaveCheckStarted = FPlatformTime::Seconds();
    bSectorCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceSectorCheck"));
    SectorCheckStarted = FPlatformTime::Seconds();
    bRefiningCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceRefiningCheck"));
    RefiningCheckStarted = FPlatformTime::Seconds();
    bProductionCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceProductionCheck"));
    ProductionCheckStarted = FPlatformTime::Seconds();
    bSpecializationCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceSpecializationCheck"));
    SpecializationCheckStarted = FPlatformTime::Seconds();
    bHUDCheck = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDCheck"));
    HUDCheckStarted = FPlatformTime::Seconds();
    bJournalCheck = !bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceJournalCheck"));
    JournalCheckStarted = FPlatformTime::Seconds();
    bTrackedInputCheck = bTrackedInputAllowed;
    TrackedInputStarted = FPlatformTime::Seconds();
    bManualInputCheck = bManualInputAllowed;
    ManualInputStarted = FPlatformTime::Seconds();
    bExpeditionInputCheck = bExpeditionInputAllowed;
    ExpeditionInputStarted = FPlatformTime::Seconds();
    bTargetingInputCheck = bTargetingInputAllowed;
    TargetingInputStarted = FPlatformTime::Seconds();
#else
    // Test-slot arguments must never fall through to the real player slots in
    // a build that does not implement the isolated automation namespace.
    FString CheckpointGuardArgument;
    if (bTestSlotRequested || FParse::Value(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint")) ||
        FParse::Value(FCommandLine::Get(), TEXT("SpaceSurpriseCheck="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceSurpriseCheck")) ||
        FParse::Value(FCommandLine::Get(), TEXT("SpaceExpeditionSaveCheck="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionSaveCheck")) ||
        FParse::Value(FCommandLine::Get(), TEXT("SpaceFutureSaveCheck="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceFutureSaveCheck")) ||
        FParse::Value(FCommandLine::Get(), TEXT("SpaceLegacyMigrationFixture="), CheckpointGuardArgument) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceLegacyMigrationFixture"))) bSaveEnabled = false;
#endif
    SessionStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    NextSaveTime = SessionStartTime + 15.0f;
    if (bSaveEnabled)
    {
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
        const FString Scope = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()).ToLower() + TEXT("|") + SaveSlotBase.ToLower();
        SaveMutex = MakeUnique<UE::FPlatformSystemWideMutex>(FString::Printf(TEXT("SpaceFlight_Save_%08x"), GetTypeHash(Scope)));
        if (!SaveMutex->IsValid())
#endif
        {
            bSaveSessionLocked = true;
            bSaveBlocked = true;
            UE_LOG(LogTemp, Error, TEXT("SAVE_SESSION_LOCKED: another instance owns this save namespace; writes are disabled."));
        }
    }
    LoadProgress();
#if WITH_DEV_AUTOMATION_TESTS
    FString FreshCheckpointArgument;
    float RequestedFreshCheckpoint = 0.0f;
    const bool bHasFreshCheckpointValue = FParse::Value(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint="), FreshCheckpointArgument);
    if (bHasFreshCheckpointValue || FParse::Param(FCommandLine::Get(), TEXT("SpaceFreshCheckpoint")))
    {
        if (bHasFreshCheckpointValue && FDefaultValueHelper::ParseFloat(FreshCheckpointArgument, RequestedFreshCheckpoint) &&
            bVerifiedTestSaveId && bSaveEnabled && !bSaveBlocked &&
            FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")) && FMath::IsFinite(RequestedFreshCheckpoint) &&
            RequestedFreshCheckpoint >= 1.0f && RequestedFreshCheckpoint <= 86400.0f)
        {
            FreshCheckpointSeconds = RequestedFreshCheckpoint;
            bFreshCheckpointPending = true;
        }
        else UE_LOG(LogTemp, Error, TEXT("FRESH_CHECKPOINT_ERROR reason=invalid_configuration"));
    }
    if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")))
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck"))) bAutopilot = false;
        else if (FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoFlightCheck"))) bAutopilot = true;
        int32 TestWorldSeed = 0;
        if (!Progress.bHasWorldSeed && FParse::Value(FCommandLine::Get(), TEXT("SpaceTestWorldSeed="), TestWorldSeed))
        {
            Progress.WorldSeed = TestWorldSeed;
            Progress.bHasWorldSeed = true;
            UE_LOG(LogTemp, Display, TEXT("TEST_WORLD_SEED seed=%d"), TestWorldSeed);
        }
    }
#endif
#if WITH_DEV_AUTOMATION_TESTS
    if (!bSaveEnabled && (FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetPreview")) ||
        FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetGranted"))))
    {
        Progress.WorldSeed = 1024;
        Progress.bHasWorldSeed = true;
        Progress.Acquired[5] = true;
        Progress.Acquired[71] = true;
        Progress.Acquired[SpaceFlight::RicochetUpgrade] = true;
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetPreview"))) Progress.bAutoPurchasesEnabled = false;
    }
    if (bSaveEnabled && !bSaveBlocked && SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) &&
        FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetSaveFixture")))
    {
        Progress.Acquired[5] = true;
        Progress.Acquired[71] = true;
        Progress.Acquired[SpaceFlight::RicochetUpgrade] = true;
    }
#endif
    if (!Progress.bHasWorldSeed)
    {
        Progress.WorldSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
        Progress.bHasWorldSeed = true;
    }
    SpawnSeed = static_cast<uint32>(Progress.WorldSeed);
    Shield.Initialize(SpawnSeed, GetWorld()->GetTimeSeconds());
    if (!bHasRestoredEvent) MineralEvent.Initialize(SpawnSeed);
#if WITH_DEV_AUTOMATION_TESTS
    if (bLegacyFixtureRequested)
    {
        if (bLegacyFixtureAllowed && !bSaveBlocked) RunLegacyMigrationFixture();
        else { bSaveEnabled = false; FPlatformMisc::RequestExitWithStatus(false, 1); }
        return;
    }
    if (bFutureSaveRequested)
    {
        if (bFutureSaveAllowed && ((FutureSavePhase == TEXT("write") && !bSaveBlocked) ||
            (FutureSavePhase == TEXT("verify") && bSaveBlocked))) RunFutureSaveCheck(FutureSavePhase);
        else { bSaveEnabled = false; FPlatformMisc::RequestExitWithStatus(false, 1); }
        return;
    }
    if (bExpeditionSaveRequested)
    {
        if (bExpeditionSaveAllowed && !bSaveBlocked) RunExpeditionSaveCheck(ExpeditionSavePhase);
        else FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceSurprisePreview")))
    {
        bSurprisePlaying = true;
        SurpriseAge = 2.0f;
        SurpriseLastRealTime = GetWorld()->GetRealTimeSeconds();
    }
    if (bSurpriseCheckAllowed)
    {
        SurpriseCheckPhase = SurpriseCheckArgument;
        SurpriseCheckStarted = GetWorld()->GetRealTimeSeconds();
        NextSaveTime = TNumericLimits<float>::Max();
        LastLaserTime = 1000000.0f;
        if (bSaveBlocked || (SurpriseCheckPhase == TEXT("baseline") && SaveRevision != 0) ||
            (SurpriseCheckPhase != TEXT("baseline") && SaveRevision == 0))
        {
            UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR invalid_snapshot"));
            FinishSurpriseCheck(false);
        }
        else if (SurpriseCheckPhase == TEXT("baseline"))
        {
            Progress.Objective.Sequence = 8;
            Progress.Objective.Kind = 0;
            Progress.Objective.Progress = 0;
            Progress.bAutoPurchasesEnabled = false;
            SavedFlightSeconds = 1800.0f;
            MineralEvent.Initialize(SpawnSeed);
            SaveProgress();
            if (bSaveWriteFailed || SaveRevision == 0)
            {
                UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR baseline_save"));
                FinishSurpriseCheck(false);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("SURPRISE_BASELINE revision=%lld pending=1"), SaveRevision);
                FinishSurpriseCheck(true);
            }
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("SURPRISE_LOAD delivered=%d revision=%lld"), Progress.Surprise.bDelivered ? 1 : 0, SaveRevision);
            bSurpriseCheckManual = SurpriseCheckPhase == TEXT("play") && FParse::Param(FCommandLine::Get(), TEXT("SpaceSurpriseManual"));
            if (bSurpriseCheckManual) bAutopilot = false;
            if ((SurpriseCheckPhase == TEXT("play") && Progress.Surprise.bDelivered) ||
                (SurpriseCheckPhase == TEXT("replay") && !Progress.Surprise.bDelivered))
            {
                UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR unexpected_delivery_state"));
                FinishSurpriseCheck(false);
            }
        }
    }
    if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && bSaveEnabled && !bHasRestoredEvent &&
        FParse::Param(FCommandLine::Get(), TEXT("SpaceEventPersistenceFixture")))
    {
        MineralEvent.Phase = SpaceFlight::EMineralPhase::Active;
        MineralEvent.Kind = SpaceFlight::ERareEventKind::MineralFlow;
        MineralEvent.Remaining = 30.0;
        MineralEvent.Anchor = 2;
        MineralEvent.Claimed = 1u << 2;
        MineralEvent.Claims = 1;
        MineralEvent.EventNumber = 1;
        LastLaserTime = 1000.0f;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceMineralCheck"))) MineralEvent.Remaining = 2.0;
    int32 MineralPreview = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceMineralPreview="), MineralPreview))
    {
        MineralEvent.Phase = MineralPreview == 0 ? SpaceFlight::EMineralPhase::Warning : SpaceFlight::EMineralPhase::Active;
        MineralEvent.Remaining = MineralPreview == 0 ? 6.0 : 32.0;
        MineralEvent.Anchor = 0;
    }
    int32 CrystalPreview = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceCrystalPreview="), CrystalPreview))
    {
        MineralEvent.Kind = SpaceFlight::ERareEventKind::UnstableCrystal;
        MineralEvent.Phase = CrystalPreview == 0 ? SpaceFlight::EMineralPhase::Warning : SpaceFlight::EMineralPhase::Active;
        MineralEvent.Remaining = CrystalPreview == 0 ? 6.0 : (CrystalPreview == 1 ? 30.0 : 12.0);
        MineralEvent.Anchor = 2;
        LastLaserTime = 1000.0f;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceCrystalCheck")))
    {
        MineralEvent.Kind = SpaceFlight::ERareEventKind::UnstableCrystal;
        MineralEvent.Phase = SpaceFlight::EMineralPhase::Warning;
        MineralEvent.Remaining = 2.0;
        MineralEvent.Anchor = 2;
    }
    int32 DepositPreview = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceDepositPreview="), DepositPreview))
    {
        MineralEvent.Kind = SpaceFlight::ERareEventKind::LargeDeposit;
        MineralEvent.Phase = DepositPreview == 0 ? SpaceFlight::EMineralPhase::Warning : SpaceFlight::EMineralPhase::Active;
        MineralEvent.Remaining = DepositPreview == 0 ? 6.0 : 30.0;
        MineralEvent.Anchor = 2;
        LastLaserTime = 1000.0f;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceDepositCheck")))
    {
        MineralEvent.Kind = SpaceFlight::ERareEventKind::LargeDeposit;
        MineralEvent.Phase = SpaceFlight::EMineralPhase::Warning;
        MineralEvent.Remaining = 2.0;
        MineralEvent.Anchor = 2;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceDepositCheckWide")))
    {
        for (int32 Index : { 0, 1, 2, 10, 11, 25, 26, 27, 28, 29 }) Progress.Acquired[Index] = true;
        bAutopilot = false;
    }
    int32 ShieldPreview = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceShieldPreview="), ShieldPreview))
    {
        Shield.Current.Start = GetWorld()->GetTimeSeconds() - .6;
        Shield.Current.Duration = 1.35f;
        Shield.Current.Pattern = FMath::Clamp(ShieldPreview, 0, 2);
        Shield.Current.Angle = .7f;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDDetailed"))) bDetailedHUD = true;
    int32 TargetingPreview = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceTargetingPreview="), TargetingPreview) &&
        TargetingPreview >= 0 && TargetingPreview <= 3) Progress.TargetingMode = TargetingPreview;
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceHudNoticePreview")))
    {
        UpgradeNotice = SpaceFlight::Localize(TEXT("Цель выполнена: +9 руды"));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 10.0f;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDLargeValues")))
    {
        Progress.AddResources(900000000);
        Progress.BlueOre = 300000000;
        Progress.BlackOre = 300000000;
        Progress.Isotope = 900000000;
        Progress.CurrentSector = 2;
        Progress.Objective.Sequence = 8;
        Progress.Objective.Kind = 4;
        Progress.Objective.Progress = 2;
        Progress.Specialization = 2;
        bAutopilot = false;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDWideValues")))
    {
        // Preview only: exercise values that exceed the legacy save format.
        Progress.Resources = 123456789012LL;
        Progress.BlueOre = 9876543210LL;
        Progress.BlackOre = 8765432109LL;
        Progress.Isotope = 3456789012LL;
        Progress.OreConverted = 2 * Progress.Isotope;
        Progress.TotalCollected = Progress.Resources + Progress.OreConverted;
        Progress.Objective.Sequence = 8;
        Progress.CurrentSector = 2;
        bAutopilot = false;
    }
    int32 PreviewSector = 0;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceSectorPreview="), PreviewSector))
    {
        Progress.Objective.Sequence = 8;
        Progress.Objective.Kind = 0;
        Progress.Objective.Progress = 0;
        Progress.CurrentSector = FMath::Clamp(PreviewSector, 0, SpaceFlight::SectorCount - 1);
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceRefiningPreview")))
    {
        bRefiningSelection = true;
        SelectedRefiningMode = 3;
    }
    int32 RefiningPreviewMode = -1;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceRefiningPreviewMode="), RefiningPreviewMode) &&
        RefiningPreviewMode >= 0 && RefiningPreviewMode <= 5)
    {
        bRefiningSelection = true;
        SelectedRefiningMode = RefiningPreviewMode;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceProductionPreview")))
    {
        Progress.AddResources(9);
        bProductionSelection = true;
        SelectedProductionKind = SpaceFlight::EOreKind::Blue;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceSpecializationPreview")))
    {
        bSpecializationSelection = true;
        SelectedSpecialization = 2;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionPreview")))
    {
        Progress.Objective.Sequence = 8;
        bExpeditionSelection = true;
        SelectedExpeditionRoute = 1;
    }
    if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionReadyPreview")))
    {
        Progress.Expedition.Start(0, 100, 0);
        Progress.Expedition.Record(0, 0, 100, false);
        bExpeditionSelection = false;
    }
    int32 WrongSectorRoute = 0;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceExpeditionWrongSectorPreview="), WrongSectorRoute) &&
        (WrongSectorRoute == 1 || WrongSectorRoute == 2))
    {
        Progress.Objective.Sequence = 8;
        Progress.CurrentSector = 0;
        Progress.Expedition.Start(WrongSectorRoute, 100, 2);
        Progress.Expedition.Record(WrongSectorRoute, WrongSectorRoute, 30, false);
        bExpeditionSelection = false;
    }
    int32 ActiveSpecialization = 0;
    if (!bSaveEnabled && FParse::Value(FCommandLine::Get(), TEXT("SpaceSpecializationActive="), ActiveSpecialization) &&
        ActiveSpecialization >= 1 && ActiveSpecialization <= 3)
        Progress.SetSpecialization(ActiveSpecialization, 0);
    bAudioCheck = FParse::Param(FCommandLine::Get(), TEXT("SpaceAudioCheck"));
    if (bAudioCheck)
    {
        AudioCheckStarted = FPlatformTime::Seconds();
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 25.0f);
        UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK recording started"));
    }
#endif
    const int32 EngineIndex = static_cast<int32>(SpaceFlight::ESoundEvent::Engine);
    if (EventSounds.IsValidIndex(EngineIndex) && EventSounds[EngineIndex])
        EngineAudio = UGameplayStatics::SpawnSound2D(this, EventSounds[EngineIndex], MasterSoundGain * EngineSoundGain,
            1.0f, 0.0f, nullptr, false, false);
    PlayEventSound(SpaceFlight::ESoundEvent::Launch);
}

float ASpaceFlightHUD::TotalFlightSeconds() const
{
    return SavedFlightSeconds + (GetWorld() ? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - SessionStartTime) : 0.0f);
}

void ASpaceFlightHUD::LoadProgress()
{
    if (!bSaveEnabled) return;
    const FString SlotA = SaveSlotBase + TEXT("A"), SlotB = SaveSlotBase + TEXT("B");
    const auto PresenceA = USpaceFlightSaveGame::InspectSlotPresence(SlotA);
    const auto PresenceB = USpaceFlightSaveGame::InspectSlotPresence(SlotB);
    const bool bExistsA = PresenceA != SpaceFlight::ESaveSlotPresence::Missing;
    const bool bExistsB = PresenceB != SpaceFlight::ESaveSlotPresence::Missing;
    SpaceFlight::ESaveEnvelope EnvelopeA = SpaceFlight::ESaveEnvelope::Unreadable, EnvelopeB = SpaceFlight::ESaveEnvelope::Unreadable;
    USpaceFlightSaveGame* A = PresenceA == SpaceFlight::ESaveSlotPresence::Present ? USpaceFlightSaveGame::LoadWithHeaderCheck(SlotA, &EnvelopeA) : nullptr;
    USpaceFlightSaveGame* B = PresenceB == SpaceFlight::ESaveSlotPresence::Present ? USpaceFlightSaveGame::LoadWithHeaderCheck(SlotB, &EnvelopeB) : nullptr;
    const USpaceFlightSaveGame* Best = USpaceFlightSaveGame::NewestValid(A, B);
    const bool bNewerFormat = (A && (A->Version > SpaceFlight::CurrentSaveVersion || A->ObjectiveSchema > 2 || A->SectorSchema > 1 || A->RefiningSchema > 1 || A->ProductionSchema > 1 || A->SpecializationSchema > 1 || A->JournalSchema > 1 || A->EventSchema > 1)) ||
        (B && (B->Version > SpaceFlight::CurrentSaveVersion || B->ObjectiveSchema > 2 || B->SectorSchema > 1 || B->RefiningSchema > 1 || B->ProductionSchema > 1 || B->SpecializationSchema > 1 || B->JournalSchema > 1 || B->EventSchema > 1));
    if (bNewerFormat || (bExistsA && (EnvelopeA == SpaceFlight::ESaveEnvelope::Unsupported || EnvelopeA == SpaceFlight::ESaveEnvelope::Unreadable)) ||
        (bExistsB && (EnvelopeB == SpaceFlight::ESaveEnvelope::Unsupported || EnvelopeB == SpaceFlight::ESaveEnvelope::Unreadable)) || (!Best && (bExistsA || bExistsB)))
    {
        bSaveBlocked = true;
        UE_LOG(LogTemp, Error, TEXT("Progress cannot be safely saved: the save is invalid or was made by a newer version."));
    }
    if (!Best) return;
    float Time = 0.0f;
    int32 Brightness = 20;
    bool bAuto = true;
    if (Best->Restore(Progress, Time, Brightness, bAuto))
    {
        SavedFlightSeconds = Time;
        StarBrightness.Value = Brightness;
        bAutopilot = bAuto;
        bDetailedHUD = Best->bDetailedHUD;
        bHasRestoredEvent = Best->EventSchema == 1 && Best->RestoreEvent(MineralEvent);
        if (bHasRestoredEvent && FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
            UE_LOG(LogTemp, Display, TEXT("EVENT_SNAPSHOT_LOAD phase=%d kind=%d remaining=%.6f anchor=%d claimed=%u claims=%d seed=%d event_number=%d"),
                static_cast<int32>(MineralEvent.Phase), static_cast<int32>(MineralEvent.Kind), MineralEvent.Remaining,
                MineralEvent.Anchor, MineralEvent.Claimed, MineralEvent.Claims, MineralEvent.Random.GetCurrentSeed(), MineralEvent.EventNumber);
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
            UE_LOG(LogTemp, Display, TEXT("TARGETING_SNAPSHOT_LOAD mode=%d schema=%d"), Progress.TargetingMode, Best->TargetingSchema);
        SaveRevision = Best->Revision;
        ActiveSaveSlot = Best == A ? 0 : 1;
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
            UE_LOG(LogTemp, Display, TEXT("SURPRISE_SNAPSHOT_LOAD version=%d due=%.3f delivered=%d"),
                Best->Version, Progress.Surprise.NotBeforeSeconds, Progress.Surprise.bDelivered ? 1 : 0);
        int32 Purchased = 0;
        for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) if (Progress.Has(I)) ++Purchased;
        UE_LOG(LogTemp, Display, TEXT("PROGRESS_LOAD revision=%lld elapsed=%.1f ore=%lld isotope=%lld purchased=%d goal=%d goal_kind=%d goal_progress=%d sector=%d seed=%d refining=%d quota_blue=%lld quota_black=%lld module_ready=%.3f spec=%d spec_ready=%.3f hud_details=%d journal=%u auto_buy=%d tracked=%d slot=%s blue=%lld black=%lld brightness=%d autopilot=%d"),
            SaveRevision, SavedFlightSeconds, Progress.Resources, Progress.Isotope, Purchased,
            Progress.Objective.Sequence, Progress.Objective.Kind, Progress.Objective.Progress, Progress.CurrentSector, Progress.WorldSeed,
            Progress.RefiningMode, Progress.BlueRefiningQuota, Progress.BlackRefiningQuota, Progress.ProductionReadyAt,
            Progress.Specialization, Progress.SpecializationReadyAt, bDetailedHUD ? 1 : 0, Progress.DiscoveryBits, Progress.bAutoPurchasesEnabled ? 1 : 0, Progress.TrackedUpgrade, Best == A ? TEXT("A") : TEXT("B"),
            Progress.BlueOre, Progress.BlackOre, StarBrightness.Value, bAutopilot ? 1 : 0);
    }
}

void ASpaceFlightHUD::SaveProgress()
{
    if (!bSaveEnabled || bSaveBlocked || bStressMode || !GetWorld()) return;
    if (SaveRevision == MAX_int64)
    {
        bSaveWriteFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Progress revision exhausted; existing save copies were retained."));
        return;
    }
    USpaceFlightSaveGame* Save = Cast<USpaceFlightSaveGame>(UGameplayStatics::CreateSaveGameObject(USpaceFlightSaveGame::StaticClass()));
    if (!Save)
    {
        bSaveWriteFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Could not allocate a save object; previous save copies were retained."));
        return;
    }
    const int64 NextRevision = SaveRevision + 1;
    Save->Capture(Progress, TotalFlightSeconds(), StarBrightness.Value, bAutopilot, NextRevision);
    Save->CaptureEvent(MineralEvent);
    Save->bDetailedHUD = bDetailedHUD;
    SpaceFlight::FProgress Verified;
    float VerifiedTime = 0.0f;
    int32 VerifiedBrightness = 0;
    bool bVerifiedAuto = false;
    if (!Save->Restore(Verified, VerifiedTime, VerifiedBrightness, bVerifiedAuto))
    {
        bSaveWriteFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Progress failed validation; existing save copies were retained."));
        return;
    }
    const int32 TargetSlot = SpaceFlight::NextSaveSlot(ActiveSaveSlot);
    const FString Slot = SaveSlotBase + (TargetSlot == 0 ? TEXT("A") : TEXT("B"));
    const FString Stage = SaveSlotBase + TEXT("Stage");
    bool bSimulateReadFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
    bSimulateReadFailure = SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceFailStageVerification"));
#endif
    TArray<uint8> RawData, SealedData;
    if (UGameplayStatics::SaveGameToMemory(Save, RawData) && SpaceFlight::SealSaveData(RawData, SealedData) &&
        USpaceFlightSaveGame::WriteVerifiedSlot(Slot, Stage, SealedData, bSimulateReadFailure))
    {
        bSaveWriteFailed = false;
        SaveRevision = NextRevision;
        ActiveSaveSlot = TargetSlot;
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
            UE_LOG(LogTemp, Display, TEXT("SURPRISE_SNAPSHOT_SAVE version=%d due=%.3f delivered=%d"),
                Save->Version, Progress.Surprise.NotBeforeSeconds, Progress.Surprise.bDelivered ? 1 : 0);
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
            UE_LOG(LogTemp, Display, TEXT("EVENT_SNAPSHOT_SAVE phase=%d kind=%d remaining=%.6f anchor=%d claimed=%u claims=%d seed=%d event_number=%d"),
                Save->EventPhase, Save->EventKind, Save->EventRemaining, Save->EventAnchor,
                static_cast<uint32>(Save->EventClaimed), Save->EventClaims, Save->EventRandomSeed, Save->EventNumber);
        if (FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace")))
        {
            int32 Purchased = 0;
            for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) if (Progress.Has(I)) ++Purchased;
            UE_LOG(LogTemp, Display, TEXT("PROGRESS_SAVE revision=%lld elapsed=%.1f ore=%lld isotope=%lld purchased=%d goal=%d goal_kind=%d goal_progress=%d sector=%d seed=%d refining=%d quota_blue=%lld quota_black=%lld module_ready=%.3f spec=%d spec_ready=%.3f hud_details=%d journal=%u auto_buy=%d tracked=%d slot=%s blue=%lld black=%lld brightness=%d autopilot=%d"),
                SaveRevision, TotalFlightSeconds(), Progress.Resources, Progress.Isotope, Purchased,
                Progress.Objective.Sequence, Progress.Objective.Kind, Progress.Objective.Progress, Progress.CurrentSector, Progress.WorldSeed,
                Progress.RefiningMode, Progress.BlueRefiningQuota, Progress.BlackRefiningQuota, Progress.ProductionReadyAt,
                Progress.Specialization, Progress.SpecializationReadyAt, bDetailedHUD ? 1 : 0, Progress.DiscoveryBits,
                Progress.bAutoPurchasesEnabled ? 1 : 0, Progress.TrackedUpgrade, TargetSlot == 0 ? TEXT("A") : TEXT("B"),
                Progress.BlueOre, Progress.BlackOre, StarBrightness.Value, bAutopilot ? 1 : 0);
        }
    }
    else
    {
        bSaveWriteFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Could not save flight progress; the previous copy was retained."));
    }
}

void ASpaceFlightHUD::RequestProgressReset()
{
    if (!GetWorld() || !SpaceFlight::CanResetProgress(GetWorld()->IsPaused(), bStressMode, bSaveBlocked)) return;
    const float Now = GetWorld()->GetRealTimeSeconds();
    if (ResetConfirmUntil <= 0.0f || Now > ResetConfirmUntil)
    {
        ResetConfirmUntil = Now + 4.0f;
        PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
        return;
    }
    // Persist the new state before discarding the current session. Existing
    // slots are never deleted: if the write fails, restore the in-memory state.
    const SpaceFlight::FProgress Previous = Progress;
    const SpaceFlight::FMineralEvent PreviousEvent = MineralEvent;
    const float PreviousSavedSeconds = SavedFlightSeconds;
    const float PreviousStartTime = SessionStartTime;
    Progress = SpaceFlight::FProgress();
    Progress.WorldSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    Progress.bHasWorldSeed = true;
    MineralEvent.Initialize(static_cast<uint32>(Progress.WorldSeed));
    SavedFlightSeconds = 0.0f;
    SessionStartTime = GetWorld()->GetTimeSeconds();
    SaveProgress();
    if (bSaveEnabled && bSaveWriteFailed)
    {
        Progress = Previous;
        MineralEvent = PreviousEvent;
        SavedFlightSeconds = PreviousSavedSeconds;
        SessionStartTime = PreviousStartTime;
        ResetConfirmUntil = 0.0f;
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
        return;
    }
    NextSaveTime = SessionStartTime + 15.0f;
    SpawnSeed = static_cast<uint32>(Progress.WorldSeed);
    Shield.Initialize(SpawnSeed, GetWorld()->GetTimeSeconds());
    bRefiningSelection = false;
    bProductionSelection = false;
    bSpecializationSelection = false;
    bExpeditionSelection = false;
    SelectedExpeditionRoute = 0;
    ExpeditionError.Reset();
    LastLaserTime = -100.0f;
    NextAutoUpgradeEvaluation = 0.0f;
    ShipX = -1.0f;
    ShipBank = 0.0f;
    BaseResetTimes.Reset();
    BaseGenerations.Reset();
    AsteroidDurability.Reset();
    DustBursts.Reset();
    Impacts.Reset();
    LaserBeams.Reset();
    bSurprisePlaying = false;
    bSurpriseReplay = false;
    bSurpriseSeenThisSession = false;
    bSurpriseCommitPending = false;
    SurpriseAge = 0.0f;
    SurpriseRetryAt = 0.0;
    ResetConfirmUntil = 0.0f;
    UpgradeNotice = SpaceFlight::Localize(TEXT("Новый полёт начат"));
    NoticeUntil = Now + 3.0f;
    PlayEventSound(SpaceFlight::ESoundEvent::ModeOn, true);
}

#if WITH_DEV_AUTOMATION_TESTS
void ASpaceFlightHUD::RunLegacyMigrationFixture()
{
    // Only the already-validated, mutex-protected test namespace reaches this
    // function. Never overwrite an existing file, including a stale test slot.
    const FString ASlot = SaveSlotBase + TEXT("A");
    const FString BSlot = SaveSlotBase + TEXT("B");
    const FString StageSlot = SaveSlotBase + TEXT("Stage");
    bool bOk = SaveRevision == 0 &&
        USpaceFlightSaveGame::InspectSlotPresence(ASlot) == SpaceFlight::ESaveSlotPresence::Missing &&
        USpaceFlightSaveGame::InspectSlotPresence(BSlot) == SpaceFlight::ESaveSlotPresence::Missing &&
        USpaceFlightSaveGame::InspectSlotPresence(StageSlot) == SpaceFlight::ESaveSlotPresence::Missing;
    SpaceFlight::FProgress Fixture;
    if (bOk)
    {
        Fixture.bAutoPurchasesEnabled = false;
        bOk = Fixture.SetRefiningMode(1) && Fixture.AddResources(1000) == 1000 &&
            Fixture.AddOre(40, SpaceFlight::EOreKind::Blue) == 40 &&
            Fixture.AddOre(20, SpaceFlight::EOreKind::Black) == 20 && Fixture.Purchase(0);
        Fixture.Objective.Sequence = 3;
        Fixture.Objective.Kind = 0;
        Fixture.Objective.Progress = 2;
        Fixture.CurrentSector = 1;
        Fixture.WorldSeed = 314159;
        Fixture.bHasWorldSeed = true;
        Fixture.Specialization = 2;
        Fixture.ProductionReadyAt = 3598.0f;
        Fixture.SpecializationReadyAt = 3599.0f;
    }
    USpaceFlightSaveGame* Legacy = bOk ? Cast<USpaceFlightSaveGame>(
        UGameplayStatics::CreateSaveGameObject(USpaceFlightSaveGame::StaticClass())) : nullptr;
    if (Legacy)
    {
        Legacy->Capture(Fixture, 3600.0f, 20, false, 1);
        Legacy->Version = 3;
        // Keep later fields at their missing-property defaults. V3 restore must
        // ignore them whether this serializer writes defaults or omits them.
        Legacy->TrackedUpgradeIndex = -2;
        Legacy->SurpriseDelivered = -1;
        Legacy->SurpriseNotBefore = -1.0f;
        Legacy->SectorReadyAt = -1.0f;
        Legacy->ExpeditionRoute = -2;
        Legacy->ExpeditionTarget = -1;
        Legacy->ExpeditionProgress = -1;
        SpaceFlight::FProgress Verified;
        float Time = 0.0f; int32 Brightness = 0; bool bAuto = true;
        TArray<uint8> Raw;
        const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (ASlot + TEXT(".sav")));
        bOk = Legacy->Restore(Verified, Time, Brightness, bAuto) && Time == 3600.0f &&
            Verified.Resources == 1048 && Verified.Has(0) && Verified.CurrentSector == 1 &&
            UGameplayStatics::SaveGameToMemory(Legacy, Raw) && SpaceFlight::HasUsableSaveHeader(Raw) &&
            FFileHelper::SaveArrayToFile(Raw, *Filename, &IFileManager::Get(), FILEWRITE_NoReplaceExisting);
        if (bOk)
        {
            SpaceFlight::ESaveEnvelope Envelope = SpaceFlight::ESaveEnvelope::Unreadable;
            const USpaceFlightSaveGame* Loaded = USpaceFlightSaveGame::LoadWithHeaderCheck(ASlot, &Envelope);
            bOk = Loaded && Envelope == SpaceFlight::ESaveEnvelope::Legacy && Loaded->Version == 3 &&
                Loaded->Revision == 1 && Loaded->Restore(Verified, Time, Brightness, bAuto) &&
                Verified.Resources == 1048 && Verified.Has(0) && Verified.CurrentSector == 1;
        }
    }
    else bOk = false;
    if (bOk)
    {
        UE_LOG(LogTemp, Display, TEXT("LEGACY_MIGRATION_FIXTURE version=3 revision=1 ore=1048 purchased=1 sector=1 current=%d"),
            SpaceFlight::CurrentSaveVersion);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("LEGACY_MIGRATION_FIXTURE_ERROR could_not_create_fixture"));
    }
    // Do not let EndPlay rewrite the freshly created V3 snapshot.
    bSaveEnabled = false;
    FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
}

void ASpaceFlightHUD::RunFutureSaveCheck(const FString& Phase)
{
    const FString ASlot = SaveSlotBase + TEXT("A");
    const FString BSlot = SaveSlotBase + TEXT("B");
    bool bOk = false;
    if (Phase == TEXT("write") && SaveRevision == 0 && !bSaveBlocked &&
        USpaceFlightSaveGame::InspectSlotPresence(ASlot) == SpaceFlight::ESaveSlotPresence::Missing &&
        USpaceFlightSaveGame::InspectSlotPresence(BSlot) == SpaceFlight::ESaveSlotPresence::Missing &&
        Progress.AddResources(123) == 123)
    {
        SaveProgress();
        if (!bSaveWriteFailed && SaveRevision == 1 && ActiveSaveSlot == 0)
        {
            USpaceFlightSaveGame* Future = Cast<USpaceFlightSaveGame>(UGameplayStatics::CreateSaveGameObject(USpaceFlightSaveGame::StaticClass()));
            if (Future)
            {
                Future->Capture(Progress, TotalFlightSeconds(), StarBrightness.Value, bAutopilot, 2);
                Future->CaptureEvent(MineralEvent);
                Future->Version = SpaceFlight::CurrentSaveVersion + 1;
                TArray<uint8> Raw, Sealed;
                bOk = UGameplayStatics::SaveGameToMemory(Future, Raw) && SpaceFlight::SealSaveData(Raw, Sealed) &&
                    USpaceFlightSaveGame::WriteVerifiedSlot(BSlot, SaveSlotBase + TEXT("Stage"), Sealed);
            }
        }
    }
    else if (Phase == TEXT("verify") && bSaveBlocked && !bSaveSessionLocked && SaveRevision == 1 &&
        ActiveSaveSlot == 0 && Progress.Resources == 123)
    {
        SpaceFlight::ESaveEnvelope EnvelopeA, EnvelopeB;
        const USpaceFlightSaveGame* A = USpaceFlightSaveGame::LoadWithHeaderCheck(ASlot, &EnvelopeA);
        const USpaceFlightSaveGame* B = USpaceFlightSaveGame::LoadWithHeaderCheck(BSlot, &EnvelopeB);
        if (A && B && EnvelopeA == SpaceFlight::ESaveEnvelope::Verified && EnvelopeB == SpaceFlight::ESaveEnvelope::Verified &&
            A->Version == SpaceFlight::CurrentSaveVersion && A->Revision == 1 &&
            B->Version == SpaceFlight::CurrentSaveVersion + 1 && B->Revision == 2 &&
            USpaceFlightSaveGame::NewestValid(A, B) == A && Progress.AddResources(1) == 1)
        {
            SaveProgress(); // A blocked write must leave the loaded revision unchanged.
            bOk = bSaveBlocked && bSaveEnabled && SaveRevision == 1 && Progress.Resources == 124;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("FUTURE_SAVE_CHECK phase=%s ok=%d blocked=%d enabled=%d revision=%lld ore=%lld"),
        *Phase, bOk ? 1 : 0, bSaveBlocked ? 1 : 0, bSaveEnabled ? 1 : 0, SaveRevision, Progress.Resources);
    // In verify mode leave saving enabled: EndPlay must independently respect
    // bSaveBlocked. The external test checks both slot hashes after exit.
    bFutureSaveEndPlayCheck = bOk && Phase == TEXT("verify");
    if (!bFutureSaveEndPlayCheck) bSaveEnabled = false;
    FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
}

void ASpaceFlightHUD::RunExpeditionSaveCheck(const FString& Phase)
{
    using SpaceFlight::EExpeditionState;
    using SpaceFlight::EOreKind;
    bool bOk = false;
    const int64 PreviousRevision = SaveRevision;
    const auto& Before = Progress.Expedition;
    if (Phase == TEXT("active") && PreviousRevision == 0 && Before.State == EExpeditionState::Idle)
    {
        Progress.Objective.Sequence = 3;
        Progress.CurrentSector = 1;
        Progress.bAutoPurchasesEnabled = false;
        SelectExpedition();
        SelectExpedition();
        ApplyExpedition();
        if (SaveRevision == PreviousRevision + 1 && Progress.Expedition.State == EExpeditionState::Active &&
            Progress.Expedition.Route == 1 && Progress.Expedition.Target == 100)
        {
            const int64 Accepted = Progress.AddOre(37, EOreKind::Blue);
            Progress.Expedition.Record(1, static_cast<int32>(EOreKind::Blue), Accepted, false);
            Progress.SectorReadyAt = TotalFlightSeconds() + 12.0f;
            SaveProgress();
            bOk = !bSaveWriteFailed && SaveRevision == PreviousRevision + 2 && Accepted == 37 &&
                Progress.Expedition.Progress == 37 && Progress.Resources == 37;
        }
    }
    else if (Phase == TEXT("ready") && PreviousRevision == 2 && Before.State == EExpeditionState::Active &&
        Before.Route == 1 && Before.Target == 100 && Before.Progress == 37 && Progress.Resources == 37 &&
        Progress.CurrentSector == 1 && Progress.SectorReadyAt > TotalFlightSeconds())
    {
        const int64 Accepted = Progress.AddOre(63, EOreKind::Blue);
        Progress.Expedition.Record(1, static_cast<int32>(EOreKind::Blue), Accepted, false);
        SaveProgress();
        bOk = !bSaveWriteFailed && SaveRevision == PreviousRevision + 1 && Accepted == 63 &&
            Progress.Expedition.State == EExpeditionState::Ready && Progress.Expedition.Progress == 100 && Progress.Resources == 100;
    }
    else if ((Phase == TEXT("failed") || Phase == TEXT("claim")) && PreviousRevision == 3 &&
        Before.State == EExpeditionState::Ready && Before.Route == 1 && Before.Target == 100 &&
        Before.Progress == 100 && Progress.Resources == 100)
    {
        SelectExpedition();
        ApplyExpedition();
        if (Phase == TEXT("failed"))
        {
            bOk = bSaveWriteFailed && SaveRevision == PreviousRevision && bExpeditionSelection && !ExpeditionError.IsEmpty() &&
                Progress.Expedition.State == EExpeditionState::Ready && Progress.Expedition.Progress == 100 && Progress.Resources == 100;
        }
        else
        {
            const bool bClaimed = !bSaveWriteFailed && SaveRevision == PreviousRevision + 1 && !bExpeditionSelection &&
                Progress.Expedition.State == EExpeditionState::Idle && Progress.Resources == 90 && Progress.BlueOre == 90 &&
                Progress.BlackOre == 0 && Progress.Isotope == 10 && Progress.TotalCollected == 110 && Progress.OreConverted == 20;
            const bool bSecondApplyHandled = ApplyExpedition();
            bOk = bClaimed && !bSecondApplyHandled && SaveRevision == PreviousRevision + 1 && Progress.Resources == 90 && Progress.Isotope == 10;
        }
    }
    else if (Phase == TEXT("verify") && PreviousRevision == 4 && Before.State == EExpeditionState::Idle &&
        Progress.Resources == 90 && Progress.BlueOre == 90 && Progress.BlackOre == 0 && Progress.Isotope == 10 &&
        Progress.TotalCollected == 110 && Progress.OreConverted == 20)
    {
        const bool bSecondApplyHandled = ApplyExpedition();
        bOk = !bSecondApplyHandled && SaveRevision == PreviousRevision && Progress.Resources == 90 && Progress.Isotope == 10;
    }
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_SAVE_CHECK phase=%s ok=%d state=%d route=%d target=%d progress=%d ore=%lld revision=%lld failed=%d"),
        *Phase, bOk ? 1 : 0, static_cast<int32>(Progress.Expedition.State), Progress.Expedition.Route,
        Progress.Expedition.Target, Progress.Expedition.Progress, Progress.Resources, SaveRevision, bSaveWriteFailed ? 1 : 0);
    // Avoid any EndPlay writes; each phase explicitly committed its intended state.
    bSaveEnabled = false;
    FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
}

void ASpaceFlightHUD::FinishSurpriseCheck(bool bSuccess)
{
    if (bSurpriseCheckFinished) return;
    bSurpriseCheckFinished = true;
    UE_LOG(LogTemp, Display, TEXT("SURPRISE_CHECK_DONE phase=%s result=%s"), *SurpriseCheckPhase, bSuccess ? TEXT("ok") : TEXT("failed"));
    // The fixture has already made its one intended write, if any.
    bSaveEnabled = false;
    FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
}

void ASpaceFlightHUD::TickTargetingInputCheck()
{
    if (!bTargetingInputCheck || bSaveEnabled) return;
    auto Fail = [&](const TCHAR* Reason)
    {
        UE_LOG(LogTemp, Error, TEXT("TARGETING_INPUT_CHECK failure=%s step=%d"), Reason, TargetingInputStep);
        bTargetingInputCheck = false;
    };
    if (FPlatformTime::Seconds() - TargetingInputStarted > 12.0) { Fail(TEXT("timeout")); return; }
    if (!PlayerOwner || !GetWorld() || ViewportWidth <= 0 || TargetingInputLastFrame == GFrameCounter) return;
    TargetingInputLastFrame = GFrameCounter;
    auto Key = [&](const FKey& Input, EInputEvent Event)
    { PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(Input, Event, Event == IE_Pressed ? 1.0f : 0.0f)); };
    for (const FKey& Input : { EKeys::X, EKeys::Tab, EKeys::E, EKeys::Escape }) Key(Input, IE_Released);
    auto Press = [&](const FKey& Input) { Key(Input, IE_Pressed); ++TargetingInputStep; };
    switch (TargetingInputStep)
    {
    case 0: if (Progress.TargetingMode != 0) { Fail(TEXT("default")); return; } Press(EKeys::X); break;
    case 1: if (Progress.TargetingMode != 1) { Fail(TEXT("blue")); return; } Press(EKeys::X); break;
    case 2: if (Progress.TargetingMode != 2) { Fail(TEXT("black")); return; } Press(EKeys::Tab); break;
    case 3: if (!bTreeOpen) { Fail(TEXT("tree_open")); return; } Press(EKeys::X); break;
    case 4: if (Progress.TargetingMode != 2) { Fail(TEXT("tree_block")); return; } Press(EKeys::Tab); break;
    case 5: if (bTreeOpen) { Fail(TEXT("tree_close")); return; } Press(EKeys::E); break;
    case 6: if (!bExpeditionSelection) { Fail(TEXT("popup_open")); return; } Press(EKeys::X); break;
    case 7: if (Progress.TargetingMode != 2) { Fail(TEXT("popup_block")); return; } Press(EKeys::Escape); break;
    case 8: if (bExpeditionSelection) { Fail(TEXT("popup_close")); return; } Press(EKeys::Escape); break;
    case 9: if (!GetWorld()->IsPaused()) { Fail(TEXT("pause")); return; } Press(EKeys::X); break;
    case 10: if (Progress.TargetingMode != 3) { Fail(TEXT("paused_event")); return; } Press(EKeys::X); break;
    case 11: if (Progress.TargetingMode != 0) { Fail(TEXT("wrap")); return; } Press(EKeys::Escape); break;
    case 12:
        if (GetWorld()->IsPaused()) { Fail(TEXT("resume")); return; }
        UE_LOG(LogTemp, Display, TEXT("TARGETING_INPUT_CHECK cycle=1 tree=1 popup=1 pause=1 wrap=1"));
        bTargetingInputCheck = false;
        break;
    }
}

void ASpaceFlightHUD::TickLanguageInputCheck()
{
    if (bSaveEnabled || !FParse::Param(FCommandLine::Get(), TEXT("SpaceLanguageInputCheck")) ||
        LanguageInputStep < 0 || !PlayerOwner || !GetWorld() || ViewportWidth <= 0 || LanguageInputLastFrame == GFrameCounter) return;
    LanguageInputLastFrame = GFrameCounter;
    auto Fail = [&] { UE_LOG(LogTemp, Error, TEXT("LANGUAGE_INPUT_CHECK failure step=%d"), LanguageInputStep); LanguageInputStep = -1; };
    auto Key = [&](const FKey& Input, EInputEvent Event)
    { PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(Input, Event, Event == IE_Pressed ? 1.0f : 0.0f)); };
    for (const FKey& Input : { EKeys::L, EKeys::Tab, EKeys::Escape }) Key(Input, IE_Released);
    auto Press = [&](const FKey& Input) { Key(Input, IE_Pressed); ++LanguageInputStep; };
    switch (LanguageInputStep)
    {
    case 0: if (SpaceFlight::GetLanguage() != SpaceFlight::ELanguage::Russian) { Fail(); return; } Press(EKeys::L); break;
    case 1: if (SpaceFlight::GetLanguage() != SpaceFlight::ELanguage::English) { Fail(); return; } Press(EKeys::Tab); break;
    case 2: if (!bTreeOpen) { Fail(); return; } Press(EKeys::L); break;
    case 3: if (SpaceFlight::GetLanguage() != SpaceFlight::ELanguage::Spanish) { Fail(); return; } Press(EKeys::Tab); break;
    case 4: if (bTreeOpen) { Fail(); return; } Press(EKeys::Escape); break;
    case 5: if (!GetWorld()->IsPaused()) { Fail(); return; } Press(EKeys::L); break;
    case 6: if (SpaceFlight::GetLanguage() != SpaceFlight::ELanguage::Russian) { Fail(); return; } Press(EKeys::Escape); break;
    case 7:
        if (GetWorld()->IsPaused()) { Fail(); return; }
        UE_LOG(LogTemp, Display, TEXT("LANGUAGE_INPUT_CHECK cycle=1 tree=1 pause=1 wrap=1"));
        LanguageInputStep = -1;
        break;
    }
}

void ASpaceFlightHUD::TickExpeditionInputCheck()
{
    if (!bExpeditionInputCheck || bSaveEnabled) return;
    auto Fail = [&](const TCHAR* Reason)
    {
        UE_LOG(LogTemp, Error, TEXT("EXPEDITION_INPUT_CHECK failure=%s step=%d"), Reason, ExpeditionInputStep);
        bExpeditionInputCheck = false;
    };
    if (FPlatformTime::Seconds() - ExpeditionInputStarted > 12.0) { Fail(TEXT("timeout")); return; }
    if (!PlayerOwner || !GetWorld() || ViewportWidth <= 0 || ViewportHeight <= 0 || ExpeditionInputLastFrame == GFrameCounter) return;
    ExpeditionInputLastFrame = GFrameCounter;
    auto Key = [&](const FKey& Input, EInputEvent Event)
    {
        PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(Input, Event, Event == IE_Pressed ? 1.0f : 0.0f));
    };
    // Release the preceding frame's key before sending the next action.
    for (const FKey& Input : { EKeys::E, EKeys::Enter, EKeys::Escape }) Key(Input, IE_Released);
    auto Press = [&](const FKey& Input) { Key(Input, IE_Pressed); ++ExpeditionInputStep; };
    using SpaceFlight::EExpeditionState;
    using SpaceFlight::EOreKind;
    auto& Expedition = Progress.Expedition;
    switch (ExpeditionInputStep)
    {
    case 0:
        bAutopilot = false;
        Progress.bAutoPurchasesEnabled = false;
        Progress.SetRefiningMode(1); // Keep the payout colour visible in this input test.
        Progress.Objective.Sequence = 0;
        Press(EKeys::Escape);
        break;
    case 1:
        if (!GetWorld()->IsPaused()) { Fail(TEXT("pause")); return; }
        Press(EKeys::E);
        break;
    case 2:
        if (!bExpeditionSelection || SelectedExpeditionRoute != 0) { Fail(TEXT("first_route")); return; }
        Press(EKeys::E);
        break;
    case 3:
        if (SelectedExpeditionRoute != 0) { Fail(TEXT("locked_route")); return; }
        Progress.Objective.Sequence = 8;
        Press(EKeys::E);
        break;
    case 4:
        if (SelectedExpeditionRoute != 1) { Fail(TEXT("blue_route")); return; }
        Press(EKeys::E);
        break;
    case 5:
        if (SelectedExpeditionRoute != 2) { Fail(TEXT("black_route")); return; }
        Press(EKeys::E);
        break;
    case 6:
        if (SelectedExpeditionRoute != 0) { Fail(TEXT("route_wrap")); return; }
        Press(EKeys::E);
        break;
    case 7:
        if (SelectedExpeditionRoute != 1) { Fail(TEXT("blue_route_again")); return; }
        Press(EKeys::Enter);
        break;
    case 8:
        if (!GetWorld()->IsPaused() || bExpeditionSelection || Expedition.State != EExpeditionState::Active || Expedition.Route != 1)
        { Fail(TEXT("start_paused")); return; }
        Press(EKeys::E);
        break;
    case 9:
        if (!bExpeditionSelection) { Fail(TEXT("reopen_active")); return; }
        Press(EKeys::Escape);
        break;
    case 10:
    {
        if (!GetWorld()->IsPaused() || bExpeditionSelection || Expedition.State != EExpeditionState::Active)
        { Fail(TEXT("escape_cancel_window")); return; }
        // Use the accepted amount from the real inventory path, just as a laser hit does.
        auto Mine = [&](int32 Sector, EOreKind Ore, int32 Amount)
        {
            const int32 AcceptedOre = static_cast<int32>(Progress.AddOre(Amount, Ore));
            return Expedition.Record(Sector, static_cast<int32>(Ore), AcceptedOre, false);
        };
        if (Mine(1, EOreKind::Common, 1) != 0 || Mine(0, EOreKind::Blue, 1) != 0 || Expedition.Progress != 0)
        { Fail(TEXT("wrong_ore_or_sector")); return; }
        if (Mine(1, EOreKind::Blue, 7) != 7 || Expedition.Progress != 7)
        { Fail(TEXT("accepted_ore")); return; }
        const int32 Remaining = Expedition.Target - Expedition.Progress;
        if (Mine(1, EOreKind::Blue, Remaining) != Remaining || Expedition.State != EExpeditionState::Ready)
        { Fail(TEXT("complete")); return; }
        ExpeditionInputReward = SpaceFlight::FExpedition::RewardAmount(Expedition.Route, Expedition.Target);
        ExpeditionInputBeforeReward = Progress.Resources;
        ExpeditionInputBeforeBlack = Progress.BlackOre;
        Press(EKeys::E);
        break;
    }
    case 11:
        if (!bExpeditionSelection || Expedition.State != EExpeditionState::Ready) { Fail(TEXT("ready_window")); return; }
        ExpeditionInputBeforeTotal = Progress.TotalCollected;
        Progress.TotalCollected = SpaceFlight::ResourceAccountingLimit;
        Press(EKeys::Enter);
        break;
    case 12:
        if (!GetWorld()->IsPaused() || !bExpeditionSelection || Expedition.State != EExpeditionState::Ready ||
            ExpeditionError.IsEmpty() || Progress.Resources != ExpeditionInputBeforeReward)
        { Fail(TEXT("reward_limit_error")); return; }
        Progress.TotalCollected = ExpeditionInputBeforeTotal;
        Press(EKeys::Enter);
        break;
    case 13:
        if (!GetWorld()->IsPaused() || bExpeditionSelection || Expedition.State != EExpeditionState::Idle ||
            !ExpeditionError.IsEmpty() || Progress.Resources != ExpeditionInputBeforeReward + ExpeditionInputReward ||
            Progress.BlackOre != ExpeditionInputBeforeBlack + ExpeditionInputReward)
        { Fail(TEXT("claim")); return; }
        Press(EKeys::Enter);
        break;
    case 14:
        if (Progress.Resources != ExpeditionInputBeforeReward + ExpeditionInputReward ||
            Progress.BlackOre != ExpeditionInputBeforeBlack + ExpeditionInputReward || Expedition.State != EExpeditionState::Idle)
        { Fail(TEXT("duplicate_claim")); return; }
        Press(EKeys::E);
        break;
    case 15:
        if (!bExpeditionSelection || SelectedExpeditionRoute != 0) { Fail(TEXT("reopen_idle")); return; }
        Press(EKeys::Escape);
        break;
    case 16:
        if (!GetWorld()->IsPaused() || bExpeditionSelection || Expedition.State != EExpeditionState::Idle)
        { Fail(TEXT("cancel_paused")); return; }
        Press(EKeys::Escape);
        break;
    case 17:
        if (GetWorld()->IsPaused() || Expedition.State != EExpeditionState::Idle) { Fail(TEXT("resume")); return; }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_INPUT_CHECK routes=1 pause=1 start=1 ore=1 ready=1 reward_once=1 escape=1 limit=1"));
        bExpeditionInputCheck = false;
        break;
    }
}

void ASpaceFlightHUD::TickManualInputCheck()
{
    if (!bManualInputCheck || bSaveEnabled) return;
    const FKey MovementKeys[] = { EKeys::D, EKeys::A, EKeys::Right, EKeys::Left };
    auto SendKey = [&](const FKey& Key, EInputEvent Event)
    {
        PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.0f : 0.0f));
    };
    auto ReleaseAll = [&]()
    {
        if (!PlayerOwner) return;
        for (const FKey& Key : MovementKeys) SendKey(Key, IE_Released);
        SendKey(EKeys::F, IE_Released);
        SendKey(EKeys::Tab, IE_Released);
        SendKey(EKeys::Escape, IE_Released);
    };
    auto Fail = [&](const TCHAR* Reason)
    {
        ReleaseAll();
        UE_LOG(LogTemp, Error, TEXT("MANUAL_INPUT_CHECK failure=%s step=%d key=%d x=%.2f start=%.2f auto=%d tree=%d"),
            Reason, ManualInputStep, ManualInputKeyIndex, ShipX, ManualInputStartX, bAutopilot ? 1 : 0, bTreeOpen ? 1 : 0);
        bManualInputCheck = false;
    };
    if (FPlatformTime::Seconds() - ManualInputStarted > 30.0)
    {
        Fail(TEXT("timeout"));
        return;
    }
    if (!PlayerOwner || !GetWorld() || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f || ManualInputLastFrame == GFrameCounter) return;
    ManualInputLastFrame = GFrameCounter;
    if (GetWorld()->IsPaused() && !((ManualInputStep >= 15 && ManualInputStep <= 22) ||
        (ManualInputStep >= 24 && ManualInputStep <= 26) || (ManualInputStep >= 29 && ManualInputStep <= 31)))
    { Fail(TEXT("unexpected_pause")); return; }
    if (ManualInputWaitFrames > 0) { --ManualInputWaitFrames; return; }
    auto Wait = [&](int32 NextStep, int32 Frames = 4)
    {
        ManualInputStep = NextStep;
        ManualInputWaitFrames = Frames;
    };
    if (ManualInputStep == 0)
    {
        bAutopilot = true;
        bTreeOpen = false;
        Progress.bAutoPurchasesEnabled = false;
        ShipX = ViewportWidth * 0.5f;
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(1);
    }
    else if (ManualInputStep == 1)
    {
        if (!bTreeOpen) { Fail(TEXT("tab_open")); return; }
        SendKey(EKeys::Tab, IE_Released);
        SendKey(EKeys::A, IE_Pressed);
        Wait(2);
    }
    else if (ManualInputStep == 2)
    {
        if (!bAutopilot) { Fail(TEXT("tree_disabled_autopilot")); return; }
        SendKey(EKeys::A, IE_Released);
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(3);
    }
    else if (ManualInputStep == 3)
    {
        if (bTreeOpen) { Fail(TEXT("tab_close")); return; }
        SendKey(EKeys::Tab, IE_Released);
        ManualInputStartX = ShipX = ViewportWidth * 0.5f;
        SendKey(MovementKeys[ManualInputKeyIndex], IE_Pressed);
        Wait(4, 15);
    }
    else if (ManualInputStep == 4)
    {
        const float Direction = ManualInputKeyIndex % 2 == 0 ? 1.0f : -1.0f;
        if (bAutopilot || (ShipX - ManualInputStartX) * Direction < 2.0f)
        { Fail(TEXT("direction_or_manual_mode")); return; }
        SendKey(MovementKeys[ManualInputKeyIndex], IE_Released);
        SendKey(EKeys::F, IE_Pressed);
        Wait(5);
    }
    else if (ManualInputStep == 5)
    {
        if (!bAutopilot) { Fail(TEXT("f_resume")); return; }
        SendKey(EKeys::F, IE_Released);
        if (++ManualInputKeyIndex < UE_ARRAY_COUNT(MovementKeys))
        {
            ManualInputStartX = ShipX = ViewportWidth * 0.5f;
            SendKey(MovementKeys[ManualInputKeyIndex], IE_Pressed);
            Wait(4, 15);
        }
        else
        {
            const float Unit = FMath::Max(1.0f, FMath::FloorToFloat(FMath::Min(ViewportWidth / 320.0f, ViewportHeight / 180.0f)));
            ShipX = ViewportWidth - 8.0f * Unit - 1.0f;
            SendKey(EKeys::D, IE_Pressed);
            Wait(6, 15);
        }
    }
    else if (ManualInputStep == 6)
    {
        const float Unit = FMath::Max(1.0f, FMath::FloorToFloat(FMath::Min(ViewportWidth / 320.0f, ViewportHeight / 180.0f)));
        if (bAutopilot || !FMath::IsNearlyEqual(ShipX, ViewportWidth - 8.0f * Unit, 0.5f))
        { Fail(TEXT("right_boundary")); return; }
        SendKey(EKeys::D, IE_Released);
        ShipX = 8.0f * Unit + 1.0f;
        SendKey(EKeys::A, IE_Pressed);
        Wait(7, 15);
    }
    else if (ManualInputStep == 7)
    {
        const float Unit = FMath::Max(1.0f, FMath::FloorToFloat(FMath::Min(ViewportWidth / 320.0f, ViewportHeight / 180.0f)));
        if (bAutopilot || !FMath::IsNearlyEqual(ShipX, 8.0f * Unit, 0.5f))
        { Fail(TEXT("left_boundary")); return; }
        SendKey(EKeys::A, IE_Released);
        ManualInputKeyIndex = 0;
        SendKey(EKeys::F, IE_Pressed);
        Wait(8);
    }
    else if (ManualInputStep == 8)
    {
        if (!bAutopilot || bTreeOpen) { Fail(TEXT("held_fixture_autopilot")); return; }
        SendKey(EKeys::F, IE_Released);
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(9);
    }
    else if (ManualInputStep == 9)
    {
        if (!bTreeOpen || !bAutopilot) { Fail(TEXT("held_tree_open")); return; }
        SendKey(EKeys::Tab, IE_Released);
        SendKey(MovementKeys[ManualInputKeyIndex], IE_Pressed);
        Wait(10);
    }
    else if (ManualInputStep == 10)
    {
        if (!bTreeOpen || !bAutopilot) { Fail(TEXT("held_tree_interrupted_autopilot")); return; }
        ManualInputStartX = ShipX = ViewportWidth * 0.5f;
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(11, 15);
    }
    else if (ManualInputStep == 11)
    {
        const float Direction = ManualInputKeyIndex % 2 == 0 ? 1.0f : -1.0f;
        if (bTreeOpen || bAutopilot || (ShipX - ManualInputStartX) * Direction < 2.0f)
        { Fail(TEXT("held_tree_does_not_activate_manual")); return; }
        SendKey(EKeys::Tab, IE_Released);
        SendKey(MovementKeys[ManualInputKeyIndex], IE_Released);
        Wait(12);
    }
    else if (ManualInputStep == 12)
    {
        if (bTreeOpen || bAutopilot) { Fail(TEXT("held_release_mode")); return; }
        // Allow the queued release to reach PlayerInput before measuring rest.
        ManualInputStartX = ShipX;
        Wait(13, 15);
    }
    else if (ManualInputStep == 13)
    {
        if (bTreeOpen || bAutopilot || !FMath::IsNearlyEqual(ShipX, ManualInputStartX, 0.1f))
        { Fail(TEXT("held_release_did_not_stop")); return; }
        if (++ManualInputKeyIndex < UE_ARRAY_COUNT(MovementKeys))
        {
            SendKey(EKeys::F, IE_Pressed);
            Wait(8);
            return;
        }
        ReleaseAll();
        SendKey(EKeys::F, IE_Pressed);
        Wait(14);
    }
    else if (ManualInputStep == 14)
    {
        if (!bAutopilot || bTreeOpen || GetWorld()->IsPaused()) { Fail(TEXT("pause_fixture_autopilot")); return; }
        SendKey(EKeys::F, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(15);
    }
    else if (ManualInputStep == 15)
    {
        if (!GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("pause_lost_autopilot")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(EKeys::A, IE_Pressed);
        Wait(16);
    }
    else if (ManualInputStep == 16)
    {
        if (!GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("pause_a_changed_autopilot")); return; }
        SendKey(EKeys::A, IE_Released);
        SendKey(EKeys::D, IE_Pressed);
        Wait(17);
    }
    else if (ManualInputStep == 17)
    {
        if (!GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("pause_d_changed_autopilot")); return; }
        SendKey(EKeys::D, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(18);
    }
    else if (ManualInputStep == 18)
    {
        if (GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("resume_lost_autopilot")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(EKeys::A, IE_Pressed);
        Wait(19);
    }
    else if (ManualInputStep == 19)
    {
        if (GetWorld()->IsPaused() || bAutopilot) { Fail(TEXT("pause_fixture_manual")); return; }
        SendKey(EKeys::A, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(20);
    }
    else if (ManualInputStep == 20)
    {
        if (!GetWorld()->IsPaused() || bAutopilot) { Fail(TEXT("pause_changed_manual")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(EKeys::D, IE_Pressed);
        Wait(21);
    }
    else if (ManualInputStep == 21)
    {
        if (!GetWorld()->IsPaused() || bAutopilot) { Fail(TEXT("pause_d_changed_manual")); return; }
        SendKey(EKeys::D, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(22);
    }
    else if (ManualInputStep == 22)
    {
        if (GetWorld()->IsPaused() || bAutopilot) { Fail(TEXT("resume_changed_manual")); return; }
        ReleaseAll();
        ManualInputKeyIndex = 0;
        SendKey(EKeys::F, IE_Pressed);
        Wait(23);
    }
    else if (ManualInputStep == 23)
    {
        if (GetWorld()->IsPaused() || bTreeOpen || !bAutopilot) { Fail(TEXT("held_pause_fixture")); return; }
        SendKey(EKeys::F, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(24);
    }
    else if (ManualInputStep == 24)
    {
        if (!GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("held_pause_enter")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(MovementKeys[ManualInputKeyIndex], IE_Pressed);
        Wait(25);
    }
    else if (ManualInputStep == 25)
    {
        if (!GetWorld()->IsPaused() || !bAutopilot) { Fail(TEXT("held_pause_changed_auto")); return; }
        ManualInputStartX = ShipX = ViewportWidth * 0.5f;
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(26, 15);
    }
    else if (ManualInputStep == 26)
    {
        const float Direction = ManualInputKeyIndex % 2 == 0 ? 1.0f : -1.0f;
        if (GetWorld()->IsPaused() || bTreeOpen || bAutopilot || (ShipX - ManualInputStartX) * Direction < 2.0f)
        { Fail(TEXT("held_pause_does_not_activate_manual")); return; }
        ReleaseAll();
        if (++ManualInputKeyIndex < UE_ARRAY_COUNT(MovementKeys))
        {
            SendKey(EKeys::F, IE_Pressed);
            Wait(23);
            return;
        }
        SendKey(EKeys::F, IE_Pressed);
        Wait(27);
    }
    else if (ManualInputStep == 27)
    {
        if (GetWorld()->IsPaused() || bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_fixture")); return; }
        SendKey(EKeys::F, IE_Released);
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(28);
    }
    else if (ManualInputStep == 28)
    {
        if (!bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_open")); return; }
        SendKey(EKeys::Tab, IE_Released);
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(29);
    }
    else if (ManualInputStep == 29)
    {
        if (!GetWorld()->IsPaused() || !bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_pause")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(EKeys::D, IE_Pressed);
        Wait(30);
    }
    else if (ManualInputStep == 30)
    {
        if (!GetWorld()->IsPaused() || !bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_held")); return; }
        SendKey(EKeys::Escape, IE_Pressed);
        Wait(31);
    }
    else if (ManualInputStep == 31)
    {
        if (GetWorld()->IsPaused() || !bTreeOpen || !bAutopilot) { Fail(TEXT("resume_tree_lost_auto")); return; }
        SendKey(EKeys::Escape, IE_Released);
        SendKey(EKeys::D, IE_Released);
        Wait(32);
    }
    else if (ManualInputStep == 32)
    {
        if (GetWorld()->IsPaused() || !bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_release")); return; }
        SendKey(EKeys::Tab, IE_Pressed);
        Wait(33);
    }
    else if (ManualInputStep == 33)
    {
        if (GetWorld()->IsPaused() || bTreeOpen || !bAutopilot) { Fail(TEXT("paused_tree_close")); return; }
        ReleaseAll();
        UE_LOG(LogTemp, Display, TEXT("MANUAL_INPUT_CHECK tree=1 ad=1 arrows=1 resume=1 bounds=1 held_tree=1 release=1 pause_auto=1 pause_manual=1 held_pause=1 paused_tree=1"));
        bManualInputCheck = false;
    }
}

void ASpaceFlightHUD::TickTrackedInputCheck()
{
    if (!bTrackedInputCheck || bSaveEnabled) return;
    auto ReleaseKey = [&]()
    {
        if (bTrackedInputKeyDown && PlayerOwner)
        {
            PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::T, IE_Released, 0.0f));
            bTrackedInputKeyDown = false;
        }
    };
    auto Fail = [&](const TCHAR* Reason)
    {
        ReleaseKey();
        UE_LOG(LogTemp, Error, TEXT("TRACKED_INPUT_CHECK running=%d paused=0 failure=%s step=%d"),
            bTrackedInputRunningPassed ? 1 : 0, Reason, TrackedInputStep);
        bTrackedInputCheck = false;
    };
    if (FPlatformTime::Seconds() - TrackedInputStarted > 30.0)
    {
        Fail(TEXT("timeout"));
        return;
    }
    if (!PlayerOwner || !GetWorld() || ViewportWidth <= 0.0f || ViewportHeight <= 0.0f || TrackedInputLastFrame == GFrameCounter) return;
    TrackedInputLastFrame = GFrameCounter;
    if (TrackedInputStep == 0)
    {
        if (GetWorld()->IsPaused() || Progress.Has(0) || Progress.TrackedUpgrade != INDEX_NONE)
        {
            Fail(TEXT("fixture"));
            return;
        }
        bAutopilot = false;
        bTreeOpen = true;
        bJournalOpen = false;
        Progress.AddResources(1000);
        TrackedInputInitialOre = Progress.Resources;
        LastLaserTime = GetWorld()->GetTimeSeconds() + 1000.0f;
        TreeNavigationCursor = INDEX_NONE;
        FocusNextAvailableUpgrade();
        if (!bTreeKeyboardFocusActive || TreeKeyboardFocus != 0)
        {
            Fail(TEXT("focus"));
            return;
        }
        PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::T, IE_Pressed, 1.0f));
        bTrackedInputKeyDown = true;
        TrackedInputStep = 1;
    }
    else if (TrackedInputStep == 1)
    {
        if (Progress.TrackedUpgrade != 0) return;
        bTrackedInputRunningPassed = !GetWorld()->IsPaused() && !Progress.Has(0) && Progress.Resources == TrackedInputInitialOre;
        if (!bTrackedInputRunningPassed)
        {
            Fail(TEXT("running_state"));
            return;
        }
        ReleaseKey();
        TrackedInputStep = 2;
    }
    else if (TrackedInputStep == 2)
    {
        if (!PlayerOwner->SetPause(true))
        {
            Fail(TEXT("pause"));
            return;
        }
        OnPauseChanged(true);
        TreeNavigationCursor = INDEX_NONE;
        FocusNextAvailableUpgrade();
        if (!bTreeKeyboardFocusActive || TreeKeyboardFocus != 0)
        {
            Fail(TEXT("paused_focus"));
            return;
        }
        TrackedInputStep = 3;
    }
    else if (TrackedInputStep == 3)
    {
        if (!GetWorld()->IsPaused())
        {
            Fail(TEXT("not_paused"));
            return;
        }
        PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::T, IE_Pressed, 1.0f));
        bTrackedInputKeyDown = true;
        TrackedInputStep = 4;
    }
    else if (TrackedInputStep == 4 && Progress.TrackedUpgrade == INDEX_NONE)
    {
        const bool bPausedPassed = GetWorld()->IsPaused() && !Progress.Has(0) && Progress.Resources == TrackedInputInitialOre;
        if (!bPausedPassed)
        {
            Fail(TEXT("paused_state"));
            return;
        }
        ReleaseKey();
        UE_LOG(LogTemp, Display, TEXT("TRACKED_INPUT_CHECK running=1 paused=1"));
        bTrackedInputCheck = false;
    }
}

void ASpaceFlightHUD::TickJournalCheck()
{
    if (!bJournalCheck || !PlayerOwner || !GetWorld() || ViewportWidth <= 0 || ViewportHeight <= 0) return;
    const double Elapsed = FPlatformTime::Seconds() - JournalCheckStarted;
    if (JournalCheckStep == 0 && Elapsed >= 1.0)
    {
        Progress = SpaceFlight::FProgress();
        Progress.AddResources(100);
        bAutopilot = false;
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        if (!bTreeOpen) ToggleTree();
        ToggleJournal();
        BeginTreePointer(SpaceFlight::ETreePointerButton::Left,
            SpaceFlight::UpgradeScreenCenter(0, ViewportWidth, ViewportHeight, TreeZoom, TreePan));
        PurchaseUpgrade(0, false);
        UE_LOG(LogTemp, Display, TEXT("JOURNAL_CHECK paused=%d open=%d purchase_blocked=%d pointer_blocked=%d"),
            GetWorld()->IsPaused() ? 1 : 0, bJournalOpen ? 1 : 0, !Progress.Has(0) ? 1 : 0, !TreePointer.IsActive() ? 1 : 0);
        ++JournalCheckStep;
    }
    else if (JournalCheckStep == 1 && Elapsed >= 2.0)
    {
        ToggleJournal();
        PurchaseUpgrade(0, false);
        const bool bPurchaseAllowed = Progress.Has(0);
        ToggleJournal();
        ToggleTree();
        ToggleTree();
        UE_LOG(LogTemp, Display, TEXT("JOURNAL_CHECK tree_purchase=%d reset_on_close=%d paused=%d"),
            bPurchaseAllowed ? 1 : 0, (bTreeOpen && !bJournalOpen) ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
        ++JournalCheckStep;
    }
}

void ASpaceFlightHUD::TickHUDCheck()
{
    if (!bHUDCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - HUDCheckStarted;
    if (HUDCheckStep == 0 && Elapsed >= 1.0)
    {
        SelectProductionRecipe();
        ToggleHUDDetails();
        UE_LOG(LogTemp, Display, TEXT("HUD_CHECK running details=%d selection=%d paused=%d"),
            bDetailedHUD ? 1 : 0, bProductionSelection ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
        ++HUDCheckStep;
    }
    else if (HUDCheckStep == 1 && Elapsed >= 2.0)
    {
        ToggleHUDDetails();
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        ToggleHUDDetails();
        UE_LOG(LogTemp, Display, TEXT("HUD_CHECK paused details=%d selection=%d paused=%d"),
            bDetailedHUD ? 1 : 0, bProductionSelection ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
        ++HUDCheckStep;
    }
}

void ASpaceFlightHUD::TickSpecializationCheck()
{
    if (!bSpecializationCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - SpecializationCheckStarted;
    if (SpecializationCheckStep == 0 && Elapsed >= 1.0)
    {
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        SelectSpecialization();
        UE_LOG(LogTemp, Display, TEXT("SPECIALIZATION_CHECK preview active=%d selected=%d paused=%d"),
            Progress.Specialization, SelectedSpecialization, GetWorld()->IsPaused() ? 1 : 0);
        const bool bCanceled = CancelSpecializationSelection();
        UE_LOG(LogTemp, Display, TEXT("SPECIALIZATION_CHECK canceled=%d active=%d selection=%d"),
            bCanceled ? 1 : 0, Progress.Specialization, bSpecializationSelection ? 1 : 0);
        SelectSpecialization();
        ApplySpecialization();
        UE_LOG(LogTemp, Display, TEXT("SPECIALIZATION_CHECK applied active=%d ready=%.3f elapsed=%.3f"),
            Progress.Specialization, Progress.SpecializationReadyAt, TotalFlightSeconds());
        ++SpecializationCheckStep;
    }
    else if (SpecializationCheckStep == 1 && Elapsed >= 2.0)
    {
        SelectSpecialization();
        ApplySpecialization();
        UE_LOG(LogTemp, Display, TEXT("SPECIALIZATION_CHECK blocked active=%d selected=%d selection=%d paused=%d"),
            Progress.Specialization, SelectedSpecialization, bSpecializationSelection ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
        ++SpecializationCheckStep;
    }
}

void ASpaceFlightHUD::TickProductionCheck()
{
    if (!bProductionCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - ProductionCheckStarted;
    if (ProductionCheckStep == 0 && Elapsed >= 1.0)
    {
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        Progress.AddResources(9);
        Progress.SetRefiningMode(1);
        SelectProductionRecipe();
        const int64 Before = Progress.CommonOre();
        const bool bCanceled = CancelProductionSelection();
        UE_LOG(LogTemp, Display, TEXT("PRODUCTION_CHECK canceled=%d before=%lld after=%lld paused=%d"),
            bCanceled ? 1 : 0, Before, Progress.CommonOre(), GetWorld()->IsPaused() ? 1 : 0);
        SelectProductionRecipe();
        ApplyProductionRecipe();
        UE_LOG(LogTemp, Display, TEXT("PRODUCTION_CHECK first common=%lld blue=%lld ready=%.3f elapsed=%.3f"),
            Progress.CommonOre(), Progress.BlueOre, Progress.ProductionReadyAt, TotalFlightSeconds());
        ++ProductionCheckStep;
    }
    else if (ProductionCheckStep == 1 && Elapsed >= 2.0)
    {
        SelectProductionRecipe();
        SelectProductionRecipe();
        ApplyProductionRecipe();
        UE_LOG(LogTemp, Display, TEXT("PRODUCTION_CHECK blocked common=%lld black=%lld selection=%d paused=%d"),
            Progress.CommonOre(), Progress.BlackOre, bProductionSelection ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
        if (PlayerOwner->SetPause(false)) OnPauseChanged(false);
        ++ProductionCheckStep;
    }
    else if (ProductionCheckStep == 2 && Elapsed >= 6.0)
    {
        ApplyProductionRecipe();
        UE_LOG(LogTemp, Display, TEXT("PRODUCTION_CHECK second common=%lld black=%lld ready=%.3f elapsed=%.3f selection=%d"),
            Progress.CommonOre(), Progress.BlackOre, Progress.ProductionReadyAt, TotalFlightSeconds(), bProductionSelection ? 1 : 0);
        ++ProductionCheckStep;
    }
}

void ASpaceFlightHUD::TickRefiningCheck()
{
    if (!bRefiningCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - RefiningCheckStarted;
    if (RefiningCheckStep == 0 && Elapsed >= 1.0)
    {
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        SelectRefiningMode();
        UE_LOG(LogTemp, Display, TEXT("REFINING_CHECK preview active=%d selected=%d paused=%d"),
            Progress.RefiningMode, SelectedRefiningMode, GetWorld()->IsPaused() ? 1 : 0);
        const bool bCanceled = CancelRefiningSelection();
        UE_LOG(LogTemp, Display, TEXT("REFINING_CHECK canceled=%d active=%d selection=%d"),
            bCanceled ? 1 : 0, Progress.RefiningMode, bRefiningSelection ? 1 : 0);
        SelectRefiningMode();
        ++RefiningCheckStep;
    }
    else if (RefiningCheckStep == 1 && Elapsed >= 2.0)
    {
        ApplyRefiningMode();
        Progress.AddOre(1, SpaceFlight::EOreKind::Blue);
        Progress.AddOre(1, SpaceFlight::EOreKind::Black);
        SelectRefiningMode();
        SelectRefiningMode();
        UE_LOG(LogTemp, Display, TEXT("REFINING_CHECK pending active=%d selected=%d isotope=%lld"),
            Progress.RefiningMode, SelectedRefiningMode, Progress.Isotope);
        ++RefiningCheckStep;
    }
    else if (RefiningCheckStep == 2 && Elapsed >= 3.0)
    {
        ApplyRefiningMode();
        UE_LOG(LogTemp, Display, TEXT("REFINING_CHECK applied active=%d quota_blue=%lld quota_black=%lld paused=%d"),
            Progress.RefiningMode, Progress.BlueRefiningQuota, Progress.BlackRefiningQuota, GetWorld()->IsPaused() ? 1 : 0);
        ++RefiningCheckStep;
    }
}

void ASpaceFlightHUD::TickSectorCheck()
{
    if (!bSectorCheck) return;
    const double Elapsed = FPlatformTime::Seconds() - SectorCheckStarted;
    if (SectorCheckStep == 0 && Elapsed >= 1.0)
    {
        Progress.Objective.Sequence = 8;
        Progress.Objective.Kind = 0;
        Progress.Objective.Progress = 0;
        CycleSector();
        UE_LOG(LogTemp, Display, TEXT("SECTOR_CHECK first sector=%d seed=%d"), Progress.CurrentSector, Progress.WorldSeed);
        ++SectorCheckStep;
    }
    else if (SectorCheckStep == 1 && Elapsed >= 2.0)
    {
        CycleSector();
        UE_LOG(LogTemp, Display, TEXT("SECTOR_CHECK cooldown sector=%d"), Progress.CurrentSector);
        ++SectorCheckStep;
    }
    else if (SectorCheckStep == 2 && Elapsed >= 3.0)
    {
        Progress.SectorReadyAt = 0.0f;
        bAutopilot = false;
        CycleSector();
        UE_LOG(LogTemp, Display, TEXT("SECTOR_CHECK manual sector=%d seed=%d"), Progress.CurrentSector, Progress.WorldSeed);
        ++SectorCheckStep;
    }
}

void ASpaceFlightHUD::TickStressSaveCheck()
{
    if (!bStressSaveCheck) return;
    const double Elapsed = FPlatformTime::Seconds() - StressSaveCheckStarted;
    if (StressSaveCheckStep == 0 && Elapsed >= 1.0)
    {
        Progress.AddResources(100);
        StartAsteroidStress();
        UE_LOG(LogTemp, Display, TEXT("STRESS_SAVE_CHECK first stress=%d save_failed=%d ore=%lld"),
            bStressMode ? 1 : 0, bSaveWriteFailed ? 1 : 0, Progress.Resources);
        ++StressSaveCheckStep;
    }
    else if (StressSaveCheckStep == 1 && Elapsed >= 6.0)
    {
        StartAsteroidStress();
        UE_LOG(LogTemp, Display, TEXT("STRESS_SAVE_CHECK retry stress=%d save_failed=%d ore=%lld revision=%lld"),
            bStressMode ? 1 : 0, bSaveWriteFailed ? 1 : 0, Progress.Resources, SaveRevision);
        ++StressSaveCheckStep;
    }
}

void ASpaceFlightHUD::TickSaveResetCheck()
{
    if (!bSaveResetCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - SaveResetCheckStarted;
    if (SaveResetCheckStep == 0 && Elapsed >= 1.0)
    {
        Progress.AddResources(100);
        if (PlayerOwner->SetPause(true)) OnPauseChanged(true);
        ++SaveResetCheckStep;
    }
    else if (SaveResetCheckStep == 1 && Elapsed >= 2.0)
    {
        const int64 Before = Progress.Resources;
        RequestProgressReset();
        UE_LOG(LogTemp, Display, TEXT("SAVE_RESET_CHECK confirmation paused=%d before=%lld after=%lld"),
            GetWorld()->IsPaused() ? 1 : 0, Before, Progress.Resources);
        ++SaveResetCheckStep;
    }
    else if (SaveResetCheckStep == 2 && Elapsed >= 3.0)
    {
        RequestProgressReset();
        UE_LOG(LogTemp, Display, TEXT("SAVE_RESET_CHECK reset paused=%d ore=%lld collected=%lld elapsed=%.1f revision=%lld"),
            GetWorld()->IsPaused() ? 1 : 0, Progress.Resources, Progress.TotalCollected, TotalFlightSeconds(), SaveRevision);
        ++SaveResetCheckStep;
    }
}

void ASpaceFlightHUD::TickAudioCheck()
{
    if (!bAudioCheck || !PlayerOwner) return;
    const double Elapsed = FPlatformTime::Seconds() - AudioCheckStarted;
    switch (AudioCheckStep)
    {
    case 0:
        if (Elapsed >= 1.0)
        {
            ToggleTree();
            UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK zoom_begin"));
            ZoomTree(1, FVector2D(ViewportWidth * .5f, ViewportHeight * .5f));
            ZoomTree(-1, FVector2D(ViewportWidth * .5f, ViewportHeight * .5f));
            ZoomTree(1, FVector2D(ViewportWidth * .5f, ViewportHeight * .5f));
            ResetTreeView();
            UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK zoom_end"));
            ++AudioCheckStep;
        }
        break;
    case 1: if (Elapsed >= 1.4) { PurchaseUpgrade(24); ++AudioCheckStep; } break;
    case 2: if (Elapsed >= 1.8) { ToggleTree(); ++AudioCheckStep; } break;
    case 3: if (Elapsed >= 2.2) { CancelAutopilot(); ++AudioCheckStep; } break;
    case 4: if (Elapsed >= 2.6) { ResumeAutopilot(); ++AudioCheckStep; } break;
    case 5: if (Elapsed >= 3.0) { if (PlayerOwner->SetPause(true)) OnPauseChanged(true); ++AudioCheckStep; } break;
    case 6: if (Elapsed >= 4.0) { AdjustStarBrightness(1); if (!bTreeOpen) ToggleTree(); ++AudioCheckStep; } break;
    case 7:
        if (Elapsed >= 4.2)
        {
            // Exercise the real manual purchase path while paused, including its UI sound.
            Progress.AddResources(100000000);
            Progress.AddOre(2000000, SpaceFlight::EOreKind::Blue);
            Progress.AddOre(2000000, SpaceFlight::EOreKind::Black);
            for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
                if (Progress.CanPurchase(I))
                {
                    PurchaseUpgrade(I);
                    UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK paused_purchase index=%d acquired=%d paused=%d"),
                        I, Progress.Has(I) ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
                    break;
                }
            ++AudioCheckStep;
        }
        break;
    case 8: if (Elapsed >= 4.5) { if (bTreeOpen) ToggleTree(); if (PlayerOwner->SetPause(false)) OnPauseChanged(false); ++AudioCheckStep; } break;
    case 9:
        if (Elapsed >= 5.0)
        {
            // Set up the maximum fire rate without playing dozens of upgrade cues at once.
            for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
                if (!Progress.Has(I)) Progress.Purchase(I);
            UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK maximum_upgrades cooldown=%.2f lasers=%d"),
                Progress.Cooldown(), Progress.Lasers());
            ResumeAutopilot();
            ++AudioCheckStep;
        }
        break;
    case 10: if (Elapsed >= 6.0) { StartAsteroidStress(); ++AudioCheckStep; } break;
    default: break;
    }
    if (!bAudioCheckRecorded && Elapsed >= 24.0)
    {
        bAudioCheckRecorded = true;
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile,
            TEXT("SpaceFlightAudioVariety"), TEXT(""));
        UE_LOG(LogTemp, Display, TEXT("AUDIO_CHECK recording finished"));
    }
}
#endif

void ASpaceFlightHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SaveProgress();
#if WITH_DEV_AUTOMATION_TESTS
    if (bFutureSaveEndPlayCheck)
        UE_LOG(LogTemp, Display, TEXT("FUTURE_SAVE_ENDPLAY blocked=%d enabled=%d revision=%lld"),
            bSaveBlocked ? 1 : 0, bSaveEnabled ? 1 : 0, SaveRevision);
#endif
    SaveMutex.Reset();
    if (EngineAudio) EngineAudio->Stop();
    Super::EndPlay(EndPlayReason);
}

void ASpaceFlightHUD::PlayEventSound(SpaceFlight::ESoundEvent Event, bool bInterface)
{
    const int32 Index = static_cast<int32>(Event);
    if (!GetWorld() || !EventSounds.IsValidIndex(Index) || !EventSounds[Index] ||
        !SoundGate.Allow(Event, FPlatformTime::Seconds())) return;
    const bool bCombat = Event == SpaceFlight::ESoundEvent::Laser || Event == SpaceFlight::ESoundEvent::Impact ||
        Event == SpaceFlight::ESoundEvent::ExplosionSmall || Event == SpaceFlight::ESoundEvent::ExplosionMedium ||
        Event == SpaceFlight::ESoundEvent::ExplosionLarge;
    // Six equally likely values keep both the initial three-way choice and
    // later two-way non-repeating choices unbiased.
    const int32 Variant = SoundVariety.Next(Event, FMath::RandHelper(6));
    const int32 Slot = SpaceFlight::CombatSoundSlot(Event);
    const int32 VariantIndex = Slot * 2 + Variant - 1;
    USoundBase* Sound = EventSounds[Index];
    if (Slot != INDEX_NONE && Variant > 0 && CombatSoundVariants.IsValidIndex(VariantIndex) && CombatSoundVariants[VariantIndex])
        Sound = CombatSoundVariants[VariantIndex];
    const float Pitch = bCombat ? FMath::FRandRange(0.96f, 1.04f) : 1.0f;
    UGameplayStatics::PlaySound2D(this, Sound, MasterSoundGain * SpaceFlight::EventGain(Event), Pitch, 0.0f, nullptr, this, bInterface);
    if (FParse::Param(FCommandLine::Get(), TEXT("SpaceAudioTrace")))
        UE_LOG(LogTemp, Display, TEXT("AUDIO_EVENT %s ui=%d variant=%d"), SpaceFlight::SoundName(Event), bInterface ? 1 : 0, Variant);
}

void ASpaceFlightGameMode::StartPlay()
{
    if (GEngine) GEngine->SetMaxFPS(60.0f);
    Super::StartPlay();
}

void ASpaceFlightPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Mode.SetHideCursorDuringCapture(false);
    SetInputMode(Mode);
}

void ASpaceFlightPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ASpaceFlightPlayerController::QuitFlight).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ASpaceFlightPlayerController::StartStress);
    InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ASpaceFlightPlayerController::CycleSector);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ASpaceFlightPlayerController::ToggleHUDDetails).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ASpaceFlightPlayerController::CycleLanguage).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ASpaceFlightPlayerController::SelectRefiningMode).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ASpaceFlightPlayerController::SelectProductionRecipe).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ASpaceFlightPlayerController::SelectSpecialization).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ASpaceFlightPlayerController::SelectExpedition).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ASpaceFlightPlayerController::CycleTargetingMode).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ASpaceFlightPlayerController::ApplyRefiningMode).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::A, IE_Pressed, this, &ASpaceFlightPlayerController::InterruptAutopilot).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::D, IE_Pressed, this, &ASpaceFlightPlayerController::InterruptAutopilot).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ASpaceFlightPlayerController::InterruptAutopilot).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ASpaceFlightPlayerController::InterruptAutopilot).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ASpaceFlightPlayerController::StartAutopilot);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ASpaceFlightPlayerController::TogglePause).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ASpaceFlightPlayerController::ToggleTree).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::J, IE_Pressed, this, &ASpaceFlightPlayerController::ToggleJournal).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ASpaceFlightPlayerController::TreeLeftDown).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ASpaceFlightPlayerController::TreeLeftUp).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ASpaceFlightPlayerController::TreeMiddleDown).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &ASpaceFlightPlayerController::TreeMiddleUp).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ASpaceFlightPlayerController::TreeRightDown).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ASpaceFlightPlayerController::TreeRightUp).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ASpaceFlightPlayerController::ZoomTreeIn).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ASpaceFlightPlayerController::ZoomTreeOut).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ASpaceFlightPlayerController::ResetTreeView).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ASpaceFlightPlayerController::FocusNextAvailableUpgrade).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ASpaceFlightPlayerController::ToggleTrackedUpgrade).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ASpaceFlightPlayerController::ToggleAutoPurchases).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::N, IE_Pressed, this, &ASpaceFlightPlayerController::ResetProgress).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ASpaceFlightPlayerController::DecreaseStarBrightness).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Subtract, IE_Pressed, this, &ASpaceFlightPlayerController::DecreaseStarBrightness).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ASpaceFlightPlayerController::IncreaseStarBrightness).bExecuteWhenPaused = true;
    InputComponent->BindKey(FInputChord(EKeys::Equals, true, false, false, false), IE_Pressed, this, &ASpaceFlightPlayerController::IncreaseStarBrightness).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Add, IE_Pressed, this, &ASpaceFlightPlayerController::IncreaseStarBrightness).bExecuteWhenPaused = true;
}

void ASpaceFlightPlayerController::InterruptAutopilot()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->CancelAutopilot();
}
void ASpaceFlightPlayerController::StartAutopilot()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ResumeAutopilot();
}
void ASpaceFlightPlayerController::CycleSector()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->CycleSector();
}
void ASpaceFlightPlayerController::ToggleHUDDetails()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ToggleHUDDetails();
}
void ASpaceFlightPlayerController::CycleLanguage()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->CycleLanguage();
}
void ASpaceFlightPlayerController::CycleTargetingMode()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->CycleTargetingMode();
}
void ASpaceFlightPlayerController::SelectRefiningMode()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->SelectRefiningMode();
}
void ASpaceFlightPlayerController::ApplyRefiningMode()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD()))
        if (!HUD->ApplyExpedition() && !HUD->ApplySpecialization() && !HUD->ApplyProductionRecipe()) HUD->ApplyRefiningMode();
}
void ASpaceFlightPlayerController::SelectProductionRecipe()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->SelectProductionRecipe();
}
void ASpaceFlightPlayerController::SelectSpecialization()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->SelectSpecialization();
}
void ASpaceFlightPlayerController::SelectExpedition()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->SelectExpedition();
}
void ASpaceFlightPlayerController::TogglePause()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD()))
        if (HUD->CancelExpeditionSelection() || HUD->CancelSpecializationSelection() || HUD->CancelProductionSelection() || HUD->CancelRefiningSelection()) return;
    const bool bNextPaused = !IsPaused();
    if (SetPause(bNextPaused))
        if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->OnPauseChanged(bNextPaused);
}
void ASpaceFlightPlayerController::ToggleTree()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ToggleTree();
}
void ASpaceFlightPlayerController::ToggleJournal()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ToggleJournal();
}
void ASpaceFlightPlayerController::ToggleAutoPurchases()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ToggleAutoPurchases();
}
void ASpaceFlightPlayerController::TreePointerDown(SpaceFlight::ETreePointerButton Button)
{
    float X = 0.0f, Y = 0.0f;
    if (GetMousePosition(X, Y))
        if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->BeginTreePointer(Button, FVector2D(X, Y));
}
void ASpaceFlightPlayerController::TreePointerUp(SpaceFlight::ETreePointerButton Button)
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD()))
    {
        float X = 0.0f, Y = 0.0f;
        if (GetMousePosition(X, Y)) HUD->EndTreePointer(Button, FVector2D(X, Y));
        else HUD->CancelTreePointer();
    }
}
void ASpaceFlightPlayerController::TreeLeftDown() { TreePointerDown(SpaceFlight::ETreePointerButton::Left); }
void ASpaceFlightPlayerController::TreeLeftUp() { TreePointerUp(SpaceFlight::ETreePointerButton::Left); }
void ASpaceFlightPlayerController::TreeMiddleDown() { TreePointerDown(SpaceFlight::ETreePointerButton::Middle); }
void ASpaceFlightPlayerController::TreeMiddleUp() { TreePointerUp(SpaceFlight::ETreePointerButton::Middle); }
void ASpaceFlightPlayerController::TreeRightDown() { TreePointerDown(SpaceFlight::ETreePointerButton::Right); }
void ASpaceFlightPlayerController::TreeRightUp() { TreePointerUp(SpaceFlight::ETreePointerButton::Right); }
void ASpaceFlightPlayerController::ZoomTreeIn()
{
    float X = 0, Y = 0;
    if (GetMousePosition(X, Y)) if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ZoomTree(1, FVector2D(X, Y));
}
void ASpaceFlightPlayerController::ZoomTreeOut()
{
    float X = 0, Y = 0;
    if (GetMousePosition(X, Y)) if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ZoomTree(-1, FVector2D(X, Y));
}
void ASpaceFlightPlayerController::ResetTreeView()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ResetTreeView();
}
void ASpaceFlightPlayerController::FocusNextAvailableUpgrade()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->FocusNextAvailableUpgrade();
}
void ASpaceFlightPlayerController::ToggleTrackedUpgrade()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->ToggleTrackedUpgrade();
}
void ASpaceFlightPlayerController::ResetProgress()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->RequestProgressReset();
}
void ASpaceFlightPlayerController::IncreaseStarBrightness()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->AdjustStarBrightness(1);
}
void ASpaceFlightPlayerController::DecreaseStarBrightness()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->AdjustStarBrightness(-1);
}

void ASpaceFlightHUD::AdjustStarBrightness(int32 Direction)
{
    const int32 Previous = StarBrightness.Value;
    StarBrightness.Adjust(Direction);
    if (Previous != StarBrightness.Value) PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
void ASpaceFlightHUD::ToggleHUDDetails()
{
    if (!GetWorld()) return;
    bDetailedHUD = !bDetailedHUD;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
    SaveProgress();
    if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("HUD_DETAILS enabled=%d paused=%d"), bDetailedHUD ? 1 : 0, GetWorld()->IsPaused() ? 1 : 0);
}
void ASpaceFlightHUD::CycleLanguage()
{
    SpaceFlight::SetLanguage(SpaceFlight::NextLanguage(SpaceFlight::GetLanguage()));
    if (bSaveEnabled)
    {
        const TCHAR* LanguageCode = SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::English ? TEXT("en") :
            SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::Spanish ? TEXT("es") : TEXT("ru");
        if (!FFileHelper::SaveStringToFile(LanguageCode, *(FPaths::ProjectSavedDir() / TEXT("SpaceFlightLanguage.txt"))))
            UE_LOG(LogTemp, Warning, TEXT("Could not save language preference"));
    }
}

void ASpaceFlightHUD::CycleTargetingMode()
{
    if (!GetWorld() || bTreeOpen || bStressMode || bRefiningSelection || bProductionSelection ||
        bSpecializationSelection || bExpeditionSelection) return;
    Progress.TargetingMode = (Progress.TargetingMode + 1) % 4;
    NextAutoUpgradeEvaluation = 0.0f;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
    UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Приоритет: %s"), TEXT("Priority: %s"), TEXT("Prioridad: %s"), TargetingModeName(Progress.TargetingMode));
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
    SaveProgress();
    if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("TARGETING_CHANGE mode=%d paused=%d"),
        Progress.TargetingMode, GetWorld()->IsPaused() ? 1 : 0);
}
void ASpaceFlightHUD::SelectRefiningMode()
{
    if (!GetWorld() || bTreeOpen) return;
    CancelExpeditionSelection();
    bProductionSelection = false;
    bSpecializationSelection = false;
    SelectedRefiningMode = ((bRefiningSelection ? SelectedRefiningMode : Progress.RefiningMode) + 1) % 6;
    bRefiningSelection = true;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
void ASpaceFlightHUD::ApplyRefiningMode()
{
    if (!GetWorld() || bTreeOpen || !bRefiningSelection) return;
    bRefiningSelection = false;
    const int64 IsotopeBefore = Progress.Isotope;
    if (Progress.SetRefiningMode(SelectedRefiningMode))
    {
        PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
        if (Progress.Isotope > IsotopeBefore) PlayEventSound(SpaceFlight::ESoundEvent::Isotope, true);
        UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Переработка: %s"), TEXT("Refining: %s"), TEXT("Refinado: %s"), RefiningModeName(Progress.RefiningMode));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
        SaveProgress();
        if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("REFINING_CHANGE mode=%d blue=%lld black=%lld isotope=%lld quota_blue=%lld quota_black=%lld"),
            Progress.RefiningMode, Progress.BlueOre, Progress.BlackOre, Progress.Isotope, Progress.BlueRefiningQuota, Progress.BlackRefiningQuota);
    }
}
bool ASpaceFlightHUD::CancelRefiningSelection()
{
    if (!bRefiningSelection) return false;
    bRefiningSelection = false;
    return true;
}
void ASpaceFlightHUD::SelectProductionRecipe()
{
    if (!GetWorld() || bTreeOpen) return;
    CancelExpeditionSelection();
    bRefiningSelection = false;
    bSpecializationSelection = false;
    SelectedProductionKind = !bProductionSelection || SelectedProductionKind == SpaceFlight::EOreKind::Black ?
        SpaceFlight::EOreKind::Blue : SpaceFlight::EOreKind::Black;
    bProductionSelection = true;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
bool ASpaceFlightHUD::ApplyProductionRecipe()
{
    if (!bProductionSelection) return false;
    if (!GetWorld() || bTreeOpen) return true;
    const int64 IsotopeBefore = Progress.Isotope;
    if (Progress.ProduceRareOre(SelectedProductionKind, TotalFlightSeconds()))
    {
        bProductionSelection = false;
        PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
        if (Progress.Isotope > IsotopeBefore) PlayEventSound(SpaceFlight::ESoundEvent::Isotope, true);
        UpgradeNotice = Progress.Isotope > IsotopeBefore ? SpaceFlight::Localize(TEXT("Руда переработана в изотоп")) :
            (SelectedProductionKind == SpaceFlight::EOreKind::Blue ? SpaceFlight::Localize(TEXT("Получена синяя руда")) : SpaceFlight::Localize(TEXT("Получена чёрная руда")));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
        SaveProgress();
        if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("PRODUCTION_ORDER kind=%d common=%lld ready=%.3f isotope=%lld"),
            static_cast<int32>(SelectedProductionKind), Progress.CommonOre(), Progress.ProductionReadyAt, Progress.Isotope);
    }
    else PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
    return true;
}
bool ASpaceFlightHUD::CancelProductionSelection()
{
    if (!bProductionSelection) return false;
    bProductionSelection = false;
    return true;
}
void ASpaceFlightHUD::SelectSpecialization()
{
    if (!GetWorld() || bTreeOpen) return;
    CancelExpeditionSelection();
    bRefiningSelection = false;
    bProductionSelection = false;
    SelectedSpecialization = ((bSpecializationSelection ? SelectedSpecialization : Progress.Specialization) + 1) % 4;
    bSpecializationSelection = true;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
bool ASpaceFlightHUD::ApplySpecialization()
{
    if (!bSpecializationSelection) return false;
    if (!GetWorld() || bTreeOpen) return true;
    if (SelectedSpecialization == Progress.Specialization)
    {
        bSpecializationSelection = false;
        return true;
    }
    const int32 PreviousGoalKind = Progress.Objective.Kind;
    if (Progress.SetSpecialization(SelectedSpecialization, TotalFlightSeconds()))
    {
        bSpecializationSelection = false;
        PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
        UpgradeNotice = PreviousGoalKind == 4 && Progress.Objective.Kind == 3 ?
            SpaceFlight::Localize(TEXT("Цель изменена: добыть руду")) : SF_LOCAL_PRINTF(TEXT("Профиль: %s"), TEXT("Profile: %s"), TEXT("Perfil: %s"), SpecializationName(Progress.Specialization));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
        NextAutoUpgradeEvaluation = 0.0f;
        SaveProgress();
        if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("SPECIALIZATION_CHANGE mode=%d ready=%.3f goal=%d"),
            Progress.Specialization, Progress.SpecializationReadyAt, Progress.Objective.Kind);
    }
    else PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
    return true;
}
bool ASpaceFlightHUD::CancelSpecializationSelection()
{
    if (!bSpecializationSelection) return false;
    bSpecializationSelection = false;
    return true;
}
FString ASpaceFlightHUD::ExpeditionTitle() const
{
    const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("СВОБОДНЫЙ РЕЙС")), SpaceFlight::Localize(TEXT("СИНЯЯ РОССЫПЬ")), SpaceFlight::Localize(TEXT("АЛМАЗНЫЙ ПОТОК")) };
    const int32 Route = Progress.Expedition.State == SpaceFlight::EExpeditionState::Idle ? SelectedExpeditionRoute : Progress.Expedition.Route;
    return SF_LOCAL_PRINTF(TEXT("РЕЙС: %s"), TEXT("EXPEDITION: %s"), TEXT("EXPEDICIÓN: %s"), Names[FMath::Clamp(Route, 0, 2)]);
}
FString ASpaceFlightHUD::ExpeditionDetail() const
{
    const auto& E = Progress.Expedition;
    const int32 Target = E.State == SpaceFlight::EExpeditionState::Idle ?
        static_cast<int32>(FMath::Clamp<int64>(100 + Progress.TotalCollected / 20, 100, 10000)) : E.Target;
    const int32 Route = E.State == SpaceFlight::EExpeditionState::Idle ? SelectedExpeditionRoute : E.Route;
    const TCHAR* Requirements[] = { SpaceFlight::Localize(TEXT("ЛЮБАЯ РУДА")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА В СИНЕМ СЕКТОРЕ")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА В АЛМАЗНОМ СЕКТОРЕ")) };
    return FString::Printf(TEXT("%s: %d/%d"), Requirements[FMath::Clamp(Route, 0, 2)],
        E.State == SpaceFlight::EExpeditionState::Idle ? 0 : E.Progress, Target);
}
FString ASpaceFlightHUD::ExpeditionRewardDetail() const
{
    const auto& E = Progress.Expedition;
    const int32 Target = E.State == SpaceFlight::EExpeditionState::Idle ?
        static_cast<int32>(FMath::Clamp<int64>(100 + Progress.TotalCollected / 20, 100, 10000)) : E.Target;
    const int32 Route = E.State == SpaceFlight::EExpeditionState::Idle ? SelectedExpeditionRoute : E.Route;
    const int32 OreCode = SpaceFlight::FExpedition::RewardOreCode(Route);
    const TCHAR* OreNames[] = { SpaceFlight::Localize(TEXT("ОБЫЧНОЙ")), SpaceFlight::Localize(TEXT("СИНЕЙ")), SpaceFlight::Localize(TEXT("ЧЁРНОЙ")) };
    if (OreCode == INDEX_NONE) return FString();
    return SF_LOCAL_PRINTF(TEXT("НАГРАДА: %d %s РУДЫ%s"), TEXT("REWARD: %d %s ORE%s"), TEXT("RECOMPENSA: %d MINERAL %s%s"), SpaceFlight::FExpedition::RewardAmount(Route, Target), OreNames[OreCode],
        OreCode != 0 && Progress.RefiningMode != 1 ? SpaceFlight::Localize(TEXT("  ·  ВОЗМОЖЕН СИНТЕЗ")) : TEXT(""));
}
FString ASpaceFlightHUD::ExpeditionHint() const
{
    switch (Progress.Expedition.State)
    {
    case SpaceFlight::EExpeditionState::Idle: return SpaceFlight::Localize(TEXT("E — МАРШРУТ  ·  ENTER — НАЧАТЬ  ·  ESC — ЗАКРЫТЬ"));
    case SpaceFlight::EExpeditionState::Active: return SpaceFlight::Localize(TEXT("ENTER — ОТМЕНИТЬ РЕЙС  ·  ESC — ЗАКРЫТЬ"));
    default: return SpaceFlight::Localize(TEXT("ENTER — ПОЛУЧИТЬ НАГРАДУ  ·  ESC — ЗАКРЫТЬ"));
    }
}
void ASpaceFlightHUD::SelectExpedition()
{
    if (!GetWorld() || bTreeOpen || bStressMode) return;
    bRefiningSelection = bProductionSelection = bSpecializationSelection = false;
    if (Progress.Expedition.State == SpaceFlight::EExpeditionState::Idle)
        SelectedExpeditionRoute = bExpeditionSelection ? (SelectedExpeditionRoute + 1) % (SpaceFlight::UnlockedSector(Progress.Objective.Sequence) + 1) : 0;
    bExpeditionSelection = true;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
bool ASpaceFlightHUD::CancelExpeditionSelection()
{
    if (!bExpeditionSelection) return false;
    bExpeditionSelection = false;
    ExpeditionError.Reset();
    return true;
}
bool ASpaceFlightHUD::ApplyExpedition()
{
    if (!bExpeditionSelection) return false;
    ExpeditionError.Reset();
    if (!GetWorld() || bTreeOpen || bStressMode) return true;
    if (bSaveBlocked)
    {
        ExpeditionError = SpaceFlight::Localize(TEXT("СОХРАНЕНИЕ НЕДОСТУПНО"));
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
        return true;
    }
    const SpaceFlight::FProgress Previous = Progress;
    const int64 PreviousRevision = SaveRevision;
    FString Message;
    auto& E = Progress.Expedition;
    if (E.State == SpaceFlight::EExpeditionState::Idle)
    {
        const int32 Target = static_cast<int32>(FMath::Clamp<int64>(100 + Progress.TotalCollected / 20, 100, 10000));
        if (!E.Start(SelectedExpeditionRoute, Target, SpaceFlight::UnlockedSector(Progress.Objective.Sequence))) return true;
        Message = SpaceFlight::Localize(TEXT("Рейс начат — E: прогресс"));
    }
    else if (E.State == SpaceFlight::EExpeditionState::Active)
    {
        if (!E.Cancel()) return true;
        Message = SpaceFlight::Localize(TEXT("Рейс отменён"));
    }
    else
    {
        const int32 OreCode = SpaceFlight::FExpedition::RewardOreCode(E.Route);
        const int64 IsotopeBefore = Progress.Isotope;
        const int32 Reward = Progress.ClaimExpeditionReward();
        if (Reward <= 0)
        {
            Progress = Previous;
            ExpeditionError = SpaceFlight::Localize(TEXT("НАГРАДА НЕДОСТУПНА: ПРЕДЕЛ РУДЫ"));
            PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
            return true;
        }
        const TCHAR* OreNames[] = { SpaceFlight::Localize(TEXT("ОБЫЧНОЙ")), SpaceFlight::Localize(TEXT("СИНЕЙ")), SpaceFlight::Localize(TEXT("ЧЁРНОЙ")) };
        Message = SF_LOCAL_PRINTF(TEXT("Рейс: +%d %s руды%s"), TEXT("Expedition: +%d %s ore%s"), TEXT("Expedición: +%d mineral %s%s"), Reward, OreNames[FMath::Clamp(OreCode, 0, 2)],
            Progress.Isotope > IsotopeBefore ? SpaceFlight::Localize(TEXT(" · синтез")) : TEXT(""));
    }
    SaveProgress();
    if (bSaveEnabled && SaveRevision == PreviousRevision)
    {
        Progress = Previous;
        ExpeditionError = SpaceFlight::Localize(TEXT("РЕЙС НЕ ИЗМЕНЁН: ОШИБКА СОХРАНЕНИЯ"));
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
        return true;
    }
    else bExpeditionSelection = false;
    UpgradeNotice = Message;
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
    return true;
}
void ASpaceFlightHUD::CycleSector()
{
    if (!GetWorld() || GetWorld()->IsPaused() || bTreeOpen || bStressMode) return;
    const float Now = TotalFlightSeconds();
    if (SpaceFlight::UnlockedSector(Progress.Objective.Sequence) == 0)
        UpgradeNotice = SpaceFlight::Localize(TEXT("Новый сектор: выполните 3 цели полёта"));
    else if (Now < Progress.SectorReadyAt)
        UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Переход доступен через %d сек."), TEXT("The transition is available in %d seconds."), TEXT("Cambio disponible en %d s."), FMath::CeilToInt(Progress.SectorReadyAt - Now));
    else if (!FMath::IsFinite(Now + 12.0f) || Now + 12.0f <= Now)
        UpgradeNotice = SpaceFlight::Localize(TEXT("Переход недоступен: предел игрового времени"));
    else
    {
        Progress.CurrentSector = SpaceFlight::NextSector(Progress.CurrentSector, Progress.Objective.Sequence);
        Progress.SectorReadyAt = Now + 12.0f;
        BaseResetTimes.Reset();
        BaseGenerations.Reset();
        AsteroidDurability.Reset();
        DustBursts.Reset();
        Impacts.Reset();
        LaserBeams.Reset();
        NextAutoUpgradeEvaluation = 0.0f;
        const TCHAR* Names[] = { SpaceFlight::Localize(TEXT("Рубеж")), SpaceFlight::Localize(TEXT("Синяя россыпь")), SpaceFlight::Localize(TEXT("Алмазный поток")) };
        UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Сектор: %s"), TEXT("Sector: %s"), TEXT("Sector: %s"), Names[Progress.CurrentSector]);
        PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
        SaveProgress();
        if (bFlightTrace) UE_LOG(LogTemp, Display, TEXT("SECTOR_CHANGE sector=%d seed=%d"), Progress.CurrentSector, Progress.WorldSeed);
    }
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
}
void ASpaceFlightHUD::CancelAutopilot()
{
    if (bAutopilot && !bTreeOpen && GetWorld() && !GetWorld()->IsPaused())
    { bAutopilot = false; PlayEventSound(SpaceFlight::ESoundEvent::ModeOff, true); }
}
void ASpaceFlightHUD::ResumeAutopilot()
{
    if (!GetWorld()->IsPaused() && !bAutopilot)
    { bAutopilot = true; NextAutoUpgradeEvaluation = 0.0f; PlayEventSound(SpaceFlight::ESoundEvent::ModeOn, true); }
}
void ASpaceFlightHUD::ToggleTree()
{
    bExpeditionSelection = false;
    ExpeditionError.Reset();
    bRefiningSelection = false;
    bProductionSelection = false;
    bSpecializationSelection = false;
    bTreeOpen = !bTreeOpen;
    bJournalOpen = false;
    TreePointer.Cancel();
    bTreeKeyboardFocusActive = false;
    if (bTreeOpen) TreeNavigationCursor = INDEX_NONE;
    else if (GetWorld() && !GetWorld()->IsPaused() && PlayerOwner &&
        (PlayerOwner->IsInputKeyDown(EKeys::A) || PlayerOwner->IsInputKeyDown(EKeys::D) ||
         PlayerOwner->IsInputKeyDown(EKeys::Left) || PlayerOwner->IsInputKeyDown(EKeys::Right)))
        CancelAutopilot();
    PlayEventSound(bTreeOpen ? SpaceFlight::ESoundEvent::TreeOpen : SpaceFlight::ESoundEvent::TreeClose, true);
}
void ASpaceFlightHUD::ToggleJournal()
{
    if (!bTreeOpen) return;
    bJournalOpen = !bJournalOpen;
    TreePointer.Cancel();
    bTreeKeyboardFocusActive = false;
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
void ASpaceFlightHUD::ToggleAutoPurchases()
{
    if (!bTreeOpen || bJournalOpen || !GetWorld()) return;
    TreePointer.Cancel();
    Progress.bAutoPurchasesEnabled = !Progress.bAutoPurchasesEnabled;
    NextAutoUpgradeEvaluation = 0.0f;
    SaveProgress();
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
void ASpaceFlightHUD::ZoomTree(float Direction, FVector2D Cursor)
{
    if (!bTreeOpen || bJournalOpen) return;
    const float NewZoom = FMath::Clamp(TreeZoom * (Direction > 0 ? 1.15f : 1.0f / 1.15f),
        SpaceFlight::MinTreeZoom, SpaceFlight::MaxTreeZoom);
    TreePan = SpaceFlight::PanForTreeZoom(Cursor, ViewportWidth, ViewportHeight, TreePan, TreeZoom, NewZoom);
    TreeZoom = NewZoom;
}
void ASpaceFlightHUD::ResetTreeView()
{
    if (bTreeOpen && bJournalOpen) { ReplaySurprise(); return; }
    if (!bTreeOpen) return;
    TreeZoom = SpaceFlight::DefaultTreeZoom;
    TreePan = FVector2D::ZeroVector;
    TreePointer.Cancel();
    bTreeKeyboardFocusActive = false;
    TreeNavigationCursor = INDEX_NONE;
}
void ASpaceFlightHUD::FocusNextAvailableUpgrade()
{
    if (!bTreeOpen || bJournalOpen || !GetWorld()) return;
    TreePointer.Cancel();
    const auto Next = SpaceFlight::NextAffordableUpgrade(Progress, TreeNavigationCursor);
    bTreeKeyboardFocusActive = true;
    TreeKeyboardFocus = Next.Index;
    float X = 0.0f, Y = 0.0f;
    bTreeFocusHasMousePosition = PlayerOwner && PlayerOwner->GetMousePosition(X, Y);
    TreeFocusMousePosition = FVector2D(X, Y);
    if ((Next.State == SpaceFlight::EUpgradeNavigationState::Available ||
         Next.State == SpaceFlight::EUpgradeNavigationState::NeedsResources) && Next.Index != INDEX_NONE)
    {
        TreeNavigationCursor = Next.Index;
        TreePan = -SpaceFlight::UpgradeTreePosition(Next.Index) * TreeZoom;
        UpgradeNotice.Reset();
    }
    else
    {
        UpgradeNotice = Next.State == SpaceFlight::EUpgradeNavigationState::Complete ?
            SpaceFlight::Localize(TEXT("Все улучшения изучены")) : SpaceFlight::Localize(TEXT("Сейчас нет доступных улучшений"));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
    }
}
int32 ASpaceFlightHUD::ResolveTreeHoveredUpgrade()
{
    float MouseX = 0.0f, MouseY = 0.0f;
    const bool bHasMouse = PlayerOwner && PlayerOwner->GetMousePosition(MouseX, MouseY);
    if (bTreeKeyboardFocusActive && bHasMouse && bTreeFocusHasMousePosition &&
        FVector2D::DistSquared(FVector2D(MouseX, MouseY), TreeFocusMousePosition) > 4.0f)
        bTreeKeyboardFocusActive = false;
    if (bTreeKeyboardFocusActive && bHasMouse && !bTreeFocusHasMousePosition)
    {
        bTreeFocusHasMousePosition = true;
        TreeFocusMousePosition = FVector2D(MouseX, MouseY);
    }
    if (bTreeKeyboardFocusActive && TreeKeyboardFocus != INDEX_NONE && !SpaceFlight::IsUpgradeEligible(Progress, TreeKeyboardFocus))
        bTreeKeyboardFocusActive = false;
    int32 Hovered = bTreeKeyboardFocusActive ? TreeKeyboardFocus :
        (bHasMouse ? SpaceFlight::UpgradeAtScreenPosition(FVector2D(MouseX, MouseY), ViewportWidth, ViewportHeight, TreeZoom, TreePan) : INDEX_NONE);
#if WITH_DEV_AUTOMATION_TESTS
    if (!bSaveEnabled) FParse::Value(FCommandLine::Get(), TEXT("SpaceTreePreviewHover="), Hovered);
#endif
    return Hovered >= 0 && Hovered < SpaceFlight::UpgradeCount ? Hovered : INDEX_NONE;
}
void ASpaceFlightHUD::ToggleTrackedUpgrade()
{
    if (!GetWorld() || !bTreeOpen || bJournalOpen) return;
    const int32 Index = ResolveTreeHoveredUpgrade();
    TreePointer.Cancel();
    if (Index == INDEX_NONE || Progress.Has(Index))
    {
        UpgradeNotice = SpaceFlight::Localize(TEXT("Выберите неизученное улучшение"));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
        return;
    }
    const bool bRemoving = Progress.TrackedUpgrade == Index;
    Progress.TrackedUpgrade = bRemoving ? INDEX_NONE : Index;
    UpgradeNotice = bRemoving ? SpaceFlight::Localize(TEXT("Цель развития снята")) : SF_LOCAL_PRINTF(TEXT("Цель развития: %s"), TEXT("Development goal: %s"), TEXT("Objetivo de desarrollo: %s"), SpaceFlight::Upgrade(Index).Name);
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
    SaveProgress();
    PlayEventSound(SpaceFlight::ESoundEvent::UiTick, true);
}
void ASpaceFlightHUD::OnPauseChanged(bool bPaused)
{
    if (EngineAudio) EngineAudio->SetPaused(bPaused);
    PlayEventSound(bPaused ? SpaceFlight::ESoundEvent::Pause : SpaceFlight::ESoundEvent::Resume, true);
    if (bPaused)
    {
        PauseStartedAt = FPlatformTime::Seconds();
    }
    else if (PauseStartedAt > 0.0)
    {
        const double PauseDuration = FPlatformTime::Seconds() - PauseStartedAt;
        if (bStressMode) StressStartTime += PauseDuration;
        if (bBenchmark) { BenchmarkWindowStart += PauseDuration; BenchmarkLastFrameTime += PauseDuration; }
        PauseStartedAt = 0.0;
    }
    if (bPaused) SaveProgress();
    else
    {
        ResetConfirmUntil = 0.0f;
        if (GetWorld() && !GetWorld()->IsPaused() && !bTreeOpen && PlayerOwner &&
            (PlayerOwner->IsInputKeyDown(EKeys::A) || PlayerOwner->IsInputKeyDown(EKeys::D) ||
             PlayerOwner->IsInputKeyDown(EKeys::Left) || PlayerOwner->IsInputKeyDown(EKeys::Right)))
            CancelAutopilot();
    }
}
void ASpaceFlightHUD::PurchaseUpgrade(int32 Index, bool bAutomatic)
{
    if ((!bAutomatic && (!bTreeOpen || bJournalOpen)) || (bAutomatic && (!bAutopilot || !Progress.bAutoPurchasesEnabled || GetWorld()->IsPaused())) ||
        Index < 0 || Index >= SpaceFlight::UpgradeCount) return;
    const auto& Node = SpaceFlight::Upgrade(Index);
    const int64 IsotopeBefore = Progress.Isotope;
    const bool bPurchased = Progress.Purchase(Index);
    if (bPurchased)
    {
        PlayEventSound(SpaceFlight::ESoundEvent::Upgrade, !bAutomatic);
        if (Progress.Isotope > IsotopeBefore) PlayEventSound(SpaceFlight::ESoundEvent::Isotope, !bAutomatic);
        UpgradeFlashStart = GetWorld()->GetTimeSeconds();
        UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Улучшение получено: %s"), TEXT("Improvement received: %s"), TEXT("Mejora recibida: %s"), Node.Name);
        UE_LOG(LogTemp, Verbose, TEXT("Upgrade purchased index=%d ore=%lld lasers=%d range=%.0f"),
            Index, Progress.Resources, Progress.Lasers(), Progress.Range());
        SaveProgress();
    }
    else if (Progress.Has(Index)) UpgradeNotice = SpaceFlight::Localize(TEXT("Это улучшение уже получено"));
    else if (Node.Parent >= 0 && !Progress.Has(Node.Parent)) UpgradeNotice = SpaceFlight::Localize(TEXT("Сначала откройте предыдущий узел ветки"));
    else UpgradeNotice = Node.bMicro ? SpaceFlight::Localize(TEXT("Недостаточно изотопа")) : SpaceFlight::Localize(TEXT("Недостаточно руды"));
    if (!bAutomatic && !bPurchased)
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 2.5f;
}
void ASpaceFlightHUD::BeginTreePointer(SpaceFlight::ETreePointerButton Button, FVector2D Point)
{
    if (!bTreeOpen || bJournalOpen) return;
    bTreeKeyboardFocusActive = false;
    TreePointer.Begin(Button, Point, ViewportWidth, ViewportHeight, TreeZoom, TreePan);
}
void ASpaceFlightHUD::EndTreePointer(SpaceFlight::ETreePointerButton Button, FVector2D Point)
{
    if (!bTreeOpen || bJournalOpen) { TreePointer.Cancel(); return; }
    const int32 Index = TreePointer.End(Button, Point, ViewportWidth, ViewportHeight, TreeZoom, TreePan);
    if (Index != INDEX_NONE) PurchaseUpgrade(Index);
}
void ASpaceFlightHUD::CancelTreePointer() { TreePointer.Cancel(); }

void ASpaceFlightPlayerController::StartStress()
{
    if (ASpaceFlightHUD* FlightHUD = Cast<ASpaceFlightHUD>(GetHUD()))
        FlightHUD->StartAsteroidStress();
}

void ASpaceFlightHUD::StartAsteroidStress()
{
    if (bStressMode || !GetWorld()) return;
    SaveProgress();
    if (!SpaceFlight::CanEnterStressMode(bSaveEnabled, bSaveBlocked, bSaveWriteFailed))
    {
        UpgradeNotice = SpaceFlight::Localize(TEXT("Не удалось сохранить: нагрузочный режим не запущен"));
        NoticeUntil = GetWorld()->GetRealTimeSeconds() + 4.0f;
        PlayEventSound(SpaceFlight::ESoundEvent::Denied, true);
        return;
    }
    bStressMode = true;
    bExpeditionSelection = false;
    ExpeditionError.Reset();
    PlayEventSound(SpaceFlight::ESoundEvent::Stress, true);
    StressStartTime = FPlatformTime::Seconds();
    BenchmarkWindowStart = StressStartTime;
    BenchmarkLastFrameTime = StressStartTime;
    CreateStressRockTextures();
    UE_LOG(LogTemp, Display, TEXT("Asteroid stress mode activated"));
}

void ASpaceFlightHUD::CreateStressRockTextures()
{
    if (!StressRockTextures.IsEmpty()) return;
    static const FLinearColor RockColors[] = {
        FLinearColor(0.60f, 0.27f, 0.11f), FLinearColor(0.65f, 0.39f, 0.17f),
        FLinearColor(0.50f, 0.20f, 0.12f)
    };
    constexpr int32 TextureSize = 32;
    for (int32 Variant = 0; Variant < 42; ++Variant)
    {
        TArray<FColor> Pixels;
        Pixels.SetNumZeroed(TextureSize * TextureSize);
        const int32 ShapeVariant = Variant % 21;
        const bool bCrystalRock = Variant >= 21;
        const float Phase = ShapeVariant * 1.731f;
        for (int32 Row = 0; Row < TextureSize; ++Row)
        {
            for (int32 Col = 0; Col < TextureSize; ++Col)
            {
                const float X = (Col + 0.5f - 16.0f) / 16.0f;
                const float Y = (Row + 0.5f - 16.0f) / 16.0f;
                const float Theta = FMath::Atan2(Y, X);
                const float Boundary = 0.82f + 0.10f * FMath::Sin(3.0f * Theta + Phase)
                    + 0.07f * FMath::Cos(5.0f * Theta - Phase * 0.7f);
                const float Distance = FMath::Sqrt(X * X + Y * Y);
                if (Distance > Boundary) continue;
                float Shade = 0.25f + 0.10f * (-X - Y);
                if (Distance > Boundary - 0.12f) Shade *= 0.62f;
                if (FMath::Square(X + 0.24f) + FMath::Square(Y + 0.15f) < 0.035f ||
                    FMath::Square(X - 0.32f) + FMath::Square(Y - 0.28f) < 0.018f) Shade *= 0.58f;
                if (((Col * 17 + Row * 31 + Variant * 13) & 7) == 0) Shade *= 1.25f;
                FLinearColor Color = bCrystalRock
                    ? CrystalRockColor(X, Y, Distance, Boundary, Shade, 0.5f, FLinearColor(0.15f, 0.68f, 0.90f))
                    : RockColors[Variant % 3] * (Shade / 0.25f);
                Color.A = 1.0f;
                Pixels[Row * TextureSize + Col] = Color.ToFColorSRGB();
            }
        }
        const TConstArrayView64<uint8> Data(reinterpret_cast<const uint8*>(Pixels.GetData()),
            Pixels.Num() * sizeof(FColor));
        UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize, TextureSize, PF_B8G8R8A8, NAME_None, Data);
        if (!Texture) continue;
        Texture->Filter = TF_Nearest;
        Texture->NeverStream = true;
        Texture->UpdateResource();
        StressRockTextures.Add(Texture);
    }
}

void ASpaceFlightHUD::CreateCrystalGlowTexture()
{
    if (CrystalGlowTexture) return;
    constexpr int32 Size = 64;
    TArray<FColor> Pixels;
    Pixels.Reserve(Size * Size);
    for (int32 Row = 0; Row < Size; ++Row)
        for (int32 Col = 0; Col < Size; ++Col)
        {
            const float X = (Col + 0.5f - Size * 0.5f) / (Size * 0.5f);
            const float Y = (Row + 0.5f - Size * 0.5f) / (Size * 0.5f);
            const uint8 Alpha = static_cast<uint8>(FMath::RoundToInt(255.0f * SpaceFlight::CrystalGlowMask(X, Y)));
            Pixels.Add(FColor(255, 255, 255, Alpha));
        }
    const TConstArrayView64<uint8> Data(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor));
    CrystalGlowTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, Data);
    if (CrystalGlowTexture)
    {
        CrystalGlowTexture->Filter = TF_Bilinear;
        CrystalGlowTexture->NeverStream = true;
        CrystalGlowTexture->UpdateResource();
    }
}

void ASpaceFlightHUD::CreateBlueAsteroidAtlases()
{
    if (!BlueAsteroidAtlases.IsEmpty()) return;
    const double StartTime = FPlatformTime::Seconds();
    constexpr int32 FrameSize = SpaceFlight::BlueAsteroidFrameSize;
    constexpr int32 Columns = SpaceFlight::BlueAsteroidAtlasColumns;
    constexpr int32 AtlasWidth = FrameSize * Columns;
    constexpr int32 AtlasHeight = FrameSize * (SpaceFlight::BlueAsteroidFrames / Columns);
    for (int32 Variant = 0; Variant < SpaceFlight::BlueAsteroidVariants; ++Variant)
    {
        for (int32 Damage = 0; Damage < SpaceFlight::BlueAsteroidDamageStates; ++Damage)
        {
            TArray<FColor> Pixels;
            Pixels.SetNumZeroed(AtlasWidth * AtlasHeight);
            for (int32 Frame = 0; Frame < SpaceFlight::BlueAsteroidFrames; ++Frame)
            {
                const float Angle = 2.0f * PI * Frame / SpaceFlight::BlueAsteroidFrames;
                const int32 OriginX = (Frame % Columns) * FrameSize;
                const int32 OriginY = (Frame / Columns) * FrameSize;
                for (int32 Row = 0; Row < FrameSize; ++Row)
                    for (int32 Col = 0; Col < FrameSize; ++Col)
                    {
                        const float X = (Col + 0.5f - FrameSize * 0.5f) / (FrameSize * 0.5f);
                        const float Y = (Row + 0.5f - FrameSize * 0.5f) / (FrameSize * 0.5f);
                        Pixels[(OriginY + Row) * AtlasWidth + OriginX + Col] =
                            SpaceFlight::SampleBlueAsteroid(X, Y, Angle, Variant, Damage);
                    }
            }
            const TConstArrayView64<uint8> Data(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor));
            UTexture2D* Texture = UTexture2D::CreateTransient(AtlasWidth, AtlasHeight, PF_B8G8R8A8, NAME_None, Data);
            if (Texture)
            {
                Texture->Filter = TF_Nearest;
                Texture->NeverStream = true;
                Texture->UpdateResource();
            }
            // Keep indices stable even if texture creation fails; the draw path has a fallback.
            BlueAsteroidAtlases.Add(Texture);
        }
    }
    if (bFlightTrace)
        UE_LOG(LogTemp, Display, TEXT("Blue asteroid sprites: %d atlases in %.0f ms"),
            BlueAsteroidAtlases.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void ASpaceFlightPlayerController::QuitFlight()
{
    if (ASpaceFlightHUD* HUD = Cast<ASpaceFlightHUD>(GetHUD())) HUD->SaveProgress();
    UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ASpaceFlightHUD::DrawHUD()
{
    Super::DrawHUD();
#if WITH_DEV_AUTOMATION_TESTS
    TickAudioCheck();
    TickSaveResetCheck();
    TickStressSaveCheck();
    TickSectorCheck();
    TickRefiningCheck();
    TickProductionCheck();
    TickSpecializationCheck();
    TickHUDCheck();
    TickJournalCheck();
#endif
    if (!Canvas || !GetWorld()) return;

    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;
    ViewportWidth = W;
    ViewportHeight = H;
    const float T = GetWorld()->GetTimeSeconds();
    if (bSaveEnabled && !bStressMode && !bSaveBlocked && T >= NextSaveTime)
    {
        SaveProgress();
        NextSaveTime = T + 15.0f;
    }
    if (!bSpawnSeedInitialized)
    {
        bSpawnSeedInitialized = true;
        bBenchmark = FParse::Param(FCommandLine::Get(), TEXT("SpaceStressBenchmark"));
        bFpsCheck = FParse::Param(FCommandLine::Get(), TEXT("SpaceFpsCheck"));
        bFlightTrace = FParse::Param(FCommandLine::Get(), TEXT("SpaceFlightTrace"));
        bTreeOpen = FParse::Param(FCommandLine::Get(), TEXT("SpaceShowTree"));
#if WITH_DEV_AUTOMATION_TESTS
        bFrameProfile = !bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceFrameProfile"));
        if (bFrameProfile) FrameProfileStart = FPlatformTime::Seconds() + 3.0;
        if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceShowJournal")))
        {
            bTreeOpen = true;
            bJournalOpen = true;
        }
        if (!bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceSurpriseReplayPreview")))
        {
            bTreeOpen = true;
            bJournalOpen = true;
            bSurprisePlaying = true;
            bSurpriseReplay = true;
            SurpriseAge = 2.0f;
            SurpriseLastRealTime = GetWorld()->GetRealTimeSeconds();
        }
        if (!bSaveEnabled)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("SpaceHUDPausedPreview")) && PlayerOwner && PlayerOwner->SetPause(true))
                OnPauseChanged(true);
            float PreviewZoom = 0.0f, PreviewPanX = 0.0f, PreviewPanY = 0.0f;
            if (FParse::Value(FCommandLine::Get(), TEXT("SpaceTreePreviewZoom="), PreviewZoom))
                TreeZoom = FMath::Clamp(PreviewZoom, SpaceFlight::MinTreeZoom, SpaceFlight::MaxTreeZoom);
            if (FParse::Value(FCommandLine::Get(), TEXT("SpaceTreePreviewPanX="), PreviewPanX)) TreePan.X = PreviewPanX;
            if (FParse::Value(FCommandLine::Get(), TEXT("SpaceTreePreviewPanY="), PreviewPanY)) TreePan.Y = PreviewPanY;
            int32 TrackedPreview = INDEX_NONE;
            if (FParse::Value(FCommandLine::Get(), TEXT("SpaceTrackedPreview="), TrackedPreview) &&
                TrackedPreview >= 0 && TrackedPreview < SpaceFlight::UpgradeCount && !Progress.Has(TrackedPreview))
            {
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedPreviewParent")) && TrackedPreview == 20)
                {
                    Progress.AddResources(SpaceFlight::Upgrade(0).Cost);
                    Progress.Purchase(0);
                }
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedPreviewRefiningOff"))) Progress.SetRefiningMode(1);
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedPreviewReady"))) Progress.AddResources(1000);
                Progress.TrackedUpgrade = TrackedPreview;
                bAutopilot = false;
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedInteractionCheck")) && PlayerOwner)
            {
                Progress.AddResources(1000);
                bAutopilot = false;
                bTreeOpen = false;
                ToggleTrackedUpgrade();
                const bool bClosedProtected = Progress.TrackedUpgrade == INDEX_NONE;
                bTreeOpen = true;
                bJournalOpen = true;
                ToggleTrackedUpgrade();
                const bool bJournalProtected = Progress.TrackedUpgrade == INDEX_NONE;
                bJournalOpen = false;
                bTreeKeyboardFocusActive = true;
                TreeKeyboardFocus = 0;
                bTreeFocusHasMousePosition = false;
                const int64 Before = Progress.Resources;
                ToggleTrackedUpgrade();
                const bool bPinned = Progress.TrackedUpgrade == 0 && Progress.Resources == Before;
                ToggleTrackedUpgrade();
                const bool bUnpinned = Progress.TrackedUpgrade == INDEX_NONE;
                ToggleTrackedUpgrade();
                PurchaseUpgrade(1, false);
                const bool bFailedKept = Progress.TrackedUpgrade == 0 && !Progress.Has(1);
                PurchaseUpgrade(0, false);
                const bool bManualCleared = Progress.TrackedUpgrade == INDEX_NONE && Progress.Has(0);
                Progress.TrackedUpgrade = 1;
                bAutopilot = true;
                PurchaseUpgrade(1, true);
                const bool bAutomaticCleared = Progress.TrackedUpgrade == INDEX_NONE && Progress.Has(1);
                bAutopilot = false;
                const bool bPaused = PlayerOwner->SetPause(true);
                bTreeKeyboardFocusActive = true;
                TreeKeyboardFocus = 2;
                bTreeFocusHasMousePosition = false;
                ToggleTrackedUpgrade();
                const bool bPausedPin = bPaused && Progress.TrackedUpgrade == 2;
                UE_LOG(LogTemp, Display, TEXT("TRACKED_INTERACTION closed=%d journal=%d pinned=%d unpinned=%d failed_kept=%d manual=%d automatic=%d paused=%d"),
                    bClosedProtected, bJournalProtected, bPinned, bUnpinned, bFailedKept, bManualCleared, bAutomaticCleared, bPausedPin);
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchaseCheck")) && PlayerOwner)
            {
                Progress.AddResources(1000);
                bAutopilot = true;
                bTreeOpen = false;
                ToggleAutoPurchases();
                const bool bClosedProtected = Progress.bAutoPurchasesEnabled;
                bTreeOpen = true;
                bJournalOpen = true;
                ToggleAutoPurchases();
                const bool bJournalProtected = Progress.bAutoPurchasesEnabled;
                bJournalOpen = false;
                BeginTreePointer(SpaceFlight::ETreePointerButton::Left, FVector2D(W * .5f, H * .5f));
                const bool bPointerStarted = TreePointer.IsActive();
                ToggleAutoPurchases();
                const bool bPointerCancelled = bPointerStarted && !TreePointer.IsActive();
                PurchaseUpgrade(0, true);
                const bool bAutomaticBlocked = !Progress.Has(0) && bAutopilot;
                PurchaseUpgrade(0, false);
                const bool bManualAllowed = Progress.Has(0);
                const bool bPaused = PlayerOwner->SetPause(true);
                ToggleAutoPurchases();
                PurchaseUpgrade(1, true);
                const bool bPausedBlocked = bPaused && Progress.bAutoPurchasesEnabled && !Progress.Has(1);
                OnPauseChanged(true);
                const bool bFlightPreserved = bAutopilot;
                if (PlayerOwner->SetPause(false)) OnPauseChanged(false);
                PurchaseUpgrade(1, true);
                const bool bResumePurchased = bAutopilot && Progress.Has(1);
                ToggleAutoPurchases();
                PurchaseUpgrade(2, true);
                const bool bDisabledBlocked = !Progress.Has(2);
                ToggleAutoPurchases();
                PurchaseUpgrade(2, true);
                const bool bEnabledAllowed = Progress.Has(2);
                ToggleAutoPurchases();
                UE_LOG(LogTemp, Display, TEXT("AUTO_PURCHASE_CHECK closed=%d journal=%d pointer=%d auto_blocked=%d manual=%d paused=%d flight_preserved=%d resume_purchased=%d disabled_blocked=%d enabled=%d disabled=%d"),
                    bClosedProtected, bJournalProtected, bPointerCancelled, bAutomaticBlocked, bManualAllowed,
                    bPausedBlocked, bFlightPreserved, bResumePurchased, bDisabledBlocked, bEnabledAllowed, !Progress.bAutoPurchasesEnabled);
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchasePausedPreview"))) PlayerOwner->SetPause(true);
            }
            if (bTreeOpen && !bJournalOpen && FParse::Param(FCommandLine::Get(), TEXT("SpaceTreeNavigationPreview")))
            {
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTreeMicroNeedPreview")))
                {
                    Progress.SetRefiningMode(1);
                    Progress.AddResources(SpaceFlight::Upgrade(0).Cost);
                    Progress.Purchase(0);
                    Progress.AddOre(1, SpaceFlight::EOreKind::Blue);
                    Progress.AddOre(1, SpaceFlight::EOreKind::Black);
                    TreeNavigationCursor = 19;
                }
                else if (!FParse::Param(FCommandLine::Get(), TEXT("SpaceTreeNoResourcesPreview"))) Progress.AddResources(500);
                bAutopilot = false;
                const int64 Before = Progress.Resources;
                if (FParse::Param(FCommandLine::Get(), TEXT("SpaceTreeNavigationPausedPreview")) && PlayerOwner)
                    PlayerOwner->SetPause(true);
                FocusNextAvailableUpgrade();
                const FVector2D Center = TreeKeyboardFocus != INDEX_NONE ?
                    SpaceFlight::UpgradeScreenCenter(TreeKeyboardFocus, W, H, TreeZoom, TreePan) : FVector2D::ZeroVector;
                UE_LOG(LogTemp, Display, TEXT("TREE_NAV_PREVIEW focus=%d before=%lld after=%lld centered=%d paused=%d"),
                    TreeKeyboardFocus, Before, Progress.Resources, Center.Equals(FVector2D(W, H) * .5f, .1f) ? 1 : 0,
                    GetWorld()->IsPaused() ? 1 : 0);
            }
        }
#endif
        CreateCrystalGlowTexture();
        CreateBlueAsteroidAtlases();
        FlightTraceStart = FPlatformTime::Seconds();
        FpsCheckStart = FPlatformTime::Seconds();
#if WITH_DEV_AUTOMATION_TESTS
        FString LanguageSavePhase;
        if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && bSaveEnabled &&
            FParse::Value(FCommandLine::Get(), TEXT("SpaceLanguageSaveCheck="), LanguageSavePhase))
        {
            if (LanguageSavePhase == TEXT("write")) CycleLanguage();
            UE_LOG(LogTemp, Display, TEXT("LANGUAGE_SETTING_CHECK phase=%s language=%d"),
                *LanguageSavePhase, static_cast<int32>(SpaceFlight::GetLanguage()));
        }
        if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceTrackedSaveCheck")))
        {
            bAutopilot = false;
            if (!Progress.Has(0)) Progress.TrackedUpgrade = 0;
            SaveProgress();
            UE_LOG(LogTemp, Display, TEXT("TRACKED_SAVE_CHECK tracked=%d failed=%d blocked=%d"),
                Progress.TrackedUpgrade, bSaveWriteFailed ? 1 : 0, bSaveBlocked ? 1 : 0);
            if (PlayerOwner) PlayerOwner->SetPause(true);
        }
        if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceAutoPurchaseSaveCheck")))
        {
            bTreeOpen = true;
            if (Progress.bAutoPurchasesEnabled) ToggleAutoPurchases();
            UE_LOG(LogTemp, Display, TEXT("AUTO_PURCHASE_SAVE_CHECK enabled=%d failed=%d blocked=%d"),
                Progress.bAutoPurchasesEnabled ? 1 : 0, bSaveWriteFailed ? 1 : 0, bSaveBlocked ? 1 : 0);
        }
        int32 TargetingFixture = -1;
        if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && bSaveEnabled &&
            FParse::Value(FCommandLine::Get(), TEXT("SpaceTargetingFixture="), TargetingFixture) &&
            TargetingFixture >= 0 && TargetingFixture <= 3)
            Progress.TargetingMode = TargetingFixture;
        if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceSaveOnceAndPause")))
        {
            bAutopilot = false;
            SaveProgress();
            if (PlayerOwner) PlayerOwner->SetPause(true);
            UE_LOG(LogTemp, Display, TEXT("SAVE_ONCE_CHECK active=%d failed=%d blocked=%d revision=%lld"),
                ActiveSaveSlot, bSaveWriteFailed ? 1 : 0, bSaveBlocked ? 1 : 0, SaveRevision);
        }
#endif
        if (bBenchmark) StartAsteroidStress();
    }
#if WITH_DEV_AUTOMATION_TESTS
    TickTrackedInputCheck();
    TickManualInputCheck();
    TickExpeditionInputCheck();
    TickTargetingInputCheck();
    TickLanguageInputCheck();
    if (FParse::Param(FCommandLine::Get(), TEXT("SpaceNoSave")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceExpeditionPreview")))
    {
        FString ScreenshotId;
        FGuid ParsedScreenshotId;
        if (FParse::Value(FCommandLine::Get(), TEXT("SpaceExpeditionScreenshotId="), ScreenshotId) &&
            FGuid::ParseExact(ScreenshotId, EGuidFormats::Digits, ParsedScreenshotId))
        {
            static int32 PreviewFrames = 0;
            if (++PreviewFrames == 60)
            {
                const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots");
                IFileManager::Get().MakeDirectory(*Directory, true);
                const FString Filename = FPaths::ConvertRelativePathToFull(Directory / (TEXT("Expedition_") + ScreenshotId + TEXT(".png")));
                FScreenshotRequest::RequestScreenshot(Filename, true, false);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_SCREENSHOT requested=%s"), *Filename);
            }
        }
    }
#endif
    auto Rect = [this](float X, float Y, float Width, float Height, const FLinearColor& Color)
    {
        DrawRect(Color, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::CeilToFloat(Width), FMath::CeilToFloat(Height));
    };

    // The scene is drawn at a consistent virtual pixel size and adapts to the window.
    const float Unit = FMath::Max(1.0f, FMath::FloorToFloat(FMath::Min(W / 320.0f, H / 180.0f)));
    const FLinearColor SpaceBackground(0.006f, 0.012f, 0.035f);
    Rect(0, 0, W, H, SpaceBackground);

    // Stable random positions, with three slow parallax layers drifting downwards.
    FRandomStream Stars(270419);
    for (int32 I = 0; I < 125; ++I)
    {
        const float X = Stars.FRand() * W;
        const float BaseY = Stars.FRand() * H;
        const float Phase = Stars.FRand() * 6.28318f;
        const int32 Layer = I % 3;
        const float Size = Unit;
        const float Y = FMath::Fmod(BaseY + T * (8.0f + Layer * 9.0f) * Unit, H + Size) - Size;
        const float Pulse = 0.82f + 0.18f * FMath::Sin(T * 1.3f + Phase);
        const float Brightness = (0.12f + Layer * 0.10f) * Pulse;
        const FLinearColor StarColor(Brightness * 0.65f, Brightness * 0.80f, Brightness);
        Rect(X, Y, Size, Size, StarBrightness.Apply(SpaceBackground, StarColor));
    }

    // Each character is a pixel of the original, hand-drawn ship.
    static const TCHAR* Ship[] = {
        TEXT(".............11............."),
        TEXT("............1221............"),
        TEXT("............1221............"),
        TEXT("...........123321..........."),
        TEXT("...........123321..........."),
        TEXT("..........12344321.........."),
        TEXT("..........12344321.........."),
        TEXT(".........1234444321........."),
        TEXT(".........1234554321........."),
        TEXT("........123455554321........"),
        TEXT("........123455554321........"),
        TEXT(".......1123455543211........"),
        TEXT(".......1623455543261........"),
        TEXT("......166234444432661......."),
        TEXT(".....16662333333326661......"),
        TEXT("....166622333333226661......"),
        TEXT("...16662223333332226661....."),
        TEXT("..1666222233333322226661...."),
        TEXT(".166622222333333222226661..."),
        TEXT("16662222223333332222226661.."),
        TEXT("16622222223333332222222661.."),
        TEXT("16222222223333332222222261.."),
        TEXT("16222222223333332222222261.."),
        TEXT("11222222223333332222222211.."),
        TEXT(".11112222233333322221111...."),
        TEXT(".....1222233333322221......."),
        TEXT(".....1277233333377221......."),
        TEXT(".....1777121112177711......."),
        TEXT(".....11111....11111.........")
    };
    const FLinearColor Palette[] = {
        FLinearColor::Transparent,
        FLinearColor(0.025f, 0.045f, 0.09f),
        FLinearColor(0.21f, 0.34f, 0.47f),
        FLinearColor(0.58f, 0.73f, 0.76f),
        FLinearColor(0.09f, 0.32f, 0.43f),
        FLinearColor(0.20f, 0.82f, 0.90f),
        FLinearColor(0.91f, 0.34f, 0.20f),
        FLinearColor(0.97f, 0.70f, 0.30f)
    };
    // The hull is one third of its former size. Draw it on a grid twice as
    // fine as the original silhouette so that small panel details remain visible.
    const float Pixel = Unit * 0.25f;
    const bool bPaused = GetWorld()->IsPaused();
    if (ShipX < 0.0f) ShipX = W * 0.5f;
    const float PreviousX = ShipX;
    const float Delta = bPaused ? 0.0f : FMath::Min(GetWorld()->GetDeltaSeconds(), 0.1f);
    if (!bPaused)
    {
        APlayerController* Controller = GetOwningPlayerController();
        const float Speed = W * 0.30f * Progress.SpeedMultiplier();
        if (bAutopilot)
        {
            const float Destination = W * (0.5f + 0.27f * FMath::Sin(T * 0.25f));
            ShipX = FMath::FInterpConstantTo(ShipX, Destination, Delta, Speed * 0.55f);
        }
        else if (Controller && !bTreeOpen)
        {
            int32 Direction = (Controller->IsInputKeyDown(EKeys::D) || Controller->IsInputKeyDown(EKeys::Right) ? 1 : 0)
                - (Controller->IsInputKeyDown(EKeys::A) || Controller->IsInputKeyDown(EKeys::Left) ? 1 : 0);
#if WITH_DEV_AUTOMATION_TESTS
            if (SaveSlotBase.StartsWith(TEXT("SpaceFlightTest_")) && FParse::Param(FCommandLine::Get(), TEXT("SpaceManualFlightCheck")))
                Direction = FMath::Fmod(T - SessionStartTime, 8.0f) < 4.0f ? 1 : -1;
#endif
            ShipX += Direction * Speed * Delta;
        }
    }
    ShipX = FMath::Clamp(ShipX, 32.0f * Pixel, W - 32.0f * Pixel);
    if (!bPaused)
    {
        const float TargetBank = SpaceFlight::ShipManeuverBank(ShipX - PreviousX, Delta, W, Progress);
        ShipBank = FMath::FInterpTo(ShipBank, TargetBank, Delta, 5.5f);
    }
    const float Top = H * 0.69f - 29.0f * Pixel + FMath::Sin(T * 1.4f) * Unit / 3.0f;
    const SpaceFlight::FShipAppearance Appearance = SpaceFlight::DescribeShipAppearance(Progress);
    auto ShipXY = [&](float X, float Y)
    { return SpaceFlight::ShipPoint(X, Y, ShipX, Top, Pixel, ShipBank); };
    auto ShipRect = [&](float X, float Y, float Width, float Height, const FLinearColor& Color)
    {
        const FVector2D Center = ShipXY(X + Width * 0.5f, Y + Height * 0.5f);
        Rect(Center.X - Width * Pixel * 0.5f, Center.Y - Height * Pixel * 0.5f,
            Width * Pixel, Height * Pixel, Color);
    };
    const FVector2D LaserOrigin = ShipXY(28.0f, 4.0f);
    TArray<SpaceFlight::FLaserCandidate, TInlineAllocator<3>> Candidates;
    TArray<SpaceFlight::FLaserCandidate, TInlineAllocator<32>> RicochetCandidates;
    const float EquippedRange = Progress.Range();
    const int32 EquippedLasers = Progress.Lasers();
    const bool bFindRicochet = Progress.Has(SpaceFlight::RicochetUpgrade) && !bPaused && T - LastLaserTime >= Progress.Cooldown();
    const bool bProtectFirstHitBonus = bFindRicochet && Progress.FirstHitOreBonus() > 0;
    const int32 EquippedDamage = bProtectFirstHitBonus ? Progress.LaserDamage() : 0;
    const bool bEvaluateUpgrade = bAutopilot && Progress.bAutoPurchasesEnabled && !bPaused && T >= NextAutoUpgradeEvaluation;
    TArray<SpaceFlight::FForecastRock, TInlineAllocator<SpaceFlight::MaxForecastRocks>> ForecastRocks;

    // Asteroids stay in two lanes outside the ship's entire horizontal span.
    // The clearance includes the asteroid's bounding square and the ship's sway.
    const double StressElapsed = bStressMode ? (PauseStartedAt > 0.0 ? PauseStartedAt : FPlatformTime::Seconds()) - StressStartTime : 0.0;
    const int32 Sector = Progress.CurrentSector;
    const uint32 SectorSpawnSeed = SpaceFlight::SectorSeed(SpawnSeed, Sector);
#if WITH_DEV_AUTOMATION_TESTS
    const bool bRicochetPreview = !bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetPreview"));
#else
    const bool bRicochetPreview = false;
#endif
    const int32 AsteroidCount = (bStressMode ? SpaceFlight::NormalAsteroidCount : SpaceFlight::SectorAsteroidCount(Sector)) +
        (bStressMode ? 10 * FMath::FloorToInt(StressElapsed / 5.0) : 0);
    const uint8 EventTransitions = MineralEvent.Advance(GetWorld()->GetDeltaSeconds(), bPaused || bTreeOpen || bStressMode, AsteroidCount);
    if (EventTransitions & SpaceFlight::WarningStarted)
        PlayEventSound(SpaceFlight::ESoundEvent::ModeOn);
    if ((EventTransitions & SpaceFlight::FlowStarted) && MineralEvent.Phase == SpaceFlight::EMineralPhase::Active)
    {
        const SpaceFlight::EDiscovery Discovery = SpaceFlight::DiscoveryForEvent(MineralEvent.Kind);
        if (Progress.RecordDiscovery(Discovery)) SaveProgress();
    }
    if (EventTransitions && FParse::Param(FCommandLine::Get(), TEXT("SpaceMineralTrace")))
        UE_LOG(LogTemp, Display, TEXT("MINERAL_EVENT phase=%d kind=%d transitions=%d remaining=%.2f count=%d"),
            static_cast<int32>(MineralEvent.Phase), static_cast<int32>(MineralEvent.Kind), EventTransitions, MineralEvent.Remaining, AsteroidCount);
    if (bStressMode && AsteroidCount != LastLoggedStressCount)
    {
        LastLoggedStressCount = AsteroidCount;
        UE_LOG(LogTemp, Verbose, TEXT("Stress asteroid count: %d"), AsteroidCount);
    }
    while (BaseResetTimes.Num() < AsteroidCount)
    {
        BaseResetTimes.Add(-1.0f);
        BaseGenerations.Add(0);
        AsteroidDurability.AddDefaulted();
    }
    for (int32 Index = 0; Index < AsteroidCount; ++Index)
    {
        FRandomStream RockRandom(4627 + Index * 719);
        const int32 Size = SpaceFlight::AsteroidSizeForIndex(Index);
        const float Radius = Size * 0.5f;
        const float RadiusOnScreen = (Radius + 1.0f) * Pixel;
        const float Speed = Unit * RockRandom.FRandRange(9.0f, 21.0f) * SpaceFlight::SectorSpeedMultiplier(Sector);
        // Each slot stays present, including after destruction and screen wrapping.
        const bool bAlwaysPresent = true;
        const float TravelLength = H;
        const float InitialTravel = RockRandom.FRand() * TravelLength;
        const float Travel = BaseResetTimes[Index] >= 0.0f
            ? (T - BaseResetTimes[Index]) * Speed : InitialTravel + T * Speed;
        const int32 Pass = FMath::FloorToInt(Travel / TravelLength);
        float Y = FMath::Fmod(Travel, TravelLength);
        const int32 Generation = BaseGenerations[Index];
        SpaceFlight::FAsteroidDurability& Durability = AsteroidDurability[Index];
        Durability.Prepare(Pass, Generation, Size);
        const int32 Damage = (SpaceFlight::AsteroidHitPoints(Size) * 10 - Durability.RemainingTenths + 9) / 10;
        const float BaseX = SpaceFlight::ChooseAsteroidSpawn(SectorSpawnSeed, Index, Pass, Generation,
            W, Pixel, RadiusOnScreen).X;
        float X = SpaceFlight::AvoidShip(BaseX, Y, ShipX, Top + 29.0f * Pixel, W, Pixel, RadiusOnScreen);
        if (bRicochetPreview && Index < 3)
        {
            X = ShipX + (Index == 2 ? 105.0f : 80.0f + 40.0f * Index) * Pixel;
            Y = static_cast<float>(LaserOrigin.Y) - (Index == 2 ? 100.0f : 20.0f + 10.0f * Index) * Pixel;
        }
        const float RotationSpeed = RockRandom.FRandRange(0.18f, 0.75f) * (Index % 3 == 0 ? -1.0f : 1.0f);
        const float Angle = RockRandom.FRandRange(0.0f, 6.28318f) + T * RotationSpeed;
        const float CosAngle = FMath::Cos(Angle);
        const float SinAngle = FMath::Sin(Angle);
        const float ShapePhase = RockRandom.FRandRange(0.0f, 6.28318f);
        // Keep all three families in the regular game and in large stress fleets.
        const SpaceFlight::EOreKind OreKind = SpaceFlight::SectorOreKind(Sector, SpawnSeed, Index, Pass, Generation);
        const bool bBlueRock = OreKind == SpaceFlight::EOreKind::Blue;
        const bool bCrystalRock = OreKind == SpaceFlight::EOreKind::Black;
        const SpaceFlight::FCrystalGlow Glow = bCrystalRock
            ? SpaceFlight::CrystalGlow(T, SectorSpawnSeed, Index, Generation + Pass) : SpaceFlight::FCrystalGlow{};
        if (bFlightTrace && Index == 0 && bCrystalRock)
        {
            const int32 Second = FMath::FloorToInt(FPlatformTime::Seconds() - FlightTraceStart);
            if (Second != CrystalGlowTraceSecond)
            {
                CrystalGlowTraceSecond = Second;
                UE_LOG(LogTemp, Display, TEXT("CRYSTAL_GLOW second=%d strength=%.3f mode=%s"),
                    Second, Glow.Strength, bStressMode ? TEXT("stress") : TEXT("normal"));
            }
        }
        // Warm mineral tones distinguish the rocks from the cool blue stars.
        static const FLinearColor RockColors[] = {
            FLinearColor(0.60f, 0.27f, 0.11f), // copper
            FLinearColor(0.65f, 0.39f, 0.17f), // sandstone
            FLinearColor(0.50f, 0.20f, 0.12f)  // iron-rich rock
        };
        const FVector2D RockPosition(X, Y);
        const bool bEventMarked = !bStressMode && MineralEvent.IsMarked(Index, AsteroidCount);
        if (bEvaluateUpgrade && Y >= 0 && Y <= H)
        {
            const SpaceFlight::FForecastRock Sample{ BaseX, Y, Speed, RadiusOnScreen, Size, bAlwaysPresent, Durability.Remaining,
                bBlueRock ? SpaceFlight::EOreKind::Blue : (bCrystalRock ? SpaceFlight::EOreKind::Black : SpaceFlight::EOreKind::Common), bEventMarked, Durability.RemainingTenths };
            const float Distance = FVector2D::DistSquared(FVector2D(BaseX, Y), LaserOrigin);
            int32 At = 0;
            while (At < ForecastRocks.Num() &&
                FVector2D::DistSquared(FVector2D(ForecastRocks[At].BaseX, ForecastRocks[At].Y), LaserOrigin) < Distance) ++At;
            if (At < SpaceFlight::MaxForecastRocks)
            {
                ForecastRocks.Insert(Sample, At);
                if (ForecastRocks.Num() > SpaceFlight::MaxForecastRocks) ForecastRocks.Pop(EAllowShrinking::No);
            }
        }
        const bool bUnstable = bEventMarked && MineralEvent.Kind == SpaceFlight::ERareEventKind::UnstableCrystal;
        const bool bDeposit = bEventMarked && MineralEvent.Kind == SpaceFlight::ERareEventKind::LargeDeposit;
        const SpaceFlight::FLaserCandidate LaserCandidate{ Index, Pass, Size, RockPosition,
            bUnstable ? FLinearColor(.56f, .24f, .90f) :
                (bDeposit ? FLinearColor(.20f, .78f, .57f) :
                    (bBlueRock ? BlueRockDust : (bCrystalRock ? BlackRockDust : RockColors[Index % UE_ARRAY_COUNT(RockColors)]))),
            0.0f, bBlueRock ? SpaceFlight::EOreKind::Blue : (bCrystalRock ? SpaceFlight::EOreKind::Black : SpaceFlight::EOreKind::Common), bEventMarked,
            !Durability.IsUndamaged(Size), SpaceFlight::RicochetWouldSpoilFirstHitBonus(Size, EquippedDamage, !Durability.IsUndamaged(Size), bProtectFirstHitBonus) };
        if (!bRicochetPreview || Index < 3)
            SpaceFlight::AddLaserCandidate(Candidates, LaserCandidate, LaserOrigin, H, EquippedRange, EquippedLasers, Progress.TargetingMode);
        if ((!bRicochetPreview || Index < 3) && bFindRicochet && Y >= 0.0f && Y <= H && FVector2D::DistSquared(LaserOrigin, RockPosition) <= FMath::Square(EquippedRange))
            RicochetCandidates.Add(LaserCandidate);

        if (bUnstable && CrystalGlowTexture && CrystalGlowTexture->GetResource())
        {
            const float Pulse = .5f + .5f * FMath::Sin(T * 4.0f);
            const float GlowRadius = RadiusOnScreen + (9.0f + 2.5f * Pulse) * Pixel;
            auto DrawHalo = [&](float DrawY)
            {
                FCanvasTileItem Halo(FVector2D(X - GlowRadius, DrawY - GlowRadius), CrystalGlowTexture->GetResource(),
                    FVector2D(GlowRadius * 2.0f), FLinearColor(.62f, .20f, 1.0f, .20f + .18f * Pulse));
                Halo.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Halo);
            };
            DrawHalo(Y);
            if (Y < GlowRadius) DrawHalo(Y + H);
            else if (Y > H - GlowRadius) DrawHalo(Y - H);
        }
        if (bEventMarked)
        {
            const float MarkerSize = 3.0f * Pixel;
            const FLinearColor Marker = bUnstable ? FLinearColor(.75f, .45f, 1.0f) :
                (bDeposit ? FLinearColor(.25f, .95f, .66f) : FLinearColor(.95f, .65f, .22f));
            auto DrawMarker = [&](float DrawY)
            {
                const float MarkerY = DrawY - RadiusOnScreen - 7.0f * Pixel;
                DrawLine(X, MarkerY - MarkerSize, X + MarkerSize, MarkerY, Marker, Pixel);
                DrawLine(X + MarkerSize, MarkerY, X, MarkerY + MarkerSize, Marker, Pixel);
                DrawLine(X, MarkerY + MarkerSize, X - MarkerSize, MarkerY, Marker, Pixel);
                DrawLine(X - MarkerSize, MarkerY, X, MarkerY - MarkerSize, Marker, Pixel);
                if (bUnstable) DrawLine(X - 5.0f * Pixel, MarkerY, X - 3.5f * Pixel, MarkerY, Marker, Pixel);
                if (bDeposit) DrawLine(X - 5.0f * Pixel, MarkerY + 5.0f * Pixel, X + 5.0f * Pixel, MarkerY + 5.0f * Pixel, Marker, Pixel);
            };
            DrawMarker(Y);
            if (Y < RadiusOnScreen) DrawMarker(Y + H);
            else if (Y > H - RadiusOnScreen) DrawMarker(Y - H);
        }
        auto DrawUnstableFacets = [&](float DrawY)
        {
            if (!bUnstable) return;
            const float R = Radius * Pixel * .64f;
            const FLinearColor Edge(.49f, .30f, .73f);
            const FLinearColor Light(.72f, .52f, .92f);
            DrawLine(X, DrawY - R, X + R, DrawY, Edge, Pixel);
            DrawLine(X + R, DrawY, X, DrawY + R, Edge, Pixel);
            DrawLine(X, DrawY + R, X - R, DrawY, Edge, Pixel);
            DrawLine(X - R, DrawY, X, DrawY - R, Edge, Pixel);
            DrawLine(X, DrawY - R, X - R * .2f, DrawY + R * .15f, Light, Pixel);
            DrawLine(X - R * .2f, DrawY + R * .15f, X, DrawY + R, Edge, Pixel);
        };
        auto DrawDepositVeins = [&](float DrawY)
        {
            if (!bDeposit) return;
            auto VeinPoint = [&](float LocalX, float LocalY)
            {
                return FVector2D(X + (LocalX * CosAngle - LocalY * SinAngle) * Radius * Pixel,
                    DrawY + (LocalX * SinAngle + LocalY * CosAngle) * Radius * Pixel);
            };
            const FVector2D A = VeinPoint(-.55f, -.40f), B = VeinPoint(-.12f, -.13f), C = VeinPoint(.18f, .09f), D = VeinPoint(.50f, .41f);
            const FVector2D Branch = VeinPoint(.38f, -.38f);
            const FLinearColor Vein(.16f, .76f, .55f);
            DrawLine(A.X, A.Y, B.X, B.Y, Vein, Pixel);
            DrawLine(B.X, B.Y, C.X, C.Y, Vein, Pixel);
            DrawLine(C.X, C.Y, D.X, D.Y, Vein, Pixel);
            DrawLine(B.X, B.Y, Branch.X, Branch.Y, Vein, Pixel);
            const float Flicker = .65f + .35f * FMath::Sin(T * 2.6f);
            Rect(C.X - Pixel, C.Y - Pixel, 2 * Pixel, 2 * Pixel, FLinearColor(.44f, Flicker, .78f));
        };

        if (bBlueRock)
        {
            const auto Motion = SpaceFlight::BlueAsteroidMotion(SectorSpawnSeed, Index, Pass, Generation);
            float Rotation = FMath::Fmod(Motion.Phase + T * Motion.AngularSpeed, 2.0f * PI);
            if (Rotation < 0.0f) Rotation += 2.0f * PI;
            const int32 Frame = FMath::FloorToInt(Rotation * SpaceFlight::BlueAsteroidFrames / (2.0f * PI)) % SpaceFlight::BlueAsteroidFrames;
            if (bFlightTrace && Index == 2)
            {
                const int32 Second = FMath::FloorToInt(FPlatformTime::Seconds() - FlightTraceStart);
                if (Second != BlueAsteroidTraceSecond)
                {
                    BlueAsteroidTraceSecond = Second;
                    UE_LOG(LogTemp, Display, TEXT("BLUE_ASTEROID second=%d x=%.1f y=%.1f frame=%d axis=%.3f rate=%.3f damage=%d stress=%d"),
                        Second, X, Y, Frame, Motion.AxisTilt, Motion.AngularSpeed, Damage, bStressMode ? 1 : 0);
                }
            }
            const int32 TextureIndex = Motion.Variant * SpaceFlight::BlueAsteroidDamageStates + FMath::Clamp(Damage, 0, 2);
            UTexture2D* Texture = BlueAsteroidAtlases.IsValidIndex(TextureIndex) ? BlueAsteroidAtlases[TextureIndex] : nullptr;
            auto DrawBlue = [&](float DrawY)
            {
                const float DrawSize = Size * Pixel;
                if (Texture && Texture->GetResource())
                {
                    const float Columns = static_cast<float>(SpaceFlight::BlueAsteroidAtlasColumns);
                    const float Rows = static_cast<float>(SpaceFlight::BlueAsteroidFrames / SpaceFlight::BlueAsteroidAtlasColumns);
                    const float U = (Frame % SpaceFlight::BlueAsteroidAtlasColumns) / Columns;
                    const float V = (Frame / SpaceFlight::BlueAsteroidAtlasColumns) / Rows;
                    const FVector2D UV0(U + 0.5f / (Columns * SpaceFlight::BlueAsteroidFrameSize),
                        V + 0.5f / (Rows * SpaceFlight::BlueAsteroidFrameSize));
                    const FVector2D UV1(U + 1.0f / Columns - 0.5f / (Columns * SpaceFlight::BlueAsteroidFrameSize),
                        V + 1.0f / Rows - 0.5f / (Rows * SpaceFlight::BlueAsteroidFrameSize));
                    FCanvasTileItem Tile(FVector2D(X - DrawSize * 0.5f, DrawY - DrawSize * 0.5f),
                        Texture->GetResource(), FVector2D(DrawSize, DrawSize), UV0, UV1, FLinearColor::White);
                    Tile.BlendMode = SE_BLEND_Translucent;
                    Tile.PivotPoint = FVector2D(0.5f, 0.5f);
                    Tile.Rotation = FRotator(0.0f, FMath::RadiansToDegrees(Motion.AxisTilt), 0.0f);
                    Canvas->DrawItem(Tile);
                    return;
                }
                const float AxisC = FMath::Cos(Motion.AxisTilt), AxisS = FMath::Sin(Motion.AxisTilt);
                for (int32 Row = 0; Row < Size; ++Row) for (int32 Col = 0; Col < Size; ++Col)
                {
                    const float DX = (Col + 0.5f - Radius) / Radius, DY = (Row + 0.5f - Radius) / Radius;
                    const FColor Color = SpaceFlight::SampleBlueAsteroid(DX * AxisC + DY * AxisS,
                        -DX * AxisS + DY * AxisC, Rotation, Motion.Variant, Damage);
                    if (Color.A > 0) Rect(X + (Col - Radius) * Pixel, DrawY + (Row - Radius) * Pixel,
                        Pixel, Pixel, FLinearColor::FromSRGBColor(Color));
                }
            };
            DrawBlue(Y);
            DrawUnstableFacets(Y);
            DrawDepositVeins(Y);
            if (bAlwaysPresent && Y < RadiusOnScreen) { DrawBlue(Y + H); DrawUnstableFacets(Y + H); DrawDepositVeins(Y + H); }
            else if (bAlwaysPresent && Y > H - RadiusOnScreen) { DrawBlue(Y - H); DrawUnstableFacets(Y - H); DrawDepositVeins(Y - H); }
            continue;
        }

        auto DrawDiamondGlint = [&](float DrawY)
        {
            if (!bCrystalRock || !CrystalGlowTexture || !CrystalGlowTexture->GetResource()) return;
            auto Facet = [&](float FacetX, float FacetY, float SizeFactor)
            {
                const float LocalX = FacetX * Radius * Pixel;
                const float LocalY = FacetY * Radius * Pixel;
                const float GlintX = X + LocalX * CosAngle - LocalY * SinAngle;
                const float GlintY = DrawY + LocalX * SinAngle + LocalY * CosAngle;
                const float Reach = FMath::Max(Pixel, Radius * Pixel * SizeFactor);
                auto DrawGlow = [&](float Radius, float Opacity)
                {
                    FLinearColor Tint = Glow.Color;
                    Tint.A = Opacity * Glow.Strength;
                    FCanvasTileItem Tile(FVector2D(GlintX - Radius, GlintY - Radius), CrystalGlowTexture->GetResource(),
                        FVector2D(2 * Radius, 2 * Radius), Tint);
                    Tile.BlendMode = SE_BLEND_Translucent;
                    Canvas->DrawItem(Tile);
                };
                DrawGlow(Reach, 0.30f);
                DrawGlow(Reach * 0.5f, 0.38f);
            };
            Facet(-0.24f, -0.15f, 0.45f);
            Facet(0.32f, 0.28f, 0.30f);
        };
        auto DrawDamage = [&](float DrawY)
        {
            if (Damage <= 0) return;
            const FLinearColor Crack = bCrystalRock ? FLinearColor(0.22f, 0.46f, 0.53f) : FLinearColor(0.055f, 0.035f, 0.025f);
            auto Point = [&](float LocalX, float LocalY)
            {
                return FVector2D(X + (LocalX * CosAngle - LocalY * SinAngle) * Radius * Pixel,
                    DrawY + (LocalX * SinAngle + LocalY * CosAngle) * Radius * Pixel);
            };
            auto CrackLine = [&](float X1, float Y1, float X2, float Y2)
            {
                const FVector2D A = Point(X1, Y1), B = Point(X2, Y2);
                DrawLine(A.X, A.Y, B.X, B.Y, Crack, FMath::Max(1.0f, 1.3f * Pixel));
            };
            CrackLine(-0.50f, -0.48f, -0.12f, -0.10f);
            CrackLine(-0.12f, -0.10f, 0.08f, 0.07f);
            CrackLine(0.08f, 0.07f, 0.45f, 0.45f);
            CrackLine(-0.12f, -0.10f, 0.30f, -0.36f);
            if (Damage >= 2)
            {
                CrackLine(0.08f, 0.07f, -0.37f, 0.48f);
                CrackLine(-0.05f, 0.18f, 0.44f, 0.15f);
                CrackLine(0.30f, -0.36f, 0.50f, -0.55f);
            }
        };

        if (bStressMode && !StressRockTextures.IsEmpty())
        {
            const int32 TextureIndex = Index % 21 + (bCrystalRock ? 21 : 0);
            UTexture2D* Texture = StressRockTextures.IsValidIndex(TextureIndex) ? StressRockTextures[TextureIndex] : nullptr;
            if (Texture && Texture->GetResource())
            {
                const float DrawSize = Size * Pixel;
                auto DrawRock = [&](float DrawY)
                {
                    FCanvasTileItem Tile(FVector2D(X - DrawSize * 0.5f, DrawY - DrawSize * 0.5f),
                        Texture->GetResource(), FVector2D(DrawSize, DrawSize), FLinearColor::White);
                    Tile.BlendMode = SE_BLEND_Translucent;
                    Tile.PivotPoint = FVector2D(0.5f, 0.5f);
                    Tile.Rotation = FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f);
                    Canvas->DrawItem(Tile);
                    DrawDiamondGlint(DrawY);
                    DrawDamage(DrawY);
                };
                DrawRock(Y);
                if (Y < RadiusOnScreen) DrawRock(Y + H);
                else if (Y > H - RadiusOnScreen) DrawRock(Y - H);
            }
            continue;
        }

        // Sample a rotating irregular silhouette in object space; surface facets
        // and craters rotate with the rock instead of sliding across its surface.
        for (int32 Row = 0; Row < Size; ++Row)
        {
            for (int32 Col = 0; Col < Size; ++Col)
            {
                const float DX = (Col + 0.5f - Radius) / Radius;
                const float DY = (Row + 0.5f - Radius) / Radius;
                const float LocalX = DX * CosAngle + DY * SinAngle;
                const float LocalY = -DX * SinAngle + DY * CosAngle;
                const float Theta = FMath::Atan2(LocalY, LocalX);
                const float Boundary = 0.82f + 0.10f * FMath::Sin(3.0f * Theta + ShapePhase)
                    + 0.07f * FMath::Cos(5.0f * Theta - ShapePhase * 0.7f);
                const float Distance = FMath::Sqrt(LocalX * LocalX + LocalY * LocalY);
                if (Distance > Boundary) continue;

                float Shade = 0.25f + 0.10f * (-LocalX - LocalY);
                if (Distance > Boundary - 0.12f) Shade *= 0.62f;
                const float CraterX = LocalX + 0.24f;
                const float CraterY = LocalY + 0.15f;
                const float Crater2X = LocalX - 0.32f;
                const float Crater2Y = LocalY - 0.28f;
                if ((CraterX * CraterX + CraterY * CraterY < 0.035f) ||
                    (Crater2X * Crater2X + Crater2Y * Crater2Y < 0.018f)) Shade *= 0.58f;
                const int32 GrainX = FMath::FloorToInt((LocalX + 1.0f) * Size * 0.5f);
                const int32 GrainY = FMath::FloorToInt((LocalY + 1.0f) * Size * 0.5f);
                if (((GrainX * 17 + GrainY * 31 + Index * 13) & 7) == 0) Shade *= 1.25f;
                FLinearColor Color = bCrystalRock
                    ? CrystalRockColor(LocalX, LocalY, Distance, Boundary, Shade, Glow.Strength, Glow.Color)
                    : RockColors[Index % UE_ARRAY_COUNT(RockColors)] * (Shade / 0.25f);
                Color.A = 1.0f;
                const float PixelX = X + (Col - Radius) * Pixel;
                const float PixelY = Y + (Row - Radius) * Pixel;
                Rect(PixelX, PixelY, Pixel, Pixel, Color);
                // Wrap the permanent rocks seamlessly so a rock remains present
                // while it crosses the top or bottom edge.
                if (bAlwaysPresent && Y < RadiusOnScreen)
                    Rect(PixelX, PixelY + H, Pixel, Pixel, Color);
                else if (bAlwaysPresent && Y > H - RadiusOnScreen)
                    Rect(PixelX, PixelY - H, Pixel, Pixel, Color);
            }
        }
        DrawDiamondGlint(Y);
        DrawDamage(Y);
        DrawUnstableFacets(Y);
        DrawDepositVeins(Y);
        if (bAlwaysPresent && Y < RadiusOnScreen) { DrawDiamondGlint(Y + H); DrawDamage(Y + H); DrawUnstableFacets(Y + H); DrawDepositVeins(Y + H); }
        else if (bAlwaysPresent && Y > H - RadiusOnScreen) { DrawDiamondGlint(Y - H); DrawDamage(Y - H); DrawUnstableFacets(Y - H); DrawDepositVeins(Y - H); }
    }

    // Quiet corner brackets identify preferred targets without hiding the
    // asteroid. Fallback targets stay unmarked when none match the preference.
    if (!bTreeOpen && !bPaused && Progress.TargetingMode != 0)
    {
        const FLinearColor Marker = Progress.TargetingMode == 1 ? FLinearColor(.36f, .78f, 1.0f, .75f) :
            (Progress.TargetingMode == 2 ? FLinearColor(.78f, .86f, 1.0f, .75f) : FLinearColor(1.0f, .79f, .36f, .75f));
        for (const SpaceFlight::FLaserCandidate& Target : Candidates)
        {
            if (SpaceFlight::TargetPriority(Progress.TargetingMode, Target.OreKind, Target.bEventMarked) != 0) continue;
            const float R = (Target.Size * .5f + 5.0f) * Pixel;
            const float L = 4.0f * Pixel;
            for (float Side : { -1.0f, 1.0f })
            {
                const float X = Target.Position.X + Side * R;
                const float Y = Target.Position.Y - R;
                DrawLine(X, Y, X - Side * L, Y, Marker, Pixel);
                DrawLine(X, Y, X, Y + L, Marker, Pixel);
                const float Bottom = Target.Position.Y + R;
                DrawLine(X, Bottom, X - Side * L, Bottom, Marker, Pixel);
                DrawLine(X, Bottom, X, Bottom - L, Marker, Pixel);
            }
        }
    }

    if (bEvaluateUpgrade)
    {
        NextAutoUpgradeEvaluation = T + 1.0f;
        const SpaceFlight::FForecastContext Context{ W, H, Pixel, ShipX, Top + 29.0f * Pixel,
            static_cast<float>(LaserOrigin.Y), T, T - LastLaserTime };
#if WITH_DEV_AUTOMATION_TESTS
        const double AutoProfileStart = bFrameProfile && FPlatformTime::Seconds() >= FrameProfileStart ? FPlatformTime::Seconds() : 0.0;
#endif
        const auto Choice = SpaceFlight::ChooseAutoUpgrade(Progress, Context, ForecastRocks);
#if WITH_DEV_AUTOMATION_TESTS
        if (AutoProfileStart > 0.0) AutoProfileSamples.Add((FPlatformTime::Seconds() - AutoProfileStart) * 1000.0);
#endif
        if (Choice.Next != INDEX_NONE && Progress.CanPurchase(Choice.Next))
        {
            const FString Name = SpaceFlight::Upgrade(Choice.Next).Name;
            PurchaseUpgrade(Choice.Next, true);
            UpgradeNotice = SF_LOCAL_PRINTF(TEXT("Автопилот: %s"), TEXT("Autopilot: %s"), TEXT("Piloto automático: %s"), *Name);
            UE_LOG(LogTemp, Display, TEXT("Autopilot upgrade index=%d goal=%d score=%.4f ore=%lld"),
                Choice.Next, Choice.Goal, Choice.Score, Progress.Resources);
        }
    }

    // Each installed laser chooses a distinct nearby asteroid.
    if (!bPaused && !Candidates.IsEmpty() && T - LastLaserTime >= Progress.Cooldown())
    {
        LastLaserTime = T;
        LaserBeams.Reset();
        PlayEventSound(SpaceFlight::ESoundEvent::Laser);
        bool bAnyImpact = false;
        int32 LargestDestroyedSize = 0;
        const int64 IsotopeBeforeVolley = Progress.Isotope;
        struct FVolleyShot { SpaceFlight::FLaserCandidate Target; int32 Weapon; bool bReflected; FVector2D From; };
        TArray<FVolleyShot, TInlineAllocator<6>> Shots;
        TArray<int32, TInlineAllocator<6>> Reserved;
        for (int32 Weapon = 0; Weapon < Candidates.Num(); ++Weapon)
        {
            Shots.Add({ Candidates[Weapon], Weapon, false, FVector2D::ZeroVector });
            Reserved.Add(Candidates[Weapon].Index);
        }
        if (bFindRicochet)
        {
            const FVector2D ShipCenter(ShipX, Top + 29.0f * Pixel);
            for (int32 Weapon = 0; Weapon < Candidates.Num(); ++Weapon)
            {
                const int32 Index = SpaceFlight::SelectRicochetTarget(RicochetCandidates, Reserved, Candidates[Weapon].Position,
                    LaserOrigin, ShipCenter, 29.0f * Pixel, EquippedRange, H, Progress.TargetingMode);
                if (Index == INDEX_NONE) continue;
                for (const auto& Candidate : RicochetCandidates)
                    if (Candidate.Index == Index)
                    {
                        Shots.Add({ Candidate, Weapon, true, Candidates[Weapon].Position });
                        Reserved.Add(Index);
                        break;
                    }
            }
        }
        for (const FVolleyShot& Shot : Shots)
        {
            const SpaceFlight::FLaserCandidate& Target = Shot.Target;
            LaserBeams.Add({ Target.Position, Shot.Weapon, Shot.From, Shot.bReflected });
            SpaceFlight::FAsteroidDurability& Durability = AsteroidDurability[Target.Index];
            const bool bFirstHit = Durability.IsUndamaged(Target.Size);
#if WITH_DEV_AUTOMATION_TESTS
            const int32 BeforeTenths = Durability.RemainingTenths;
#endif
            const bool bDestroyed = Shot.bReflected ? Durability.HitTenths(Progress.LaserDamage() * 4) : Durability.Hit(Progress.LaserDamage());
#if WITH_DEV_AUTOMATION_TESTS
            if (Shot.bReflected && !bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceRicochetTrace")))
                UE_LOG(LogTemp, Display, TEXT("RICOCHET_HIT frame=%llu time=%.3f target=%d from_x=%.1f from_y=%.1f to_x=%.1f to_y=%.1f before=%d after=%d damage=%d destroyed=%d"),
                    GFrameCounter, T, Target.Index, Shot.From.X, Shot.From.Y, Target.Position.X, Target.Position.Y,
                    BeforeTenths, Durability.RemainingTenths, Progress.LaserDamage() * 4, bDestroyed ? 1 : 0);
#endif
            const int32 PreviousUnlocked = SpaceFlight::UnlockedSector(Progress.Objective.Sequence);
            const bool bHitObjective = Progress.Objective.Kind == 5;
            const int32 HitReward = Progress.ObjectiveReward(Progress.Objective.RecordHit(Progress.RichOreChance() > 0.0f));
            auto CompleteObjective = [&](int32 Reward)
            {
                if (Reward <= 0) return;
                // Reward is int32 and AddResources accepts no more than requested.
                const int32 AcceptedReward = static_cast<int32>(Progress.AddResources(Reward));
                if (bFlightTrace)
                    UE_LOG(LogTemp, Display, TEXT("GOAL_COMPLETE reward=%d next=%d kind=%d"),
                        Reward, Progress.Objective.Sequence, Progress.Objective.Kind);
                UpgradeNotice = AcceptedReward > 0 ? SF_LOCAL_PRINTF(TEXT("Цель выполнена: +%d руды"), TEXT("Goal accomplished: +%d ore"), TEXT("Meta cumplida: +%d mineral"), AcceptedReward) :
                    FString(SpaceFlight::Localize(TEXT("Цель выполнена: предел учёта руды")));
                if (SpaceFlight::UnlockedSector(Progress.Objective.Sequence) > PreviousUnlocked)
                    UpgradeNotice = SpaceFlight::Localize(TEXT("Открыт новый сектор — клавиша C"));
                NoticeUntil = GetWorld()->GetRealTimeSeconds() + 3.0f;
            };
            UE_LOG(LogTemp, Verbose, TEXT("Asteroid hit index=%d size=%d remaining=%d destroyed=%d"),
                Target.Index, Target.Size, Durability.Remaining, bDestroyed ? 1 : 0);
            if (!bDestroyed)
            {
                CompleteObjective(HitReward);
                if (HitReward > 0) SaveProgress();
                bAnyImpact = true;
                FSpaceImpact& Impact = Impacts.AddDefaulted_GetRef();
                Impact.Position = Target.Position;
                Impact.Color = Target.Color;
                Impact.StartTime = T;
                Impact.Radius = Target.Size * Pixel * 0.5f;
                Impact.Seed = 937 + Target.Index * 997 + FMath::FloorToInt(T * 100.0f);
                continue;
            }
            LargestDestroyedSize = FMath::Max(LargestDestroyedSize, Target.Size);
            const bool bRichOre = SpaceFlight::IsRichOre(SectorSpawnSeed, Target.Index, Target.Pass,
                BaseGenerations[Target.Index], Progress.RichOreChance());
            BaseResetTimes[Target.Index] = T;
            ++BaseGenerations[Target.Index];
            const int32 EventBonus = (bStressMode || bTreeOpen) ? 0 :
                SpaceFlight::ScaledEventBonus(MineralEvent.ClaimBonus(Target.Index, AsteroidCount), Progress);
            const int32 Mined = Progress.Yield(Target.Size, Target.OreKind, bRichOre, bFirstHit) + EventBonus;
            if (EventBonus > 0 && FParse::Param(FCommandLine::Get(), TEXT("SpaceMineralTrace")))
                UE_LOG(LogTemp, Display, TEXT("MINERAL_BONUS index=%d amount=%d ore_kind=%d"), Target.Index, EventBonus, static_cast<int32>(Target.OreKind));
            if (bFlightTrace && Progress.Specialization == 2)
                UE_LOG(LogTemp, Display, TEXT("PRECISION_MINE size=%d first=%d bonus=%d mined=%d rich=%d"),
                    Target.Size, bFirstHit ? 1 : 0, bFirstHit ? 1 : 0, Mined, bRichOre ? 1 : 0);
            // Mined is int32; the accepted amount cannot exceed it.
            const int32 AcceptedOre = static_cast<int32>(Progress.AddOre(Mined, Target.OreKind));
            const bool bExpeditionWasActive = Progress.Expedition.State == SpaceFlight::EExpeditionState::Active;
            Progress.Expedition.Record(Progress.CurrentSector, static_cast<int32>(Target.OreKind), AcceptedOre, bStressMode);
            const bool bExpeditionReady = bExpeditionWasActive && Progress.Expedition.State == SpaceFlight::EExpeditionState::Ready;
            const int32 GoalReward = bHitObjective ? HitReward : Progress.ObjectiveReward(
                Progress.Objective.Record(Target.OreKind, AcceptedOre, bRichOre, Progress.RichOreChance() > 0.0f));
            CompleteObjective(GoalReward);
            if (bExpeditionReady)
            {
                UpgradeNotice = SpaceFlight::Localize(TEXT("Рейс выполнен — E: получить награду"));
                NoticeUntil = GetWorld()->GetRealTimeSeconds() + 4.0f;
            }
            if (GoalReward > 0 || EventBonus > 0 || bExpeditionReady) SaveProgress();
            UE_LOG(LogTemp, Verbose, TEXT("Laser hit asteroid %d; ore=%lld, weapons=%d"),
                Target.Index, Progress.Resources, Progress.Lasers());
            FSpaceDustBurst& Burst = DustBursts.AddDefaulted_GetRef();
            Burst.Position = Target.Position;
            Burst.Color = Target.Color;
            Burst.StartTime = T;
            Burst.Seed = 6029 + Target.Index * 997 + FMath::FloorToInt(T * 100.0f);
            Burst.Strength = SpaceFlight::AsteroidHitPoints(Target.Size);
            Burst.BonusOre = (bRichOre ? Burst.Strength * 2 - 1 : 0) + (Progress.Specialization == 2 && bFirstHit ? 1 : 0) +
                Progress.ConditionalOreBonus(Target.Size, bFirstHit) + EventBonus;
            Burst.BonusOre = FMath::Min(Burst.BonusOre, FMath::Max(0, AcceptedOre - (Burst.Strength * 2 - 1)));
        }
        if (LargestDestroyedSize > 0)
            PlayEventSound(SpaceFlight::SoundForImpact(true, LargestDestroyedSize));
        else if (bAnyImpact)
            PlayEventSound(SpaceFlight::ESoundEvent::Impact);
        if (Progress.Isotope > IsotopeBeforeVolley)
            PlayEventSound(SpaceFlight::ESoundEvent::Isotope);
    }

    const float BeamAge = T - LastLaserTime;
    if (BeamAge >= 0.0f && BeamAge < 0.19f)
    {
        for (const FSpaceBeam& Beam : LaserBeams)
        {
            const float LocalAge = BeamAge - (Beam.bReflected ? 0.03f : 0.0f);
            if (LocalAge < 0.0f || LocalAge >= 0.16f) continue;
            const float Fade = 1.0f - LocalAge / 0.16f;
            const FVector2D Origin = Beam.bReflected ? Beam.ReflectionOrigin : (Beam.WeaponIndex == 0 ? LaserOrigin :
                ShipXY(Beam.WeaponIndex == 1 ? 10.0f : 46.0f, 30.0f));
            const float Strength = Beam.bReflected ? 0.64f : 1.0f;
            DrawLine(Origin.X, Origin.Y, Beam.Target.X, Beam.Target.Y,
                FLinearColor(0.04f, 0.45f, 1.0f, 0.20f * Fade * Strength), (Beam.bReflected ? 7.0f : 12.0f) * Pixel);
            DrawLine(Origin.X, Origin.Y, Beam.Target.X, Beam.Target.Y,
                FLinearColor(0.20f, 0.85f, 1.0f, 0.75f * Fade * Strength), (Beam.bReflected ? 2.5f : 4.0f) * Pixel);
            DrawLine(Origin.X, Origin.Y, Beam.Target.X, Beam.Target.Y,
                FLinearColor(0.90f, 1.0f, 1.0f, Fade * Strength), (Beam.bReflected ? 1.0f : 1.5f) * Pixel);
            if (Beam.bReflected && CrystalGlowTexture && CrystalGlowTexture->GetResource())
            {
                const float Radius = 3.5f * Pixel;
                FCanvasTileItem Glow(Beam.Target - FVector2D(Radius, Radius), CrystalGlowTexture->GetResource(),
                    FVector2D(Radius * 2), FLinearColor(.45f, .85f, 1.0f, Fade * .7f));
                Glow.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Glow);
            }
            else if (!Beam.bReflected)
            {
                Rect(Beam.Target.X - 3 * Pixel, Beam.Target.Y - Pixel, 6 * Pixel, 2 * Pixel, FLinearColor(0.8f, 1.0f, 1.0f, Fade));
                Rect(Beam.Target.X - Pixel, Beam.Target.Y - 3 * Pixel, 2 * Pixel, 6 * Pixel, FLinearColor(0.8f, 1.0f, 1.0f, Fade));
            }
        }
    }

    // Brief impact chips signal a hit without looking like the final explosion.
    constexpr float ImpactLifetime = 0.24f;
    for (int32 I = Impacts.Num() - 1; I >= 0; --I)
    {
        const FSpaceImpact& Impact = Impacts[I];
        const float Age = T - Impact.StartTime;
        if (Age >= ImpactLifetime) { Impacts.RemoveAtSwap(I); continue; }
        const float Fade = FMath::Square(1.0f - Age / ImpactLifetime);
        FRandomStream Random(Impact.Seed);
        FLinearColor Chip = FMath::Lerp(Impact.Color, FLinearColor(0.55f, 0.78f, 0.90f), 0.45f);
        Chip.A = 0.75f * Fade;
        for (int32 Particle = 0; Particle < 7; ++Particle)
        {
            const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
            const float Distance = Impact.Radius * 0.25f + Age * Random.FRandRange(8.0f, 20.0f) * Unit;
            const float ChipSize = Random.FRandRange(0.7f, 1.5f) * Pixel;
            Rect(Impact.Position.X + FMath::Cos(Angle) * Distance,
                Impact.Position.Y + FMath::Sin(Angle) * Distance, ChipSize, ChipSize, Chip);
        }
    }

    // Isolated, repeatable A/B capture; never touches the player's progress.
#if WITH_DEV_AUTOMATION_TESTS
    static bool bBurstPreviewCreated = false;
    if (!bSaveEnabled && !bBurstPreviewCreated && FParse::Param(FCommandLine::Get(), TEXT("SpaceBurstPreview")))
    {
        bBurstPreviewCreated = true;
        const FLinearColor PreviewColors[] = { FLinearColor(.60f, .27f, .11f), BlueRockDust, BlackRockDust };
        for (int32 I = 0; I < 3; ++I)
        {
            FSpaceDustBurst& Preview = DustBursts.AddDefaulted_GetRef();
            Preview.Position = FVector2D(W * (.38f + .12f * I), H * .44f);
            Preview.Color = PreviewColors[I];
            Preview.StartTime = T;
            Preview.Seed = 4200 + I;
            Preview.Strength = I + 1;
            Preview.BonusOre = 0;
        }
    }
    const bool bLegacyBurstPreview = !bSaveEnabled && FParse::Param(FCommandLine::Get(), TEXT("SpaceBurstLegacyPreview"));
#else
    const bool bLegacyBurstPreview = false;
#endif
    // Every particle follows a stable outward trajectory and fades independently.
    constexpr float DustLifetime = 2.2f;
    for (int32 BurstIndex = DustBursts.Num() - 1; BurstIndex >= 0; --BurstIndex)
    {
        const FSpaceDustBurst& Burst = DustBursts[BurstIndex];
        const float Age = T - Burst.StartTime;
        if (Age >= DustLifetime)
        {
            DustBursts.RemoveAtSwap(BurstIndex);
            continue;
        }
        if (Burst.BonusOre > 0 && Age < 1.0f && GEngine)
            DrawText(FString::Printf(TEXT("+%d"), Burst.BonusOre), FLinearColor(1.0f, 0.78f, 0.35f, 1.0f - Age),
                Burst.Position.X + 6.0f * Pixel, Burst.Position.Y - (10.0f + Age * 16.0f) * Pixel,
                GEngine->GetSmallFont(), FMath::Max(0.65f, Unit * 0.20f));
        FRandomStream DustRandom(Burst.Seed);
        if (Age < 0.16f)
        {
            const float Fade = 1.0f - Age / 0.16f;
            FLinearColor Flash = FMath::Lerp(Burst.Color, FLinearColor(0.65f, 0.85f, 1.0f), 0.55f);
            Flash.A = 0.7f * Fade;
            const float Radius = (2.0f + Burst.Strength) * Pixel * (1.0f + Age * 3.0f);
            if (!bLegacyBurstPreview && CrystalGlowTexture && CrystalGlowTexture->GetResource())
            {
                FCanvasTileItem Glow(Burst.Position - FVector2D(Radius, Radius), CrystalGlowTexture->GetResource(),
                    FVector2D(2 * Radius, 2 * Radius), Flash);
                Glow.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Glow);
            }
            else
            {
                Rect(Burst.Position.X - Radius, Burst.Position.Y - Pixel, 2 * Radius, 2 * Pixel, Flash);
                Rect(Burst.Position.X - Pixel, Burst.Position.Y - Radius, 2 * Pixel, 2 * Radius, Flash);
            }
        }
        const int32 ParticleCount = 24 + 16 * Burst.Strength;
        for (int32 Particle = 0; Particle < ParticleCount; ++Particle)
        {
            const float Direction = DustRandom.FRandRange(0.0f, 6.28318f);
            const float Velocity = DustRandom.FRandRange(4.0f, 25.0f) * Unit * (0.85f + 0.15f * Burst.Strength);
            const float Drift = DustRandom.FRandRange(-3.0f, 3.0f) * Unit;
            const float ParticleLife = DustRandom.FRandRange(1.2f, DustLifetime);
            const float ParticleSize = DustRandom.FRandRange(0.7f, 2.2f) * Pixel;
            const float Brightness = DustRandom.FRandRange(0.55f, 1.35f);
            if (Age >= ParticleLife) continue;
            const float Fade = FMath::Square(1.0f - Age / ParticleLife);
            const float Distance = Velocity * Age / (1.0f + 0.6f * Age);
            const float DustX = Burst.Position.X + FMath::Cos(Direction) * Distance + Drift * Age;
            const float DustY = Burst.Position.Y + FMath::Sin(Direction) * Distance + 5.0f * Unit * Age;
            FLinearColor DustColor = Burst.Color * Brightness;
            DustColor.A = Fade * 0.85f;
            Rect(DustX, DustY, ParticleSize, ParticleSize, DustColor);
        }
        if (Age < 0.85f)
        {
            const float Fade = FMath::Square(1.0f - Age / 0.85f);
            for (int32 Fragment = 0; Fragment < 2 + 2 * Burst.Strength; ++Fragment)
            {
                const float Direction = DustRandom.FRandRange(0.0f, 2.0f * PI);
                const float Distance = Age * DustRandom.FRandRange(12.0f, 27.0f) * Unit;
                const float Size = DustRandom.FRandRange(1.3f, 2.7f) * Pixel;
                FLinearColor FragmentColor = Burst.Color * 1.8f;
                FragmentColor.A = 0.9f * Fade;
                Rect(Burst.Position.X + FMath::Cos(Direction) * Distance,
                    Burst.Position.Y + FMath::Sin(Direction) * Distance, Size, Size, FragmentColor);
            }
        }
    }

    // A transparent spherical projection stays behind the ship.  The schedule
    // uses game time so the field freezes with the rest of the scene on pause.
    if (Shield.Update(T) && FParse::Param(FCommandLine::Get(), TEXT("SpaceShieldTrace")))
        UE_LOG(LogTemp, Display, TEXT("SHIELD_PULSE start=%.3f next=%.3f pattern=%d duration=%.3f"),
            Shield.Current.Start, Shield.NextStart, Shield.Current.Pattern, Shield.Current.Duration);
    const FVector2D FieldCenter = ShipXY(28.0f, 29.0f);
    const float FieldRadius = 47.0f * Pixel;
    static const TArray<SpaceFlight::FShieldSegment> FieldMesh = SpaceFlight::BuildShieldSegments();
    TArray<FCanvasUVTri> FieldTriangles;
    FieldTriangles.Reserve((FieldMesh.Num() + 48) * 2);
    auto AddFieldSegment = [&](FVector2D A, FVector2D B, FLinearColor Color)
    {
        const FVector2D Direction = (B - A).GetSafeNormal();
        const FVector2D Normal(-Direction.Y * Pixel * .55f, Direction.X * Pixel * .55f);
        FCanvasUVTri Triangle;
        Triangle.V0_UV = Triangle.V1_UV = Triangle.V2_UV = FVector2D::ZeroVector;
        Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = Color;
        Triangle.V0_Pos = A - Normal;
        Triangle.V1_Pos = A + Normal;
        Triangle.V2_Pos = B + Normal;
        FieldTriangles.Add(Triangle);
        Triangle.V1_Pos = B + Normal;
        Triangle.V2_Pos = B - Normal;
        FieldTriangles.Add(Triangle);
    };
    for (int32 I = 0; I < FieldMesh.Num(); ++I)
    {
        const auto& Segment = FieldMesh[I];
        const float Strength = SpaceFlight::ShieldSegmentStrength(Shield.Current, Segment, I, T);
        const FVector2D A = FieldCenter + Segment.A * FieldRadius;
        const FVector2D B = FieldCenter + Segment.B * FieldRadius;
        AddFieldSegment(A, B, FLinearColor(.12f, .68f, .95f, .035f + .28f * Strength));
    }
    constexpr int32 OutlineSegments = 48;
    for (int32 I = 0; I < OutlineSegments; ++I)
    {
        const float A = 2.0f * PI * I / OutlineSegments;
        const float B = 2.0f * PI * (I + 1) / OutlineSegments;
        const SpaceFlight::FShieldSegment Segment{ FVector2D(FMath::Cos(A), FMath::Sin(A)), FVector2D(FMath::Cos(B), FMath::Sin(B)) };
        const float Strength = SpaceFlight::ShieldSegmentStrength(Shield.Current, Segment, FieldMesh.Num() + I, T);
        const FVector2D From = FieldCenter + Segment.A * FieldRadius;
        const FVector2D To = FieldCenter + Segment.B * FieldRadius;
        AddFieldSegment(From, To, FLinearColor(.18f, .72f, 1.0f, .10f + .35f * Strength));
    }
    if (!FieldTriangles.IsEmpty())
    {
        FCanvasTriangleItem FieldItem(FieldTriangles, GWhiteTexture);
        FieldItem.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(FieldItem);
    }

    // Layered, independent plumes flicker at a pixel-art cadence. The engine
    // opposite a turn burns harder, and the engine upgrade adds a longer jet.
    const int32 Frame = FMath::FloorToInt(T * 15.0f);
    for (int32 Engine = 0; Engine < 2; ++Engine)
    {
        const int32 Center = Engine == 0 ? 15 : 37;
        const int32 Steering = FMath::RoundToInt(ShipBank * (Engine == 0 ? 4.0f : -4.0f));
        const int32 Flicker = ((Frame * 7 + Engine * 11) % 5) - 2;
        const int32 Length = FMath::Max(12, 21 + Flicker + Steering + (Appearance.bEngineUpgrade ? 7 : 0));
        const float Glow = 0.10f + 0.025f * FMath::Sin(T * 9.0f + Engine * 2.1f);
        ShipRect(Center - 6, 55, 12, 7, FLinearColor(0.10f, 0.40f, 0.90f, Glow));
        for (int32 Row = 0; Row < Length; ++Row)
        {
            const float Fraction = float(Row) / float(Length);
            const int32 HalfWidth = Fraction < 0.22f ? 4 : (Fraction < 0.62f ? 3 : (Fraction < 0.86f ? 2 : 1));
            const int32 Drift = FMath::RoundToInt(-ShipBank * Row * 0.09f +
                FMath::Sin(T * 6.5f + Row * 0.34f + Engine * 2.0f) * Fraction);
            for (int32 DX = -HalfWidth; DX <= HalfWidth; ++DX)
            {
                if (Fraction > 0.68f && ((Frame + Row * 3 + DX + Engine) & 3) == 0) continue;
                const bool bCore = FMath::Abs(DX) <= (Fraction < 0.55f ? 1 : 0);
                FLinearColor Flame = bCore && Fraction < 0.35f ? FLinearColor(0.83f, 0.98f, 1.0f) :
                    (bCore ? FLinearColor(0.32f, 0.86f, 1.0f) :
                    (Fraction < 0.65f ? FLinearColor(0.09f, 0.53f, 1.0f) : FLinearColor(0.08f, 0.25f, 0.73f)));
                if (Appearance.bEngineUpgrade && !bCore && Fraction > 0.30f)
                    Flame = FLinearColor(0.24f, 0.34f, 0.95f);
                ShipRect(float(Center + DX + Drift), float(56 + Row), 1, 1, Flame);
            }
        }
        for (int32 Spark = 0; Spark < 4; ++Spark)
        {
            const float Life = FMath::Frac(T * 0.65f + Spark * 0.27f + Engine * 0.13f);
            const float Drift = -ShipBank * (Length + Life * 11.0f) * 0.09f +
                FMath::Sin(Spark * 5.1f + Engine) * (1.0f + Life * 3.0f);
            ShipRect(Center + Drift, 56.0f + Length + Life * 12.0f, 1, 1,
                FLinearColor(0.24f, 0.67f, 1.0f, (1.0f - Life) * 0.75f));
        }
    }
    for (int32 Row = 0; Row < UE_ARRAY_COUNT(Ship); ++Row)
    {
        for (int32 Col = 0; Ship[Row][Col] != 0; ++Col)
        {
            const TCHAR Cell = Ship[Row][Col];
            if (Cell >= '1' && Cell <= '7')
                ShipRect(Col * 2.0f, Row * 2.0f, 2, 2, Palette[Cell - '0']);
        }
    }

    // Fine highlights and seams on the canopy, fuselage, wings and engine housings.
    auto Detail = [&](int32 X, int32 Y, int32 Width, int32 Height, const FLinearColor& Color)
    {
        ShipRect(float(X), float(Y), float(Width), float(Height), Color);
    };
    const FLinearColor Highlight(0.78f, 0.93f, 0.91f);
    const FLinearColor Panel(0.11f, 0.23f, 0.32f);
    const FLinearColor Glint(0.72f, 1.0f, 1.0f);
    Detail(27, 5, 2, 7, Highlight);
    Detail(25, 15, 2, 7, Glint);
    Detail(27, 16, 1, 3, Glint);
    Detail(25, 26, 1, 15, Highlight);
    Detail(30, 26, 1, 15, Panel);
    Detail(22, 34, 2, 1, Panel);
    Detail(32, 34, 2, 1, Panel);
    Detail(10, 37, 10, 1, Panel);
    Detail(36, 37, 10, 1, Panel);
    Detail(6, 43, 13, 1, Highlight);
    Detail(37, 43, 13, 1, Highlight);
    Detail(11, 46, 7, 1, Panel);
    Detail(38, 46, 7, 1, Panel);
    Detail(12, 52, 5, 1, Highlight);
    Detail(34, 52, 5, 1, Highlight);
    Detail(3, 40, 2, 2, FLinearColor(1.0f, 0.54f, 0.25f));
    Detail(49, 40, 2, 2, FLinearColor(1.0f, 0.54f, 0.25f));

    // Equipment is mounted on the hull, and each purchased tier adds a feature.
    const FLinearColor DarkMetal(0.035f, 0.075f, 0.105f);
    const FLinearColor Sensor(0.24f, 0.95f, 0.78f);
    const FLinearColor Alloy(0.94f, 0.62f, 0.23f);
    const FLinearColor Ice(0.38f, 0.86f, 1.0f);
    if (Appearance.RadarLevel >= 1)
    {
        Detail(22, 10, 3, 4, DarkMetal); Detail(32, 10, 3, 4, DarkMetal);
        Detail(23, 10, 2, 2, Sensor); Detail(32, 10, 2, 2, Sensor);
    }
    if (Appearance.RadarLevel >= 2)
    {
        Detail(20, 7, 2, 6, Sensor); Detail(36, 7, 2, 6, Sensor);
        Detail(26, 6, 4, 2, FLinearColor(0.60f, 1.0f, 0.90f));
    }
    if (Appearance.RadarLevel >= 3)
    {
        Detail(27, -2, 2, 7, DarkMetal);
        Detail(26, -3, 4, 2, Sensor);
        Detail(27, -4, 2, 1, FLinearColor(0.82f, 1.0f, 0.95f));
    }
    if (Appearance.CollectorLevel >= 1)
    {
        Detail(3, 37, 6, 5, DarkMetal); Detail(47, 37, 6, 5, DarkMetal);
        Detail(4, 37, 5, 2, Alloy); Detail(47, 37, 5, 2, Alloy);
    }
    if (Appearance.CollectorLevel >= 2)
    {
        Detail(0, 35, 4, 7, Alloy); Detail(52, 35, 4, 7, Alloy);
        Detail(1, 35, 2, 2, FLinearColor(1.0f, 0.86f, 0.43f));
        Detail(53, 35, 2, 2, FLinearColor(1.0f, 0.86f, 0.43f));
    }
    auto DrawWingLaser = [&](int32 X)
    {
        Detail(X, 29, 6, 9, DarkMetal);
        Detail(X + 1, 29, 4, 5, FLinearColor(0.19f, 0.48f, 0.60f));
        Detail(X + 2, 28, 2, 3, FLinearColor(0.58f, 0.95f, 1.0f));
    };
    if (Appearance.bLeftLaser) DrawWingLaser(7);
    if (Appearance.bRightLaser) DrawWingLaser(43);
    if (Appearance.CoolingLevel >= 1)
    {
        Detail(20, 38, 3, 6, Ice); Detail(33, 38, 3, 6, Ice);
        Detail(20, 40, 3, 1, DarkMetal); Detail(33, 40, 3, 1, DarkMetal);
    }
    if (Appearance.CoolingLevel >= 2)
    {
        Detail(18, 44, 5, 4, Ice); Detail(33, 44, 5, 4, Ice);
        Detail(19, 45, 3, 1, FLinearColor(0.84f, 1.0f, 1.0f));
        Detail(34, 45, 3, 1, FLinearColor(0.84f, 1.0f, 1.0f));
    }
    if (Appearance.bEngineUpgrade)
    {
        Detail(10, 52, 10, 3, Alloy); Detail(32, 52, 10, 3, Alloy);
        Detail(12, 55, 6, 2, FLinearColor(0.50f, 0.89f, 1.0f));
        Detail(34, 55, 6, 2, FLinearColor(0.50f, 0.89f, 1.0f));
    }
    const float UpgradeAge = T - UpgradeFlashStart;
    if (UpgradeAge >= 0.0f && UpgradeAge < 0.9f)
    {
        const float Fade = 1.0f - UpgradeAge / 0.9f;
        const float Distance = 18.0f + UpgradeAge * 20.0f;
        for (int32 Spark = 0; Spark < 8; ++Spark)
        {
            const float Angle = Spark * (2.0f * PI / 8.0f);
            ShipRect(28.0f + FMath::Cos(Angle) * Distance, 29.0f + FMath::Sin(Angle) * Distance,
                2, 2, FLinearColor(1.0f, 0.76f, 0.30f, Fade));
        }
    }

    DrawFlightHUD(W, H, TotalFlightSeconds(), bPaused);
    if (!bTreeOpen && !bStressMode && MineralEvent.Phase != SpaceFlight::EMineralPhase::Waiting && GEngine)
    {
        const bool bHasTracked = SpaceFlight::DescribeTrackedUpgrade(Progress).Status != SpaceFlight::ETrackedUpgradeStatus::None;
        const SpaceFlight::FCompactHudLayout Layout = SpaceFlight::MakeCompactHudLayout(W, H, bHasTracked);
        SpaceFlight::FPortraitHudOptions Options;
        Options.bTracked = bHasTracked;
        Options.bDetailed = bDetailedHUD;
        Options.bPopup = bSpecializationSelection || bProductionSelection || bRefiningSelection || bExpeditionSelection;
        Options.bPaused = bPaused;
        Options.bReset = bPaused && !bStressMode && !bSaveBlocked;
        Options.bNotice = !bPaused && !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil;
        Options.bEvent = true;
        Options.bStress = bStressMode;
        Options.bWarning = bSaveBlocked || bSaveWriteFailed;
        const auto Portrait = SpaceFlight::MakePortraitHudLayout(W, H, Options);
        if (Layout.bValid)
        {
            const float S = Portrait.bValid ? Portrait.Scale : Layout.Scale;
            const bool bCrystal = MineralEvent.Kind == SpaceFlight::ERareEventKind::UnstableCrystal;
            const bool bDeposit = MineralEvent.Kind == SpaceFlight::ERareEventKind::LargeDeposit;
            FString Message;
            if (MineralEvent.Phase == SpaceFlight::EMineralPhase::Warning)
            {
                const int32 Seconds = static_cast<int32>(FMath::CeilToInt(MineralEvent.Remaining));
                if (bCrystal) Message = SF_LOCAL_PRINTF(TEXT("НЕСТАБИЛЬНЫЙ КРИСТАЛЛ ЧЕРЕЗ %d СЕК."), TEXT("UNSTABLE CRYSTAL IN %d SEC."), TEXT("CRISTAL INESTABLE EN %d SEC."), Seconds);
                else if (bDeposit) Message = SF_LOCAL_PRINTF(TEXT("КРУПНОЕ МЕСТОРОЖДЕНИЕ ЧЕРЕЗ %d СЕК."), TEXT("LARGE DEPOSIT IN %d SEC."), TEXT("GRAN DEPÓSITO EN %d SEC."), Seconds);
                else Message = SF_LOCAL_PRINTF(TEXT("МИНЕРАЛЬНЫЙ ПОТОК ЧЕРЕЗ %d СЕК."), TEXT("MINERAL STREAM IN %d SEC."), TEXT("CORRIENTE MINERAL EN %d S"), Seconds);
            }
            else if (bDeposit)
                Message = MineralEvent.Claims > 0 ? SpaceFlight::Localize(TEXT("МЕСТОРОЖДЕНИЕ ДОБЫТО")) :
                    SF_LOCAL_PRINTF(TEXT("МЕСТОРОЖДЕНИЕ: +%d РУДЫ  ·  %d СЕК."), TEXT("DEPOSIT: +%d ORE · %d SEC."), TEXT("DEPÓSITO: +%d MINERAL · %d SEC."), SpaceFlight::ScaledEventBonus(9, Progress), static_cast<int32>(FMath::CeilToInt(MineralEvent.Remaining)));
            else if (!bCrystal)
                Message = SF_LOCAL_PRINTF(TEXT("МИНЕРАЛЬНЫЙ ПОТОК  ·  МЕТКА: +%d РУДЫ"), TEXT("MINERAL STREAM · MARKED: +%d ORE"), TEXT("CORRIENTE MINERAL · MARCADO: +%d MINERAL"), SpaceFlight::ScaledEventBonus(2, Progress));
            else if (MineralEvent.Claims > 0)
                Message = SpaceFlight::Localize(TEXT("НЕСТАБИЛЬНЫЙ КРИСТАЛЛ ДОБЫТ"));
            else if (MineralEvent.Remaining > 20.0)
                Message = SF_LOCAL_PRINTF(TEXT("КРИСТАЛЛ: +%d РУДЫ  ·  ЧЕРЕЗ %d СЕК. +%d"), TEXT("CRYSTAL: +%d ORE · IN %d SEC. +%d"), TEXT("CRISTAL: +%d MINERAL · EN %d SEC. +%d"), SpaceFlight::ScaledEventBonus(6, Progress), static_cast<int32>(FMath::CeilToInt(MineralEvent.Remaining - 20.0)), SpaceFlight::ScaledEventBonus(3, Progress));
            else
                Message = SF_LOCAL_PRINTF(TEXT("КРИСТАЛЛ: +%d РУДЫ  ·  ОСТАЛОСЬ %d СЕК."), TEXT("CRYSTAL: +%d ORE · %d SEC REMAINING."), TEXT("CRISTAL: +%d MINERAL · %d SEC RESTANTE."), SpaceFlight::ScaledEventBonus(3, Progress), static_cast<int32>(FMath::CeilToInt(MineralEvent.Remaining)));
            float TextWidth = 0, TextHeight = 0;
            GetTextSize(Message, TextWidth, TextHeight, GEngine->GetSmallFont(), .9f * S);
            // The portrait event has a reserved row below any active popup.
            const float Available = Portrait.bValid ? Portrait.Event.Width - 16 * S :
                FMath::Max(1.0f, Layout.Resources.X - (Layout.Status.X + Layout.Status.Width) - 24.0f * S);
            const float Fit = TextWidth > 0 ? FMath::Min(1.0f, Available / TextWidth) : 1.0f;
            TextWidth *= Fit;
            TextHeight *= Fit;
            const float X = Portrait.bValid ? Portrait.Event.X + (Portrait.Event.Width - TextWidth) * .5f :
                (Layout.Status.X + Layout.Status.Width + Layout.Resources.X - TextWidth) * .5f;
            float Y = Portrait.bValid ? Portrait.Event.Y + 7 * S :
                FMath::Max(Layout.Goal.Y + Layout.Goal.Height, Layout.Resources.Y + Layout.Resources.Height) + 9.0f * S;
            if (!Portrait.bValid)
            {
                if (!bPaused && !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil)
                    Y = FMath::Max(Y, bDetailedHUD ? 128.0f * S : Layout.Resources.Y + Layout.Resources.Height + 42.0f * S);
                if (bPaused) Y = FMath::Max(Y, (bDetailedHUD ? 166.0f : 136.0f) * S);
                if (!bDetailedHUD && bRefiningSelection)
                {
                    const auto Popup = SpaceFlight::RefiningPopupRect(Layout, W);
                    Y = FMath::Max(Y, Popup.Y + Popup.Height + 11 * S);
                    if (bPaused)
                    {
                        SpaceFlight::FHudRect Pause{ W * .5f - 125 * S, Layout.Clock.Y + 48 * S, 250 * S, 28 * S };
                        Pause = SpaceFlight::BelowIfOverlapping(Pause, Popup, 8 * S);
                        SpaceFlight::FHudRect Last = Pause;
                        if (!bStressMode && !bSaveBlocked)
                        {
                            Last = { W * .5f - 145 * S, Pause.Y + 34 * S, 290 * S, 27 * S };
                            Last = SpaceFlight::BelowIfOverlapping(Last, Popup, 8 * S);
                        }
                        Y = FMath::Max(Y, Last.Y + Last.Height + 11 * S);
                    }
                    else if (!UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil)
                    {
                        const float Gap = Layout.Resources.X - (Layout.Status.X + Layout.Status.Width) - 16 * S;
                        const float NoticeWidth = FMath::Min(380 * S, Gap);
                        SpaceFlight::FHudRect Notice{ (Layout.Status.X + Layout.Status.Width + Layout.Resources.X - NoticeWidth) * .5f,
                            Layout.Resources.Y + Layout.Resources.Height + 8 * S, NoticeWidth, 28 * S };
                        Notice = SpaceFlight::BelowIfOverlapping(Notice, Popup, 8 * S);
                        Y = FMath::Max(Y, Notice.Y + Notice.Height + 11 * S);
                    }
                }
            }
            DrawRect(FLinearColor(.005f, .010f, .02f, .72f), X - 8 * S, Y - 3 * S, TextWidth + 16 * S, TextHeight + 6 * S);
            const FLinearColor LabelColor = bCrystal ? FLinearColor(.84f, .66f, 1.0f) :
                (bDeposit ? FLinearColor(.53f, 1.0f, .77f) : FLinearColor(.98f, .79f, .42f));
            DrawText(Message, LabelColor, X, Y, GEngine->GetSmallFont(), .9f * S * Fit);
        }
    }
    if (!bTreeOpen)
    {
        DrawText(FString::Printf(TEXT("%d"), StarBrightness.Value), FLinearColor(0.65f, 0.70f, 0.75f),
            12.0f, H - 23.0f, GEngine->GetSmallFont(), 0.8f);
        const FString TargetLabel = SF_LOCAL_PRINTF(TEXT("X — ЦЕЛИ: %s"), TEXT("X - TARGET: %s"), TEXT("X - OBJETIVO: %s"), TargetingModeName(Progress.TargetingMode));
        float LabelWidth = 0.0f, LabelHeight = 0.0f;
        GetTextSize(TargetLabel, LabelWidth, LabelHeight, GEngine->GetSmallFont(), 0.8f);
        DrawText(TargetLabel, FLinearColor(0.78f, 0.76f, 0.66f), FMath::Max(48.0f, W - LabelWidth - 12.0f),
            H - 23.0f, GEngine->GetSmallFont(), 0.8f);
    }
    if (bTreeOpen)
    {
        if (bJournalOpen) DrawDiscoveryJournal(W, H);
        else DrawUpgradeTree(W, H);
    }
    const TCHAR* LanguageTag = SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::English ? TEXT("EN") :
        SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::Spanish ? TEXT("ES") : TEXT("RU");
    const FString LanguageHint = FString::Printf(TEXT("L: %s / RU / EN / ES"), LanguageTag);
    float LanguageWidth = 0.0f, LanguageHeight = 0.0f;
    GetTextSize(LanguageHint, LanguageWidth, LanguageHeight, GEngine->GetSmallFont(), 0.75f);
    const float LanguageX = bTreeOpen ? FMath::Max(12.0f, W - LanguageWidth - 46.0f) : FMath::Max(48.0f, (W - LanguageWidth) * 0.5f);
    const float LanguageY = bTreeOpen ? 42.0f : H - 23.0f;
    DrawText(LanguageHint, FLinearColor(0.65f, 0.70f, 0.75f), LanguageX, LanguageY, GEngine->GetSmallFont(), 0.75f);
    DrawSurprise(W, H);

#if WITH_DEV_AUTOMATION_TESTS
    bool bFlightTraceWrittenThisFrame = false;
#endif
    if (bFlightTrace)
    {
        const int32 Second = FMath::FloorToInt(FPlatformTime::Seconds() - FlightTraceStart);
        if (Second != FlightTraceSecond)
        {
            FlightTraceSecond = Second;
            int32 Purchased = 0;
            for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) if (Progress.Has(I)) ++Purchased;
            UE_LOG(LogTemp, Display, TEXT("FLIGHT_TRACE second=%d x=%.1f bank=%.2f auto=%d paused=%d tree=%d ore=%lld collected=%lld spent=%lld game_time=%.1f lasers=%d range=%.0f radar=%d collector=%d cooling=%d engine=%d asteroids=%d isotope=%lld blue=%lld black=%lld damage=%d purchased=%d goal=%d converted=%lld isotope_spent=%lld"),
                Second, ShipX, ShipBank, bAutopilot ? 1 : 0, bPaused ? 1 : 0, bTreeOpen ? 1 : 0,
                Progress.Resources, Progress.TotalCollected, Progress.TotalSpent, T, Progress.Lasers(), Progress.Range(),
                Appearance.RadarLevel, Appearance.CollectorLevel, Appearance.CoolingLevel, Appearance.bEngineUpgrade ? 1 : 0,
                AsteroidCount, Progress.Isotope, Progress.BlueOre, Progress.BlackOre, Progress.LaserDamage(), Purchased,
                Progress.Objective.Sequence, Progress.OreConverted, Progress.IsotopeSpent);
#if WITH_DEV_AUTOMATION_TESTS
            bFlightTraceWrittenThisFrame = true;
#endif
        }
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bFlightTraceWrittenThisFrame && bFreshCheckpointPending && T >= FreshCheckpointSeconds)
    {
        bFreshCheckpointPending = false;
        if (!PlayerOwner || !PlayerOwner->SetPause(true) || !GetWorld()->IsPaused())
        {
            UE_LOG(LogTemp, Error, TEXT("FRESH_CHECKPOINT_ERROR reason=pause_failed game_time=%.3f"), T);
        }
        else
        {
            // Freeze the sampled state without OnPauseChanged: this is
            // a test fixture, and the saved autopilot must remain unchanged.
            const int64 PreviousRevision = SaveRevision;
            SaveProgress();
            const bool bFailed = bSaveWriteFailed || SaveRevision <= PreviousRevision;
            if (bFailed || bSaveBlocked)
            {
                UE_LOG(LogTemp, Error, TEXT("FRESH_CHECKPOINT_FROZEN revision=%lld failed=%d blocked=%d game_time=%.3f"),
                    SaveRevision, bFailed ? 1 : 0, bSaveBlocked ? 1 : 0, T);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("FRESH_CHECKPOINT_FROZEN revision=%lld failed=0 blocked=0 game_time=%.3f"),
                    SaveRevision, T);
            }
        }
    }
#endif

#if WITH_DEV_AUTOMATION_TESTS
    if (bFrameProfile)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now >= FrameProfileStart)
        {
            if (FrameProfileLast > 0.0) FrameProfileSamples.Add((Now - FrameProfileLast) * 1000.0);
            FrameProfileLast = Now;
            if (Now - FrameProfileStart >= 30.0 && !FrameProfileSamples.IsEmpty())
            {
                FrameProfileSamples.Sort();
                AutoProfileSamples.Sort();
                double FrameSum = 0.0, AutoSum = 0.0;
                for (double Sample : FrameProfileSamples) FrameSum += Sample;
                for (double Sample : AutoProfileSamples) AutoSum += Sample;
                auto Percentile = [](const TArray<double>& Samples, double Fraction) {
                    return Samples.IsEmpty() ? 0.0 : Samples[FMath::Clamp(FMath::CeilToInt(Samples.Num() * Fraction) - 1, 0, Samples.Num() - 1)];
                };
                UE_LOG(LogTemp, Display, TEXT("FRAME_PROFILE tree=%d samples=%d mean_ms=%.2f p95_ms=%.2f p99_ms=%.2f max_ms=%.2f auto_samples=%d auto_mean_ms=%.2f auto_p95_ms=%.2f"),
                    bTreeOpen ? 1 : 0, FrameProfileSamples.Num(), FrameSum / FrameProfileSamples.Num(),
                    Percentile(FrameProfileSamples, .95), Percentile(FrameProfileSamples, .99), FrameProfileSamples.Last(),
                    AutoProfileSamples.Num(), AutoProfileSamples.IsEmpty() ? 0.0 : AutoSum / AutoProfileSamples.Num(), Percentile(AutoProfileSamples, .95));
                bFrameProfile = false;
            }
        }
    }
#endif

    if (bFpsCheck)
    {
        ++FpsCheckFrames;
        const double Elapsed = FPlatformTime::Seconds() - FpsCheckStart;
        if (Elapsed >= 6.0)
        {
            UE_LOG(LogTemp, Display, TEXT("FPS_CHECK mode=%s seconds=%.1f fps=%.1f"),
                bStressMode ? TEXT("stress") : TEXT("normal"), Elapsed, FpsCheckFrames / Elapsed);
            bFpsCheck = false;
        }
    }

    if (bBenchmark && bStressMode && !bPaused)
    {
        const double Now = FPlatformTime::Seconds();
        const double FrameTime = Now - BenchmarkLastFrameTime;
        BenchmarkLastFrameTime = Now;
        BenchmarkFrameTotal += FrameTime;
        BenchmarkWorstFrame = FMath::Max(BenchmarkWorstFrame, FrameTime);
        ++BenchmarkFrames;
        if (Now - BenchmarkWindowStart >= 1.0)
        {
            BenchmarkRecentFps = BenchmarkFrames / BenchmarkFrameTotal;
            BenchmarkRecentWorstFrame = BenchmarkWorstFrame;
            BenchmarkWindowStart = Now;
            BenchmarkFrameTotal = 0.0;
            BenchmarkWorstFrame = 0.0;
            BenchmarkFrames = 0;
        }
        const int32 CheckpointCount = SpaceFlight::NormalAsteroidCount + 10 * (BenchmarkCheckpoint / 5);
        // Wait for a frame drawn after the scheduled batch has actually been added.
        if (BenchmarkCheckpoint <= 300 && AsteroidCount >= CheckpointCount &&
            Now - StressStartTime >= BenchmarkCheckpoint)
        {
            UE_LOG(LogTemp, Display, TEXT("STRESS_BENCH seconds=%d asteroids=%d fps_1s=%.1f worst_frame_ms_1s=%.1f"),
                BenchmarkCheckpoint, AsteroidCount, BenchmarkRecentFps, BenchmarkRecentWorstFrame * 1000.0);
            BenchmarkCheckpoint += 100;
        }
    }
}

void ASpaceFlightHUD::DrawCompactFlightHUD(float W, float H, float Elapsed, bool bPaused)
{
    if (SpaceFlight::UsePortraitHud(W, H))
    {
        if (!GEngine || !GetWorld()) return;
        SpaceFlight::FPortraitHudOptions Options;
        const auto Tracked = SpaceFlight::DescribeTrackedUpgrade(Progress);
        Options.bTracked = Tracked.Status != SpaceFlight::ETrackedUpgradeStatus::None;
        Options.bDetailed = bDetailedHUD;
        Options.bPopup = bSpecializationSelection || bProductionSelection || bRefiningSelection || bExpeditionSelection;
        Options.bPaused = bPaused;
        Options.bReset = bPaused && !bStressMode && !bSaveBlocked;
        Options.bNotice = !bPaused && !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil;
        Options.bEvent = !bTreeOpen && !bStressMode && MineralEvent.Phase != SpaceFlight::EMineralPhase::Waiting;
        Options.bStress = bStressMode;
        Options.bWarning = bSaveBlocked || bSaveWriteFailed;
        const auto Layout = SpaceFlight::MakePortraitHudLayout(W, H, Options);
        if (!Layout.bValid) return;
        const float S = Layout.Scale;
        UFont* Font = GEngine->GetSmallFont();
        const FLinearColor Gold(.78f, .56f, .27f), PaleGold(.96f, .85f, .63f);
        const FLinearColor Cool(.80f, .90f, .93f), Muted(.72f, .72f, .68f);
        const FLinearColor Active(.55f, .95f, .72f), Blue(.53f, .82f, 1.0f);
        auto Panel = [&](const SpaceFlight::FHudRect& R)
        {
            DrawRect(FLinearColor(.004f, .009f, .018f, .87f), R.X, R.Y, R.Width, R.Height);
            const float L = 10 * S, T = FMath::Max(1.0f, S);
            DrawLine(R.X, R.Y, R.X + L, R.Y, Gold, T); DrawLine(R.X, R.Y, R.X, R.Y + L, Gold, T);
            DrawLine(R.X + R.Width - L, R.Y, R.X + R.Width, R.Y, Gold, T); DrawLine(R.X + R.Width, R.Y, R.X + R.Width, R.Y + L, Gold, T);
            DrawLine(R.X, R.Y + R.Height, R.X + L, R.Y + R.Height, Gold, T); DrawLine(R.X, R.Y + R.Height - L, R.X, R.Y + R.Height, Gold, T);
            DrawLine(R.X + R.Width - L, R.Y + R.Height, R.X + R.Width, R.Y + R.Height, Gold, T); DrawLine(R.X + R.Width, R.Y + R.Height - L, R.X + R.Width, R.Y + R.Height, Gold, T);
        };
        auto Text = [&](const FString& Value, const FLinearColor& Color, float X, float Y, float Width, float Size = .9f)
        {
            float TW = 0, TH = 0;
            GetTextSize(Value, TW, TH, Font, Size * S);
            const float Fit = TW > 0 ? FMath::Min(1.0f, FMath::Max(0.0f, Width) / TW) : 1.0f;
            DrawText(Value, Color, X, Y, Font, Size * S * Fit);
        };
        auto Center = [&](const FString& Value, const FLinearColor& Color, const SpaceFlight::FHudRect& R, float Size = .9f)
        {
            float TW = 0, TH = 0;
            GetTextSize(Value, TW, TH, Font, Size * S);
            const float Fit = TW > 0 ? FMath::Min(1.0f, (R.Width - 16 * S) / TW) : 1.0f;
            DrawText(Value, Color, R.X + (R.Width - TW * Fit) * .5f, R.Y + 7 * S, Font, Size * S * Fit);
        };
        const TCHAR* Goals[] = { SpaceFlight::Localize(TEXT("АСТЕРОИДЫ")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА")), SpaceFlight::Localize(TEXT("ДОБЫЧА РУДЫ")), SpaceFlight::Localize(TEXT("БОГАТЫЕ ЗАЛЕЖИ")), SpaceFlight::Localize(TEXT("ПОПАДАНИЯ")) };
        bool bUpgradeAvailable = false;
        for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) bUpgradeAvailable |= Progress.CanPurchase(I);
        Panel(Layout.Status);
        Text(SpaceFlight::SectorStatusLabel(bAutopilot, Progress.CurrentSector, Progress.Objective.Sequence,
            SpaceFlight::CanChangeSectorNow(bPaused, bStressMode, TotalFlightSeconds(), Progress.SectorReadyAt)),
            bAutopilot ? Active : Cool, Layout.Status.X + 10 * S, Layout.Status.Y + 7 * S,
            Layout.Status.Width - 130 * S);
        Text(bUpgradeAvailable ? SpaceFlight::Localize(TEXT("TAB: ДЕРЕВО +")) : SpaceFlight::Localize(TEXT("TAB: ДЕРЕВО")), bUpgradeAvailable ? Active : PaleGold,
            Layout.Status.X + Layout.Status.Width - 118 * S, Layout.Status.Y + 7 * S, 110 * S, .80f);
        const bool bRewardReady = Progress.Expedition.State == SpaceFlight::EExpeditionState::Ready && !bStressMode;
        const FString ExpeditionSector = SpaceFlight::ExpeditionSectorHint(Progress.Expedition, Progress.CurrentSector, bStressMode);
        const FString Profile = ExpeditionSector.IsEmpty() ? SF_LOCAL_PRINTF(TEXT("ПРОФИЛЬ: %s · E — %s"), TEXT("PROFILE: %s · E — %s"), TEXT("PERFIL: %s · E — %s"),
            SpecializationName(Progress.Specialization), SpaceFlight::ExpeditionActionWord(Progress.Expedition.State, bStressMode)) : ExpeditionSector;
        Text(Profile, !ExpeditionSector.IsEmpty() ? PaleGold : (bRewardReady ? Active : Muted),
            Layout.Status.X + 10 * S, Layout.Status.Y + 27 * S, Layout.Status.Width - 120 * S, .80f);
        Text(Options.bDetailed ? SpaceFlight::Localize(TEXT("H — СКРЫТЬ")) : SpaceFlight::Localize(TEXT("H — СПРАВКА")), PaleGold,
            Layout.Status.X + Layout.Status.Width - 108 * S, Layout.Status.Y + 27 * S, 100 * S, .80f);
        Panel(Layout.Clock); Center(SpaceFlight::FormatFlightTime(Elapsed), PaleGold, Layout.Clock, 1.0f);
        Panel(Layout.Goal);
        const auto& Goal = Progress.Objective;
        Text(SF_LOCAL_PRINTF(TEXT("ЦЕЛЬ: %s"), TEXT("OBJECTIVE: %s"), TEXT("OBJETIVO: %s"), Goals[FMath::Clamp(Goal.Kind, 0, 5)]), Cool,
            Layout.Goal.X + 10 * S, Layout.Goal.Y + 7 * S, Layout.Goal.Width - 190 * S, .90f);
        Text(FString::Printf(TEXT("%d/%d"), Goal.Progress, Goal.Target()), Active,
            Layout.Goal.X + Layout.Goal.Width - 174 * S, Layout.Goal.Y + 7 * S, 65 * S, .90f);
        Text(SF_LOCAL_PRINTF(TEXT("+%d РУДЫ"), TEXT("+%d ORE"), TEXT("+%d MINERAL"), Progress.ObjectiveReward(Goal.Reward())), PaleGold,
            Layout.Goal.X + Layout.Goal.Width - 100 * S, Layout.Goal.Y + 7 * S, 92 * S, .90f);
        if (Options.bTracked)
        {
            const auto& R = Layout.TrackedUpgrade;
            Panel(R);
            Text(SF_LOCAL_PRINTF(TEXT("РАЗВИТИЕ: %s  ·  БЕЗ РЕЗЕРВА"), TEXT("DEVELOPMENT: %s · NO RESERVE"), TEXT("DESARROLLO: %s · SIN RESERVA"), SpaceFlight::Upgrade(Tracked.Index).Name), PaleGold,
                R.X + 10 * S, R.Y + 5 * S, R.Width - 20 * S, .88f);
            Text(TrackedUpgradeDetail(Progress, Tracked), Tracked.Status == SpaceFlight::ETrackedUpgradeStatus::Ready ? Active : Cool,
                R.X + 10 * S, R.Y + 25 * S, R.Width - 20 * S, .82f);
        }
        Panel(Layout.Resources);
        const float Half = Layout.Resources.Width * .5f;
        Text(SF_LOCAL_PRINTF(TEXT("РУДА: %lld"), TEXT("ORE: %lld"), TEXT("MINERAL: %lld"), Progress.Resources), PaleGold, Layout.Resources.X + 10 * S,
            Layout.Resources.Y + 5 * S, Half - 18 * S, .92f);
        Text(SF_LOCAL_PRINTF(TEXT("ИЗОТОП: %lld"), TEXT("ISOTOPE: %lld"), TEXT("ISÓTOPO: %lld"), Progress.Isotope), Cool, Layout.Resources.X + Half + 5 * S,
            Layout.Resources.Y + 5 * S, Half - 15 * S, .92f);
        Text(SF_LOCAL_PRINTF(TEXT("СИНЯЯ: %lld"), TEXT("BLUE: %lld"), TEXT("AZUL: %lld"), Progress.BlueOre), Blue, Layout.Resources.X + 10 * S,
            Layout.Resources.Y + 27 * S, Half - 18 * S, .90f);
        Text(SF_LOCAL_PRINTF(TEXT("ЧЁРНАЯ: %lld"), TEXT("BLACK: %lld"), TEXT("NEGRO: %lld"), Progress.BlackOre), Cool, Layout.Resources.X + Half + 5 * S,
            Layout.Resources.Y + 27 * S, Half - 15 * S, .90f);
        if (Options.bDetailed)
        {
            const auto& R = Layout.Details; Panel(R);
            Text(SF_LOCAL_PRINTF(TEXT("ПРОФИЛЬ: %s"), TEXT("PROFILE: %s"), TEXT("PERFIL: %s"), SpecializationName(Progress.Specialization)), PaleGold, R.X + 10 * S, R.Y + 6 * S, R.Width - 20 * S);
            Text(SpecializationDescription(Progress.Specialization), Muted, R.X + 10 * S, R.Y + 26 * S, R.Width - 20 * S, .85f);
            Text(SF_LOCAL_PRINTF(TEXT("ПОЛУЧЕНО: %lld  ·  РАСХОД: %lld"), TEXT("MINED: %lld  ·  SPENT: %lld"), TEXT("OBTENIDO: %lld  ·  GASTADO: %lld"), Progress.TotalCollected,
                static_cast<long long>(Progress.TotalSpent) + Progress.OreConverted), Cool, R.X + 10 * S, R.Y + 46 * S, R.Width - 20 * S, .88f);
            Text(SF_LOCAL_PRINTF(TEXT("ЛАЗЕРЫ: %d/3  ·  РАДИУС: %.0f  ·  ПЕРЕРАБОТКА: %s"), TEXT("LASERS: %d/3 RADIUS: %.0f PROCESSING: %s"), TEXT("LÁSERES: %d/3 RADIO: %.0f PROCESAMIENTO: %s"), Progress.Lasers(), Progress.Range(), RefiningModeName(Progress.RefiningMode)),
                Muted, R.X + 10 * S, R.Y + 66 * S, R.Width - 20 * S, .85f);
            const int32 Unlocked = SpaceFlight::UnlockedSector(Progress.Objective.Sequence);
            const FString SectorProgress = Unlocked >= 2 ? SpaceFlight::Localize(TEXT("ОТКРЫТЫ ВСЕ СЕКТОРЫ")) :
                SF_LOCAL_PRINTF(TEXT("СЛЕДУЮЩИЙ СЕКТОР: %d / %d ЦЕЛЕЙ"), TEXT("NEXT SECTOR: %d / %d OBJECTIVES"), TEXT("PRÓXIMO SECTOR: %d / %d OBJETIVOS"), Progress.Objective.Sequence, Unlocked == 0 ? 3 : 8);
            Text(SectorProgress, Muted, R.X + 10 * S, R.Y + 86 * S, R.Width - 20 * S, .85f);
            Text(SF_LOCAL_PRINTF(TEXT("A/D — ПОЛЁТ  ·  F — АВТО  ·  TAB — НАВЫКИ  ·  ESC — %s"), TEXT("A/D — FLY · F — AUTO · TAB — UPGRADES · ESC — %s"), TEXT("A/D — VOLAR · F — AUTO · TAB — MEJORAS · ESC — %s"),
                SpaceFlight::EscapeActionShort(bPaused, bRefiningSelection || bExpeditionSelection, bProductionSelection, bSpecializationSelection)), Cool,
                R.X + 10 * S, R.Y + 107 * S, R.Width - 20 * S, .82f);
            const bool bShowSectorAction = SpaceFlight::UnlockedSector(Progress.Objective.Sequence) > 0 &&
                SpaceFlight::CanChangeSectorNow(bPaused, bStressMode, TotalFlightSeconds(), Progress.SectorReadyAt);
            Text(bShowSectorAction ? SpaceFlight::Localize(TEXT("I — ПЕРЕРАБОТКА  ·  M — РЕЦЕПТЫ  ·  K — ПРОФИЛЬ  ·  C — СЕКТОР")) :
                SpaceFlight::Localize(TEXT("I — ПЕРЕРАБОТКА  ·  M — РЕЦЕПТЫ  ·  K — ПРОФИЛЬ")), Cool,
                R.X + 10 * S, R.Y + 127 * S, R.Width - 20 * S, .82f);
            Text(SF_LOCAL_PRINTF(TEXT("E — %s  ·  Q — ВЫХОД  ·  P — ТЕСТ"), TEXT("E — %s · Q — QUIT · P — TEST"), TEXT("E — %s · Q — SALIR · P — PRUEBA"),
                SpaceFlight::ExpeditionActionWord(Progress.Expedition.State, bStressMode)), bRewardReady ? Active : Cool,
                R.X + 10 * S, R.Y + 147 * S, R.Width - 20 * S, .82f);
        }
        if (Options.bPopup)
        {
            const auto& R = Layout.Popup; Panel(R);
            if (bExpeditionSelection)
            {
                Text(ExpeditionTitle(), PaleGold, R.X + 10*S, R.Y + 7*S, R.Width - 20*S);
                Text(ExpeditionDetail(), Cool, R.X + 10*S, R.Y + 30*S, R.Width - 20*S, .83f);
                Text(ExpeditionError.IsEmpty() ? ExpeditionRewardDetail() : ExpeditionError,
                    ExpeditionError.IsEmpty() ? PaleGold : FLinearColor(1.0f, .48f, .34f), R.X + 10*S, R.Y + 50*S, R.Width - 20*S, .78f);
                Text(ExpeditionHint(), Active, R.X + 10*S, R.Y + 70*S, R.Width - 20*S, .80f);
            }
            else if (bSpecializationSelection)
            {
                Text(SF_LOCAL_PRINTF(TEXT("ВЫБОР: %s"), TEXT("SELECTION: %s"), TEXT("SELECCIÓN: %s"), SpecializationName(SelectedSpecialization)), PaleGold, R.X + 10*S, R.Y + 7*S, R.Width - 20*S);
                Text(SpecializationDescription(SelectedSpecialization), Cool, R.X + 10*S, R.Y + 28*S, R.Width - 20*S, .85f);
                const float Wait = Progress.SpecializationReadyAt - Elapsed;
                const FString State = SelectedSpecialization == Progress.Specialization ? SpaceFlight::Localize(TEXT("УЖЕ ВЫБРАНО")) :
                    (Wait > 0 ? SF_LOCAL_PRINTF(TEXT("СМЕНА ЧЕРЕЗ %d СЕК."), TEXT("CHANGE IN %d SEC."), TEXT("CAMBIO EN %d SEC."), FMath::CeilToInt(Wait)) : SpaceFlight::Localize(TEXT("МОЖНО ВЫБРАТЬ")));
                Text(State, Wait <= 0 ? Active : Muted, R.X + 10*S, R.Y + 49*S, R.Width - 20*S, .85f);
                Text(SpaceFlight::Localize(TEXT("K — ДАЛЕЕ  ·  ENTER — ПРИМЕНИТЬ  ·  ESC — ОТМЕНА")), Cool, R.X + 10*S, R.Y + 70*S, R.Width - 20*S, .82f);
            }
            else if (bProductionSelection)
            {
                Text(SF_LOCAL_PRINTF(TEXT("ФАБРИКАТОР  ·  ОБЫЧНАЯ: %lld"), TEXT("FABRICATOR REGULAR: %lld"), TEXT("FABRICADOR REGULAR: %lld"), Progress.CommonOre()), PaleGold, R.X + 10*S, R.Y + 7*S, R.Width - 20*S);
                const FString Recipe = SelectedProductionKind == SpaceFlight::EOreKind::Blue ?
                    SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ → 1 СИНЕЙ"), TEXT("%d REGULAR → 1 BLUE"), TEXT("%d REGULAR → 1 AZUL"), Progress.ProductionCost()) :
                    SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ → 1 ЧЁРНОЙ"), TEXT("%d REGULAR → 1 BLACK"), TEXT("%d REGULAR → 1 NEGRO"), Progress.ProductionCost());
                Text(Recipe, Cool, R.X + 10*S, R.Y + 28*S, R.Width - 20*S, .85f);
                const float Wait = Progress.ProductionReadyAt - Elapsed;
                const FString State = Wait > 0 ? SF_LOCAL_PRINTF(TEXT("ГОТОВ ЧЕРЕЗ %d СЕК."), TEXT("READY IN %d SEC."), TEXT("LISTO EN %d SEC."), FMath::CeilToInt(Wait)) :
                    (Progress.CommonOre() < Progress.ProductionCost() ? SpaceFlight::Localize(TEXT("НЕДОСТАТОЧНО ОБЫЧНОЙ РУДЫ")) : SpaceFlight::Localize(TEXT("МОДУЛЬ ГОТОВ")));
                Text(State, Wait <= 0 && Progress.CommonOre() >= Progress.ProductionCost() ? Active : Muted, R.X + 10*S, R.Y + 49*S, R.Width - 20*S, .85f);
                Text(SpaceFlight::Localize(TEXT("M — ВЫБОР  ·  ENTER — СОЗДАТЬ  ·  ESC — ОТМЕНА")), Cool, R.X + 10*S, R.Y + 70*S, R.Width - 20*S, .82f);
            }
            else
            {
                Text(SF_LOCAL_PRINTF(TEXT("ПЕРЕРАБОТКА: %s → %s"), TEXT("PROCESSING: %s → %s"), TEXT("PROCESAMIENTO: %s → %s"), RefiningModeName(Progress.RefiningMode), RefiningModeName(SelectedRefiningMode)), PaleGold, R.X + 10*S, R.Y + 6*S, R.Width - 20*S, .88f);
                Text(SpaceFlight::Localize(TEXT("1 СИНЯЯ + 1 ЧЁРНАЯ → 1 ИЗОТОП")), Cool, R.X + 10*S, R.Y + 24*S, R.Width - 20*S, .82f);
                Text(RefiningModeDescription(SelectedRefiningMode), Cool, R.X + 10*S, R.Y + 42*S, R.Width - 20*S, .80f);
                Text(RefiningApplyHint(Progress.RefiningMode, SelectedRefiningMode), Muted, R.X + 10*S, R.Y + 60*S, R.Width - 20*S, .78f);
                Text(SpaceFlight::Localize(TEXT("I — ДАЛЕЕ  ·  ENTER — ПРИМЕНИТЬ  ·  ESC — ОТМЕНА")), Cool, R.X + 10*S, R.Y + 78*S, R.Width - 20*S, .78f);
            }
        }
        if (Options.bPaused)
        {
            Panel(Layout.Pause);
            Center(SpaceFlight::PauseActionLabel(bRefiningSelection || bExpeditionSelection, bProductionSelection, bSpecializationSelection), PaleGold, Layout.Pause, .9f);
            if (Options.bReset)
            {
                Panel(Layout.Reset);
                Center(ResetConfirmUntil > GetWorld()->GetRealTimeSeconds() ? SpaceFlight::Localize(TEXT("ЕЩЁ РАЗ N — СТЕРЕТЬ ПРОГРЕСС")) : SpaceFlight::Localize(TEXT("N — НАЧАТЬ НОВЫЙ ПОЛЁТ")), PaleGold, Layout.Reset, .85f);
            }
        }
        else if (Options.bNotice) { Panel(Layout.Notice); Center(UpgradeNotice, PaleGold, Layout.Notice, .88f); }
        if (Options.bStress) { Panel(Layout.Stress); Center(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), PaleGold, Layout.Stress, .85f); }
        if (Options.bWarning)
        {
            Panel(Layout.Warning);
            Center(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА: СОХРАНЕНИЕ ОТКЛЮЧЕНО")) :
                (bSaveBlocked ? SpaceFlight::Localize(TEXT("СОХРАНЕНИЕ НЕДОСТУПНО, ПОВРЕЖДЕНО ИЛИ НОВЕЕ ИГРЫ")) : SpaceFlight::Localize(TEXT("НЕ УДАЛОСЬ СОХРАНИТЬ ПРОГРЕСС"))),
                FLinearColor(1.0f, .48f, .34f), Layout.Warning, .82f);
        }
        return;
    }
    const auto Tracked = SpaceFlight::DescribeTrackedUpgrade(Progress);
    const bool bHasTracked = Tracked.Status != SpaceFlight::ETrackedUpgradeStatus::None;
    const SpaceFlight::FCompactHudLayout Layout = SpaceFlight::MakeCompactHudLayout(W, H, bHasTracked);
    if (!Layout.bValid || !GEngine) return;
    const float S = Layout.Scale;
    UFont* Font = GEngine->GetSmallFont();
    const FLinearColor Gold(0.78f, 0.56f, 0.27f);
    const FLinearColor PaleGold(0.96f, 0.85f, 0.63f);
    const FLinearColor Cool(0.80f, 0.90f, 0.93f);
    const FLinearColor Muted(0.72f, 0.72f, 0.68f);
    const FLinearColor Active(0.55f, 0.95f, 0.72f);
    const FLinearColor Blue(0.53f, 0.82f, 1.0f);
    auto Panel = [&](const SpaceFlight::FHudRect& R, float Alpha = 0.78f)
    {
        DrawRect(FLinearColor(0.004f, 0.009f, 0.018f, Alpha), R.X, R.Y, R.Width, R.Height);
        const float L = 12.0f * S, T = FMath::Max(1.0f, S);
        DrawLine(R.X, R.Y, R.X + L, R.Y, Gold, T);
        DrawLine(R.X, R.Y, R.X, R.Y + L, Gold, T);
        DrawLine(R.X + R.Width - L, R.Y, R.X + R.Width, R.Y, Gold, T);
        DrawLine(R.X + R.Width, R.Y, R.X + R.Width, R.Y + L, Gold, T);
        DrawLine(R.X, R.Y + R.Height - L, R.X, R.Y + R.Height, Gold, T);
        DrawLine(R.X, R.Y + R.Height, R.X + L, R.Y + R.Height, Gold, T);
        DrawLine(R.X + R.Width - L, R.Y + R.Height, R.X + R.Width, R.Y + R.Height, Gold, T);
        DrawLine(R.X + R.Width, R.Y + R.Height - L, R.X + R.Width, R.Y + R.Height, Gold, T);
    };
    auto Text = [&](const FString& Value, const FLinearColor& Color, float X, float Y, float Size = 0.90f)
    { DrawText(Value, Color, X, Y, Font, Size * S); };
    auto FittedText = [&](const FString& Value, const FLinearColor& Color, float X, float Y, float MaxWidth, float Size = 0.90f)
    {
        float TextWidth = 0, TextHeight = 0;
        GetTextSize(Value, TextWidth, TextHeight, Font, Size * S);
        const float Fit = TextWidth > MaxWidth && TextWidth > 0 ? MaxWidth / TextWidth : 1.0f;
        Text(Value, Color, X, Y, Size * Fit);
    };
    auto CenterText = [&](const FString& Value, const FLinearColor& Color, float CenterX, float Y, float Size = 0.90f, float MaxWidth = 100000.0f)
    {
        float TextWidth = 0, TextHeight = 0;
        GetTextSize(Value, TextWidth, TextHeight, Font, Size * S);
        const float Fit = TextWidth > 0 ? FMath::Min(1.0f, MaxWidth / TextWidth) : 1.0f;
        Text(Value, Color, CenterX - TextWidth * Fit * .5f, Y, Size * Fit);
    };

    const SpaceFlight::FHudRect& Status = Layout.Status;
    bool bUpgradeAvailable = false;
    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) bUpgradeAvailable |= Progress.CanPurchase(I);
    Panel(Status);
    const FString Flight = SpaceFlight::SectorStatusLabel(bAutopilot, Progress.CurrentSector, Progress.Objective.Sequence,
        SpaceFlight::CanChangeSectorNow(bPaused, bStressMode, TotalFlightSeconds(), Progress.SectorReadyAt));
    FittedText(Flight, bAutopilot ? Active : Cool, Status.X + 12 * S, Status.Y + 6 * S,
        206 * S, 0.92f);
    FittedText(bUpgradeAvailable ? SpaceFlight::Localize(TEXT("TAB: ДЕРЕВО +")) : SpaceFlight::Localize(TEXT("TAB: ДЕРЕВО")), bUpgradeAvailable ? Active : PaleGold,
        Status.X + 230 * S, Status.Y + 6 * S, 108 * S, 0.80f);
    const bool bRewardReady = Progress.Expedition.State == SpaceFlight::EExpeditionState::Ready && !bStressMode;
    const FString ExpeditionSector = SpaceFlight::ExpeditionSectorHint(Progress.Expedition, Progress.CurrentSector, bStressMode);
    const FString Profile = ExpeditionSector.IsEmpty() ? SF_LOCAL_PRINTF(TEXT("ПРОФИЛЬ: %s · E — %s"), TEXT("PROFILE: %s · E — %s"), TEXT("PERFIL: %s · E — %s"),
        SpecializationName(Progress.Specialization), SpaceFlight::ExpeditionActionWord(Progress.Expedition.State, bStressMode)) : ExpeditionSector;
    FittedText(Profile, !ExpeditionSector.IsEmpty() ? PaleGold : (bRewardReady ? Active : Muted),
        Status.X + 12 * S, Status.Y + 27 * S, 218 * S, 0.82f);
    FittedText(SpaceFlight::Localize(TEXT("H — СПРАВКА")), PaleGold, Status.X + 236 * S, Status.Y + 27 * S, 100 * S, 0.82f);

    const SpaceFlight::FFlightObjective& Goal = Progress.Objective;
    const TCHAR* GoalNames[] = { SpaceFlight::Localize(TEXT("АСТЕРОИДЫ")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА")), SpaceFlight::Localize(TEXT("ДОБЫЧА РУДЫ")), SpaceFlight::Localize(TEXT("БОГАТЫЕ ЗАЛЕЖИ")), SpaceFlight::Localize(TEXT("ПОПАДАНИЯ")) };
    const int32 GoalKind = FMath::Clamp(Goal.Kind, 0, 5);
    Panel(Layout.Goal);
    FittedText(SF_LOCAL_PRINTF(TEXT("ЦЕЛЬ: %s"), TEXT("OBJECTIVE: %s"), TEXT("OBJETIVO: %s"), GoalNames[GoalKind]), Cool,
        Layout.Goal.X + 12 * S, Layout.Goal.Y + 9 * S, 175 * S, 0.88f);
    CenterText(FString::Printf(TEXT("%d/%d"), Goal.Progress, Goal.Target()), Active,
        Layout.Goal.X + 222 * S, Layout.Goal.Y + 9 * S, 0.90f, 70 * S);
    CenterText(SF_LOCAL_PRINTF(TEXT("+%d РУДЫ"), TEXT("+%d ORE"), TEXT("+%d MINERAL"), Progress.ObjectiveReward(Goal.Reward())), PaleGold,
        Layout.Goal.X + 308 * S, Layout.Goal.Y + 9 * S, 0.90f, 68 * S);
    if (bHasTracked)
    {
        const auto& R = Layout.TrackedUpgrade;
        Panel(R);
        FittedText(SF_LOCAL_PRINTF(TEXT("РАЗВИТИЕ: %s  ·  БЕЗ РЕЗЕРВА"), TEXT("DEVELOPMENT: %s · NO RESERVE"), TEXT("DESARROLLO: %s · SIN RESERVA"), SpaceFlight::Upgrade(Tracked.Index).Name), PaleGold,
            R.X + 12 * S, R.Y + 5 * S, R.Width - 24 * S, .88f);
        FittedText(TrackedUpgradeDetail(Progress, Tracked), Tracked.Status == SpaceFlight::ETrackedUpgradeStatus::Ready ? Active : Cool,
            R.X + 12 * S, R.Y + 25 * S, R.Width - 24 * S, .82f);
    }

    Panel(Layout.Clock);
    CenterText(SpaceFlight::FormatFlightTime(Elapsed), PaleGold,
        Layout.Clock.X + Layout.Clock.Width * .5f, Layout.Clock.Y + 10 * S, 1.20f, Layout.Clock.Width - 20 * S);

    const SpaceFlight::FHudRect& Resources = Layout.Resources;
    Panel(Resources);
    FittedText(SF_LOCAL_PRINTF(TEXT("РУДА: %lld"), TEXT("ORE: %lld"), TEXT("MINERAL: %lld"), Progress.Resources), PaleGold,
        Resources.X + 12 * S, Resources.Y + 7 * S, 170 * S, 0.94f);
    FittedText(SF_LOCAL_PRINTF(TEXT("ИЗОТОП: %lld"), TEXT("ISOTOPE: %lld"), TEXT("ISÓTOPO: %lld"), Progress.Isotope), Cool,
        Resources.X + 202 * S, Resources.Y + 7 * S, 175 * S, 0.92f);
    FittedText(SF_LOCAL_PRINTF(TEXT("СИНЯЯ: %lld"), TEXT("BLUE: %lld"), TEXT("AZUL: %lld"), Progress.BlueOre), Blue,
        Resources.X + 12 * S, Resources.Y + 32 * S, 170 * S, 0.88f);
    FittedText(SF_LOCAL_PRINTF(TEXT("ЧЁРНАЯ: %lld"), TEXT("BLACK: %lld"), TEXT("NEGRO: %lld"), Progress.BlackOre), Cool,
        Resources.X + 202 * S, Resources.Y + 32 * S, 175 * S, 0.88f);

    if (bExpeditionSelection)
    {
        const auto& R = Layout.LeftPopup;
        Panel(R, 0.92f);
        FittedText(ExpeditionTitle(), PaleGold, R.X + 12*S, R.Y + 8*S, R.Width - 24*S, .92f);
        FittedText(ExpeditionDetail(), Cool, R.X + 12*S, R.Y + 30*S, R.Width - 24*S, .82f);
        FittedText(ExpeditionError.IsEmpty() ? ExpeditionRewardDetail() : ExpeditionError,
            ExpeditionError.IsEmpty() ? PaleGold : FLinearColor(1.0f, .48f, .34f), R.X + 12*S, R.Y + 51*S, R.Width - 24*S, .78f);
        FittedText(ExpeditionHint(), Active, R.X + 12*S, R.Y + 73*S, R.Width - 24*S, .80f);
    }
    else if (bSpecializationSelection)
    {
        const auto& R = Layout.LeftPopup;
        Panel(R, 0.92f);
        FittedText(SF_LOCAL_PRINTF(TEXT("ВЫБОР: %s"), TEXT("SELECTION: %s"), TEXT("SELECCIÓN: %s"), SpecializationName(SelectedSpecialization)), PaleGold,
            R.X + 12 * S, R.Y + 8 * S, R.Width - 24 * S, 0.92f);
        FittedText(SpecializationDescription(SelectedSpecialization), Cool,
            R.X + 12 * S, R.Y + 30 * S, R.Width - 24 * S, 0.82f);
        const float Wait = Progress.SpecializationReadyAt - Elapsed;
        const FString State = SelectedSpecialization == Progress.Specialization ? SpaceFlight::Localize(TEXT("УЖЕ ВЫБРАНО")) :
            (Wait > 0 ? SF_LOCAL_PRINTF(TEXT("СМЕНА ЧЕРЕЗ %d СЕК."), TEXT("CHANGE IN %d SEC."), TEXT("CAMBIO EN %d SEC."), FMath::CeilToInt(Wait)) : SpaceFlight::Localize(TEXT("МОЖНО ВЫБРАТЬ")));
        Text(State, Wait <= 0 ? Active : Muted, R.X + 12 * S, R.Y + 51 * S, 0.82f);
        FittedText(SpaceFlight::Localize(TEXT("K — ДАЛЕЕ  ENTER — ПРИМЕНИТЬ  ESC — ОТМЕНА")), Cool,
            R.X + 12 * S, R.Y + 73 * S, R.Width - 24 * S, 0.80f);
    }
    else if (bProductionSelection)
    {
        const auto& R = Layout.RightPopup;
        Panel(R, 0.92f);
        FittedText(SF_LOCAL_PRINTF(TEXT("ФАБРИКАТОР  ·  ОБЫЧНАЯ: %lld"), TEXT("FABRICATOR REGULAR: %lld"), TEXT("FABRICADOR REGULAR: %lld"), Progress.CommonOre()), PaleGold,
            R.X + 12 * S, R.Y + 8 * S, R.Width - 24 * S, 0.92f);
        const FString Recipe = SelectedProductionKind == SpaceFlight::EOreKind::Blue ?
            SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ → 1 СИНЕЙ"), TEXT("%d REGULAR → 1 BLUE"), TEXT("%d REGULAR → 1 AZUL"), Progress.ProductionCost()) :
            SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ → 1 ЧЁРНОЙ"), TEXT("%d REGULAR → 1 BLACK"), TEXT("%d REGULAR → 1 NEGRO"), Progress.ProductionCost());
        Text(Recipe, Cool, R.X + 12 * S, R.Y + 30 * S, 0.88f);
        const float Wait = Progress.ProductionReadyAt - Elapsed;
        const FString State = Wait > 0 ? SF_LOCAL_PRINTF(TEXT("ГОТОВ ЧЕРЕЗ %d СЕК."), TEXT("READY IN %d SEC."), TEXT("LISTO EN %d SEC."), FMath::CeilToInt(Wait)) :
            (Progress.CommonOre() < Progress.ProductionCost() ? SpaceFlight::Localize(TEXT("НЕДОСТАТОЧНО ОБЫЧНОЙ РУДЫ")) : SpaceFlight::Localize(TEXT("МОДУЛЬ ГОТОВ")));
        Text(State, Wait <= 0 && Progress.CommonOre() >= Progress.ProductionCost() ? Active : Muted,
            R.X + 12 * S, R.Y + 51 * S, 0.82f);
        FittedText(SpaceFlight::Localize(TEXT("M — ВЫБОР  ENTER — СОЗДАТЬ  ESC — ОТМЕНА")), Cool,
            R.X + 12 * S, R.Y + 73 * S, R.Width - 24 * S, 0.80f);
    }
    else if (bRefiningSelection)
    {
        const SpaceFlight::FHudRect R = SpaceFlight::RefiningPopupRect(Layout, W);
        const bool bSmallWindow = W < 900.0f;
        const float ReadScale = bSmallWindow ? 1.20f : 1.0f;
        const float RowStep = bSmallWindow ? 20.0f : 18.0f;
        Panel(R, 0.92f);
        FittedText(SF_LOCAL_PRINTF(TEXT("ПЕРЕРАБОТКА: %s → %s"), TEXT("PROCESSING: %s → %s"), TEXT("PROCESAMIENTO: %s → %s"), RefiningModeName(Progress.RefiningMode), RefiningModeName(SelectedRefiningMode)),
            PaleGold, R.X + 12 * S, R.Y + 6 * S, R.Width - 24 * S, 0.88f * ReadScale);
        FittedText(SpaceFlight::Localize(TEXT("1 СИНЯЯ + 1 ЧЁРНАЯ → 1 ИЗОТОП")), Cool, R.X + 12 * S, R.Y + (6 + RowStep) * S, R.Width - 24 * S, 0.82f * ReadScale);
        FittedText(RefiningModeDescription(SelectedRefiningMode), Cool, R.X + 12 * S, R.Y + (6 + 2 * RowStep) * S, R.Width - 24 * S, 0.80f * ReadScale);
        FittedText(RefiningApplyHint(Progress.RefiningMode, SelectedRefiningMode), Muted, R.X + 12 * S, R.Y + (6 + 3 * RowStep) * S, R.Width - 24 * S, 0.78f * ReadScale);
        FittedText(SpaceFlight::Localize(TEXT("I — ДАЛЕЕ  ENTER — ПРИМЕНИТЬ  ESC — ОТМЕНА")), Cool, R.X + 12 * S, R.Y + (6 + 4 * RowStep) * S, R.Width - 24 * S, 0.78f * ReadScale);
    }

    if (bPaused)
    {
        SpaceFlight::FHudRect Pause{ W * .5f - 125 * S, Layout.Clock.Y + 48 * S, 250 * S, 28 * S };
        if (bRefiningSelection) Pause = SpaceFlight::BelowIfOverlapping(Pause, SpaceFlight::RefiningPopupRect(Layout, W), 8 * S);
        Panel(Pause, 0.90f);
        CenterText(SpaceFlight::PauseActionLabel(bRefiningSelection || bExpeditionSelection, bProductionSelection, bSpecializationSelection),
            PaleGold, W * .5f, Pause.Y + 6 * S, 0.80f);
        if (!bStressMode && !bSaveBlocked)
        {
            SpaceFlight::FHudRect Reset{ W * .5f - 145 * S, Pause.Y + 34 * S, 290 * S, 27 * S };
            if (bRefiningSelection) Reset = SpaceFlight::BelowIfOverlapping(Reset, SpaceFlight::RefiningPopupRect(Layout, W), 8 * S);
            Panel(Reset, 0.90f);
            CenterText(ResetConfirmUntil > GetWorld()->GetRealTimeSeconds() ? SpaceFlight::Localize(TEXT("ЕЩЁ РАЗ N — СТЕРЕТЬ ПРОГРЕСС")) : SpaceFlight::Localize(TEXT("N — НАЧАТЬ НОВЫЙ ПОЛЁТ")),
                PaleGold, W * .5f, Reset.Y + 6 * S, 0.80f);
        }
    }
    else if (!UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil)
    {
        const float Gap = Layout.Resources.X - (Layout.Status.X + Layout.Status.Width) - 16 * S;
        const float NoticeWidth = FMath::Min(380 * S, Gap);
        SpaceFlight::FHudRect Notice{ (Layout.Status.X + Layout.Status.Width + Layout.Resources.X - NoticeWidth) * .5f,
            Layout.Resources.Y + Layout.Resources.Height + 8 * S, NoticeWidth, 28 * S };
        if (bRefiningSelection) Notice = SpaceFlight::BelowIfOverlapping(Notice, SpaceFlight::RefiningPopupRect(Layout, W), 8 * S);
        Panel(Notice, 0.90f);
        FittedText(UpgradeNotice, PaleGold, Notice.X + 12 * S, Notice.Y + 6 * S, Notice.Width - 24 * S, 0.84f);
    }
    if (bStressMode)
    {
        const SpaceFlight::FHudRect Stress{ W * .5f - 130 * S, H - 37 * S, 260 * S, 25 * S };
        Panel(Stress, 0.92f);
        CenterText(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), PaleGold, W * .5f, Stress.Y + 5 * S, 0.78f);
    }
    if (bSaveBlocked || bSaveWriteFailed)
    {
        const SpaceFlight::FHudRect Warning{ 12 * S, H - 50 * S, FMath::Min(520 * S, W - 24 * S), 25 * S };
        Panel(Warning, 0.92f);
        FittedText(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА: СОХРАНЕНИЕ ОТКЛЮЧЕНО")) : (bSaveBlocked ? SpaceFlight::Localize(TEXT("СОХРАНЕНИЕ НЕДОСТУПНО, ПОВРЕЖДЕНО ИЛИ НОВЕЕ ИГРЫ")) : SpaceFlight::Localize(TEXT("НЕ УДАЛОСЬ СОХРАНИТЬ ПРОГРЕСС"))),
            FLinearColor(1.0f, 0.48f, 0.34f), Warning.X + 8 * S, Warning.Y + 5 * S, Warning.Width - 16 * S, 0.80f);
    }
}

void ASpaceFlightHUD::DrawFlightHUD(float W, float H, float Elapsed, bool bPaused)
{
    if (!bDetailedHUD || SpaceFlight::UsePortraitHud(W, H))
    {
        DrawCompactFlightHUD(W, H, Elapsed, bPaused);
        return;
    }
    const float S = FMath::Min(W / 1280.0f, H / 720.0f);
    if (S <= 0.0f || !GEngine) return;
    UFont* Font = GEngine->GetSmallFont();
    const FLinearColor Gold(0.72f, 0.46f, 0.18f);
    const FLinearColor PaleGold(0.93f, 0.78f, 0.50f);
    const FLinearColor Muted(0.64f, 0.60f, 0.51f);
    const FLinearColor Cool(0.72f, 0.86f, 0.88f);
    const FLinearColor Active(0.50f, 0.88f, 0.68f);
    const FLinearColor DarkMetal(0.08f, 0.07f, 0.055f);
    const FLinearColor Accent(0.22f, 0.60f, 0.66f);
    auto Line = [&](float X1, float Y1, float X2, float Y2, const FLinearColor& Color, float Thickness = 1.0f)
    { DrawLine(X1, Y1, X2, Y2, Color, Thickness * S); };
    auto Diamond = [&](float X, float Y, float Radius, const FLinearColor& Color)
    {
        const float R = Radius * S;
        Line(X, Y - R, X + R, Y, Color);
        Line(X + R, Y, X, Y + R, Color);
        Line(X, Y + R, X - R, Y, Color);
        Line(X - R, Y, X, Y - R, Color);
    };
    auto Panel = [&](float X, float Y, float Width, float Height)
    {
        DrawRect(FLinearColor(0.004f, 0.006f, 0.012f, 0.48f), X + 4 * S, Y + 5 * S, Width - 8 * S, Height - 3 * S);
        if (HudFrame && HudFrame->GetResource())
        {
            FCanvasTileItem Frame(FVector2D(X, Y), HudFrame->GetResource(), FVector2D(Width, Height), FLinearColor::White);
            Frame.BlendMode = SE_BLEND_Opaque;
            Canvas->DrawItem(Frame);
            return;
        }
        // A lightweight fallback keeps the interface usable if the art asset is missing.
        const float Cut = 9.0f * S;
        const FLinearColor Surface(0.010f, 0.015f, 0.025f, 0.95f);
        const FLinearColor Edge(0.50f, 0.31f, 0.12f);
        DrawRect(Surface, X + Cut, Y, Width - 2 * Cut, Height);
        DrawRect(Surface, X, Y + Cut, Width, Height - 2 * Cut);
        DrawRect(FLinearColor(0.035f, 0.040f, 0.048f, 0.90f), X + Cut, Y + 2 * S, Width - 2 * Cut, 24 * S);
        Line(X + Cut, Y, X + Width - Cut, Y, Gold, 1.4f);
        Line(X + Width - Cut, Y, X + Width, Y + Cut, Gold, 1.4f);
        Line(X + Width, Y + Cut, X + Width, Y + Height - Cut, Edge, 1.4f);
        Line(X + Width, Y + Height - Cut, X + Width - Cut, Y + Height, Gold, 1.4f);
        Line(X + Width - Cut, Y + Height, X + Cut, Y + Height, Edge, 1.4f);
        Line(X + Cut, Y + Height, X, Y + Height - Cut, Gold, 1.4f);
        Line(X, Y + Height - Cut, X, Y + Cut, Edge, 1.4f);
        Line(X, Y + Cut, X + Cut, Y, Gold, 1.4f);
        Line(X + Cut + 4 * S, Y + 3 * S, X + Width - Cut - 4 * S, Y + 3 * S, DarkMetal);
        Line(X + Cut + 5 * S, Y + Height - 4 * S, X + Width - Cut - 5 * S, Y + Height - 4 * S, DarkMetal);
        DrawRect(Gold, X + Cut - 2 * S, Y - S, 4 * S, 3 * S);
        DrawRect(Gold, X + Width - Cut - 2 * S, Y + Height - 2 * S, 4 * S, 3 * S);
    };
    auto Text = [&](const FString& Value, const FLinearColor& Color, float X, float Y, float Size = 1.0f)
    { DrawText(Value, Color, X, Y, Font, Size * S); };
    auto FittedText = [&](const FString& Value, const FLinearColor& Color, float X, float Y, float MaxWidth, float Size)
    {
        float TextWidth = 0.0f, TextHeight = 0.0f;
        GetTextSize(Value, TextWidth, TextHeight, Font, Size * S);
        const float Fit = TextWidth > 0 ? FMath::Min(1.0f, MaxWidth / TextWidth) : 1.0f;
        Text(Value, Color, X, Y, Size * Fit);
    };
    auto CenterText = [&](const FString& Value, const FLinearColor& Color, float CenterX, float Y, float Size, float MaxWidth = 100000.0f)
    {
        float TextWidth = 0.0f, TextHeight = 0.0f;
        GetTextSize(Value, TextWidth, TextHeight, Font, Size * S);
        const float Fit = TextWidth > 0 ? FMath::Min(1.0f, MaxWidth / TextWidth) : 1.0f;
        Text(Value, Color, CenterX - TextWidth * Fit * 0.5f, Y, Size * Fit);
    };
    auto Key = [&](const TCHAR* Value, float X, float Y, float Width)
    {
        DrawRect(FLinearColor(0.38f, 0.25f, 0.11f), X, Y, Width * S, 17 * S);
        DrawRect(FLinearColor(0.035f, 0.045f, 0.055f), X + S, Y + S, (Width - 2) * S, 15 * S);
        DrawRect(FLinearColor(0.65f, 0.43f, 0.17f), X + 2 * S, Y + S, (Width - 4) * S, S);
        CenterText(Value, PaleGold, X + Width * S * 0.5f, Y + 1 * S, 0.82f);
    };
    auto AtlasIcon = [&](int32 AtlasIndex, float X, float Y, float Size)
    {
        if (!SkillAtlas || !SkillAtlas->GetResource()) return;
        const float U = (AtlasIndex % 2) * 0.5f;
        const float V = (AtlasIndex / 2) / 3.0f;
        FCanvasTileItem Icon(FVector2D(X, Y), SkillAtlas->GetResource(), FVector2D(Size * S, Size * S),
            FVector2D(U + 0.001f, V + 0.001f), FVector2D(U + 0.499f, V + 1.0f / 3.0f - 0.001f), FLinearColor::White);
        Icon.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Icon);
    };

    // Helm: controls are grouped by task and remain readable at a glance.
    const float Left = 18 * S;
    const float Top = 16 * S;
    const float HelmWidth = 324 * S;
    Panel(Left, Top, HelmWidth, 110 * S);
    AtlasIcon(5, Left + 10 * S, Top + 4 * S, 23);
    Text(SpaceFlight::Localize(TEXT("НАВИГАЦИЯ")), PaleGold, Left + 39 * S, Top + 8 * S, 0.88f);
    Diamond(Left + 174 * S, Top + 14 * S, 3, bAutopilot ? Active : Cool);
    Text(bAutopilot ? SpaceFlight::Localize(TEXT("АВТОПИЛОТ")) : SpaceFlight::Localize(TEXT("РУЧНОЙ РЕЖИМ")),
        bAutopilot ? Active : Cool, Left + 183 * S, Top + 8 * S, 0.86f);
    Line(Left + 11 * S, Top + 28 * S, Left + HelmWidth - 11 * S, Top + 28 * S, DarkMetal);
    Line(Left + 12 * S, Top + 29 * S, Left + 75 * S, Top + 29 * S, Accent);
    Key(TEXT("A / D"), Left + 12 * S, Top + 35 * S, 45);
    Key(TEXT("←  →"), Left + 62 * S, Top + 35 * S, 42);
    Text(SpaceFlight::Localize(TEXT("ПОЛЁТ")), Cool, Left + 111 * S, Top + 36 * S, 0.88f);
    Text(SpaceFlight::Localize(TEXT("H — КОМПАКТНО")), Muted, Left + 205 * S, Top + 37 * S, 0.70f);
    Key(TEXT("F"), Left + 12 * S, Top + 57 * S, 22);
    Text(SpaceFlight::Localize(TEXT("АВТО")), Cool, Left + 40 * S, Top + 58 * S, 0.86f);
    Key(TEXT("TAB"), Left + 107 * S, Top + 57 * S, 36);
    Text(SpaceFlight::Localize(TEXT("НАВЫКИ")), Cool, Left + 149 * S, Top + 58 * S, 0.86f);
    Key(TEXT("E"), Left + 220 * S, Top + 57 * S, 22);
    const bool bRewardReady = Progress.Expedition.State == SpaceFlight::EExpeditionState::Ready && !bStressMode;
    FittedText(SpaceFlight::ExpeditionActionWord(Progress.Expedition.State, bStressMode), bRewardReady ? Active : Cool,
        Left + 248 * S, Top + 58 * S, 65 * S, 0.86f);
    Key(TEXT("ESC"), Left + 12 * S, Top + 79 * S, 36);
    FittedText(SpaceFlight::EscapeActionShort(bPaused, bRefiningSelection || bExpeditionSelection, bProductionSelection, bSpecializationSelection),
        Cool, Left + 54 * S, Top + 80 * S, 60 * S, 0.86f);
    Key(TEXT("Q"), Left + 120 * S, Top + 79 * S, 22);
    Text(SpaceFlight::Localize(TEXT("ВЫХОД")), Cool, Left + 148 * S, Top + 80 * S, 0.86f);
    Key(TEXT("P"), Left + 207 * S, Top + 79 * S, 22);
    Text(SpaceFlight::Localize(TEXT("АСТЕРОИДЫ")), Cool, Left + 235 * S, Top + 80 * S, 0.80f);
    if (bStressMode)
    {
        Panel(Left, Top + 116 * S, HelmWidth, 28 * S);
        CenterText(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), PaleGold,
            Left + HelmWidth * 0.5f, Top + 122 * S, 0.76f);
    }

    const float SpecTop = Top + (bStressMode ? 150 : 116) * S;
    Panel(Left, SpecTop, HelmWidth, 78 * S);
    Text(SpaceFlight::Localize(TEXT("СПЕЦИАЛИЗАЦИЯ")), PaleGold, Left + 17 * S, SpecTop + 7 * S, 0.80f);
    Text(SpaceFlight::Localize(TEXT("K — ВЫБРАТЬ")), Cool, Left + 206 * S, SpecTop + 7 * S, 0.76f);
    Line(Left + 12 * S, SpecTop + 26 * S, Left + 312 * S, SpecTop + 26 * S, DarkMetal);
    Text(SpecializationName(Progress.Specialization), Active, Left + 17 * S, SpecTop + 31 * S, 0.80f);
    Text(SpecializationDescription(Progress.Specialization), Cool, Left + 17 * S, SpecTop + 51 * S, 0.70f);
    if (bSpecializationSelection)
    {
        const float SelectTop = SpecTop + 85 * S;
        Panel(Left, SelectTop, HelmWidth, 88 * S);
        Text(SF_LOCAL_PRINTF(TEXT("ВЫБОР: %s"), TEXT("SELECTION: %s"), TEXT("SELECCIÓN: %s"), SpecializationName(SelectedSpecialization)),
            PaleGold, Left + 17 * S, SelectTop + 7 * S, 0.80f);
        Line(Left + 12 * S, SelectTop + 26 * S, Left + 312 * S, SelectTop + 26 * S, DarkMetal);
        Text(SpecializationDescription(SelectedSpecialization), Cool, Left + 17 * S, SelectTop + 31 * S, 0.70f);
        const float Wait = Progress.SpecializationReadyAt - Elapsed;
        const FString State = SelectedSpecialization == Progress.Specialization ? SpaceFlight::Localize(TEXT("УЖЕ ВЫБРАНО")) :
            (Wait > 0 ? SF_LOCAL_PRINTF(TEXT("СМЕНА ЧЕРЕЗ %d СЕК."), TEXT("CHANGE IN %d SEC."), TEXT("CAMBIO EN %d SEC."), FMath::CeilToInt(Wait)) : SpaceFlight::Localize(TEXT("МОЖНО ВЫБРАТЬ")));
        Text(State, Wait <= 0 ? Active : Muted, Left + 17 * S, SelectTop + 50 * S, 0.72f);
        Text(SpaceFlight::Localize(TEXT("K — ДАЛЕЕ  ENTER — ПРИМЕНИТЬ  ESC — ОТМЕНА")),
            Cool, Left + 17 * S, SelectTop + 68 * S, 0.66f);
    }
    const auto Tracked = SpaceFlight::DescribeTrackedUpgrade(Progress);
    if (Tracked.Status != SpaceFlight::ETrackedUpgradeStatus::None)
    {
        const float TrackTop = SpecTop + (bSpecializationSelection ? 181.0f : 85.0f) * S;
        Panel(Left, TrackTop, HelmWidth, 46 * S);
        FittedText(SF_LOCAL_PRINTF(TEXT("РАЗВИТИЕ: %s  ·  БЕЗ РЕЗЕРВА"), TEXT("DEVELOPMENT: %s · NO RESERVE"), TEXT("DESARROLLO: %s · SIN RESERVA"), SpaceFlight::Upgrade(Tracked.Index).Name), PaleGold,
            Left + 17 * S, TrackTop + 6 * S, HelmWidth - 34 * S, .82f);
        FittedText(TrackedUpgradeDetail(Progress, Tracked), Tracked.Status == SpaceFlight::ETrackedUpgradeStatus::Ready ? Active : Cool,
            Left + 17 * S, TrackTop + 25 * S, HelmWidth - 34 * S, .76f);
    }

    // Accumulated play time continues across sessions and stops during pause.
    const float ClockWidth = 180 * S;
    const float ClockLeft = W * 0.5f - ClockWidth * 0.5f;
    Panel(ClockLeft, Top, ClockWidth, 68 * S);
    Diamond(ClockLeft + 15 * S, Top + 37 * S, 4, Gold);
    Diamond(ClockLeft + ClockWidth - 15 * S, Top + 37 * S, 4, Gold);
    CenterText(SpaceFlight::Localize(TEXT("ВРЕМЯ ПОЛЁТА")), Muted, W * 0.5f, Top + 9 * S, 0.85f);
    CenterText(SpaceFlight::FormatFlightTime(Elapsed), PaleGold, W * 0.5f, Top + 29 * S, 1.35f, ClockWidth - 44 * S);
    if (bPaused)
    {
        Panel(ClockLeft, Top + 76 * S, ClockWidth, 27 * S);
        CenterText(SpaceFlight::PauseActionLabel(bRefiningSelection || bExpeditionSelection, bProductionSelection, bSpecializationSelection),
            PaleGold, W * 0.5f, Top + 81 * S, 0.76f, ClockWidth - 12 * S);
        if (!bStressMode && !bSaveBlocked)
        {
            Panel(ClockLeft - 55 * S, Top + 107 * S, ClockWidth + 110 * S, 25 * S);
            CenterText(ResetConfirmUntil > GetWorld()->GetRealTimeSeconds() ?
                SpaceFlight::Localize(TEXT("ЕЩЁ РАЗ N — СТЕРЕТЬ ПРОГРЕСС")) : SpaceFlight::Localize(TEXT("N — НАЧАТЬ НОВЫЙ ПОЛЁТ")),
                PaleGold, W * 0.5f, Top + 112 * S, 0.76f);
        }
    }
    if (bSaveBlocked || bSaveWriteFailed)
        Text(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА: СОХРАНЕНИЕ ОТКЛЮЧЕНО")) : (bSaveBlocked ? SpaceFlight::Localize(TEXT("СОХРАНЕНИЕ НЕДОСТУПНО, ПОВРЕЖДЕНО ИЛИ НОВЕЕ ИГРЫ")) : SpaceFlight::Localize(TEXT("НЕ УДАЛОСЬ СОХРАНИТЬ ПРОГРЕСС"))),
            FLinearColor(1.0f, 0.40f, 0.28f), 12 * S, H - 46 * S, 0.80f);
    if (!bPaused && !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil)
    {
        Panel(ClockLeft - 35 * S, Top + 76 * S, ClockWidth + 70 * S, 27 * S);
        CenterText(UpgradeNotice, PaleGold, W * 0.5f, Top + 81 * S, 0.78f);
    }

    // Ore colors are subsets of the shared ore stock; paired ore becomes isotope.
    const float Right = W - 18 * S - 324 * S;
    Panel(Right, Top, 324 * S, 180 * S);
    AtlasIcon(1, Right + 10 * S, Top + 9 * S, 34);
    Text(SpaceFlight::Localize(TEXT("РУДА В ЗАПАСЕ")), Muted, Right + 53 * S, Top + 8 * S, 0.82f);
    FittedText(FString::Printf(TEXT("%lld"), Progress.Resources), PaleGold, Right + 53 * S, Top + 24 * S, 100 * S, 1.25f);
    bool bUpgradeAvailable = false;
    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) bUpgradeAvailable |= Progress.CanPurchase(I);
    if (bUpgradeAvailable)
    {
        Diamond(Right + 157 * S, Top + 33 * S, 3, Active);
        Text(SpaceFlight::Localize(TEXT("ДОСТУПНО УЛУЧШЕНИЕ")), Active, Right + 167 * S, Top + 27 * S, 0.75f);
    }
    Line(Right + 11 * S, Top + 49 * S, Right + 313 * S, Top + 49 * S, DarkMetal);
    Line(Right + 249 * S, Top + 50 * S, Right + 312 * S, Top + 50 * S, Accent);
    FittedText(SF_LOCAL_PRINTF(TEXT("ПОЛУЧЕНО: %lld"), TEXT("RECEIVED: %lld"), TEXT("RECIBIDO: %lld"), Progress.TotalCollected), Cool,
        Right + 19 * S, Top + 56 * S, 135 * S, 0.86f);
    FittedText(SF_LOCAL_PRINTF(TEXT("РАСХОД: %lld"), TEXT("CONSUMPTION: %lld"), TEXT("CONSUMO: %lld"), static_cast<long long>(Progress.TotalSpent) + Progress.OreConverted), Cool,
        Right + 165 * S, Top + 56 * S, 142 * S, 0.86f);
    Line(Right + 11 * S, Top + 77 * S, Right + 313 * S, Top + 77 * S, DarkMetal);
    FittedText(SF_LOCAL_PRINTF(TEXT("СИНЯЯ: %lld"), TEXT("BLUE: %lld"), TEXT("AZUL: %lld"), Progress.BlueOre), FLinearColor(0.45f, 0.76f, 0.94f),
        Right + 19 * S, Top + 82 * S, 135 * S, 0.86f);
    FittedText(SF_LOCAL_PRINTF(TEXT("ЧЁРНАЯ: %lld"), TEXT("BLACK: %lld"), TEXT("NEGRO: %lld"), Progress.BlackOre), Cool,
        Right + 165 * S, Top + 82 * S, 142 * S, 0.86f);
    FittedText(SF_LOCAL_PRINTF(TEXT("ИЗОТОП: %lld"), TEXT("ISOTOPE: %lld"), TEXT("ISÓTOPO: %lld"), Progress.Isotope), FLinearColor(0.54f, 0.88f, 0.93f),
        Right + 19 * S, Top + 101 * S, 135 * S, 0.92f);
    Text(SpaceFlight::Localize(TEXT("СИНЯЯ + ЧЁРНАЯ 1:1")), Muted,
        Right + 165 * S, Top + 101 * S, 0.70f);
    Line(Right + 11 * S, Top + 120 * S, Right + 313 * S, Top + 120 * S, DarkMetal);
    Text(SpaceFlight::Localize(TEXT("I: ПЕРЕРАБОТКА")), Cool, Right + 19 * S, Top + 126 * S, 0.80f);
    Text(RefiningModeName(Progress.RefiningMode), PaleGold, Right + 165 * S, Top + 126 * S, 0.84f);
    Text(SpaceFlight::Localize(TEXT("M: РЕЦЕПТЫ")), Cool, Right + 234 * S, Top + 127 * S, 0.66f);
    Line(Right + 11 * S, Top + 145 * S, Right + 313 * S, Top + 145 * S, DarkMetal);
    Text(SF_LOCAL_PRINTF(TEXT("ЛАЗЕРЫ: %d / 3"), TEXT("LASERS: %d/3"), TEXT("LÁSERES: %d/3"), Progress.Lasers()), Muted,
        Right + 19 * S, Top + 151 * S, 0.86f);
    Text(SF_LOCAL_PRINTF(TEXT("РАДИУС: %.0f"), TEXT("RADIUS: %.0f"), TEXT("RADIO: %.0f"), Progress.Range()), Muted,
        Right + 165 * S, Top + 151 * S, 0.86f);

    // The objective is anchored below the resource panel and advances only on mining events.
    const SpaceFlight::FFlightObjective& Goal = Progress.Objective;
    const float GoalTop = Top + 187 * S;
    Panel(Right, GoalTop, 324 * S, 68 * S);
    Text(SpaceFlight::Localize(TEXT("ЦЕЛЬ ПОЛЁТА")), PaleGold, Right + 17 * S, GoalTop + 7 * S, 0.82f);
    FittedText(SF_LOCAL_PRINTF(TEXT("НАГРАДА: %d РУДЫ"), TEXT("REWARD: %d ORE"), TEXT("RECOMPENSA: MINERAL %d"), Progress.ObjectiveReward(Goal.Reward())), Muted,
        Right + 166 * S, GoalTop + 7 * S, 141 * S, 0.76f);
    Line(Right + 12 * S, GoalTop + 27 * S, Right + 312 * S, GoalTop + 27 * S, DarkMetal);
    const TCHAR* GoalName = SpaceFlight::Localize(TEXT("УНИЧТОЖИТЬ АСТЕРОИДЫ"));
    switch (Goal.Kind)
    {
    case 1: GoalName = SpaceFlight::Localize(TEXT("ДОБЫТЬ СИНЮЮ РУДУ")); break;
    case 2: GoalName = SpaceFlight::Localize(TEXT("ДОБЫТЬ ЧЁРНУЮ РУДУ")); break;
    case 3: GoalName = SpaceFlight::Localize(TEXT("ДОБЫТЬ РУДУ")); break;
    case 4: GoalName = SpaceFlight::Localize(TEXT("НАЙТИ БОГАТЫЕ ЗАЛЕЖИ")); break;
    case 5: GoalName = SpaceFlight::Localize(TEXT("ПОПАСТЬ ПО АСТЕРОИДАМ")); break;
    }
    FittedText(GoalName, Cool, Right + 17 * S, GoalTop + 36 * S, 210 * S, 0.82f);
    const FString GoalCount = FString::Printf(TEXT("%d / %d"), Goal.Progress, Goal.Target());
    CenterText(GoalCount, Active, Right + 278 * S, GoalTop + 36 * S, 0.90f, 70 * S);

    const float SectorTop = GoalTop + 75 * S;
    Panel(Right, SectorTop, 324 * S, 78 * S);
    const TCHAR* SectorNames[] = { SpaceFlight::Localize(TEXT("РУБЕЖ")), SpaceFlight::Localize(TEXT("СИНЯЯ РОССЫПЬ")), SpaceFlight::Localize(TEXT("АЛМАЗНЫЙ ПОТОК")) };
    const TCHAR* SectorDetails[] = {
        SpaceFlight::Localize(TEXT("15 АСТЕРОИДОВ  •  СМЕШАННАЯ РУДА")),
        SpaceFlight::Localize(TEXT("12 АСТЕРОИДОВ  •  СИНЯЯ РУДА  •  МЕДЛЕННЕЕ")),
        SpaceFlight::Localize(TEXT("15 АСТЕРОИДОВ  •  ЧЁРНАЯ РУДА  •  БЫСТРЕЕ")) };
    const int32 Sector = Progress.CurrentSector;
    Text(SF_LOCAL_PRINTF(TEXT("СЕКТОР: %s"), TEXT("SECTOR: %s"), TEXT("SECTOR: %s"), SectorNames[Sector]), PaleGold, Right + 17 * S, SectorTop + 7 * S, 0.82f);
    Line(Right + 12 * S, SectorTop + 26 * S, Right + 312 * S, SectorTop + 26 * S, DarkMetal);
    const FString ExpeditionSector = SpaceFlight::ExpeditionSectorHint(Progress.Expedition, Sector, bStressMode);
    FittedText(ExpeditionSector.IsEmpty() ? FString(SectorDetails[Sector]) : ExpeditionSector,
        ExpeditionSector.IsEmpty() ? Cool : PaleGold, Right + 17 * S, SectorTop + 31 * S, 290 * S, 0.72f);
    const int32 Unlocked = SpaceFlight::UnlockedSector(Goal.Sequence);
    const bool bCanChangeSector = Unlocked > 0 &&
        SpaceFlight::CanChangeSectorNow(bPaused, bStressMode, TotalFlightSeconds(), Progress.SectorReadyAt);
    if (bCanChangeSector)
        Text(SpaceFlight::Localize(TEXT("C — СМЕНИТЬ СЕКТОР")), Active, Right + 17 * S, SectorTop + 51 * S, 0.76f);
    if (Unlocked < 2)
    {
        const int32 Needed = Unlocked == 0 ? 3 : 8;
        const FString Next = bCanChangeSector ? SF_LOCAL_PRINTF(TEXT("ДАЛЕЕ: %d / %d"), TEXT("NEXT: %d / %d"), TEXT("SIGUIENTE: %d / %d"), Goal.Sequence, Needed) :
            SF_LOCAL_PRINTF(TEXT("НОВЫЙ СЕКТОР: %d / %d ЦЕЛЕЙ"), TEXT("NEW SECTOR: %d / %d OBJECTIVES"), TEXT("NUEVO SECTOR: %d / %d OBJETIVOS"), Goal.Sequence, Needed);
        Text(Next, Muted, Right + (bCanChangeSector ? 192 : 17) * S, SectorTop + 51 * S, 0.75f);
    }
    if (bExpeditionSelection)
    {
        const float SelectTop = SectorTop + 85 * S;
        Panel(Right, SelectTop, 324 * S, 88 * S);
        FittedText(ExpeditionTitle(), PaleGold, Right + 17*S, SelectTop + 7*S, 290*S, .82f);
        FittedText(ExpeditionDetail(), Cool, Right + 17*S, SelectTop + 31*S, 290*S, .76f);
        FittedText(ExpeditionError.IsEmpty() ? ExpeditionRewardDetail() : ExpeditionError,
            ExpeditionError.IsEmpty() ? PaleGold : FLinearColor(1.0f, .48f, .34f), Right + 17*S, SelectTop + 50*S, 290*S, .70f);
        FittedText(ExpeditionHint(), Active, Right + 17*S, SelectTop + 68*S, 290*S, .70f);
    }
    if (bRefiningSelection)
    {
        const float SelectTop = SectorTop + 85 * S;
        Panel(Right, SelectTop, 324 * S, 96 * S);
        FittedText(SF_LOCAL_PRINTF(TEXT("ПЕРЕРАБОТКА: %s → %s"), TEXT("PROCESSING: %s → %s"), TEXT("PROCESAMIENTO: %s → %s"), RefiningModeName(Progress.RefiningMode), RefiningModeName(SelectedRefiningMode)),
            PaleGold, Right + 17 * S, SelectTop + 6 * S, 290 * S, 0.80f);
        FittedText(SpaceFlight::Localize(TEXT("1 СИНЯЯ + 1 ЧЁРНАЯ → 1 ИЗОТОП")), Cool, Right + 17 * S, SelectTop + 24 * S, 290 * S, 0.75f);
        FittedText(RefiningModeDescription(SelectedRefiningMode), Cool, Right + 17 * S, SelectTop + 42 * S, 290 * S, 0.72f);
        FittedText(RefiningApplyHint(Progress.RefiningMode, SelectedRefiningMode), Muted, Right + 17 * S, SelectTop + 60 * S, 290 * S, 0.70f);
        FittedText(SpaceFlight::Localize(TEXT("I — ДАЛЕЕ   ENTER — ПРИМЕНИТЬ   ESC — ОТМЕНА")), Cool,
            Right + 17 * S, SelectTop + 78 * S, 290 * S, 0.68f);
    }
    if (bProductionSelection)
    {
        const float ProductionTop = SectorTop + 85 * S;
        Panel(Right, ProductionTop, 324 * S, 88 * S);
        Text(SpaceFlight::Localize(TEXT("ФАБРИКАТОР")), PaleGold, Right + 17 * S, ProductionTop + 7 * S, 0.80f);
        FittedText(SF_LOCAL_PRINTF(TEXT("ОБЫЧНАЯ: %lld"), TEXT("REGULAR: %lld"), TEXT("NORMAL: %lld"), Progress.CommonOre()), Muted,
            Right + 178 * S, ProductionTop + 7 * S, 129 * S, 0.75f);
        Line(Right + 12 * S, ProductionTop + 26 * S, Right + 312 * S, ProductionTop + 26 * S, DarkMetal);
        const FString Recipe = SelectedProductionKind == SpaceFlight::EOreKind::Blue ?
            SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ  →  1 СИНЕЙ"), TEXT("%d REGULAR → 1 BLUE"), TEXT("%d REGULAR → 1 AZUL"), Progress.ProductionCost()) :
            SF_LOCAL_PRINTF(TEXT("%d ОБЫЧНОЙ  →  1 ЧЁРНОЙ"), TEXT("%d REGULAR → 1 BLACK"), TEXT("%d REGULAR → 1 NEGRO"), Progress.ProductionCost());
        Text(Recipe, Cool, Right + 17 * S, ProductionTop + 31 * S, 0.80f);
        const float Wait = Progress.ProductionReadyAt - Elapsed;
        const FString State = Wait > 0 ? SF_LOCAL_PRINTF(TEXT("ГОТОВ ЧЕРЕЗ %d СЕК."), TEXT("READY IN %d SEC."), TEXT("LISTO EN %d SEC."), FMath::CeilToInt(Wait)) :
            (Progress.CommonOre() < Progress.ProductionCost() ? SpaceFlight::Localize(TEXT("НЕДОСТАТОЧНО ОБЫЧНОЙ РУДЫ")) : SpaceFlight::Localize(TEXT("МОДУЛЬ ГОТОВ")));
        Text(State, Wait <= 0 && Progress.CommonOre() >= Progress.ProductionCost() ? Active : Muted,
            Right + 17 * S, ProductionTop + 50 * S, 0.72f);
        Text(SpaceFlight::Localize(TEXT("M — ВЫБОР  ENTER — СОЗДАТЬ  ESC — ОТМЕНА")),
            Cool, Right + 17 * S, ProductionTop + 68 * S, 0.66f);
    }
}

void ASpaceFlightHUD::ReplaySurprise()
{
    if (!GetWorld() || !bTreeOpen || !bJournalOpen || bStressMode || bSaveBlocked || bSaveWriteFailed ||
        (!Progress.Surprise.bDelivered && !bSurpriseSeenThisSession)) return;
    bSurprisePlaying = true;
    bSurpriseReplay = true;
    SurpriseAge = 0.0f;
    SurpriseLastRealTime = GetWorld()->GetRealTimeSeconds();
#if WITH_DEV_AUTOMATION_TESTS
    if (SurpriseCheckPhase == TEXT("replay")) UE_LOG(LogTemp, Display, TEXT("SURPRISE_REPLAY"));
#endif
}

void ASpaceFlightHUD::DrawSurprise(float W, float H)
{
    if (!GetWorld() || !GEngine || W <= 0.0f || H <= 0.0f) return;
    const double Now = GetWorld()->GetRealTimeSeconds();
    const float RealDelta = SurpriseLastRealTime > 0.0 ? static_cast<float>(FMath::Clamp(Now - SurpriseLastRealTime, 0.0, 0.1)) : 0.0f;
    SurpriseLastRealTime = Now;
#if WITH_DEV_AUTOMATION_TESTS
    if (!SurpriseCheckPhase.IsEmpty() && !bSurpriseCheckFinished)
    {
        if (SurpriseCheckFinishAt > 0.0 && Now >= SurpriseCheckFinishAt)
        {
            FinishSurpriseCheck(true);
            return;
        }
        if (SurpriseCheckPhase == TEXT("replay") && PlayerOwner)
        {
            auto SendKey = [&](const FKey& Key, EInputEvent Event)
            { PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.0f : 0.0f)); };
            if (SurpriseCheckStep == 0 && Now - SurpriseCheckStarted >= 2.0)
            {
                if (!PlayerOwner->SetPause(true))
                {
                    UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR replay_pause"));
                    FinishSurpriseCheck(false);
                    return;
                }
                SendKey(EKeys::Tab, IE_Pressed);
                SurpriseCheckStep = 1;
            }
            else if (SurpriseCheckStep == 1 && bTreeOpen)
            { SendKey(EKeys::Tab, IE_Released); SendKey(EKeys::J, IE_Pressed); SurpriseCheckStep = 2; }
            else if (SurpriseCheckStep == 2 && bJournalOpen)
            { SendKey(EKeys::J, IE_Released); SendKey(EKeys::R, IE_Pressed); SurpriseCheckStep = 3; }
            else if (SurpriseCheckStep == 3 && bSurprisePlaying && bSurpriseReplay)
            { SendKey(EKeys::R, IE_Released); SurpriseCheckStep = 4; }
            else if (SurpriseCheckStep == 5 && !bJournalOpen)
            { SendKey(EKeys::J, IE_Released); SendKey(EKeys::Tab, IE_Pressed); SurpriseCheckStep = 6; }
            else if (SurpriseCheckStep == 6 && !bTreeOpen)
            { SendKey(EKeys::Tab, IE_Released); SendKey(EKeys::Escape, IE_Pressed); SurpriseCheckStep = 7; }
            else if (SurpriseCheckStep == 7 && !GetWorld()->IsPaused())
            { SendKey(EKeys::Escape, IE_Released); FinishSurpriseCheck(true); return; }
        }
        else if (SurpriseCheckPhase == TEXT("play") && PlayerOwner)
        {
            if (SurpriseCheckStep == 0)
            {
                if (!PlayerOwner->SetPause(true)) { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR initial_pause")); FinishSurpriseCheck(false); return; }
                SurpriseCheckStepAt = Now + 1.0;
                SurpriseCheckStep = 1;
            }
            else if (SurpriseCheckStep == 1 && Now >= SurpriseCheckStepAt)
            {
                if (bSurprisePlaying || !PlayerOwner->SetPause(false)) { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR paused_gate")); FinishSurpriseCheck(false); return; }
                ToggleTree();
                SurpriseCheckStepAt = Now + .5;
                SurpriseCheckStep = 2;
            }
            else if (SurpriseCheckStep == 2 && Now >= SurpriseCheckStepAt)
            {
                if (bSurprisePlaying || !bTreeOpen) { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR tree_gate")); FinishSurpriseCheck(false); return; }
                ToggleTree();
                SurpriseCheckStep = 3;
            }
            else if (SurpriseCheckStep == 3 && bSurprisePlaying && SurpriseAge >= 2.0f)
            {
                SurpriseCheckAge = SurpriseAge;
                if (!PlayerOwner->SetPause(true)) { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR mid_pause")); FinishSurpriseCheck(false); return; }
                SurpriseCheckStepAt = Now + 1.0;
                SurpriseCheckStep = 4;
            }
            else if (SurpriseCheckStep == 4 && Now >= SurpriseCheckStepAt)
            {
                if (!FMath::IsNearlyEqual(SurpriseAge, SurpriseCheckAge, .001f) || !PlayerOwner->SetPause(false))
                { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR paused_animation")); FinishSurpriseCheck(false); return; }
                SurpriseCheckStep = 5;
            }
        }
    }
#endif
    if (bSurpriseCommitPending && Now >= SurpriseRetryAt && bSaveEnabled && !bSaveBlocked && !bStressMode)
    {
        const int64 PreviousRevision = SaveRevision;
        Progress.Surprise.bDelivered = true;
        SaveProgress();
        const bool bCommitted = !bSaveWriteFailed && SaveRevision > PreviousRevision;
        if (bCommitted) bSurpriseCommitPending = false;
        else { Progress.Surprise.bDelivered = false; SurpriseRetryAt = Now + 15.0; }
#if WITH_DEV_AUTOMATION_TESTS
        if (SurpriseCheckPhase == TEXT("play"))
        {
            UE_LOG(LogTemp, Display, TEXT("SURPRISE_COMMIT result=%s revision=%lld delivered=%d"),
                bCommitted ? TEXT("saved") : TEXT("failed"), SaveRevision, Progress.Surprise.bDelivered ? 1 : 0);
            if (bCommitted) FinishSurpriseCheck(true);
            else if (SurpriseCheckFinishAt <= 0.0) SurpriseCheckFinishAt = Now + 17.0;
        }
#endif
    }
    const bool bImportantNotice = !UpgradeNotice.IsEmpty() && Now < NoticeUntil;
    const bool bBlocked = GetWorld()->IsPaused() || bTreeOpen || bStressMode || bExpeditionSelection || bRefiningSelection ||
        bProductionSelection || bSpecializationSelection || bSaveBlocked || bSaveWriteFailed ||
        MineralEvent.Phase != SpaceFlight::EMineralPhase::Waiting || bImportantNotice;
    if (!bSurprisePlaying && !bSurpriseSeenThisSession && !bSurpriseCommitPending && bSaveEnabled &&
        SpaceFlight::CanPresentSurprise(Progress.Surprise, TotalFlightSeconds(), Progress.Objective.Sequence, bBlocked))
    {
        bSurprisePlaying = true;
        bSurpriseReplay = false;
        SurpriseAge = 0.0f;
#if WITH_DEV_AUTOMATION_TESTS
        if (SurpriseCheckPhase == TEXT("play"))
        {
            UE_LOG(LogTemp, Display, TEXT("SURPRISE_START mode=%s"), bSurpriseCheckManual ? TEXT("manual") : TEXT("auto"));
            if (bSurpriseCheckManual && PlayerOwner)
            {
                SurpriseCheckShipX = ShipX;
                PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::D, IE_Pressed, 1.0f));
            }
        }
#endif
    }
    if (!bSurprisePlaying) return;
    if (bSurpriseReplay && (!bTreeOpen || !bJournalOpen || bStressMode)) { bSurprisePlaying = false; bSurpriseReplay = false; return; }
    if (bSurpriseReplay && (bSaveBlocked || bSaveWriteFailed)) return;
    if (!bSurpriseReplay && bBlocked) return;
    SurpriseAge += bSurpriseReplay ? RealDelta : FMath::Clamp(GetWorld()->GetDeltaSeconds(), 0.0f, 0.1f);
    constexpr float Duration = 9.0f;
    if (SurpriseAge >= Duration)
    {
        bSurprisePlaying = false;
        if (bSurpriseReplay)
        {
            bSurpriseReplay = false;
#if WITH_DEV_AUTOMATION_TESTS
            if (SurpriseCheckPhase == TEXT("replay") && PlayerOwner)
            {
                PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::J, IE_Pressed, 1.0f));
                SurpriseCheckStep = 5;
            }
#endif
        }
        else
        {
#if WITH_DEV_AUTOMATION_TESTS
            if (SurpriseCheckPhase == TEXT("play") && bSurpriseCheckManual && PlayerOwner)
            {
                PlayerOwner->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::D, IE_Released, 0.0f));
                if (bAutopilot || ShipX <= SurpriseCheckShipX + 1.0f)
                { UE_LOG(LogTemp, Error, TEXT("SURPRISE_CHECK_ERROR manual_movement")); FinishSurpriseCheck(false); return; }
            }
#endif
            bSurpriseSeenThisSession = true;
            bSurpriseCommitPending = true;
            SurpriseRetryAt = Now;
#if WITH_DEV_AUTOMATION_TESTS
            if (SurpriseCheckPhase == TEXT("play")) UE_LOG(LogTemp, Display, TEXT("SURPRISE_FINISH"));
#endif
        }
        return;
    }
    const float Alpha = FMath::Min(FMath::Clamp(SurpriseAge / 1.2f, 0.0f, 1.0f), FMath::Clamp((Duration - SurpriseAge) / 1.6f, 0.0f, 1.0f));
    const float Scale = FMath::Clamp(FMath::Min(W / 600.0f, H / 450.0f), 0.72f, 1.1f);
    const float PanelW = FMath::Min(W - 24.0f, 500.0f * Scale);
    const float PanelH = 76.0f * Scale;
    const float X = (W - PanelW) * .5f;
    const float Y = bSurpriseReplay ? FMath::Max(12.0f, H * .58f - PanelH * .5f) :
        FMath::Min(H - PanelH - 14.0f, H * .81f);
    DrawRect(FLinearColor(.006f, .025f, .041f, .88f * Alpha), X, Y, PanelW, PanelH);
    DrawRect(FLinearColor(.34f, .78f, .78f, .85f * Alpha), X, Y, PanelW, 2.0f * Scale);
    const FString Lines[] = { SpaceFlight::Localize(TEXT("СИГНАЛ ДАЛЁКОГО ИССЛЕДОВАТЕЛЯ")), SpaceFlight::Localize(TEXT("Хорошего полёта. Ты не один среди звёзд.")) };
    for (int32 I = 0; I < 2; ++I)
    {
        const float FontScale = (I == 0 ? .83f : .95f) * Scale;
        float TextW = 0.0f, TextH = 0.0f;
        GetTextSize(Lines[I], TextW, TextH, GEngine->GetSmallFont(), FontScale);
        const float Fit = TextW > 0.0f ? FMath::Min(1.0f, (PanelW - 26.0f * Scale) / TextW) : 1.0f;
        DrawText(Lines[I], I == 0 ? FLinearColor(.51f, .85f, .82f, Alpha) : FLinearColor(.91f, .93f, .86f, Alpha),
            X + (PanelW - TextW * Fit) * .5f, Y + (I == 0 ? 14.0f : 40.0f) * Scale, GEngine->GetSmallFont(), FontScale * Fit);
    }
    const float Travel = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(SurpriseAge / Duration, 0.0f, 1.0f));
    const FVector2D Beacon(W * (.22f + .56f * Travel), H * (.43f - .025f * FMath::Sin(Travel * PI)));
    if (CrystalGlowTexture && CrystalGlowTexture->GetResource())
    {
        auto SoftLight = [&](FVector2D Position, float Radius, float Opacity)
        {
            FCanvasTileItem Tile(Position - FVector2D(Radius, Radius), CrystalGlowTexture->GetResource(),
                FVector2D(2.0f * Radius, 2.0f * Radius), FLinearColor(.48f, .91f, .89f, Alpha * Opacity));
            Tile.BlendMode = SE_BLEND_Translucent;
            Canvas->DrawItem(Tile);
        };
        for (int32 I = 3; I >= 1; --I)
            SoftLight(Beacon - FVector2D(I * 6.0f * Scale, 0.0f), (5.0f - I * .6f) * Scale, .12f / I);
        SoftLight(Beacon, 12.0f * Scale, .32f);
        SoftLight(Beacon, 4.0f * Scale, .75f);
    }
    else DrawLine(Beacon.X - 2.0f * Scale, Beacon.Y, Beacon.X + 2.0f * Scale, Beacon.Y,
        FLinearColor(.55f, .95f, .94f, .7f * Alpha), 1.5f * Scale);
}

void ASpaceFlightHUD::DrawDiscoveryJournal(float W, float H)
{
    if (!GEngine || W <= 0 || H <= 0) return;
    const float S = SpaceFlight::UpgradeTreeScale(W, H);
    const FVector2D Origin = SpaceFlight::UpgradeTreeOrigin(W, H);
    const FLinearColor Back(.006f, .008f, .016f);
    const FLinearColor PanelColor(.022f, .020f, .021f);
    const FLinearColor Gold(.60f, .33f, .09f);
    const FLinearColor Bright(.94f, .66f, .27f);
    const FLinearColor Muted(.58f, .56f, .50f);
    const FLinearColor Active(.48f, .86f, .70f);
    if (SpaceFlight::IsPortraitUpgradeLayout(W, H))
    {
        // Reflow the two desktop columns into a single vertical journal.
        const float J = FMath::Min(W / 512.0f, H / 900.0f);
        const float Left = (W - 512.0f * J) * .5f;
        auto PBox = [&](float X, float Y, float Width, float Height, FLinearColor Color)
        { DrawRect(Color, Left + X * J, Y * J, Width * J, Height * J); };
        auto PText = [&](const FString& Value, float X, float Y, FLinearColor Color, float Size = .95f, float MaxWidth = 448.0f)
        {
            float TextWidth = 0, TextHeight = 0;
            GetTextSize(Value, TextWidth, TextHeight, GEngine->GetSmallFont(), Size * J);
            const float Fit = TextWidth > 0 ? FMath::Min(1.0f, MaxWidth * J / TextWidth) : 1.0f;
            DrawText(Value, Color, Left + X * J, Y * J, GEngine->GetSmallFont(), Size * J * Fit);
        };
        DrawRect(Back, 0, 0, W, H);
        PBox(12, 12, 488, 2, Gold); PBox(12, 886, 488, 2, Gold);
        PBox(12, 12, 2, 876, Gold); PBox(498, 12, 2, 876, Gold);
        PText(SpaceFlight::Localize(TEXT("ЖУРНАЛ ОТКРЫТИЙ")), 26, 25, Bright, 1.35f);
        PText(SpaceFlight::Localize(TEXT("Развитие корабля и подтверждённые находки")), 26, 52, Muted, .85f);
        if (bSaveBlocked || bSaveWriteFailed)
            PText(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА")) : SpaceFlight::Localize(TEXT("ОШИБКА СОХРАНЕНИЯ")), 26, 72, FLinearColor(1.0f, .4f, .28f), .82f);
        else if (bStressMode) PText(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), 26, 72, Bright, .82f);
        PBox(24, 94, 464, 1, Gold);
        PText(SpaceFlight::Localize(TEXT("ВЕТВИ РАЗВИТИЯ")), 26, 105, Bright, 1.02f);
        const TCHAR* Branches[] = { SpaceFlight::Localize(TEXT("РАДАР")), SpaceFlight::Localize(TEXT("СБОРЩИК")), SpaceFlight::Localize(TEXT("ЛАЗЕРЫ")), SpaceFlight::Localize(TEXT("ОХЛАЖДЕНИЕ")), SpaceFlight::Localize(TEXT("ДВИГАТЕЛЬ")) };
        for (int32 I = 0; I < 5; ++I)
        {
            const auto Stats = SpaceFlight::JournalBranchStats(Progress, I);
            const float Y = 135.0f + I * 28.0f;
            PText(Branches[I], 26, Y, FLinearColor(.84f, .79f, .68f), .9f, 150);
            PBox(205, Y + 6, 175, 5, FLinearColor(.11f, .09f, .07f));
            if (Stats.Total > 0) PBox(205, Y + 6, 175.0f * Stats.Owned / Stats.Total, 5, Gold);
            PText(FString::Printf(TEXT("%d / %d"), Stats.Owned, Stats.Total), 405, Y, Active, .9f, 72);
        }
        const int32 Unlocked = SpaceFlight::UnlockedSector(Progress.Objective.Sequence);
        PText(SF_LOCAL_PRINTF(TEXT("ЦЕЛЕЙ ВЫПОЛНЕНО: %d"), TEXT("OBJECTIVES COMPLETED: %d"), TEXT("OBJETIVOS CUMPLIDOS: %d"), Progress.Objective.Sequence), 26, 285, Bright, .90f);
        const FString NextSector = Unlocked >= 2 ? SpaceFlight::Localize(TEXT("ОТКРЫТЫ ВСЕ СЕКТОРЫ")) :
            SF_LOCAL_PRINTF(TEXT("СЛЕДУЮЩИЙ СЕКТОР: %d / %d ЦЕЛЕЙ"), TEXT("NEXT SECTOR: %d / %d OBJECTIVES"), TEXT("PRÓXIMO SECTOR: %d / %d OBJETIVOS"), Progress.Objective.Sequence, Unlocked == 0 ? 3 : 8);
        PText(NextSector, 26, 309, Muted, .85f);
        PBox(24, 338, 464, 1, Gold);
        PText(SpaceFlight::Localize(TEXT("ОТКРЫТЫЕ СЕКТОРА")), 26, 348, Bright, 1.0f);
        const TCHAR* Sectors[] = { SpaceFlight::Localize(TEXT("РУБЕЖ")), SpaceFlight::Localize(TEXT("СИНЯЯ РОССЫПЬ")), SpaceFlight::Localize(TEXT("АЛМАЗНЫЙ ПОТОК")) };
        for (int32 I = 0; I < 3; ++I)
        {
            const float Y = 378.0f + I * 25.0f;
            PText(Sectors[I], 26, Y, I <= Unlocked ? FLinearColor(.84f, .79f, .68f) : Muted, .86f, 300);
            PText(I <= Unlocked ? SpaceFlight::Localize(TEXT("ОТКРЫТ")) : SpaceFlight::Localize(TEXT("ЗАКРЫТ")), 366, Y, I <= Unlocked ? Active : Muted, .84f, 110);
        }
        PBox(24, 462, 464, 1, Gold);
        PText(SpaceFlight::Localize(TEXT("РЕСУРСЫ")), 26, 472, Bright, 1.0f);
        const TCHAR* ResourceNames[] = { SpaceFlight::Localize(TEXT("ОБЫЧНАЯ РУДА")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА")), SpaceFlight::Localize(TEXT("ИЗОТОП")) };
        const SpaceFlight::EDiscovery ResourceBits[] = { SpaceFlight::EDiscovery::CommonOre, SpaceFlight::EDiscovery::BlueOre,
            SpaceFlight::EDiscovery::BlackOre, SpaceFlight::EDiscovery::Isotope };
        for (int32 I = 0; I < 4; ++I)
        {
            const bool bFound = Progress.HasDiscovery(ResourceBits[I]);
            const float Y = 500.0f + I * 25.0f;
            PText(ResourceNames[I], 26, Y, bFound ? FLinearColor(.84f, .79f, .68f) : Muted, .86f, 300);
            PText(bFound ? SpaceFlight::Localize(TEXT("ОТМЕЧЕНО")) : SpaceFlight::Localize(TEXT("НЕТ ДАННЫХ")), 360, Y, bFound ? Active : Muted, .80f, 116);
        }
        PBox(24, 608, 464, 1, Gold);
        PText(SpaceFlight::Localize(TEXT("РЕДКИЕ ВСТРЕЧИ")), 26, 618, Bright, 1.0f);
        const TCHAR* EventNames[] = { SpaceFlight::Localize(TEXT("МИНЕРАЛЬНЫЙ ПОТОК")), SpaceFlight::Localize(TEXT("НЕСТАБИЛЬНЫЙ КРИСТАЛЛ")), SpaceFlight::Localize(TEXT("КРУПНОЕ МЕСТОРОЖДЕНИЕ")) };
        const SpaceFlight::EDiscovery EventBits[] = { SpaceFlight::EDiscovery::MineralFlow, SpaceFlight::EDiscovery::UnstableCrystal, SpaceFlight::EDiscovery::LargeDeposit };
        for (int32 I = 0; I < 3; ++I)
        {
            const bool bFound = Progress.HasDiscovery(EventBits[I]);
            const float Y = 648.0f + I * 25.0f;
            PText(EventNames[I], 26, Y, bFound ? FLinearColor(.84f, .79f, .68f) : Muted, .84f, 300);
            PText(bFound ? SpaceFlight::Localize(TEXT("ОТМЕЧЕНО")) : SpaceFlight::Localize(TEXT("НЕТ ДАННЫХ")), 360, Y, bFound ? Active : Muted, .80f, 116);
        }
        PText(SpaceFlight::Localize(TEXT("Прошлые встречи до обновления журнала неизвестны.")), 26, 741, Muted, .80f);
        if (!bStressMode && (Progress.Surprise.bDelivered || bSurpriseSeenThisSession))
            PText(SpaceFlight::Localize(TEXT("R — ПОВТОРИТЬ ПОСЛАНИЕ")), 26, 779, Active, .90f);
        PText(SF_LOCAL_PRINTF(TEXT("J — ДЕРЕВО   •   TAB — ЗАКРЫТЬ   •   ESC — %s"), TEXT("J - TREE • TAB - CLOSE • ESC - %s"), TEXT("J - ÁRBOL • TAB - CERRAR • ESC - %s"),
            SpaceFlight::EscapeActionShort(GetWorld()->IsPaused(), false, false, false)), 26, 846, Gold, .82f);
        return;
    }
    auto Box = [&](float X, float Y, float Width, float Height, FLinearColor Color)
    { DrawRect(Color, Origin.X + X * S, Origin.Y + Y * S, Width * S, Height * S); };
    auto Text = [&](const FString& Value, float X, float Y, FLinearColor Color, float Size = .95f, float MaxWidth = 500.0f)
    {
        float Width = 0, Height = 0;
        GetTextSize(Value, Width, Height, GEngine->GetSmallFont(), Size * S);
        const float Fit = Width > 0 ? FMath::Min(1.0f, MaxWidth * S / Width) : 1.0f;
        DrawText(Value, Color, Origin.X + X * S, Origin.Y + Y * S, GEngine->GetSmallFont(), Size * S * Fit);
    };
    DrawRect(Back, 0, 0, W, H);
    Box(30, 18, 1220, 2, Gold); Box(30, 700, 1220, 2, Gold);
    Box(30, 18, 2, 684, Gold); Box(1248, 18, 2, 684, Gold);
    Text(SpaceFlight::Localize(TEXT("ЖУРНАЛ ОТКРЫТИЙ")), 82, 37, Bright, 1.6f);
    Text(SpaceFlight::Localize(TEXT("Развитие корабля и подтверждённые находки")), 82, 71, Muted);
    if (bSaveBlocked || bSaveWriteFailed)
        Text(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА")) : (bSaveBlocked ? SpaceFlight::Localize(TEXT("ОШИБКА ЧТЕНИЯ СОХРАНЕНИЯ")) : SpaceFlight::Localize(TEXT("ОШИБКА СОХРАНЕНИЯ"))), 850, 44, FLinearColor(1.0f, .4f, .28f), .85f, 300);
    else if (bStressMode)
        Text(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), 850, 44, Bright, .85f, 300);
    Box(54, 97, 1172, 1, Gold);
    Box(70, 122, 550, 495, PanelColor);
    Box(650, 122, 560, 495, PanelColor);
    Text(SpaceFlight::Localize(TEXT("ВЕТВИ РАЗВИТИЯ")), 92, 140, Bright, 1.1f);
    Text(SpaceFlight::Localize(TEXT("Изучено улучшений, включая микроузлы")), 92, 169, Muted, .85f);
    const TCHAR* BranchNames[] = { SpaceFlight::Localize(TEXT("РАДАР")), SpaceFlight::Localize(TEXT("СБОРЩИК")), SpaceFlight::Localize(TEXT("ЛАЗЕРЫ")), SpaceFlight::Localize(TEXT("ОХЛАЖДЕНИЕ")), SpaceFlight::Localize(TEXT("ДВИГАТЕЛЬ")) };
    for (int32 I = 0; I < 5; ++I)
    {
        const auto Stats = SpaceFlight::JournalBranchStats(Progress, I);
        const float Y = 205.0f + I * 52.0f;
        Text(BranchNames[I], 92, Y, FLinearColor(.84f, .79f, .68f), .95f, 180);
        Box(285, Y + 6, 210, 5, FLinearColor(.11f, .09f, .07f));
        if (Stats.Total > 0) Box(285, Y + 6, 210.0f * Stats.Owned / Stats.Total, 5, Gold);
        Text(FString::Printf(TEXT("%d / %d"), Stats.Owned, Stats.Total), 520, Y, Active, .95f, 70);
    }
    Box(92, 480, 506, 1, Gold);
    const int32 Unlocked = SpaceFlight::UnlockedSector(Progress.Objective.Sequence);
    Text(SF_LOCAL_PRINTF(TEXT("ЦЕЛЕЙ ВЫПОЛНЕНО: %d"), TEXT("OBJECTIVES COMPLETED: %d"), TEXT("OBJETIVOS CUMPLIDOS: %d"), Progress.Objective.Sequence), 92, 502, Bright, .95f, 500);
    const FString NextSector = Unlocked >= 2 ? SpaceFlight::Localize(TEXT("ОТКРЫТЫ ВСЕ СЕКТОРЫ")) :
        SF_LOCAL_PRINTF(TEXT("СЛЕДУЮЩИЙ СЕКТОР: %d / %d ЦЕЛЕЙ"), TEXT("NEXT SECTOR: %d / %d OBJECTIVES"), TEXT("PRÓXIMO SECTOR: %d / %d OBJETIVOS"), Progress.Objective.Sequence, Unlocked == 0 ? 3 : 8);
    Text(NextSector, 92, 535, Muted, .90f, 500);

    Text(SpaceFlight::Localize(TEXT("ОТКРЫТЫЕ СЕКТОРА")), 674, 140, Bright, 1.1f);
    const TCHAR* Sectors[] = { SpaceFlight::Localize(TEXT("РУБЕЖ")), SpaceFlight::Localize(TEXT("СИНЯЯ РОССЫПЬ")), SpaceFlight::Localize(TEXT("АЛМАЗНЫЙ ПОТОК")) };
    for (int32 I = 0; I < 3; ++I)
    {
        const float Y = 175.0f + I * 26.0f;
        Text(Sectors[I], 674, Y, I <= Unlocked ? FLinearColor(.84f, .79f, .68f) : Muted, .88f, 300);
        Text(I <= Unlocked ? SpaceFlight::Localize(TEXT("ОТКРЫТ")) : SpaceFlight::Localize(TEXT("ЗАКРЫТ")), 1070, Y, I <= Unlocked ? Active : Muted, .85f, 115);
    }
    Box(674, 256, 512, 1, Gold);
    Text(SpaceFlight::Localize(TEXT("РЕСУРСЫ")), 674, 270, Bright, 1.0f);
    const TCHAR* ResourceNames[] = { SpaceFlight::Localize(TEXT("ОБЫЧНАЯ РУДА")), SpaceFlight::Localize(TEXT("СИНЯЯ РУДА")), SpaceFlight::Localize(TEXT("ЧЁРНАЯ РУДА")), SpaceFlight::Localize(TEXT("ИЗОТОП")) };
    const SpaceFlight::EDiscovery ResourceBits[] = { SpaceFlight::EDiscovery::CommonOre, SpaceFlight::EDiscovery::BlueOre,
        SpaceFlight::EDiscovery::BlackOre, SpaceFlight::EDiscovery::Isotope };
    for (int32 I = 0; I < 4; ++I)
    {
        const bool bFound = Progress.HasDiscovery(ResourceBits[I]);
        const float Y = 296.0f + I * 24.0f;
        Text(ResourceNames[I], 674, Y, bFound ? FLinearColor(.84f, .79f, .68f) : Muted, .88f, 300);
        Text(bFound ? SpaceFlight::Localize(TEXT("ОТМЕЧЕНО")) : SpaceFlight::Localize(TEXT("НЕТ ДАННЫХ")), 1050, Y, bFound ? Active : Muted, .82f, 135);
    }
    Box(674, 396, 512, 1, Gold);
    Text(SpaceFlight::Localize(TEXT("РЕДКИЕ ВСТРЕЧИ")), 674, 410, Bright, 1.0f);
    const TCHAR* EventNames[] = { SpaceFlight::Localize(TEXT("МИНЕРАЛЬНЫЙ ПОТОК")), SpaceFlight::Localize(TEXT("НЕСТАБИЛЬНЫЙ КРИСТАЛЛ")), SpaceFlight::Localize(TEXT("КРУПНОЕ МЕСТОРОЖДЕНИЕ")) };
    const SpaceFlight::EDiscovery EventBits[] = { SpaceFlight::EDiscovery::MineralFlow, SpaceFlight::EDiscovery::UnstableCrystal, SpaceFlight::EDiscovery::LargeDeposit };
    for (int32 I = 0; I < 3; ++I)
    {
        const bool bFound = Progress.HasDiscovery(EventBits[I]);
        const float Y = 436.0f + I * 26.0f;
        Text(EventNames[I], 674, Y, bFound ? FLinearColor(.84f, .79f, .68f) : Muted, .86f, 345);
        Text(bFound ? SpaceFlight::Localize(TEXT("ОТМЕЧЕНО")) : SpaceFlight::Localize(TEXT("НЕТ ДАННЫХ")), 1050, Y, bFound ? Active : Muted, .82f, 135);
    }
    Text(SpaceFlight::Localize(TEXT("Прошлые встречи до обновления журнала не восстанавливаются.")), 674, 547, Muted, .78f, 512);
    if (!bStressMode && (Progress.Surprise.bDelivered || bSurpriseSeenThisSession))
        Text(SpaceFlight::Localize(TEXT("R — ПОВТОРИТЬ ПОСЛАНИЕ")), 674, 580, Active, .90f, 512);
    Text(SF_LOCAL_PRINTF(TEXT("J — дерево    •    TAB — закрыть    •    ESC — %s"), TEXT("J - tree • TAB - close • ESC - %s"), TEXT("J - árbol • TAB - cerrar • ESC - %s"),
        SpaceFlight::EscapeActionShort(GetWorld()->IsPaused(), false, false, false)),
        416, 659, FLinearColor(.70f, .59f, .39f), .95f, 650);
}

void ASpaceFlightHUD::DrawUpgradeTree(float W, float H)
{
    const float UIScale = SpaceFlight::UpgradeTreeScale(W, H);
    if (PlayerOwner && TreePointer.IsActive())
    {
        const auto Button = TreePointer.Button();
        const FKey Key = Button == SpaceFlight::ETreePointerButton::Left ? EKeys::LeftMouseButton :
            (Button == SpaceFlight::ETreePointerButton::Middle ? EKeys::MiddleMouseButton : EKeys::RightMouseButton);
        if (!PlayerOwner->IsInputKeyDown(Key)) TreePointer.Cancel();
        else
        {
            float X = 0, Y = 0;
            if (PlayerOwner->GetMousePosition(X, Y)) TreePointer.Update(FVector2D(X, Y), W, H, TreePan);
        }
    }
    const int32 Hovered = ResolveTreeHoveredUpgrade();
    const float Scale = UIScale * TreeZoom;
    const FVector2D Origin = SpaceFlight::UpgradeTreeOrigin(W, H);
    auto Panel = [&](float X, float Y, float Width, float Height, const FLinearColor& Color)
    { DrawRect(Color, X, Y, Width, Height); };
    const FLinearColor Gold(0.60f, 0.33f, 0.09f);
    const FLinearColor GoldLight(0.94f, 0.66f, 0.27f);
    const FLinearColor BronzeDark(0.12f, 0.055f, 0.018f);
    Panel(0, 0, W, H, FLinearColor(0.006f, 0.008f, 0.016f, 0.97f));
    auto Box = [&](float X, float Y, float Width, float Height, const FLinearColor& Color)
    { Panel(Origin.X + X * UIScale, Origin.Y + Y * UIScale, Width * UIScale, Height * UIScale, Color); };

    auto Icon = [&](int32 UpgradeIndex, float X, float Y, float Size, const FLinearColor& Tint, bool bShip = false)
    {
        const bool bHasTalent = TalentAtlas && TalentAtlas->GetResource();
        const bool bHasExtra = ExtraTalentAtlas && ExtraTalentAtlas->GetResource();
        const bool bHasLegacy = SkillAtlas && SkillAtlas->GetResource();
        const auto Art = bShip ? SpaceFlight::ResolveShipIcon(bHasTalent, bHasLegacy) :
            SpaceFlight::ResolveTalentIcon(UpgradeIndex, bHasExtra, bHasTalent, bHasLegacy);
        if (Art.Source == SpaceFlight::ETalentAtlasSource::None) return;
        const bool bExtra = Art.Source == SpaceFlight::ETalentAtlasSource::Extra;
        const bool bTalent = bExtra || Art.Source == SpaceFlight::ETalentAtlasSource::Talent;
        UTexture2D* Texture = bExtra ? ExtraTalentAtlas.Get() : (bTalent ? TalentAtlas.Get() : SkillAtlas.Get());
        const int32 Columns = bTalent ? SpaceFlight::TalentAtlasColumns : 2;
        const int32 Rows = bTalent ? SpaceFlight::TalentAtlasRows : 3;
        const float CellW = 1.0f / Columns, CellH = 1.0f / Rows;
        const float U = (Art.Index % Columns) * CellW;
        const float V = (Art.Index / Columns) * CellH;
        const float Inset = bTalent ? 0.002f : 0.001f;
        const FVector2D UV0(U + Inset, V + Inset);
        const FVector2D UV1(U + CellW - Inset, V + CellH - Inset);
        FCanvasTileItem Tile(FVector2D(X, Y), Texture->GetResource(), FVector2D(Size, Size), UV0, UV1, Tint);
        Tile.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Tile);
    };
    // Reserve detail-panel space only when this particular upgrade has art
    // in an available atlas; the extra atlas covers a subset of upgrades.
    const bool bCanDrawUpgradeArt = SpaceFlight::ResolveTalentIcon(Hovered,
        ExtraTalentAtlas && ExtraTalentAtlas->GetResource(),
        TalentAtlas && TalentAtlas->GetResource(), SkillAtlas && SkillAtlas->GetResource()).Source !=
        SpaceFlight::ETalentAtlasSource::None;
    auto FittedText = [&](const FString& Text, const FLinearColor& Color, float X, float Y,
                          float MaxWidth, float BaseScale)
    {
        float TextWidth = 0.0f, TextHeight = 0.0f;
        GetTextSize(Text, TextWidth, TextHeight, GEngine->GetSmallFont(), BaseScale * Scale);
        const float Fit = TextWidth > 0.0f ? FMath::Min(1.0f, MaxWidth * Scale / TextWidth) : 1.0f;
        DrawText(Text, Color, X, Y, GEngine->GetSmallFont(), BaseScale * Scale * Fit);
    };

    auto CenteredText = [&](const FString& Text, const FLinearColor& Color, float CenterX, float Y,
                            float MaxWidth, float BaseScale)
    {
        float TextWidth = 0.0f, TextHeight = 0.0f;
        GetTextSize(Text, TextWidth, TextHeight, GEngine->GetSmallFont(), BaseScale * Scale);
        const float Fit = TextWidth > 0.0f ? FMath::Min(1.0f, MaxWidth * Scale / TextWidth) : 1.0f;
        DrawText(Text, Color, CenterX - TextWidth * Fit * 0.5f, Y,
            GEngine->GetSmallFont(), BaseScale * Scale * Fit);
    };
    // Fixed frame labels must use UI scale, independent of the movable tree zoom.
    auto FrameText = [&](const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, float Size)
    {
        float TextWidth = 0.0f, TextHeight = 0.0f;
        GetTextSize(Text, TextWidth, TextHeight, GEngine->GetSmallFont(), Size * UIScale);
        const float Fit = TextWidth > 0 ? FMath::Min(1.0f, MaxWidth * UIScale / TextWidth) : 1.0f;
        DrawText(Text, Color, Origin.X + X * UIScale, Origin.Y + Y * UIScale,
            GEngine->GetSmallFont(), Size * UIScale * Fit);
    };

    const FVector2D Ship = SpaceFlight::UpgradeTreeShipCenter(W, H, TreePan);
    auto BranchLine = [&](FVector2D From, FVector2D To, FLinearColor Color)
    {
        DrawLine(From.X, From.Y, To.X, To.Y, BronzeDark, 5.0f * Scale);
        DrawLine(From.X, From.Y, To.X, To.Y, Color, 1.5f * Scale);
    };
    auto ScreenNode = [&](int32 Index) { return SpaceFlight::UpgradeScreenCenter(Index, W, H, TreeZoom, TreePan); };
    const FVector2D Split = Ship + FVector2D(0, 80 * Scale);
    BranchLine(Ship + FVector2D(0, 30 * Scale), Split, Gold);
    for (int32 Root = 0; Root < SpaceFlight::UpgradeCount; ++Root)
    {
        const auto& Definition = SpaceFlight::Upgrade(Root);
        if (Definition.Parent >= 0) continue;
        const FVector2D Node = ScreenNode(Root);
        if (Definition.Column == 0)
            BranchLine(Ship + FVector2D(0, -30 * Scale), Node + FVector2D(0, 46 * Scale), Gold);
        else if (Definition.Column == 1)
            BranchLine(Ship + FVector2D(-72 * Scale, 0), Node + FVector2D(94 * Scale, 0), Gold);
        else if (Definition.Column == 2)
            BranchLine(Ship + FVector2D(72 * Scale, 0), Node + FVector2D(-94 * Scale, 0), Gold);
        else
        {
            const FVector2D Turn(Node.X, Split.Y);
            BranchLine(Split, Turn, Gold);
            BranchLine(Turn, Node + FVector2D(0, -46 * Scale), Gold);
        }
    }

    Panel(Ship.X - 72 * Scale, Ship.Y - 30 * Scale, 144 * Scale, 60 * Scale, Gold);
    Panel(Ship.X - 70 * Scale, Ship.Y - 28 * Scale, 140 * Scale, 56 * Scale, FLinearColor(0.024f, 0.017f, 0.016f));
    Icon(INDEX_NONE, Ship.X - 68 * Scale, Ship.Y - 26 * Scale, 52 * Scale, FLinearColor::White, true);
    DrawText(SpaceFlight::Localize(TEXT("КОРАБЛЬ")), GoldLight, Ship.X - 8 * Scale, Ship.Y - 8 * Scale,
        GEngine->GetSmallFont(), 1.0f * Scale);

    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
    {
        const auto& Node = SpaceFlight::Upgrade(I);
        if (Node.Parent < 0) continue;
        const FVector2D P = ScreenNode(I);
        const FVector2D Parent = ScreenNode(Node.Parent);
        const FLinearColor Link = Progress.Has(Node.Parent) ? Gold : FLinearColor(0.18f, 0.16f, 0.14f);
        if (SpaceFlight::IsSideUpgrade(I))
        {
            const FLinearColor SideLink = Progress.Has(Node.Parent) ? FLinearColor(.46f, .60f, .28f) : FLinearColor(.16f, .21f, .13f);
            if (I == SpaceFlight::RicochetUpgrade)
            {
                BranchLine(Parent + FVector2D(94 * Scale, 0), P - FVector2D(94 * Scale, 0), SideLink);
                continue;
            }
            const int32 Position = (I - SpaceFlight::OriginalUpgradeCount) % 3;
            if (Position != 0)
            {
                const float Direction = P.Y < Parent.Y ? -1.0f : 1.0f;
                BranchLine(Parent + FVector2D(0, 46 * Scale * Direction), P - FVector2D(0, 46 * Scale * Direction), SideLink);
            }
            else if (Node.Column == 0)
                BranchLine(Parent - FVector2D(94 * Scale, 0), P + FVector2D(94 * Scale, 0), SideLink);
            else if (Node.Column == 1)
                BranchLine(Parent - FVector2D(0, 46 * Scale), P + FVector2D(0, 46 * Scale), SideLink);
            else
            {
                const bool bAbove = Node.Column == 2;
                const float Side = Node.Column == 3 ? -1.0f : 1.0f;
                const FVector2D Start = Parent + (bAbove ? FVector2D(0, -46 * Scale) : FVector2D(Side * 94 * Scale, 0));
                const FVector2D Exit = bAbove ? Start : Parent + FVector2D(Side * 106 * Scale, 0);
                const FVector2D Bend(Exit.X, Parent.Y + (bAbove ? -80.0f : -65.0f) * Scale);
                const FVector2D Turn(P.X, Bend.Y);
                const FVector2D End = P + FVector2D(0, (bAbove ? 46.0f : -46.0f) * Scale);
                if (!bAbove) BranchLine(Start, Exit, SideLink);
                BranchLine(Exit, Bend, SideLink);
                BranchLine(Bend, Turn, SideLink);
                BranchLine(Turn, End, SideLink);
            }
            continue;
        }
        if (Node.bMicro)
        {
            const FLinearColor MicroLink = Progress.Has(Node.Parent) ? FLinearColor(0.20f, 0.58f, 0.68f) : FLinearColor(0.12f, 0.20f, 0.23f);
            if (Node.Column == 0)
                BranchLine(Parent + FVector2D(94 * Scale, 0), P - FVector2D(76 * Scale, 0), MicroLink);
            else
            {
                const FVector2D Start = Parent + FVector2D(0, 46 * Scale);
                const FVector2D Bend1(Parent.X, Start.Y + 12 * Scale);
                const FVector2D Bend2(P.X, Bend1.Y);
                BranchLine(Start, Bend1, MicroLink);
                BranchLine(Bend1, Bend2, MicroLink);
                BranchLine(Bend2, P - FVector2D(0, 36 * Scale), MicroLink);
            }
            continue;
        }
        const FVector2D Direction = (P - Parent).GetSafeNormal();
        const float Edge = (FMath::Abs(Direction.X) > 0.5f ? 94.0f : 46.0f) * Scale;
        BranchLine(Parent + Direction * Edge, P - Direction * Edge, Link);
    }

    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I)
    {
        const auto& Node = SpaceFlight::Upgrade(I);
        const FVector2D P = ScreenNode(I);
        const bool bOwned = Progress.Has(I);
        const bool bUnlocked = Node.Parent < 0 || Progress.Has(Node.Parent);
        const bool bAffordable = Progress.CanPurchase(I);
        const bool bSide = SpaceFlight::IsSideUpgrade(I);
        const FLinearColor Border = Node.bMicro
            ? (bOwned ? FLinearColor(0.32f, 0.82f, 0.90f) : (bAffordable ? FLinearColor(0.16f, 0.58f, 0.72f) : FLinearColor(0.12f, 0.30f, 0.36f)))
            : (bSide ? (bOwned ? FLinearColor(.63f, .77f, .32f) : (bAffordable ? FLinearColor(.43f, .56f, .20f) : FLinearColor(.19f, .24f, .13f))) :
                (bOwned ? FLinearColor(0.71f, 0.52f, 0.18f)
                : (bAffordable ? FLinearColor(0.58f, 0.32f, 0.08f) : FLinearColor(0.17f, 0.13f, 0.10f))));
        const FLinearColor Text = bOwned ? FLinearColor(0.94f, 0.82f, 0.47f)
            : (bUnlocked ? FLinearColor(0.83f, 0.76f, 0.60f) : FLinearColor(0.53f, 0.49f, 0.44f));
        const FVector2D Half = SpaceFlight::UpgradeCardHalfSize(I);
        const float Left = P.X - Half.X * Scale;
        const float Top = P.Y - Half.Y * Scale;
        const float CardWidth = Half.X * 2;
        const float CardHeight = Half.Y * 2;
        Panel(Left, Top, CardWidth * Scale, CardHeight * Scale, Border);
        if (bTreeKeyboardFocusActive && I == TreeKeyboardFocus)
        {
            const FLinearColor Focus(0.62f, 0.95f, 0.90f, 1.0f);
            const float Edge = FMath::Max(1.0f, 2.0f * Scale);
            DrawLine(Left, Top, Left + CardWidth * Scale, Top, Focus, Edge);
            DrawLine(Left, Top + CardHeight * Scale, Left + CardWidth * Scale, Top + CardHeight * Scale, Focus, Edge);
            DrawLine(Left, Top, Left, Top + CardHeight * Scale, Focus, Edge);
            DrawLine(Left + CardWidth * Scale, Top, Left + CardWidth * Scale, Top + CardHeight * Scale, Focus, Edge);
        }
        Panel(Left + 2 * Scale, Top + 2 * Scale, (CardWidth - 4) * Scale, (CardHeight - 4) * Scale,
            Node.bMicro ? FLinearColor(0.012f, 0.025f, 0.032f) : FLinearColor(0.024f, 0.017f, 0.016f));
        Panel(Left + 2 * Scale, Top + 2 * Scale, (CardWidth - 4) * Scale, 1 * Scale,
            Node.bMicro ? Border : (bUnlocked ? GoldLight : BronzeDark));
        Panel(Left + 2 * Scale, Top + (CardHeight - 3) * Scale, (CardWidth - 4) * Scale, 1 * Scale, Node.bMicro ? Border : BronzeDark);
        // Small machined corner details reinforce the card without changing
        // the branch, micro-upgrade, locked, or focus colors.
        const FLinearColor Rivet = bUnlocked ? FLinearColor(Border.R * .75f, Border.G * .75f, Border.B * .75f) : BronzeDark;
        Panel(Left + 4 * Scale, Top + 5 * Scale, 2 * Scale, 5 * Scale, Rivet);
        Panel(Left + (CardWidth - 6) * Scale, Top + 5 * Scale, 2 * Scale, 5 * Scale, Rivet);
        Panel(Left + 4 * Scale, Top + (CardHeight - 10) * Scale, 2 * Scale, 5 * Scale, Rivet);
        Panel(Left + (CardWidth - 6) * Scale, Top + (CardHeight - 10) * Scale, 2 * Scale, 5 * Scale, Rivet);
        // Fifth and tenth steps of the main branches get a small engraved
        // crest, while the existing frame still communicates their state.
        if (!bSide && !Node.bMicro && (Node.Tier == 4 || Node.Tier == 9))
        {
            const FLinearColor Crest = bOwned ? GoldLight : (bUnlocked ? Gold : FLinearColor(.35f, .25f, .15f));
            const float X = P.X, Y = Top + 5 * Scale;
            const float Stroke = FMath::Max(1.0f, Scale);
            DrawLine(X - 5 * Scale, Y, X, Y + 4 * Scale, Crest, Stroke);
            DrawLine(X, Y + 4 * Scale, X + 5 * Scale, Y, Crest, Stroke);
            DrawLine(X - 14 * Scale, Y, X - 7 * Scale, Y, Crest, Stroke);
            DrawLine(X + 7 * Scale, Y, X + 14 * Scale, Y, Crest, Stroke);
        }
        if (Progress.TrackedUpgrade == I)
        {
            const FLinearColor TargetMark(.98f, .81f, .35f);
            Panel(Left + (CardWidth - 13) * Scale, Top + 5 * Scale, 7 * Scale, 7 * Scale, TargetMark);
        }
        // The frame and status text already distinguish locked upgrades. Keep
        // their artwork legible, especially the darker industrial silhouettes.
        const float IconBrightness = bUnlocked ? 1.0f : 0.80f;
        Icon(I, Left + 9 * Scale, Top + 9 * Scale, (Node.bMicro ? 34 : 48) * Scale,
            FLinearColor(IconBrightness, IconBrightness, IconBrightness, 1.0f));
        const float TextX = Node.bMicro ? 48.0f : 63.0f;
        const float TextWidth = CardWidth - TextX - (Progress.TrackedUpgrade == I ? 17.0f : 7.0f);
        FittedText(Node.Name, Text, Left + TextX * Scale, Top + (Node.bMicro ? 8 : 12) * Scale, TextWidth, Node.bMicro ? 0.82f : 0.95f);
        FittedText(Node.Effect, Text, Left + TextX * Scale, Top + (Node.bMicro ? 27 : 34) * Scale, TextWidth, Node.bMicro ? 0.72f : 0.82f);
        FString Status;
        const TCHAR* Currency = Node.bMicro ? SpaceFlight::Localize(TEXT("ИЗОТ.")) : SpaceFlight::Localize(TEXT("РУДЫ"));
        const int64 Balance = Node.bMicro ? Progress.Isotope : Progress.Resources;
        if (bOwned) Status = SpaceFlight::Localize(TEXT("ИЗУЧЕНО"));
        else if (!bUnlocked) Status = SF_LOCAL_PRINTF(TEXT("%d %s  •  НУЖНО: %s"), TEXT("%d %s • NEEDED: %s"), TEXT("%d %s • NECESARIO: %s"), Node.Cost, Currency, SpaceFlight::Upgrade(Node.Parent).Name);
        else if (!bAffordable) Status = SF_LOCAL_PRINTF(TEXT("%d %s  •  НЕ ХВАТАЕТ: %lld"), TEXT("%d %s • MISSING: %lld"), TEXT("%d %s • FALTA: %lld"), Node.Cost, Currency, Node.Cost - Balance);
        else Status = SF_LOCAL_PRINTF(TEXT("%d %s  •  МОЖНО ОТКРЫТЬ"), TEXT("%d %s • CAN BE OPENED"), TEXT("%d %s • SE PUEDE ABRIR"), Node.Cost, Currency);
        FittedText(Status, bOwned ? FLinearColor(0.40f, 0.85f, 0.58f) : Text,
            Left + 10 * Scale, Top + (Node.bMicro ? 52 : 68) * Scale, CardWidth - 20, Node.bMicro ? 0.72f : 0.82f);
    }
    // The fixed frame masks the movable tree; hit testing uses the same visible rectangle.
    const FLinearColor FrameBackground(0.006f, 0.008f, 0.016f, 1.0f);
    if (SpaceFlight::IsPortraitUpgradeLayout(W, H))
    {
        const FVector2D Min = SpaceFlight::UpgradeTreeCanvasMin(W, H);
        const FVector2D Max = SpaceFlight::UpgradeTreeCanvasMax(W, H);
        const float S = UIScale;
        // Cover exactly the same non-interactive region as the hit test.
        Panel(0, 0, W, Min.Y, FrameBackground);
        Panel(0, Max.Y, W, H - Max.Y, FrameBackground);
        Panel(0, Min.Y, Min.X, Max.Y - Min.Y, FrameBackground);
        Panel(Max.X, Min.Y, W - Max.X, Max.Y - Min.Y, FrameBackground);
        Panel(12 * S, 12 * S, W - 24 * S, 2 * S, Gold);
        Panel(12 * S, H - 14 * S, W - 24 * S, 2 * S, Gold);
        Panel(12 * S, 12 * S, 2 * S, H - 24 * S, Gold);
        Panel(W - 14 * S, 12 * S, 2 * S, H - 24 * S, Gold);
        auto PortraitText = [&](const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, float Size)
        {
            float Width = 0, Height = 0;
            GetTextSize(Text, Width, Height, GEngine->GetSmallFont(), Size * S);
            const float Fit = Width > 0 ? FMath::Min(1.0f, MaxWidth * S / Width) : 1.0f;
            DrawText(Text, Color, X * S, Y, GEngine->GetSmallFont(), Size * S * Fit);
        };
        int32 Purchased = 0;
        for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) if (Progress.Has(I)) ++Purchased;
        PortraitText(SpaceFlight::Localize(TEXT("ПУТИ РАЗВИТИЯ")), GoldLight, 26, 25 * S, 460, 1.35f);
        PortraitText(SF_LOCAL_PRINTF(TEXT("РУДА: %lld  •  ИЗОТОП: %lld  •  ИЗУЧЕНО: %d / %d"), TEXT("ORE: %lld  •  ISOTOPE: %lld  •  UNLOCKED: %d / %d"), TEXT("MINERAL: %lld  •  ISÓTOPO: %lld  •  MEJORAS: %d / %d"),
            Progress.Resources, Progress.Isotope, Purchased, SpaceFlight::UpgradeCount),
            FLinearColor(.87f, .77f, .55f), 26, 54 * S, 460, .90f);
        const FString Header = bSaveBlocked || bSaveWriteFailed ?
            (bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА")) : SpaceFlight::Localize(TEXT("ОШИБКА СОХРАНЕНИЯ"))) :
            (bStressMode ? SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")) :
                (GetWorld()->IsPaused() ?
                    (ResetConfirmUntil > GetWorld()->GetRealTimeSeconds() ? SpaceFlight::Localize(TEXT("ЕЩЁ РАЗ N — СТЕРЕТЬ ПРОГРЕСС")) : SpaceFlight::Localize(TEXT("ПАУЗА  ·  N — НАЧАТЬ НОВЫЙ ПОЛЁТ"))) :
                    SpaceFlight::Localize(TEXT("РАЗВИВАЙТЕ КОРАБЛЬ ОТ ЦЕНТРА"))));
        PortraitText(Header, bSaveBlocked || bSaveWriteFailed ? FLinearColor(1.0f, .4f, .28f) : GoldLight,
            26, 79 * S, 460, .85f);
        Panel(24 * S, Max.Y + 8 * S, W - 48 * S, 106 * S, FLinearColor(.022f, .020f, .021f));
        if (Hovered >= 0 && Hovered < SpaceFlight::UpgradeCount)
        {
            const auto& Node = SpaceFlight::Upgrade(Hovered);
            const FLinearColor NameColor = Node.bMicro ? FLinearColor(.45f, .85f, .91f) :
                (SpaceFlight::IsSideUpgrade(Hovered) ? FLinearColor(.72f, .88f, .46f) : GoldLight);
            const TCHAR* Currency = Node.bMicro ? SpaceFlight::Localize(TEXT("ИЗОТОПА")) : SpaceFlight::Localize(TEXT("РУДЫ"));
            const FString Status = UpgradeStatusText(Progress, Hovered);
            if (bCanDrawUpgradeArt) Icon(Hovered, 34 * S, Max.Y + 20 * S, 72 * S, FLinearColor::White);
            const float DetailX = bCanDrawUpgradeArt ? 120.0f : 34.0f;
            const float DetailWidth = bCanDrawUpgradeArt ? 354.0f : 440.0f;
            PortraitText(FString(Node.Name) + (Progress.TrackedUpgrade == Hovered ? SpaceFlight::Localize(TEXT("  ·  ЦЕЛЬ")) : TEXT("")),
                NameColor, DetailX, Max.Y + 16 * S, DetailWidth, .98f);
            PortraitText(SF_LOCAL_PRINTF(TEXT("ЭФФЕКТ: %s"), TEXT("EFFECT: %s"), TEXT("EFECTO: %s"), Node.Effect), FLinearColor(.74f, .70f, .61f), DetailX, Max.Y + 36 * S, DetailWidth, .84f);
            PortraitText(FString::Printf(TEXT("%d %s  •  %s"), Node.Cost, Currency, *Status), NameColor, DetailX, Max.Y + 56 * S, DetailWidth, .82f);
            const FString Comparison = SpaceFlight::DescribeUpgradeComparison(Progress, Hovered);
            FString Detail = Comparison.IsEmpty() ? FString() : FString(SpaceFlight::Localize(TEXT("ДО → ПОСЛЕ: "))) + Comparison;
            if (Progress.TrackedUpgrade == Hovered)
            {
                if (!Detail.IsEmpty()) Detail += TEXT("  ·  ");
                Detail += SpaceFlight::Localize(TEXT("РЕСУРСЫ НЕ РЕЗЕРВИРУЮТСЯ"));
            }
            if (!Detail.IsEmpty())
                PortraitText(Detail, FLinearColor(.59f, .85f, .75f), DetailX, Max.Y + 76 * S, DetailWidth, .78f);
        }
        else
        {
            const bool bShowNotice = !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil;
            PortraitText(bShowNotice ? UpgradeNotice : FString(SpaceFlight::Localize(TEXT("ГОЛУБАЯ РАМКА — МИКРОУЛУЧШЕНИЕ"))),
                bShowNotice ? FLinearColor(1.0f, .75f, .40f) : GoldLight, 34, Max.Y + 18 * S, 440, .88f);
            PortraitText(SpaceFlight::Localize(TEXT("ЗЕЛЁНАЯ РАМКА — БОКОВАЯ ВЕТВЬ")), FLinearColor(.72f, .80f, .56f), 34, Max.Y + 41 * S, 440, .84f);
            PortraitText(SpaceFlight::Localize(TEXT("ИЗОТОП: 1 СИНЯЯ + 1 ЧЁРНАЯ РУДА")), FLinearColor(.67f, .61f, .52f), 34, Max.Y + 64 * S, 440, .84f);
        }
        PortraitText(SF_LOCAL_PRINTF(TEXT("B — АВТОПОКУПКИ: %s   •   КОЛЕСО / МЫШЬ — ВИД"), TEXT("B — AUTO-BUY: %s  •  WHEEL / MOUSE — VIEW"), TEXT("B — AUTOCOMPRA: %s  •  RUEDA / RATÓN — VISTA"),
            Progress.bAutoPurchasesEnabled ? SpaceFlight::Localize(TEXT("ВКЛ")) : SpaceFlight::Localize(TEXT("ВЫКЛ"))), GoldLight, 26, Max.Y + 122 * S, 460, .82f);
        PortraitText(SF_LOCAL_PRINTF(TEXT("G — СЛЕДУЮЩЕЕ  •  T — %s  •  R — ЦЕНТР  •  J — ЖУРНАЛ  •  TAB — ЗАКРЫТЬ"), TEXT("G - NEXT • T - %s • R - CENTER • J - JOURNAL • TAB - CLOSE"), TEXT("G - SIGUIENTE • T - %s • R - CENTRO • J - DIARIO • TAB - CERRAR"),
            Hovered != INDEX_NONE && Progress.TrackedUpgrade == Hovered ? SpaceFlight::Localize(TEXT("СНЯТЬ ЦЕЛЬ")) : SpaceFlight::Localize(TEXT("СЛЕДИТЬ"))),
            GoldLight, 26, Max.Y + 145 * S, 460, .82f);
        return;
    }
    // The letterboxed area outside the 1280x720 frame must mask the tree as
    // well; otherwise panned branches leak into portrait or ultrawide margins.
    Panel(0, 0, W, Origin.Y, FrameBackground);
    Panel(0, Origin.Y + 720 * UIScale, W, FMath::Max(0.0f, H - Origin.Y - 720 * UIScale), FrameBackground);
    Panel(0, Origin.Y, Origin.X, 720 * UIScale, FrameBackground);
    Panel(Origin.X + 1280 * UIScale, Origin.Y, FMath::Max(0.0f, W - Origin.X - 1280 * UIScale), 720 * UIScale, FrameBackground);
    Box(0, 0, 1280, 100, FrameBackground);
    Box(0, 575, 1280, 145, FrameBackground);
    Box(0, 100, 30, 475, FrameBackground);
    Box(1250, 100, 30, 475, FrameBackground);
    Box(30, 18, 1220, 2, Gold);
    Box(30, 700, 1220, 2, Gold);
    Box(30, 18, 2, 684, Gold);
    Box(1248, 18, 2, 684, Gold);
    Box(54, 97, 1172, 1, BronzeDark);
    DrawText(SpaceFlight::Localize(TEXT("ПУТИ РАЗВИТИЯ")), GoldLight, Origin.X + 82 * UIScale, Origin.Y + 37 * UIScale,
        GEngine->GetSmallFont(), 1.7f * UIScale);
    int32 Purchased = 0;
    for (int32 I = 0; I < SpaceFlight::UpgradeCount; ++I) if (Progress.Has(I)) ++Purchased;
    FrameText(SF_LOCAL_PRINTF(TEXT("РУДА: %lld   •   ИЗОТОП: %lld   •   ИЗУЧЕНО: %d / %d"), TEXT("ORE: %lld  •  ISOTOPE: %lld  •  UNLOCKED: %d / %d"), TEXT("MINERAL: %lld  •  ISÓTOPO: %lld  •  MEJORAS: %d / %d"), Progress.Resources, Progress.Isotope, Purchased, SpaceFlight::UpgradeCount),
        FLinearColor(0.87f, 0.77f, 0.55f), 750, 41, 450, 0.95f);
    DrawText(SpaceFlight::Localize(TEXT("Развивайте корабль от центра к вершинам ветвей")),
        FLinearColor(0.65f, 0.58f, 0.46f), Origin.X + 82 * UIScale, Origin.Y + 71 * UIScale,
        GEngine->GetSmallFont(), 0.95f * UIScale);
    if (bSaveBlocked || bSaveWriteFailed)
        FrameText(bSaveSessionLocked ? SpaceFlight::Localize(TEXT("ИГРА УЖЕ ЗАПУЩЕНА")) : (bSaveBlocked ? SpaceFlight::Localize(TEXT("ОШИБКА ЧТЕНИЯ СОХРАНЕНИЯ")) : SpaceFlight::Localize(TEXT("ОШИБКА СОХРАНЕНИЯ"))),
            FLinearColor(1.0f, 0.40f, 0.28f), 750, 71, 380, 0.82f);
    else if (bStressMode)
        DrawText(SpaceFlight::Localize(TEXT("ТЕСТ: ПРОГРЕСС НЕ СОХРАНЯЕТСЯ")), FLinearColor(0.94f, 0.66f, 0.27f),
            Origin.X + 750 * UIScale, Origin.Y + 71 * UIScale, GEngine->GetSmallFont(), 0.82f * UIScale);
    if (GetWorld()->IsPaused())
        DrawText(SpaceFlight::Localize(TEXT("ПАУЗА")), GoldLight, Origin.X + 1150 * UIScale, Origin.Y + 72 * UIScale,
            GEngine->GetSmallFont(), 0.9f * UIScale);

    Box(106, 575, 1068, 72, FLinearColor(0.022f, 0.020f, 0.021f));
    Box(106, 575, 1068, 1, BronzeDark);
    if (Hovered != INDEX_NONE)
    {
        const auto& Node = SpaceFlight::Upgrade(Hovered);
        const TCHAR* Currency = Node.bMicro ? SpaceFlight::Localize(TEXT("ИЗОТОПА")) : SpaceFlight::Localize(TEXT("РУДЫ"));
        const FLinearColor NameColor = Node.bMicro ? FLinearColor(0.45f, 0.85f, 0.91f) :
            (SpaceFlight::IsSideUpgrade(Hovered) ? FLinearColor(.72f, .88f, .46f) : GoldLight);
        if (bCanDrawUpgradeArt) Icon(Hovered, Origin.X + 116 * UIScale, Origin.Y + 583 * UIScale, 56 * UIScale, FLinearColor::White);
        const float DetailX = bCanDrawUpgradeArt ? 184.0f : 124.0f;
        const float DetailWidth = bCanDrawUpgradeArt ? 510.0f : 550.0f;
        FrameText(FString(Node.Name) + (Progress.TrackedUpgrade == Hovered ? SpaceFlight::Localize(TEXT("  ·  ЦЕЛЬ")) : TEXT("")),
            NameColor, DetailX, 585, DetailWidth, 1.05f);
        FrameText(SF_LOCAL_PRINTF(TEXT("ЭФФЕКТ: %s"), TEXT("EFFECT: %s"), TEXT("EFECTO: %s"), Node.Effect), FLinearColor(0.74f, 0.70f, 0.61f),
            DetailX, 609, DetailWidth, 0.9f);
        const FString Status = UpgradeStatusText(Progress, Hovered);
        FrameText(SF_LOCAL_PRINTF(TEXT("СТОИМОСТЬ: %d %s"), TEXT("PRICE: %d %s"), TEXT("PRECIO: %d %s"), Node.Cost, Currency), NameColor,
            700, 585, 450, 0.9f);
        FrameText(Status, FLinearColor(0.67f, 0.61f, 0.52f), 700, 609, 450, 0.85f);
        const FString Comparison = SpaceFlight::DescribeUpgradeComparison(Progress, Hovered);
        FString Line = Comparison.IsEmpty() ? FString() : FString(SpaceFlight::Localize(TEXT("ДО → ПОСЛЕ: "))) + Comparison;
        if (Progress.TrackedUpgrade == Hovered)
        {
            if (!Line.IsEmpty()) Line += TEXT("  ·  ");
            Line += SpaceFlight::Localize(TEXT("РЕСУРСЫ НЕ РЕЗЕРВИРУЮТСЯ"));
        }
        if (!Line.IsEmpty())
        {
            float TextWidth = 0, TextHeight = 0;
            const float BaseScale = 0.82f * UIScale;
            GetTextSize(Line, TextWidth, TextHeight, GEngine->GetSmallFont(), BaseScale);
            const float LineWidth = bCanDrawUpgradeArt ? 965.0f : 1025.0f;
            const float Fit = TextWidth > 0 ? FMath::Min(1.0f, LineWidth * UIScale / TextWidth) : 1.0f;
            DrawText(Line, FLinearColor(0.59f, 0.85f, 0.75f), Origin.X + DetailX * UIScale,
                Origin.Y + 628 * UIScale, GEngine->GetSmallFont(), BaseScale * Fit);
        }
    }
    else
    {
        const bool bShowNotice = !UpgradeNotice.IsEmpty() && GetWorld()->GetRealTimeSeconds() < NoticeUntil;
        FrameText(bShowNotice ? UpgradeNotice : FString(SpaceFlight::Localize(TEXT("ГОЛУБАЯ РАМКА — МИКРОУЛУЧШЕНИЕ  •  ЗЕЛЁНАЯ — БОКОВАЯ ВЕТВЬ"))),
            bShowNotice ? FLinearColor(1.0f, 0.75f, 0.40f) : GoldLight, 124, 585, 1025, 0.9f);
        DrawText(SpaceFlight::Localize(TEXT("ИЗОТОП: 1 СИНЯЯ + 1 ЧЁРНАЯ РУДА. Наведите на улучшение для подробностей.")),
            FLinearColor(0.67f, 0.61f, 0.52f), Origin.X + 124 * UIScale, Origin.Y + 609 * UIScale,
            GEngine->GetSmallFont(), 0.9f * UIScale);
    }
    CenteredText(SpaceFlight::Localize(TEXT("Колесо — масштаб   •   ЛКМ по фону / СКМ / ПКМ — перемещение   •   R — центр")),
        FLinearColor(0.70f, 0.59f, 0.39f), Origin.X + 640 * UIScale, Origin.Y + 648 * UIScale, 1000 / TreeZoom, 0.9f / TreeZoom);
    const FString Footer = GetWorld()->IsPaused() && !bStressMode && !bSaveBlocked && ResetConfirmUntil > GetWorld()->GetRealTimeSeconds()
        ? FString(SpaceFlight::Localize(TEXT("ЕЩЁ РАЗ N — СТЕРЕТЬ ПРОГРЕСС")))
        : SF_LOCAL_PRINTF(TEXT("G — следующее   •   T — %s   •   B — автопокупки: %s   •   J — журнал   •   %sTAB — закрыть"), TEXT("G — next • T — %s • B — auto-buy: %s • J — journal • %sTAB — close"), TEXT("G — siguiente • T — %s • B — autocompra: %s • J — diario • %sTAB — cerrar"),
            Hovered != INDEX_NONE && Progress.TrackedUpgrade == Hovered ? SpaceFlight::Localize(TEXT("снять цель")) : SpaceFlight::Localize(TEXT("следить")),
            Progress.bAutoPurchasesEnabled ? SpaceFlight::Localize(TEXT("ВКЛ")) : SpaceFlight::Localize(TEXT("ВЫКЛ")),
            GetWorld()->IsPaused() && !bStressMode && !bSaveBlocked ? SpaceFlight::Localize(TEXT("N — новый полёт   •   ")) : TEXT(""));
    CenteredText(Footer,
        FLinearColor(0.70f, 0.59f, 0.39f), Origin.X + 640 * UIScale, Origin.Y + 675 * UIScale, 1000 / TreeZoom, 0.95f / TreeZoom);
}
