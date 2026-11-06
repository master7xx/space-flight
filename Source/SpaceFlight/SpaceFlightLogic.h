#pragma once

#include "SpaceFlightLocalization.h"

#include "CoreMinimal.h"
#include "SpaceFlightSurprise.h"
#include "SpaceFlightExpedition.h"

namespace SpaceFlight
{
    constexpr float LaserRange = 220.0f;
    constexpr int32 UpgradeCount = 81;
    constexpr int32 RicochetUpgrade = 80;
    constexpr float RicochetRange = 120.0f;
    constexpr int32 OriginalUpgradeCount = 65;
    inline bool IsSideUpgrade(int32 Index) { return Index >= OriginalUpgradeCount && Index < UpgradeCount; }
    constexpr int32 NormalAsteroidCount = 15;
    constexpr int32 ProductionOreCost = 3;
    constexpr float ProductionCooldownSeconds = 3.0f;
    constexpr float SpecializationCooldownSeconds = 60.0f;
    enum class EOreKind : uint8 { Common, Blue, Black };
    static_assert(static_cast<int32>(EOreKind::Common) == 0 && static_cast<int32>(EOreKind::Blue) == 1 &&
        static_cast<int32>(EOreKind::Black) == 2, "Expedition reward ore codes must match EOreKind");
    enum class EDiscovery : uint32
    {
        CommonOre = 1u << 0, BlueOre = 1u << 1, BlackOre = 1u << 2, Isotope = 1u << 3,
        MineralFlow = 1u << 4, UnstableCrystal = 1u << 5, LargeDeposit = 1u << 6
    };
    constexpr uint32 KnownDiscoveryBits = (1u << 7) - 1u;

    struct FStarBrightness
    {
        int32 Value = 20;
        void Adjust(int32 Direction) { Value = FMath::Clamp(Value + FMath::Clamp(Direction, -1, 1) * 10, 0, 200); }
        FLinearColor Apply(const FLinearColor& Background, const FLinearColor& Star) const
        {
            const float Factor = Value / 100.0f;
            return FLinearColor(
                FMath::Clamp(Background.R + (Star.R - Background.R) * Factor, 0.0f, 1.0f),
                FMath::Clamp(Background.G + (Star.G - Background.G) * Factor, 0.0f, 1.0f),
                FMath::Clamp(Background.B + (Star.B - Background.B) * Factor, 0.0f, 1.0f), 1.0f);
        }
    };

    // The three silhouettes have one, two and three layers of durability.
    inline int32 AsteroidHitPoints(int32 Size) { return Size <= 9 ? 1 : (Size <= 18 ? 2 : 3); }
    inline int32 AsteroidSizeForIndex(int32 Index)
    {
        static constexpr int32 Sizes[] = { 9, 18, 27, 9, 18, 27, 18 };
        return Index < 0 ? 0 : Sizes[Index % UE_ARRAY_COUNT(Sizes)];
    }

    struct FAsteroidDurability
    {
        int32 Pass = INDEX_NONE;
        int32 Generation = INDEX_NONE;
        int32 Remaining = 0;
        int32 RemainingTenths = 0;
        void Prepare(int32 NewPass, int32 NewGeneration, int32 Size)
        {
            if (Pass == NewPass && Generation == NewGeneration) return;
            Pass = NewPass;
            Generation = NewGeneration;
            Remaining = AsteroidHitPoints(Size);
            RemainingTenths = Remaining * 10;
        }
        bool IsUndamaged(int32 Size) const { return RemainingTenths == AsteroidHitPoints(Size) * 10; }
        bool HitTenths(int32 DamageTenths)
        {
            if (RemainingTenths <= 0) return false;
            RemainingTenths = FMath::Max(0, RemainingTenths - FMath::Max(0, DamageTenths));
            Remaining = (RemainingTenths + 9) / 10;
            return RemainingTenths == 0;
        }
        bool Hit(int32 Damage = 1)
        {
            return HitTenths(FMath::Clamp(Damage, 0, MAX_int32 / 10) * 10);
        }
    };

    struct FUpgradeDefinition
    {
        const TCHAR* Name;
        const TCHAR* Effect;
        int32 Cost;
        int32 Parent;
        int32 Column;
        int32 Tier;
        float RangeBonus;
        int32 LaserBonus;
        float CooldownReduction;
        int32 YieldBonus;
        float SpeedBonus;
        int32 DamageBonus = 0;
        bool bMicro = false;
        int32 RareYieldBonus = 0;
        float RichOreChance = 0.0f;
        int32 LargeOreBonus = 0;
        int32 FirstHitOreBonus = 0;
        float StabilityBonus = 0.0f;
        int32 RicochetBonus = 0;
    };

