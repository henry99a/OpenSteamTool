#include "Hooks_AppTicket.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/AppTicket.h"
#include <memory>

namespace {
    HOOK_FUNC(GetConfigString, bool, void* pThis, ERegistrySubTree eRegistrySubTree, const char* pchKey, char* pchValue, uint32* pcbValue) {
        if (eRegistrySubTree == k_ERegistrySubTreeAppOwnershipTickets) {
            LOG_TRACE("AppOwnershipTickets requested");
            if (std::all_of(pchKey, pchKey + strlen(pchKey), ::isdigit)) {
                if (AppId_t appid = std::stoul(pchKey); LuaConfig::HasDepot(appid)) {
                    std::vector<uint8_t> ticket = AppTicket::GetAppOwnershipTicketFromRegistry(appid);
                    if (!ticket.empty() && *pcbValue > ticket.size()) {
                        memcpy(pchValue, ticket.data(), ticket.size());
                        *pcbValue = static_cast<uint32>(ticket.size());
                        LOG_DEBUG("Provided AppOwnershipTicket for AppId {} ({} bytes)", appid, *pcbValue);
                        return true;
                    }
                }
            }
        }
        return oGetConfigString(pThis, eRegistrySubTree, pchKey, pchValue, pcbValue);
    }
}

namespace Hooks_AppTicket {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_D(GetConfigString);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(GetConfigString);
        UNHOOK_END();
    }
}
