#pragma once

namespace Hooks_AccessToken {
    // Installs the int3 software breakpoint at the add_access_token call
    // site, plus a VEH handler that injects a saved access token (from
    // LuaConfig) when the breakpoint fires.
    void Install();
    void Uninstall();
}
