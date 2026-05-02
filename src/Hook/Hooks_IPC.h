#pragma once

#include "dllmain.h"

// IPC-interface call hooks.
// Handler table driven — add new functions by defining a struct + handler + registry entry.
namespace Hooks_IPC {
    void Install();
    void Uninstall();
}
