#include "Hooks_AppState.h"
#include "HookMacros.h"
#include "dllmain.h"

namespace {
    HOOK_FUNC(ModifyStateFlags, bool, CSteamApp* pApp, int32 set, int32 clear, int32 a4) {
        if (LuaConfig::pinApp(pApp->AppID)
            && (pApp->StateFlags & k_EAppStateFullyInstalled)) {
            constexpr int32 kUpdateFlags = k_EAppStateUpdateRequired
                                         | k_EAppStateUpdateQueued
                                         | k_EAppStateUpdateRunning
                                         | k_EAppStateUpdateStarted;
            set   &= ~kUpdateFlags;
            clear |=  kUpdateFlags;
        }
        return oModifyStateFlags(pApp, set, clear, a4);
    }
}

namespace Hooks_AppState {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_D(ModifyStateFlags);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(ModifyStateFlags);
        UNHOOK_END();
    }
}
