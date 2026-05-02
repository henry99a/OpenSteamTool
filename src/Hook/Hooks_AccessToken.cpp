#include "Hooks_AccessToken.h"
#include "HookMacros.h"
#include "Utils/VehCommon.h"
#include "dllmain.h"

namespace {
    uint8_t* g_addAccessTokenTarget = nullptr;
    PVOID    g_vehHandle            = nullptr;

    LONG CALLBACK VehHandler(PEXCEPTION_POINTERS pExInfo) {
        PCONTEXT ctx = pExInfo->ContextRecord;

        if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT
            && ctx->Rip == reinterpret_cast<uint64_t>(g_addAccessTokenTarget)) {
            // Original instruction: 48 89 48 18  mov [rax+18h], rcx
            uint32_t appid = *reinterpret_cast<uint32_t*>(ctx->Rax + 0x20);
            if (uint64_t access_token = LuaConfig::GetAccessToken(appid))
                ctx->Rcx = access_token;
            // Restore the original 0x48 prefix and arm TF so we can
            // re-install the int3 after the original instruction runs.
            *g_addAccessTokenTarget = 0x48;
            ctx->EFlags |= 0x100;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP
            && ctx->Rip == reinterpret_cast<uint64_t>(g_addAccessTokenTarget + 4)) {
            *g_addAccessTokenTarget = 0xCC;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
}

namespace Hooks_AccessToken {
    void Install() {
        if (g_vehHandle) return;

        auto* p = static_cast<uint8_t*>(FIND_SIG(diversion_hMdoule, AddAccessToken));
        if (!p) return;
        g_addAccessTokenTarget = p + 10;  // offset to mov [rax+18h], rcx
        VehCommon::ArmInt3(g_addAccessTokenTarget);
        g_vehHandle = AddVectoredExceptionHandler(1, VehHandler);
    }

    void Uninstall() {
        if (g_vehHandle) {
            RemoveVectoredExceptionHandler(g_vehHandle);
            g_vehHandle = nullptr;
        }
        if (g_addAccessTokenTarget && *g_addAccessTokenTarget == 0xCC)
            VehCommon::RestoreByte(g_addAccessTokenTarget, 0x48);
        g_addAccessTokenTarget = nullptr;
    }
}