    inline const FUpgradeDefinition& Upgrade(int32 Index)
    {
        static const FUpgradeDefinition Definitions[UpgradeCount] = {
            { TEXT("РАДАР I"), TEXT("радиус +15"), 12, -1, 0, 0, 15, 0, 0, 0, 0 },
            { TEXT("РАДАР II"), TEXT("радиус +20"), 90, 0, 0, 1, 20, 0, 0, 0, 0 },
            { TEXT("РАДАР III"), TEXT("радиус +25"), 650, 1, 0, 2, 25, 0, 0, 0, 0 },
            { TEXT("СБОРЩИК I"), TEXT("+1 руда за астероид"), 16, -1, 1, 0, 0, 0, 0, 1, 0 },
            { TEXT("СБОРЩИК II"), TEXT("+1 руда за астероид"), 120, 3, 1, 1, 0, 0, 0, 1, 0 },
            { TEXT("ЛЕВЫЙ ЛАЗЕР"), TEXT("второй лазер"), 100, -1, 2, 0, 0, 1, 0, 0, 0 },
            { TEXT("ПРАВЫЙ ЛАЗЕР"), TEXT("третий лазер"), 750, 5, 2, 1, 0, 1, 0, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ I"), TEXT("перезарядка −0,04 с"), 20, -1, 3, 0, 0, 0, .04f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ II"), TEXT("перезарядка −0,04 с"), 160, 7, 3, 1, 0, 0, .04f, 0, 0 },
            { TEXT("ДВИГАТЕЛЬ I"), TEXT("скорость +8%"), 50, -1, 4, 0, 0, 0, 0, 0, .08f },
            { TEXT("РАДАР IV"), TEXT("радиус +25"), 4000, 2, 0, 3, 25, 0, 0, 0, 0 },
            { TEXT("РАДАР V"), TEXT("радиус +25"), 18000, 10, 0, 4, 25, 0, 0, 0, 0 },
            { TEXT("СБОРЩИК III"), TEXT("+1 руда за астероид"), 900, 4, 1, 2, 0, 0, 0, 1, 0 },
            { TEXT("ПОИСК БОГАТЫХ ЖИЛ"), TEXT("шанс +5%: ×2 базовой руды"), 5500, 12, 1, 3, 0, 0, 0, 0, 0, 0, false, 0, .05f },
            { TEXT("ПРОБИВНОЙ ЛУЧ"), TEXT("урон лазера +1"), 5000, 6, 2, 2, 0, 0, 0, 0, 0, 1 },
            { TEXT("ЛИНЗА НАВЕДЕНИЯ"), TEXT("радиус +15"), 20000, 14, 2, 3, 15, 0, 0, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ III"), TEXT("перезарядка −0,035 с"), 1200, 8, 3, 2, 0, 0, .035f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ IV"), TEXT("перезарядка −0,035 с"), 7500, 16, 3, 3, 0, 0, .035f, 0, 0 },
            { TEXT("ДВИГАТЕЛЬ II"), TEXT("скорость +8%"), 300, 9, 4, 1, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ III"), TEXT("скорость +8%"), 1800, 18, 4, 2, 0, 0, 0, 0, .08f },
            { TEXT("ТОНКИЙ СЕНСОР"), TEXT("радиус +5"), 35, 0, 0, 0, 5, 0, 0, 0, 0, 0, true },
            { TEXT("МИКРОСБОР"), TEXT("+1 руда за астероид"), 35, 3, 1, 0, 0, 0, 0, 1, 0, 0, true },
            { TEXT("РЕЗОНАТОР"), TEXT("урон лазера +1"), 250, 5, 2, 0, 0, 0, 0, 0, 0, 1, true },
            { TEXT("КРИОКОНТУР"), TEXT("перезарядка −0,01 с"), 35, 7, 3, 0, 0, 0, .01f, 0, 0, 0, true },
            { TEXT("МИКРОТЯГА"), TEXT("скорость +4%"), 40, 9, 4, 0, 0, 0, 0, 0, .04f, 0, true },
            { TEXT("РАДАР VI"), TEXT("радиус +25"), 30000, 11, 0, 5, 25, 0, 0, 0, 0 },
            { TEXT("РАДАР VII"), TEXT("радиус +20"), 100000, 25, 0, 6, 20, 0, 0, 0, 0 },
            { TEXT("РАДАР VIII"), TEXT("радиус +20"), 200000, 26, 0, 7, 20, 0, 0, 0, 0 },
            { TEXT("РАДАР IX"), TEXT("радиус +20"), 400000, 27, 0, 8, 20, 0, 0, 0, 0 },
            { TEXT("РАДАР X"), TEXT("радиус +20"), 700000, 28, 0, 9, 20, 0, 0, 0, 0 },
            { TEXT("СПЕКТРАЛЬНЫЙ СБОР"), TEXT("+1 синяя или чёрная руда"), 25000, 13, 1, 4, 0, 0, 0, 0, 0, 0, false, 1 },
            { TEXT("СБОРЩИК VI"), TEXT("+1 руда за астероид"), 40000, 30, 1, 5, 0, 0, 0, 1, 0 },
            { TEXT("ГЛУБОКАЯ РАЗВЕДКА"), TEXT("шанс +5%: ×2 базовой руды"), 130000, 31, 1, 6, 0, 0, 0, 0, 0, 0, false, 0, .05f },
            { TEXT("ГЛУБОКИЙ АНАЛИЗ"), TEXT("+1 синяя или чёрная руда"), 250000, 32, 1, 7, 0, 0, 0, 0, 0, 0, false, 1 },
            { TEXT("ПОИСК ЖИЛ II"), TEXT("шанс +5%: ×2 базовой руды"), 500000, 33, 1, 8, 0, 0, 0, 0, 0, 0, false, 0, .05f },
            { TEXT("СБОРЩИК X"), TEXT("+1 руда за астероид"), 900000, 34, 1, 9, 0, 0, 0, 1, 0 },
            { TEXT("ИМПУЛЬСНЫЙ КОНТУР"), TEXT("перезарядка −0,02 с"), 60000, 15, 2, 4, 0, 0, .02f, 0, 0 },
            { TEXT("ЛАЗЕРНАЯ СОРТИРОВКА"), TEXT("шанс +5%: ×2 базовой руды"), 80000, 36, 2, 5, 0, 0, 0, 0, 0, 0, false, 0, .05f },
            { TEXT("ФОКУСИРОВКА"), TEXT("радиус +15"), 200000, 37, 2, 6, 15, 0, 0, 0, 0 },
            { TEXT("ИМПУЛЬСНЫЙ КОНТУР II"), TEXT("перезарядка −0,02 с"), 400000, 38, 2, 7, 0, 0, .02f, 0, 0 },
            { TEXT("СБОР ОСКОЛКОВ"), TEXT("+1 руда за астероид"), 800000, 39, 2, 8, 0, 0, 0, 1, 0 },
            { TEXT("ЛИНЗА ГОРИЗОНТА"), TEXT("радиус +20"), 1500000, 40, 2, 9, 20, 0, 0, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ V"), TEXT("перезарядка −0,03 с"), 32000, 17, 3, 4, 0, 0, .03f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ VI"), TEXT("перезарядка −0,025 с"), 50000, 42, 3, 5, 0, 0, .025f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ VII"), TEXT("перезарядка −0,025 с"), 150000, 43, 3, 6, 0, 0, .025f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ VIII"), TEXT("перезарядка −0,02 с"), 300000, 44, 3, 7, 0, 0, .02f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ IX"), TEXT("перезарядка −0,02 с"), 600000, 45, 3, 8, 0, 0, .02f, 0, 0 },
            { TEXT("ОХЛАЖДЕНИЕ X"), TEXT("перезарядка −0,02 с"), 1100000, 46, 3, 9, 0, 0, .02f, 0, 0 },
            { TEXT("ДВИГАТЕЛЬ IV"), TEXT("скорость +8%"), 9000, 19, 4, 3, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ V"), TEXT("скорость +8%"), 35000, 48, 4, 4, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ VI"), TEXT("скорость +8%"), 55000, 49, 4, 5, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ VII"), TEXT("скорость +8%"), 160000, 50, 4, 6, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ VIII"), TEXT("скорость +8%"), 350000, 51, 4, 7, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ IX"), TEXT("скорость +8%"), 700000, 52, 4, 8, 0, 0, 0, 0, .08f },
            { TEXT("ДВИГАТЕЛЬ X"), TEXT("скорость +8%"), 1300000, 53, 4, 9, 0, 0, 0, 0, .08f },
            { TEXT("СЕНСОРНЫЙ КРИСТАЛЛ"), TEXT("радиус +10"), 5000, 10, 0, 3, 10, 0, 0, 0, 0, 0, true },
            { TEXT("КВАНТОВЫЙ СЕНСОР"), TEXT("радиус +10"), 100000, 27, 0, 7, 10, 0, 0, 0, 0, 0, true },
            { TEXT("МИНЕРАЛЬНЫЙ ФИЛЬТР"), TEXT("+1 синяя или чёрная руда"), 6000, 13, 1, 3, 0, 0, 0, 0, 0, 0, true, 1 },
            { TEXT("МИКРОАНАЛИЗ ЖИЛ"), TEXT("шанс +5%: ×2 базовой руды"), 120000, 33, 1, 7, 0, 0, 0, 0, 0, 0, true, 0, .05f },
            { TEXT("ЛИНЗА РЕЗОНАНСА"), TEXT("радиус +10"), 8000, 15, 2, 3, 10, 0, 0, 0, 0, 0, true },
            { TEXT("КВАНТОВЫЙ ИМПУЛЬС"), TEXT("перезарядка −0,01 с"), 150000, 39, 2, 7, 0, 0, .01f, 0, 0, 0, true },
            { TEXT("КРИОКОНТУР II"), TEXT("перезарядка −0,01 с"), 6000, 17, 3, 3, 0, 0, .01f, 0, 0, 0, true },
            { TEXT("КРИОКОНТУР III"), TEXT("перезарядка −0,01 с"), 130000, 45, 3, 7, 0, 0, .01f, 0, 0, 0, true },
            { TEXT("МИКРОТЯГА II"), TEXT("скорость +3%"), 6500, 48, 4, 3, 0, 0, 0, 0, .03f, 0, true },
            { TEXT("МИКРОТЯГА III"), TEXT("скорость +3%"), 140000, 52, 4, 7, 0, 0, 0, 0, .03f, 0, true },

            { TEXT("СПЕКТРАЛЬНАЯ АНТЕННА"), TEXT("радиус +8; шанс жил +2 п.п."), 400, 1, 0, 2, 8, 0, 0, 0, 0, 0, false, 0, .02f },
            { TEXT("КАРТА РУДЫ"), TEXT("редкая руда +1; шанс жил +2 п.п."), 6500, 65, 0, 3, 0, 0, 0, 0, 0, 0, false, 1, .02f },
            { TEXT("РЕЗЕРВНЫЙ ДИАПАЗОН"), TEXT("радиус +12; редкая руда +1"), 85000, 66, 0, 4, 12, 0, 0, 0, 0, 0, false, 1 },
            { TEXT("ДРОБИЛКА ПОРОД"), TEXT("крупные астероиды: руда +2"), 450, 4, 1, 2, 0, 0, 0, 0, 0, 0, false, 0, 0, 2 },
            { TEXT("КОНТУР ДРОБЛЕНИЯ"), TEXT("крупные +1; шанс жил +2 п.п."), 7500, 68, 1, 3, 0, 0, 0, 0, 0, 0, false, 0, .02f, 1 },
            { TEXT("ГЛУБОКАЯ ПЕРЕРАБОТКА"), TEXT("крупные астероиды: руда +2"), 100000, 69, 1, 4, 0, 0, 0, 0, 0, 0, false, 0, 0, 2 },
            { TEXT("ЧИСТЫЙ СРЕЗ"), TEXT("уничтожение первым выстрелом: руда +1"), 400, 5, 2, 1, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 1 },
            { TEXT("ТОЧНАЯ ОПТИКА"), TEXT("разрушение с 1-го: +1 руда; радиус +8"), 6800, 71, 2, 2, 8, 0, 0, 0, 0, 0, false, 0, 0, 0, 1 },
            { TEXT("ЛАЗЕРНЫЙ АНАЛИЗ"), TEXT("уничтожение первым выстрелом: руда +2"), 90000, 72, 2, 3, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 2 },
            { TEXT("ТЕРМОРЕГУЛЯТОР"), TEXT("перезарядка −0,015 с; редкая руда +1"), 500, 8, 3, 2, 0, 0, .015f, 0, 0, 0, false, 1 },
            { TEXT("КОНТУР ВОССТАНОВЛЕНИЯ"), TEXT("перезарядка −0,015 с; крупные +1"), 9000, 74, 3, 3, 0, 0, .015f, 0, 0, 0, false, 0, 0, 1 },
            { TEXT("ТЕПЛОВОЙ КАСКАД"), TEXT("перезарядка −0,02 с; редкая руда +1"), 110000, 75, 3, 4, 0, 0, .02f, 0, 0, 0, false, 1 },
            { TEXT("СТАБИЛИЗАТОР I"), TEXT("крен −20%; радиус +5"), 550, 18, 4, 2, 5, 0, 0, 0, 0, 0, false, 0, 0, 0, 0, .20f },
            { TEXT("СТАБИЛИЗАТОР II"), TEXT("крен −20%; разрушение с 1-го: +1 руда"), 10000, 77, 4, 3, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 1, .20f },
            { TEXT("СТАБИЛИЗАТОР III"), TEXT("крен −20%; радиус +8"), 120000, 78, 4, 4, 8, 0, 0, 0, 0, 0, false, 0, 0, 0, 0, .20f },
            { TEXT("ОТРАЖАЮЩАЯ ЛИНЗА"), TEXT("Рикошет до 120: 40% урона"), 700, 71, 2, 2, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0, 0, 1 }
        };
        struct FLocalizedDefinitions
        {
            FUpgradeDefinition English[UpgradeCount];
            FUpgradeDefinition Spanish[UpgradeCount];
            explicit FLocalizedDefinitions(const FUpgradeDefinition* Source)
            {
                for (int32 I = 0; I < UpgradeCount; ++I)
                {
                    English[I] = Source[I];
                    Spanish[I] = Source[I];
                    English[I].Name = LocalizeForLanguage(Source[I].Name, ELanguage::English);
                    English[I].Effect = LocalizeForLanguage(Source[I].Effect, ELanguage::English);
                    Spanish[I].Name = LocalizeForLanguage(Source[I].Name, ELanguage::Spanish);
                    Spanish[I].Effect = LocalizeForLanguage(Source[I].Effect, ELanguage::Spanish);
                }
            }
        };
        static const FLocalizedDefinitions Localized(Definitions);
        const int32 SafeIndex = FMath::Clamp(Index, 0, UpgradeCount - 1);
        return GetLanguage() == ELanguage::English ? Localized.English[SafeIndex] :
            GetLanguage() == ELanguage::Spanish ? Localized.Spanish[SafeIndex] : Definitions[SafeIndex];
    }

    inline bool IsPortraitUpgradeLayout(float Width, float Height)
    {
        return Width > 0.0f && Height > 0.0f && Width < Height * 1.1f;
    }

    inline float UpgradeTreeScale(float Width, float Height)
    {
        // Portrait has a dedicated frame and uses the available vertical space.
        // A 512px-wide window keeps cards at the desktop reading scale.
        if (IsPortraitUpgradeLayout(Width, Height)) return Width / 512.0f;
        return FMath::Min(Width / 1280.0f, Height / 720.0f);
    }

    inline FVector2D UpgradeTreeOrigin(float Width, float Height)
    {
        if (IsPortraitUpgradeLayout(Width, Height)) return FVector2D::ZeroVector;
        const float Scale = UpgradeTreeScale(Width, Height);
        return FVector2D((Width - 1280.0f * Scale) * 0.5f, (Height - 720.0f * Scale) * 0.5f);
    }

    inline FVector2D UpgradeTreeCanvasMin(float Width, float Height)
    {
        const float S = UpgradeTreeScale(Width, Height);
        if (IsPortraitUpgradeLayout(Width, Height)) return FVector2D(12 * S, 112 * S);
        return UpgradeTreeOrigin(Width, Height) + FVector2D(30 * S, 100 * S);
    }

    inline FVector2D UpgradeTreeCanvasMax(float Width, float Height)
    {
        const float S = UpgradeTreeScale(Width, Height);
        if (IsPortraitUpgradeLayout(Width, Height)) return FVector2D(Width - 12 * S, Height - 180 * S);
        return UpgradeTreeOrigin(Width, Height) + FVector2D(1250 * S, 575 * S);
    }

    // Open on legible nearby branches; the overview remains available by zooming out.
    constexpr float DefaultTreeZoom = 0.85f;
    constexpr float MinTreeZoom = 0.36f;
    constexpr float MaxTreeZoom = 1.4f;

    // Coordinates are relative to the ship. Extending a branch requires only another tier.
    inline FVector2D UpgradeTreePosition(int32 Index)
    {
        const auto& Node = Upgrade(Index);
        if (Index == RicochetUpgrade) return FVector2D(655.0f, -160.0f);
        if (IsSideUpgrade(Index))
        {
            const int32 Branch = (Index - OriginalUpgradeCount) / 3;
            const float Step = static_cast<float>((Index - OriginalUpgradeCount) % 3);
            switch (Branch)
            {
            case 0: return FVector2D(-225.0f, -255.0f - 110.0f * Step);
            case 1: return FVector2D(-445.0f, -160.0f - 110.0f * Step);
            case 2: return FVector2D(445.0f, -160.0f - 110.0f * Step);
            case 3: return FVector2D(-550.0f, 255.0f + 110.0f * Step);
            default: return FVector2D(550.0f, 255.0f + 110.0f * Step);
            }
        }
        if (Node.bMicro)
        {
            const FVector2D Parent = UpgradeTreePosition(Node.Parent);
            if (Node.Column == 0) return Parent + FVector2D(200, 0);
            if (Node.Column == 1) return Parent + FVector2D(-115, 120);
            if (Node.Column == 2) return Parent + FVector2D(115, 120);
            return Parent + FVector2D(Node.Column == 3 ? -240 : 240, 120);
        }
        switch (Node.Column)
        {
            case 0: return FVector2D(0, -145.0f - 110.0f * Node.Tier);
            case 1: return FVector2D(-235.0f - 210.0f * Node.Tier, 0);
            case 2: return FVector2D(235.0f + 210.0f * Node.Tier, 0);
            case 3: return FVector2D(-110.0f, 145.0f + 110.0f * Node.Tier);
            default: return FVector2D(110.0f, 145.0f + 110.0f * Node.Tier);
        }
    }

    inline FVector2D UpgradeCardHalfSize(int32 Index)
    {
        return Upgrade(Index).bMicro ? FVector2D(76, 36) : FVector2D(94, 46);
    }

    inline FVector2D UpgradeTreeShipCenter(float Width, float Height, FVector2D Pan = FVector2D::ZeroVector)
    {
        return FVector2D(Width, Height) * 0.5f + Pan * UpgradeTreeScale(Width, Height);
    }

    inline FVector2D UpgradeScreenCenter(int32 Index, float Width, float Height,
        float Zoom = DefaultTreeZoom, FVector2D Pan = FVector2D::ZeroVector)
    {
        return UpgradeTreeShipCenter(Width, Height, Pan) + UpgradeTreePosition(Index) * UpgradeTreeScale(Width, Height) * Zoom;
    }

    inline FVector2D PanForTreeZoom(FVector2D Point, float Width, float Height, FVector2D Pan, float OldZoom, float NewZoom)
    {
        const float BaseScale = UpgradeTreeScale(Width, Height);
        if (BaseScale <= 0 || OldZoom <= 0) return Pan;
        return Pan + (Point - UpgradeTreeShipCenter(Width, Height, Pan)) * ((1.0f - NewZoom / OldZoom) / BaseScale);
    }

    inline int32 UpgradeAtScreenPosition(FVector2D Point, float Width, float Height,
        float Zoom = DefaultTreeZoom, FVector2D Pan = FVector2D::ZeroVector)
    {
        if (Width <= 0.0f || Height <= 0.0f || Zoom <= 0.0f) return INDEX_NONE;
        const float BaseScale = UpgradeTreeScale(Width, Height);
        const FVector2D Min = UpgradeTreeCanvasMin(Width, Height);
        const FVector2D Max = UpgradeTreeCanvasMax(Width, Height);
        if (Point.X < Min.X || Point.X > Max.X || Point.Y < Min.Y || Point.Y > Max.Y) return INDEX_NONE;
        const float Scale = BaseScale * Zoom;
        for (int32 I = 0; I < UpgradeCount; ++I)
        {
            const FVector2D NodeCenter = UpgradeScreenCenter(I, Width, Height, Zoom, Pan);
            const FVector2D Half = UpgradeCardHalfSize(I) * Scale;
            if (FMath::Abs(Point.X - NodeCenter.X) <= Half.X &&
                FMath::Abs(Point.Y - NodeCenter.Y) <= Half.Y) return I;
        }
        return INDEX_NONE;
    }

    struct FFlightObjective
    {
        // 0: asteroids, 1: blue ore, 2: black ore, 3: all ore, 4: rich deposits, 5: laser hits.
        int32 Sequence = 0;
        int32 Kind = 0;
        int32 Progress = 0;

        int32 Target() const
        {
            const int32 Tier = FMath::Min(Sequence / 4, 10);
            switch (Kind)
            {
            case 0: return 10 + 3 * Tier;
            case 1: case 2: return 20 + 10 * Tier;
            case 3: return 45 + 20 * Tier;
            case 4: return 3 + Tier;
            case 5: return 12 + 2 * Tier;
            default: return 0;
            }
        }
        int32 Reward() const { return 5 + 2 * FMath::Min(Sequence / 4, 10); }
        bool IsValid() const
        {
            return Sequence >= 0 && Kind >= 0 && Kind <= 5 && Progress >= 0 && Progress < Target();
        }
        int32 ApplyDelta(int32 Delta, bool bRichUnlocked)
        {
            if (Delta < Target() - Progress) { Progress += Delta; return 0; }
            const int32 Earned = Reward();
            Sequence = Sequence < MAX_int32 ? Sequence + 1 : 0;
            Kind = Sequence % 4;
            if (Sequence % 8 == 5) Kind = 5;
            else if (Kind == 3 && Sequence % 8 == 7 && bRichUnlocked) Kind = 4;
            Progress = 0;
            return Earned;
        }
        int32 RecordHit(bool bRichUnlocked)
        {
            return IsValid() && Kind == 5 ? ApplyDelta(1, bRichUnlocked) : 0;
        }
        int32 Record(EOreKind Ore, int32 Amount, bool bRich, bool bRichUnlocked)
        {
            if (!IsValid() || Amount < 0 || (Amount == 0 && Kind != 0 && Kind != 4)) return 0;
            int32 Delta = 0;
            switch (Kind)
            {
            case 0: Delta = 1; break;
            case 1: Delta = Ore == EOreKind::Blue ? Amount : 0; break;
            case 2: Delta = Ore == EOreKind::Black ? Amount : 0; break;
            case 3: Delta = Amount; break;
            case 4: Delta = bRich ? 1 : 0; break;
            }
            return ApplyDelta(Delta, bRichUnlocked);
        }
    };

    // Quotas store hundredths of ore in int64, so this bound allows every
    // valid resource balance to be multiplied by 100 without overflow.
    constexpr int64 ResourceAccountingLimit = MAX_int64 / 100;

    struct FProgress
    {
        FExpedition Expedition;
        float SectorReadyAt = 0.0f;
        FSurpriseProgress Surprise;
        bool bAutoPurchasesEnabled = true;
        // An optional player-selected objective; it never affects purchases.
        int32 TrackedUpgrade = INDEX_NONE;
        int64 Resources = 0;
        int64 TotalCollected = 0;
        int64 TotalSpent = 0;
        int64 BlueOre = 0;
        int64 BlackOre = 0;
        int64 Isotope = 0;
        int64 OreConverted = 0;
        int64 IsotopeSpent = 0;
        uint32 DiscoveryBits = 0;
        TBitArray<> Acquired = TBitArray<>(false, UpgradeCount);
        FFlightObjective Objective;
        int32 CurrentSector = 0;
        int32 WorldSeed = 0;
        bool bHasWorldSeed = false;
        // 0 = original automatic reserve, 1 = off, 2..5 = 25..100%.
        int32 RefiningMode = 0;
        // Separate hundredths of eligible blue/black ore, carried between pickups.
        int64 BlueRefiningQuota = 0;
        int64 BlackRefiningQuota = 0;
        float ProductionReadyAt = 0.0f;
        // 0 = neutral, 1 = exploration, 2 = precision, 3 = logistics.
        int32 Specialization = 0;
        float SpecializationReadyAt = 0.0f;
        // 0 = nearest, 1 = blue, 2 = black, 3 = active event targets.
        int32 TargetingMode = 0;

        bool HasDiscovery(EDiscovery Discovery) const { return (DiscoveryBits & static_cast<uint32>(Discovery)) != 0; }
        bool RecordDiscovery(EDiscovery Discovery)
        {
            const uint32 Bit = static_cast<uint32>(Discovery) & KnownDiscoveryBits;
            const bool bNew = Bit != 0 && (DiscoveryBits & Bit) == 0;
            DiscoveryBits |= Bit;
            return bNew;
        }

        bool SetSpecialization(int32 NewMode, float FlightTime)
        {
            if (NewMode < 0 || NewMode > 3 || NewMode == Specialization || !FMath::IsFinite(FlightTime) ||
                FlightTime < 0 || FlightTime < SpecializationReadyAt ||
                !FMath::IsFinite(FlightTime + SpecializationCooldownSeconds) ||
                FlightTime + SpecializationCooldownSeconds <= FlightTime) return false;
            const int32 PreviousTarget = Objective.Target();
            const int32 PreviousProgress = Objective.Progress;
            Specialization = NewMode;
            // A scout can temporarily unlock rich-deposit goals. If changing
            // profiles removes the only source of rich deposits, keep the
            // objective achievable and retain its proportional completion.
            if (Objective.Kind == 4 && RichOreChance() <= 0.0f && PreviousTarget > 0)
            {
                Objective.Kind = 3;
                Objective.Progress = static_cast<int32>(static_cast<int64>(PreviousProgress) * Objective.Target() / PreviousTarget);
            }
            SpecializationReadyAt = FlightTime + SpecializationCooldownSeconds;
            return true;
        }
        int32 ObjectiveReward(int32 Base) const { return Base > 0 ? Base + (Specialization == 3 ? 1 : 0) : 0; }
        int32 ProductionCost() const { return Specialization == 3 ? 2 : ProductionOreCost; }

        int64 CommonOre() const { return Resources - BlueOre - BlackOre; }
        bool ProduceRareOre(EOreKind Kind, float FlightTime)
        {
            if ((Kind != EOreKind::Blue && Kind != EOreKind::Black) || !FMath::IsFinite(FlightTime) ||
                FlightTime < 0.0f || FlightTime < ProductionReadyAt || CommonOre() < ProductionCost() ||
                TotalCollected >= ResourceAccountingLimit || !FMath::IsFinite(FlightTime + ProductionCooldownSeconds) ||
                FlightTime + ProductionCooldownSeconds <= FlightTime) return false;
            Resources -= ProductionCost();
            TotalSpent += ProductionCost();
            AddOre(1, Kind);
            ProductionReadyAt = FlightTime + ProductionCooldownSeconds;
            return true;
        }

        int32 RefiningPercent() const { return RefiningMode >= 2 ? (RefiningMode - 1) * 25 : 0; }
        bool SetRefiningMode(int32 NewMode)
        {
            if (NewMode < 0 || NewMode > 5 || NewMode == RefiningMode) return false;
            RefiningMode = NewMode;
            const int32 Percent = RefiningPercent();
            BlueRefiningQuota = static_cast<int64>(BlueOre) * Percent;
            BlackRefiningQuota = static_cast<int64>(BlackOre) * Percent;
            RefineIsotope();
            return true;
        }

        int64 AddResources(int64 Amount)
        {
            if (Amount <= 0 || TotalCollected < 0 || TotalCollected >= ResourceAccountingLimit) return 0;
            // Keep hundredth-unit refinery quotas representable.
            const int64 Accepted = FMath::Min(Amount, ResourceAccountingLimit - TotalCollected);
            Resources += Accepted;
            TotalCollected += Accepted;
            return Accepted;
        }
        void RefineIsotope()
        {
            int64 Needed = 0;
            for (int32 I = 0; I < UpgradeCount; ++I)
            {
                const FUpgradeDefinition& Node = Upgrade(I);
                if (!Node.bMicro || Has(I)) continue;
                // Reserve for available micro upgrades and the five first
                // branches, not for tiers that are still hours away.
                if (Node.Parent < 0 || Has(Node.Parent) || Upgrade(Node.Parent).Parent < 0)
                    Needed += Node.Cost;
            }
            int64 Pairs = FMath::Min(FMath::Min(BlueOre, BlackOre), FMath::Max<int64>(0, Needed - Isotope));
            if (RefiningMode != 0)
            {
                Pairs = FMath::Min<int64>(Pairs, FMath::Min(BlueRefiningQuota, BlackRefiningQuota) / 100);
                BlueRefiningQuota -= Pairs * 100;
                BlackRefiningQuota -= Pairs * 100;
            }
            BlueOre -= Pairs;
            BlackOre -= Pairs;
            if (Pairs > 0) RecordDiscovery(EDiscovery::Isotope);
            Resources -= 2 * Pairs;
            OreConverted += 2 * Pairs;
            Isotope += Pairs;
        }
        int64 AddOre(int64 Amount, EOreKind Kind)
        {
            if (Amount <= 0) return 0;
            Amount = AddResources(Amount);
            if (Amount <= 0) return 0;
            RecordDiscovery(Kind == EOreKind::Blue ? EDiscovery::BlueOre :
                (Kind == EOreKind::Black ? EDiscovery::BlackOre : EDiscovery::CommonOre));
            if (Kind == EOreKind::Blue)
            {
                BlueOre += Amount;
                if (RefiningMode >= 2) BlueRefiningQuota += Amount * RefiningPercent();
            }
            else if (Kind == EOreKind::Black)
            {
                BlackOre += Amount;
                if (RefiningMode >= 2) BlackRefiningQuota += Amount * RefiningPercent();
            }
            RefineIsotope();
            return Amount;
        }
        int32 ClaimExpeditionReward()
        {
            if (!Expedition.IsValid() || Expedition.State != EExpeditionState::Ready) return 0;
            const int32 OreCode = FExpedition::RewardOreCode(Expedition.Route);
            const int32 Reward = FExpedition::RewardAmount(Expedition.Route, Expedition.Target);
            if (OreCode == INDEX_NONE || Reward <= 0 || TotalCollected < 0 ||
                TotalCollected > ResourceAccountingLimit - Reward) return 0;
            const FProgress Before = *this;
            const int64 Accepted = OreCode == 0 ? AddResources(Reward) : AddOre(Reward, static_cast<EOreKind>(OreCode));
            if (Accepted != Reward || Expedition.Claim() != Reward)
            {
                *this = Before;
                return 0;
            }
            return Reward;
        }
        bool Has(int32 Index) const { return Index >= 0 && Index < Acquired.Num() && Acquired[Index]; }
        bool CanPurchase(int32 Index) const
        {
            if (Index < 0 || Index >= UpgradeCount || Has(Index)) return false;
            const FUpgradeDefinition& Node = Upgrade(Index);
            return (Node.Parent < 0 || Has(Node.Parent)) && (Node.bMicro ? Isotope : Resources) >= Node.Cost;
        }
        bool Purchase(int32 Index)
        {
            if (!CanPurchase(Index)) return false;
            const auto& Node = Upgrade(Index);
            if (Node.bMicro)
            {
                Isotope -= Node.Cost;
                IsotopeSpent += Node.Cost;
            }
            else
            {
                const int64 Untagged = Resources - BlueOre - BlackOre;
                int64 TaggedCost = FMath::Max<int64>(0, Node.Cost - Untagged);
                // Spend the surplus color first, then evenly from both. This
                // preserves as many 1:1 isotope pairs as the cost permits.
                const int64 BlueSurplusCost = FMath::Min(TaggedCost, FMath::Max<int64>(0, BlueOre - BlackOre));
                BlueOre -= BlueSurplusCost;
                TaggedCost -= BlueSurplusCost;
                const int64 BlackSurplusCost = FMath::Min(TaggedCost, FMath::Max<int64>(0, BlackOre - BlueOre));
                BlackOre -= BlackSurplusCost;
                TaggedCost -= BlackSurplusCost;
                BlueOre -= (TaggedCost + 1) / 2;
                BlackOre -= TaggedCost / 2;
                BlueRefiningQuota = FMath::Min(BlueRefiningQuota, static_cast<int64>(BlueOre) * 100);
                BlackRefiningQuota = FMath::Min(BlackRefiningQuota, static_cast<int64>(BlackOre) * 100);
                Resources -= Node.Cost;
                TotalSpent += Node.Cost;
            }
            Acquired[Index] = true;
            if (TrackedUpgrade == Index) TrackedUpgrade = INDEX_NONE;
            RefineIsotope();
            return true;
        }
        float Range() const { float Value = LaserRange; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).RangeBonus; return Value; }
        int32 Lasers() const { int32 Value = 1; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).LaserBonus; return Value; }
        int32 LaserDamage() const { int32 Value = 1; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).DamageBonus; return Value; }
        int32 RicochetCount() const { int32 Value = 0; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).RicochetBonus; return FMath::Min(Value, 1); }
        int32 FirstHitOreBonus() const
        {
            int32 Value = Specialization == 2 ? 1 : 0;
            for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).FirstHitOreBonus;
            return Value;
        }
        float Cooldown() const { float Value = 0.7f; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value -= Upgrade(I).CooldownReduction; return FMath::Max(Value, 0.05f); }
        float RichOreChance() const
        {
            float Value = Specialization == 1 ? 0.05f : 0.0f;
            for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).RichOreChance;
            return FMath::Clamp(Value, 0.0f, 1.0f);
        }
        int32 Yield(int32 Size, EOreKind Kind = EOreKind::Common, bool bRich = false, bool bFirstHit = false) const
        {
            int32 Value = AsteroidHitPoints(Size) * 2 - 1;
            if (bRich) Value *= 2;
            for (int32 I = 0; I < UpgradeCount; ++I)
                if (Has(I)) Value += Upgrade(I).YieldBonus + (Kind == EOreKind::Common ? 0 : Upgrade(I).RareYieldBonus) +
                    (AsteroidHitPoints(Size) == 3 ? Upgrade(I).LargeOreBonus : 0) + (bFirstHit ? Upgrade(I).FirstHitOreBonus : 0);
            if (Specialization == 2 && bFirstHit) ++Value;
            return Value;
        }
        int32 ConditionalOreBonus(int32 Size, bool bFirstHit) const
        {
            int32 Value = 0;
            for (int32 I = 0; I < UpgradeCount; ++I)
                if (Has(I)) Value += (AsteroidHitPoints(Size) == 3 ? Upgrade(I).LargeOreBonus : 0) +
                    (bFirstHit ? Upgrade(I).FirstHitOreBonus : 0);
            return Value;
        }
        float SpeedMultiplier() const { float Value = 1.0f; for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).SpeedBonus; return Value; }
        float BankStability() const
        {
            float Value = 0.0f;
            for (int32 I = 0; I < UpgradeCount; ++I) if (Has(I)) Value += Upgrade(I).StabilityBonus;
            return FMath::Clamp(Value, 0.0f, .6f);
        }
    };

    // Compare the single hovered node with the actual current ship. Locked
    // ancestors are not silently included and no currency or save is changed.
    inline FString DescribeUpgradeComparison(const FProgress& Before, int32 Index)
    {
        if (Index < 0 || Index >= UpgradeCount || Before.Has(Index)) return FString();
        const FUpgradeDefinition& Node = Upgrade(Index);
        FProgress After = Before;
        After.Acquired[Index] = true;
        TArray<FString> Parts;
        if (Node.RangeBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("РАДИУС %.0f→%.0f"), TEXT("RADIUS %.0f→%.0f"), TEXT("RADIO %.0f→%.0f"), Before.Range(), After.Range()));
        if (Node.LaserBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("ЛАЗЕРЫ %d→%d"), TEXT("LASERS %d→%d"), TEXT("LÁSERES %d→%d"), Before.Lasers(), After.Lasers()));
        if (Node.DamageBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("УРОН %d→%d"), TEXT("DAMAGE %d→%d"), TEXT("DAÑO %d→%d"), Before.LaserDamage(), After.LaserDamage()));
        if (Node.RicochetBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("РИКОШЕТЫ %d→%d"), TEXT("RICOCHETS %d→%d"), TEXT("REBOTES %d→%d"), Before.RicochetCount(), After.RicochetCount()));
        if (Node.CooldownReduction > 0) Parts.Add(SF_LOCAL_PRINTF(TEXT("ПЕРЕЗАРЯДКА %.3f→%.3f с"), TEXT("COOLDOWN %.3f→%.3f s"), TEXT("RECARGA %.3f→%.3f s"), Before.Cooldown(), After.Cooldown()));
        if (Node.SpeedBonus > 0) Parts.Add(SF_LOCAL_PRINTF(TEXT("СКОРОСТЬ %.0f%%→%.0f%%"), TEXT("SPEED %.0f%%→%.0f%%"), TEXT("VELOCIDAD %.0f%%→%.0f%%"), Before.SpeedMultiplier() * 100, After.SpeedMultiplier() * 100));
        if (Node.StabilityBonus > 0) Parts.Add(SF_LOCAL_PRINTF(TEXT("КРЕН %.0f%%→%.0f%%"), TEXT("ROLL %.0f%%→%.0f%%"), TEXT("INCLINACIÓN %.0f%%→%.0f%%"), (1 - Before.BankStability()) * 100, (1 - After.BankStability()) * 100));
        if (Node.RichOreChance > 0) Parts.Add(SF_LOCAL_PRINTF(TEXT("ШАНС ЖИЛ %.0f%%→%.0f%%"), TEXT("RICH CHANCE %.0f%%→%.0f%%"), TEXT("PROB. VETA %.0f%%→%.0f%%"), Before.RichOreChance() * 100, After.RichOreChance() * 100));
        if (Node.YieldBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("РУДА %d→%d"), TEXT("ORE %d→%d"), TEXT("MINERAL %d→%d"), Before.Yield(9), After.Yield(9)));
        if (Node.RareYieldBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("РЕДКАЯ РУДА %d→%d"), TEXT("RARE ORE %d→%d"), TEXT("MINERAL RARO %d→%d"), Before.Yield(9, EOreKind::Blue), After.Yield(9, EOreKind::Blue)));
        if (Node.LargeOreBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("КРУПН. РУДА %d→%d"), TEXT("LARGE ORE %d→%d"), TEXT("MINERAL GRANDE %d→%d"), Before.Yield(27), After.Yield(27)));
        if (Node.FirstHitOreBonus) Parts.Add(SF_LOCAL_PRINTF(TEXT("1-Й ВЫСТРЕЛ %d→%d"), TEXT("1ST SHOT %d→%d"), TEXT("1ER DISPARO %d→%d"), Before.Yield(9, EOreKind::Common, false, true), After.Yield(9, EOreKind::Common, false, true)));
        return FString::Join(Parts, TEXT("   •   "));
    }

    struct FJournalBranchStats { int32 Owned = 0; int32 Total = 0; };
    inline FJournalBranchStats JournalBranchStats(const FProgress& Progress, int32 Column)
    {
        FJournalBranchStats Result;
        if (Column < 0 || Column >= 5) return Result;
        for (int32 I = 0; I < UpgradeCount; ++I)
            if (Upgrade(I).Column == Column) { ++Result.Total; if (Progress.Has(I)) ++Result.Owned; }
        return Result;
    }

    inline bool IsRichOre(uint32 SessionSeed, int32 Index, int32 Pass, int32 Generation, float Chance)
    {
        if (Chance <= 0.0f) return false;
        if (Chance >= 1.0f) return true;
        uint32 Seed = HashCombineFast(SessionSeed, 0x52ad0187u);
        Seed = HashCombineFast(Seed, GetTypeHash(Index));
        Seed = HashCombineFast(Seed, GetTypeHash(Pass));
        Seed = HashCombineFast(Seed, GetTypeHash(Generation));
        return FRandomStream(static_cast<int32>(Seed)).FRand() < Chance;
    }

    struct FShipAppearance
    {
        int32 RadarLevel = 0;
        int32 CollectorLevel = 0;
        int32 CoolingLevel = 0;
        bool bLeftLaser = false;
        bool bRightLaser = false;
        bool bEngineUpgrade = false;
    };

    inline FShipAppearance DescribeShipAppearance(const FProgress& Progress)
    {
        FShipAppearance Appearance;
        for (int32 I = 0; I < UpgradeCount; ++I)
        {
            if (!Progress.Has(I)) continue;
            const FUpgradeDefinition& Node = Upgrade(I);
            if (Node.RangeBonus > 0) ++Appearance.RadarLevel;
            if (Node.YieldBonus > 0 || Node.RareYieldBonus > 0 || Node.RichOreChance > 0) ++Appearance.CollectorLevel;
            if (Node.CooldownReduction > 0) ++Appearance.CoolingLevel;
            if (Node.SpeedBonus > 0) Appearance.bEngineUpgrade = true;
        }
        Appearance.bLeftLaser = Progress.Lasers() >= 2;
        Appearance.bRightLaser = Progress.Lasers() >= 3;
        return Appearance;
    }

    inline float ManeuverBankTarget(float Displacement, float DeltaSeconds, float Width, float SpeedMultiplier)
    {
        if (DeltaSeconds <= 0.0f || Width <= 0.0f || SpeedMultiplier <= 0.0f) return 0.0f;
        return FMath::Clamp(Displacement / (DeltaSeconds * Width * 0.12f * SpeedMultiplier), -1.0f, 1.0f);
    }
    inline float ShipManeuverBank(float Displacement, float DeltaSeconds, float Width, const FProgress& Progress)
    {
        return ManeuverBankTarget(Displacement, DeltaSeconds, Width, Progress.SpeedMultiplier()) * (1.0f - Progress.BankStability());
    }

    inline FVector2D ShipPoint(float LocalX, float LocalY, float CenterX, float Top, float Pixel, float Bank)
    {
        const float Angle = FMath::Clamp(Bank, -1.0f, 1.0f) * 0.15f;
        const float C = FMath::Cos(Angle);
        const float S = FMath::Sin(Angle);
        const float X = LocalX - 28.0f;
        const float Y = LocalY - 29.0f;
        return FVector2D(CenterX + (X * C - Y * S) * Pixel,
            Top + 29.0f * Pixel + (X * S + Y * C) * Pixel);
    }

    inline FString FormatFlightTime(float ElapsedSeconds)
    {
        const int32 Seconds = FMath::Max(0, FMath::FloorToInt(ElapsedSeconds));
        const int32 Hours = Seconds / 3600;
        const int32 Minutes = (Seconds / 60) % 60;
        return FString::Printf(TEXT("%02d:%02d:%02d"), Hours, Minutes, Seconds % 60);
    }

    struct FAsteroidSpawn
    {
        float X;
        bool bLeft;
    };

    struct FLaserCandidate
    {
        int32 Index;
        int32 Pass;
        int32 Size;
        FVector2D Position;
        FLinearColor Color;
        float DistanceSquared = 0.0f;
        EOreKind OreKind = EOreKind::Common;
        bool bEventMarked = false;
        bool bDamaged = false;
        bool bAvoidRicochet = false;
    };

    inline bool RicochetWouldSpoilFirstHitBonus(int32 Size, int32 DirectDamage, bool bDamaged, bool bHasBonus)
    {
        const int32 Health = AsteroidHitPoints(Size) * 10;
        return bHasBonus && !bDamaged && Health <= DirectDamage * 10 && Health > DirectDamage * 4;
    }

    inline int32 TargetPriority(int32 Mode, EOreKind OreKind, bool bEventMarked)
    {
        if (Mode == 1) return OreKind == EOreKind::Blue ? 0 : 1;
        if (Mode == 2) return OreKind == EOreKind::Black ? 0 : 1;
        if (Mode == 3) return bEventMarked ? 0 : 1;
        return 0;
    }

    inline bool SegmentIntersectsShip(const FVector2D& From, const FVector2D& To, const FVector2D& Center, float Radius)
    {
        const FVector2D Delta = To - From;
        const double LengthSquared = Delta.SizeSquared();
        const double Along = LengthSquared > SMALL_NUMBER ?
            FMath::Clamp(FVector2D::DotProduct(Center - From, Delta) / LengthSquared, 0.0, 1.0) : 0.0;
        return FVector2D::DistSquared(From + Delta * Along, Center) <= FMath::Square(FMath::Max(0.0f, Radius));
    }

    // Returns an asteroid ID, not its array index. The primary volley and
    // earlier reflections are all reserved before a new target is chosen.
    inline int32 SelectRicochetTarget(TConstArrayView<FLaserCandidate> Rocks, TConstArrayView<int32> Reserved,
        const FVector2D& From, const FVector2D& LaserOrigin, const FVector2D& ShipCenter,
        float ShipRadius, float RadarRange, float Height, int32 TargetingMode, float BounceRange = RicochetRange)
    {
        int32 Best = INDEX_NONE, BestPriority = MAX_int32;
        bool bBestDamaged = false;
        double BestDistance = FMath::Square(BounceRange);
        for (const FLaserCandidate& Rock : Rocks)
        {
            if (Rock.Index < 0 || Rock.bAvoidRicochet || Rock.Position.Y < 0 || Rock.Position.Y > Height ||
                FVector2D::DistSquared(LaserOrigin, Rock.Position) > FMath::Square(RadarRange)) continue;
            bool bReserved = false;
            for (int32 Index : Reserved) if (Index == Rock.Index) { bReserved = true; break; }
            if (bReserved) continue;
            const double Distance = FVector2D::DistSquared(From, Rock.Position);
            if (Distance > FMath::Square(BounceRange) || SegmentIntersectsShip(From, Rock.Position, ShipCenter, ShipRadius)) continue;
            const int32 Priority = TargetPriority(TargetingMode, Rock.OreKind, Rock.bEventMarked);
            if (Priority < BestPriority || (Priority == BestPriority &&
                ((Rock.bDamaged && !bBestDamaged) || (Rock.bDamaged == bBestDamaged && Distance < BestDistance))))
            { Best = Rock.Index; BestPriority = Priority; bBestDamaged = Rock.bDamaged; BestDistance = Distance; }
        }
        return Best;
    }

    inline void AddLaserCandidate(TArray<FLaserCandidate, TInlineAllocator<3>>& Candidates,
        FLaserCandidate Candidate, const FVector2D& Origin, float Height, float Range, int32 Weapons, int32 TargetingMode = 0)
    {
        Candidate.DistanceSquared = FVector2D::DistSquared(Origin, Candidate.Position);
        if (Weapons <= 0 || Candidate.Position.Y < 0.0f || Candidate.Position.Y > Height ||
            Candidate.DistanceSquared > FMath::Square(Range)) return;
        for (const FLaserCandidate& Existing : Candidates)
            if (Existing.Index == Candidate.Index) return;
        int32 InsertAt = 0;
        const int32 Priority = TargetPriority(TargetingMode, Candidate.OreKind, Candidate.bEventMarked);
        while (InsertAt < Candidates.Num())
        {
            const auto& Existing = Candidates[InsertAt];
            const int32 ExistingPriority = TargetPriority(TargetingMode, Existing.OreKind, Existing.bEventMarked);
            if (ExistingPriority > Priority || (ExistingPriority == Priority && Existing.DistanceSquared > Candidate.DistanceSquared)) break;
            ++InsertAt;
        }
        if (InsertAt >= Weapons) return;
        Candidates.Insert(Candidate, InsertAt);
        if (Candidates.Num() > Weapons) Candidates.Pop(EAllowShrinking::No);
    }

    // The seed changes for each session, slot, pass and replacement. A given
    // asteroid keeps its lane throughout its pass, independent of frame rate.
    inline FAsteroidSpawn ChooseAsteroidSpawn(uint32 SessionSeed, int32 Index, int32 Pass,
        int32 Generation, float Width, float Pixel, float RadiusOnScreen)
    {
        uint32 Seed = HashCombineFast(SessionSeed, GetTypeHash(Index));
        Seed = HashCombineFast(Seed, GetTypeHash(Pass));
        Seed = HashCombineFast(Seed, GetTypeHash(Generation));
        FRandomStream Random(static_cast<int32>(Seed));
        const bool bLeft = Random.FRand() < 0.5f;
        const float Clearance = 28.0f * Pixel + RadiusOnScreen + 3.0f * Pixel;
        const float Edge = RadiusOnScreen + 3.0f * Pixel;
        const float LaneMin = bLeft ? Edge : Width * 0.5f + Clearance;
        const float LaneMax = bLeft ? Width * 0.5f - Clearance : Width - Edge;
        return { FMath::Lerp(LaneMin, LaneMax, Random.FRandRange(0.1f, 0.9f)), bLeft };
    }

    inline bool ConsiderLaserTarget(const FVector2D& Origin, const FVector2D& Target,
        float Height, float& NearestDistanceSquared)
    {
        const float DistanceSquared = FVector2D::DistSquared(Origin, Target);
        if (Target.Y < 0.0f || Target.Y > Height || DistanceSquared > NearestDistanceSquared)
            return false;
        NearestDistanceSquared = DistanceSquared;
        return true;
    }

    inline float AvoidShip(float X, float Y, float ShipX, float ShipY,
        float Width, float Pixel, float Radius)
    {
        const float Clearance = 28.0f * Pixel + Radius + 5.0f * Pixel;
        const float Dy = FMath::Abs(Y - ShipY);
        const float Inner = 29.0f * Pixel + Radius + 5.0f * Pixel;
        if (Dy >= Inner + 80.0f * Pixel || FMath::Abs(X - ShipX) >= Clearance) return X;
        float Side = X < ShipX ? -1.0f : 1.0f;
        if (ShipX + Side * Clearance < Radius || ShipX + Side * Clearance > Width - Radius) Side = -Side;
        const float SafeX = FMath::Clamp(ShipX + Side * Clearance, Radius, Width - Radius);
        const float Blend = FMath::Clamp((Inner + 80.0f * Pixel - Dy) / (80.0f * Pixel), 0.0f, 1.0f);
        return FMath::Lerp(X, SafeX, Blend);
    }

    struct FCrystalGlow
    {
        float Strength;
        FLinearColor Color;
    };

    inline float CrystalGlowMask(float X, float Y)
    {
        const float Falloff = FMath::Max(0.0f, 1.0f - X * X - Y * Y);
        return Falloff * Falloff;
    }

    inline FCrystalGlow CrystalGlow(float Time, uint32 Seed, int32 Index, int32 Generation)
    {
        uint32 Key = HashCombineFast(Seed, GetTypeHash(Index));
        Key = HashCombineFast(Key, GetTypeHash(Generation));
        FRandomStream Random(static_cast<int32>(Key));
        const float Period = Random.FRandRange(3.8f, 5.0f);
        const float Phase = Random.FRandRange(0.0f, 2.0f * PI);
        const float Wave = 0.5f + 0.5f * FMath::Sin(Time * 2.0f * PI / Period + Phase);
        const FLinearColor Colors[] = {
            FLinearColor(0.15f, 0.68f, 0.90f),
            FLinearColor(0.13f, 0.66f, 0.72f),
            FLinearColor(0.25f, 0.51f, 0.88f)
        };
        return { 0.32f + 0.38f * Wave, Colors[Random.RandRange(0, 2)] };
    }

    // Samples are an estimate of the current asteroid field, not a fixed ranking
    // of upgrade types. Their size is bounded to keep the stress mode responsive.
    constexpr int32 MaxForecastRocks = 64;
    struct FForecastRock
    {
        float BaseX;
        float Y;
        float Speed;
        float Radius;
        int32 Size;
        bool bWrap;
        int32 HitsRemaining = 0;
        EOreKind OreKind = EOreKind::Common;
        bool bEventMarked = false;
        int32 HitsRemainingTenths = 0;
    };
    struct FForecastContext
    {
        float Width;
        float Height;
        float Pixel;
        float ShipX;
        float ShipY;
        float LaserY;
        float Time;
        float TimeSinceShot;
    };
    struct FAutoUpgradeChoice
    {
        int32 Next = INDEX_NONE;
        int32 Goal = INDEX_NONE;
        float Score = 0.0f;
    };

    struct FForecastWeights { float Blue = 1.0f; float Black = 1.0f; };

    inline FForecastWeights AutoForecastWeights(const FProgress& Progress)
    {
        FForecastWeights Weights;
        if (Progress.RefiningMode == 1) return Weights;
        int32 Needed = 0;
        for (int32 I = 0; I < UpgradeCount; ++I)
        {
            const auto& Node = Upgrade(I);
            if (Node.bMicro && !Progress.Has(I) && (Node.Parent < 0 || Progress.Has(Node.Parent))) Needed += Node.Cost;
        }
        if (Progress.Isotope >= Needed) return Weights;
        const float Eligible = Progress.RefiningMode == 0 ? 1.0f : Progress.RefiningPercent() / 100.0f;
        const float Bonus = 0.20f * Eligible;
        if (Progress.BlueOre <= Progress.BlackOre) Weights.Blue += Bonus;
        if (Progress.BlackOre <= Progress.BlueOre) Weights.Black += Bonus;
        return Weights;
    }

    inline float ForecastOre(const FProgress& Progress, const FForecastContext& Context, TConstArrayView<FForecastRock> Rocks,
        FForecastWeights Weights = {}, float ForecastHorizon = 12.0f)
    {
        if (Context.Width <= 0 || Context.Height <= 0 || Rocks.IsEmpty()) return 0.0f;
        constexpr float Step = 0.25f;
        const float Horizon = FMath::Clamp(ForecastHorizon, Step, 30.0f);
        TArray<int32, TInlineAllocator<MaxForecastRocks>> Remaining;
        Remaining.Reserve(Rocks.Num());
        for (const FForecastRock& Rock : Rocks)
            Remaining.Add(Rock.HitsRemainingTenths > 0 ? Rock.HitsRemainingTenths :
                (Rock.HitsRemaining > 0 ? Rock.HitsRemaining : AsteroidHitPoints(Rock.Size)) * 10);
        float ShipX = Context.ShipX;
        const float Cooldown = Progress.Cooldown();
        const int32 Weapons = Progress.Lasers();
        const int32 Damage = Progress.LaserDamage();
        const bool bHasFirstHitBonus = Progress.Has(RicochetUpgrade) && Progress.FirstHitOreBonus() > 0;
        const float RichChance = Progress.RichOreChance();
        const float RangeSquared = FMath::Square(Progress.Range());
        const float Speed = Context.Width * 0.30f * Progress.SpeedMultiplier() * 0.55f;
        float NextShot = FMath::Max(0.0f, Cooldown - Context.TimeSinceShot);
        float Value = 0.0f;
        float Future = 0.0f;
        float Previous = 0.0f;
        int32 NextBoundary = 1;
        while (Future <= Horizon + SMALL_NUMBER)
        {
            const float Delta = Future - Previous;
            if (Delta > 0.0f)
            {
                const float Destination = Context.Width * (0.5f + 0.27f * FMath::Sin((Context.Time + Future) * 0.25f));
                ShipX = FMath::FInterpConstantTo(ShipX, Destination, Delta, Speed);
                ShipX = FMath::Clamp(ShipX, 32.0f * Context.Pixel, Context.Width - 32.0f * Context.Pixel);
            }
            int32 Fired = 0;
            if (Future + SMALL_NUMBER >= NextShot)
            {
                TArray<bool, TInlineAllocator<MaxForecastRocks>> ShotThisVolley;
                ShotThisVolley.Init(false, Rocks.Num());
                TArray<int32, TInlineAllocator<3>> Primaries;
                while (Fired < Weapons)
                {
                    int32 Closest = INDEX_NONE;
                    float ClosestDistance = RangeSquared;
                    int32 ClosestPriority = MAX_int32;
                    for (int32 I = 0; I < Rocks.Num(); ++I)
                    {
                        if (Remaining[I] <= 0 || ShotThisVolley[I]) continue;
                        const FForecastRock& Rock = Rocks[I];
                        float Y = Rock.Y + Rock.Speed * Future;
                        if (Rock.bWrap) Y = FMath::Fmod(Y, Context.Height);
                        if (Y < 0 || Y > Context.Height) continue;
                        const float X = AvoidShip(Rock.BaseX, Y, ShipX, Context.ShipY,
                            Context.Width, Context.Pixel, Rock.Radius);
                        const float Distance = FVector2D::DistSquared(FVector2D(ShipX, Context.LaserY), FVector2D(X, Y));
                        const int32 Priority = TargetPriority(Progress.TargetingMode, Rock.OreKind, Rock.bEventMarked);
                        if (Distance <= RangeSquared && (Priority < ClosestPriority ||
                            (Priority == ClosestPriority && Distance <= ClosestDistance)))
                        { ClosestPriority = Priority; ClosestDistance = Distance; Closest = I; }
                    }
                    if (Closest == INDEX_NONE) break;
                    ShotThisVolley[Closest] = true;
                    Primaries.Add(Closest);
                    ++Fired;
                }
                TArray<int32, TInlineAllocator<3>> Reflections;
                if (Progress.Has(RicochetUpgrade) && !Primaries.IsEmpty())
                {
                    TArray<FLaserCandidate, TInlineAllocator<MaxForecastRocks>> Candidates;
                    for (int32 I = 0; I < Rocks.Num(); ++I)
                    {
                        if (Remaining[I] <= 0) continue;
                        const auto& Rock = Rocks[I];
                        float Y = Rock.Y + Rock.Speed * Future;
                        if (Rock.bWrap) Y = FMath::Fmod(Y, Context.Height);
                        if (Y < 0 || Y > Context.Height) continue;
                        const float X = AvoidShip(Rock.BaseX, Y, ShipX, Context.ShipY, Context.Width, Context.Pixel, Rock.Radius);
                        const bool bDamaged = Remaining[I] < AsteroidHitPoints(Rock.Size) * 10;
                        Candidates.Add({ I, 0, Rock.Size, FVector2D(X, Y), FLinearColor::White, 0.0f, Rock.OreKind,
                            Rock.bEventMarked, bDamaged, RicochetWouldSpoilFirstHitBonus(Rock.Size, Damage, bDamaged, bHasFirstHitBonus) });
                    }
                    TArray<int32, TInlineAllocator<6>> Reserved;
                    for (int32 Index : Primaries) Reserved.Add(Index);
                    for (int32 Primary : Primaries)
                    {
                        const FLaserCandidate* Source = Candidates.FindByPredicate([Primary](const FLaserCandidate& R) { return R.Index == Primary; });
                        if (!Source) continue;
                        const int32 Bounce = SelectRicochetTarget(Candidates, Reserved, Source->Position,
                            FVector2D(ShipX, Context.LaserY), FVector2D(ShipX, Context.ShipY), 29.0f * Context.Pixel,
                            Progress.Range(), Context.Height, Progress.TargetingMode);
                        if (Bounce != INDEX_NONE) { Reflections.Add(Bounce); Reserved.Add(Bounce); }
                    }
                }
                auto ApplyForecastHit = [&](int32 Index, int32 DamageTenths)
                {
                    const bool bFirstHit = Remaining[Index] == AsteroidHitPoints(Rocks[Index].Size) * 10;
                    Remaining[Index] -= DamageTenths;
                    if (Remaining[Index] > 0) return;
                    const EOreKind Kind = Rocks[Index].OreKind;
                    const float Weight = Kind == EOreKind::Blue ? Weights.Blue : (Kind == EOreKind::Black ? Weights.Black : 1.0f);
                    Value += Weight * (Progress.Yield(Rocks[Index].Size, Kind, false, bFirstHit) +
                        RichChance * (AsteroidHitPoints(Rocks[Index].Size) * 2 - 1)) / (1.0f + Future / 8.0f);
                };
                for (int32 Index : Primaries) ApplyForecastHit(Index, Damage * 10);
                for (int32 Index : Reflections) ApplyForecastHit(Index, Damage * 4);
                // Check for newly entering targets periodically when no shot was fired.
                NextShot = Future + (Fired > 0 ? Cooldown : Step);
            }
            Previous = Future;
            while (NextBoundary * Step <= Future + SMALL_NUMBER) ++NextBoundary;
            Future = FMath::Min(NextShot, NextBoundary * Step);
        }
        return Value;
    }

    inline FAutoUpgradeChoice ChooseAutoUpgrade(const FProgress& Progress, const FForecastContext& Context,
        TConstArrayView<FForecastRock> Rocks, bool bIncludeSideBranches = true, float ForecastHorizon = 12.0f)
    {
        FAutoUpgradeChoice Best;
        FAutoUpgradeChoice BestAffordable;
        FAutoUpgradeChoice Fallback;
        const FForecastWeights Weights = AutoForecastWeights(Progress);
        const float Baseline = ForecastOre(Progress, Context, Rocks, Weights, ForecastHorizon);
        for (int32 Goal = 0; Goal < UpgradeCount; ++Goal)
        {
            if (Progress.Has(Goal) || (!bIncludeSideBranches && IsSideUpgrade(Goal))) continue;
            TArray<int32, TInlineAllocator<16>> Path;
            int32 Current = Goal;
            int32 TotalCost = 0;
            bool bValid = true;
            while (Current != INDEX_NONE && !Progress.Has(Current))
            {
                if (Current < 0 || Current >= UpgradeCount || Path.Num() >= UpgradeCount) { bValid = false; break; }
                const FUpgradeDefinition& Node = Upgrade(Current);
                const int32 ScoreCost = Node.bMicro ? Node.Cost * 2 : Node.Cost;
                if (Node.Cost <= 0 || TotalCost > MAX_int32 - ScoreCost) { bValid = false; break; }
                Path.Add(Current);
                TotalCost += ScoreCost;
                Current = Node.Parent;
            }
            if (!bValid || Path.IsEmpty()) continue;
            FProgress Preview = Progress;
            // Forecasting depends on acquired effects, not on hypothetical
            // accounting. Grant the validated chain without overflowing funds.
            for (int32 I = Path.Num() - 1; I >= 0; --I)
                Preview.Acquired[Path[I]] = true;
            const int32 Next = Path.Last();
            const bool bAffordable = Progress.CanPurchase(Next);
            const int32 NextCost = Upgrade(Next).Cost * (Upgrade(Next).bMicro ? 2 : 1);
            const int32 FallbackCost = Fallback.Next == INDEX_NONE ? MAX_int32 :
                Upgrade(Fallback.Next).Cost * (Upgrade(Fallback.Next).bMicro ? 2 : 1);
            if (bAffordable && NextCost < FallbackCost)
                Fallback = { Next, Next, 0.0f };
            const float Gain = ForecastOre(Preview, Context, Rocks, Weights, ForecastHorizon) - Baseline;
            const float Score = Gain / TotalCost;
            // With hour-scale prices a useful gain/cost ratio can be far below
            // KINDA_SMALL_NUMBER; reject negligible gains, not valid ratios.
            if (Gain > KINDA_SMALL_NUMBER && Score > Best.Score)
                Best = { Next, Goal, Score };
            if (bAffordable && Gain > KINDA_SMALL_NUMBER && Score > BestAffordable.Score)
                BestAffordable = { Next, Goal, Score };
        }
        if (BestAffordable.Next != INDEX_NONE) return BestAffordable;
        if (Fallback.Next != INDEX_NONE) return Fallback;
        return Best;
    }
}
