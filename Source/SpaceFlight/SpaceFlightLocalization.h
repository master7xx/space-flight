#pragma once
#include "CoreMinimal.h"

namespace SpaceFlight
{
    enum class ELanguage : uint8 { Russian, English, Spanish };
    ELanguage GetLanguage();
    void SetLanguage(ELanguage Language);
    ELanguage NextLanguage(ELanguage Language);
    const TCHAR* LocalizeForLanguage(const TCHAR* Source, ELanguage Language);
    inline const TCHAR* Localize(const TCHAR* Source) { return LocalizeForLanguage(Source, GetLanguage()); }
}

// Keep Unreal's compile-time format checking for each translated literal.
#define SF_LOCAL_PRINTF(FmtRu, FmtEn, FmtEs, ...) \
    (SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::English ? FString::Printf(FmtEn, __VA_ARGS__) : \
     SpaceFlight::GetLanguage() == SpaceFlight::ELanguage::Spanish ? FString::Printf(FmtEs, __VA_ARGS__) : \
     FString::Printf(FmtRu, __VA_ARGS__))
