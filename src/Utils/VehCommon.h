#pragma once

#include <windows.h>
#include <cstdint>

namespace VehCommon {
    // Patches a single byte at `target` to 0xCC (int3). The page is left
    // PAGE_EXECUTE_READWRITE so the VEH handler can rewrite the byte from
    // the exception path without another VirtualProtect round-trip.
    void ArmInt3(void* target);

    // Restores a single byte at `target`. Used by Uninstall paths to
    // disarm an int3 site that may not have fired yet.
    void RestoreByte(void* target, uint8_t original);
}
