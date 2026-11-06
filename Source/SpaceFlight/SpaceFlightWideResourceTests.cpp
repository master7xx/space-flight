#include "SpaceFlightSave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
    struct FWideField
    {
        const TCHAR* Name;
        int64 Value;
    };

    // Use reflection until the implementation exists: the red test must not
    // narrow out-of-range integers into the current int32 fields.
    bool SetWideValues(FAutomationTestBase& Test, USpaceFlightSaveGame* Save,
        TConstArrayView<FWideField> Fields, int32 Omitted = INDEX_NONE)
    {
        bool bFoundAll = true;
        for (int32 I = 0; I < Fields.Num(); ++I)
        {
            FInt64Property* Property = FindFProperty<FInt64Property>(USpaceFlightSaveGame::StaticClass(), Fields[I].Name);
            if (!Test.TestNotNull(FString::Printf(TEXT("64-bit save field %s exists"), Fields[I].Name), Property))
            {
                bFoundAll = false;
                continue;
            }
            if (I != Omitted) Property->SetPropertyValue_InContainer(Save, Fields[I].Value);
        }
        return bFoundAll;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightWideLegacySaveTest, "SpaceFlight.Progress.WideResource.LegacyV1",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightWideLegacySaveTest::RunTest(const FString& Parameters)
{
    // Construct the same named int32 fields used by historical v1 saves.
    USpaceFlightSaveGame* Legacy = NewObject<USpaceFlightSaveGame>();
    Legacy->Version = 1;
    Legacy->Revision = 17;
    Legacy->Resources = 123;
    Legacy->TotalCollected = 167;
    Legacy->TotalSpent = 4;
    Legacy->BlueOre = 3;
    Legacy->BlackOre = 5;
    Legacy->Isotope = 13;
    Legacy->OreConverted = 40;
    Legacy->IsotopeSpent = 7;
    Legacy->RefiningSchema = 1;
    Legacy->RefiningMode = 2;
    Legacy->BlueRefiningQuota = 75;
    Legacy->BlackRefiningQuota = 125;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("legacy v1 serializes"), UGameplayStatics::SaveGameToMemory(Legacy, Bytes))) return false;
    const USpaceFlightSaveGame* Reloaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("legacy v1 deserializes"), Reloaded)) return false;
    SpaceFlight::FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    if (!TestTrue(TEXT("legacy v1 restores with its old fields"), Reloaded->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestEqual(TEXT("legacy resources survive"), static_cast<int64>(Restored.Resources), int64(123));
    TestEqual(TEXT("legacy collected survives"), static_cast<int64>(Restored.TotalCollected), int64(167));
    TestEqual(TEXT("legacy isotope spending survives"), static_cast<int64>(Restored.IsotopeSpent), int64(7));
    TestEqual(TEXT("legacy blue quota survives"), Restored.BlueRefiningQuota, int64(75));
    TestEqual(TEXT("legacy black quota survives"), Restored.BlackRefiningQuota, int64(125));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightWideResourceV2Test, "SpaceFlight.Progress.WideResource.RoundTripV2",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightWideResourceV2Test::RunTest(const FString& Parameters)
{
    constexpr int64 Unit = int64(MAX_int32) + 1;
    const FWideField Fields[] = {
        {TEXT("Resources64"), 3 * Unit + 1000},
        {TEXT("TotalCollected64"), 8 * Unit + 1254},
        {TEXT("TotalSpent64"), Unit + 110},
        {TEXT("BlueOre64"), Unit + 13},
        {TEXT("BlackOre64"), Unit + 17},
        {TEXT("Isotope64"), Unit + 31},
        {TEXT("OreConverted64"), 4 * Unit + 144},
        {TEXT("IsotopeSpent64"), Unit + 41}
    };
    SpaceFlight::FProgress SizeProbe;
    TestEqual(TEXT("in-memory resource counter has 64-bit width"), int32(sizeof(SizeProbe.Resources)), int32(sizeof(int64)));
    TestEqual(TEXT("in-memory lifetime counter has 64-bit width"), int32(sizeof(SizeProbe.TotalCollected)), int32(sizeof(int64)));
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Version = 2;
    Save->Revision = 42;
    Save->RefiningSchema = 1;
    Save->RefiningMode = 2;
    Save->BlueRefiningQuota = Unit * 100 + 5;
    Save->BlackRefiningQuota = Unit * 100 + 7;
    if (!SetWideValues(*this, Save, MakeArrayView(Fields))) return false;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("wide v2 serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
    const USpaceFlightSaveGame* Loaded = Cast<USpaceFlightSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("wide v2 deserializes"), Loaded)) return false;
    SpaceFlight::FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    if (!TestTrue(TEXT("v2 with values above int32 restores"), Loaded->Restore(Restored, Time, Brightness, bAuto))) return false;
    TestEqual(TEXT("wide resources"), static_cast<int64>(Restored.Resources), Fields[0].Value);
    TestEqual(TEXT("wide total collected"), static_cast<int64>(Restored.TotalCollected), Fields[1].Value);
    TestEqual(TEXT("wide total spent"), static_cast<int64>(Restored.TotalSpent), Fields[2].Value);
    TestEqual(TEXT("wide blue ore"), static_cast<int64>(Restored.BlueOre), Fields[3].Value);
    TestEqual(TEXT("wide black ore"), static_cast<int64>(Restored.BlackOre), Fields[4].Value);
    TestEqual(TEXT("wide isotope"), static_cast<int64>(Restored.Isotope), Fields[5].Value);
    TestEqual(TEXT("wide converted ore"), static_cast<int64>(Restored.OreConverted), Fields[6].Value);
    TestEqual(TEXT("wide isotope spent"), static_cast<int64>(Restored.IsotopeSpent), Fields[7].Value);
    TestEqual(TEXT("wide blue quota"), Restored.BlueRefiningQuota, Unit * 100 + 5);
    TestEqual(TEXT("wide black quota"), Restored.BlackRefiningQuota, Unit * 100 + 7);
    USpaceFlightSaveGame* Reencoded = NewObject<USpaceFlightSaveGame>();
    Reencoded->Capture(Restored, Time, Brightness, bAuto, 43);
    TestEqual(TEXT("wide progress writes the current version"), Reencoded->Version, SpaceFlight::CurrentSaveVersion);
    for (const FWideField& Field : Fields)
    {
        const FInt64Property* Property = FindFProperty<FInt64Property>(USpaceFlightSaveGame::StaticClass(), Field.Name);
        if (Property) TestEqual(FString::Printf(TEXT("recaptured %s"), Field.Name), Property->GetPropertyValue_InContainer(Reencoded), Field.Value);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightWideMissingFieldTest, "SpaceFlight.Progress.WideResource.MissingV2Fields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightWideMissingFieldTest::RunTest(const FString& Parameters)
{
    const FWideField Fields[] = {
        {TEXT("Resources64"), 1}, {TEXT("TotalCollected64"), 1}, {TEXT("TotalSpent64"), 0},
        {TEXT("BlueOre64"), 0}, {TEXT("BlackOre64"), 0}, {TEXT("Isotope64"), 0},
        {TEXT("OreConverted64"), 0}, {TEXT("IsotopeSpent64"), 0}
    };
    USpaceFlightSaveGame* Complete = NewObject<USpaceFlightSaveGame>();
    Complete->Version = 2;
    if (!SetWideValues(*this, Complete, MakeArrayView(Fields))) return false;
    SpaceFlight::FProgress Progress; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    if (!TestTrue(TEXT("complete minimal v2 snapshot restores"), Complete->Restore(Progress, Time, Brightness, bAuto))) return false;
    for (int32 Missing = 0; Missing < UE_ARRAY_COUNT(Fields); ++Missing)
    {
        USpaceFlightSaveGame* Partial = NewObject<USpaceFlightSaveGame>();
        Partial->Version = 2;
        if (!SetWideValues(*this, Partial, MakeArrayView(Fields), Missing)) return false;
        TestFalse(FString::Printf(TEXT("v2 missing %s is rejected"), Fields[Missing].Name), Partial->Restore(Progress, Time, Brightness, bAuto));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceFlightWideQuotaBoundaryTest, "SpaceFlight.Progress.WideResource.QuotaBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceFlightWideQuotaBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace SpaceFlight;
    FProgress P;
    P.SetRefiningMode(5);
    const int64 Half = ResourceAccountingLimit / 2;
    TestEqual(TEXT("large blue amount is accepted"), P.AddOre(Half, EOreKind::Blue), Half);
    TestEqual(TEXT("large black amount is accepted"), P.AddOre(Half, EOreKind::Black), Half);
    TestTrue(TEXT("large quotas remain nonnegative"), P.BlueRefiningQuota >= 0 && P.BlackRefiningQuota >= 0);
    TestTrue(TEXT("blue quota stays within its reserve"), P.BlueRefiningQuota <= P.BlueOre * 100);
    TestTrue(TEXT("black quota stays within its reserve"), P.BlackRefiningQuota <= P.BlackOre * 100);
    USpaceFlightSaveGame* Save = NewObject<USpaceFlightSaveGame>();
    Save->Capture(P, 1, 20, true, 1);
    FProgress Restored; float Time = 0; int32 Brightness = 0; bool bAuto = false;
    TestTrue(TEXT("large quotas and resources restore"), Save->Restore(Restored, Time, Brightness, bAuto));
    Save->TotalCollected64 = ResourceAccountingLimit + 1;
    TestFalse(TEXT("save above the supported accounting limit is rejected"), Save->Restore(Restored, Time, Brightness, bAuto));
    return true;
}
#endif
