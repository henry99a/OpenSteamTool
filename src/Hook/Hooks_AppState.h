#pragma once

namespace Hooks_AppState {
    // ModifyStateFlags hook: prevents Steam from auto-updating apps the user
    // has pinned to a specific manifest.
    void Install();
    void Uninstall();
}
