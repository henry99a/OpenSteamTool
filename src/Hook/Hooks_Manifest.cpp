#include "Hooks_Manifest.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/WinHttp.h"
#include <charconv>

// ═══════════════════════════════════════════════════════════════════
//  Manifest request-code resolution.
//  No cache — codes rotate over time, fetch every call.
// ═══════════════════════════════════════════════════════════════════
namespace {
    // GET https://manifest.steam.run/api/manifest/{gid}
    // Response: {"content":"1666836470726104466"}
    bool FetchSteamRun(uint64 manifest_gid, uint64* outRequestCode) {
        char url[128];
        snprintf(url, sizeof(url), "https://manifest.steam.run/api/manifest/%llu", manifest_gid);

        auto r = WinHttp::Execute(L"GET", url, nullptr, 0, nullptr,
                                 Config::manifestTimeoutResolve,
                                 Config::manifestTimeoutConnect,
                                 Config::manifestTimeoutSend,
                                 Config::manifestTimeoutRecv);
        LOG_MANIFEST_INFO("Manifest steamrun status={} gid={}", r.status, manifest_gid);

        if (!r.ok || r.status != 200) return false;

        if (size_t key = r.body.find("\"content\""); key != std::string::npos) {
            if (size_t q1 = r.body.find('"', key + 9); q1 != std::string::npos) {
                if (size_t q2 = r.body.find('"', q1 + 1); q2 != std::string::npos) {
                    uint64 code = 0;
                    auto [_, ec] = std::from_chars(
                        r.body.data() + q1 + 1, r.body.data() + q2, code);
                    if (ec == std::errc{}) {
                        *outRequestCode = code;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ── provider: gmrc.wudrm.com ───────────────────────────────────
    // GET http://gmrc.wudrm.com/manifest/{gid}
    // Response: plain-text uint64, e.g. "10570517747114638659"
    bool FetchWudrm(uint64 manifest_gid, uint64* outRequestCode) {
        char url[128];
        snprintf(url, sizeof(url), "http://gmrc.wudrm.com/manifest/%llu", manifest_gid);

        auto r = WinHttp::Execute(L"GET", url, nullptr, 0, nullptr,
                                 Config::manifestTimeoutResolve,
                                 Config::manifestTimeoutConnect,
                                 Config::manifestTimeoutSend,
                                 Config::manifestTimeoutRecv);
        LOG_MANIFEST_INFO("Manifest wudrm status={} gid={}", r.status, manifest_gid);
            
        if (!r.ok || r.status != 200) return false;

        uint64 code = 0;
        auto [_, ec] = std::from_chars(r.body.data(), r.body.data() + r.body.size(), code);
        if (ec == std::errc{}) {
            *outRequestCode = code;
            return true;
        }
        return false;
    }

    // ── resolve (single-provider, no fallback) ────────────────────
    bool FetchManifestRequestCode(uint64 manifest_gid, uint64* outRequestCode) {
        if (LuaConfig::HasManifestCodeFunc()) {
            if (LuaConfig::CallManifestFetchCode(manifest_gid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via manifest.lua", manifest_gid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} lua returned nil, falling back to config", manifest_gid);
        }

        switch (Config::manifestUrl) {
        case Config::ManifestUrl::Wudrm:
            return FetchWudrm(manifest_gid, outRequestCode);
        case Config::ManifestUrl::SteamRun:
        default:
            return FetchSteamRun(manifest_gid, outRequestCode);
        }
    }

    HOOK_FUNC(GetManifestRequestCode, EResult, void* pObject, AppId_t AppId, AppId_t DepotId,
              uint64 manifest_gid, const char* branch, uint64* outRequestCode) {
        LOG_MANIFEST_DEBUG("GetManifestRequestCode: AppId={} DepotId={} gid={} branch={}",
                          AppId, DepotId, manifest_gid, branch);
        if (LuaConfig::HasDepot(DepotId)
            && FetchManifestRequestCode(manifest_gid, outRequestCode))
            return k_EResultOK;
        return oGetManifestRequestCode(pObject, AppId, DepotId, manifest_gid, branch, outRequestCode);
    }
}

namespace Hooks_Manifest {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_D(GetManifestRequestCode);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(GetManifestRequestCode);
        UNHOOK_END();
    }
}
