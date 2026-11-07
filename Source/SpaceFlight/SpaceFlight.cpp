#include "Modules/ModuleManager.h"

// A packaged build ships with its own Engine directory. Avoid embedding the
// build machine's absolute engine path as an unused development fallback.
#if UE_BUILD_SHIPPING && PLATFORM_DESKTOP
#undef IMPLEMENT_FOREIGN_ENGINE_DIR
#define IMPLEMENT_FOREIGN_ENGINE_DIR() const TCHAR* GForeignEngineDir = nullptr;
#endif

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SpaceFlight, "SpaceFlight");
