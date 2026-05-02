#include "Patches.h"
#include "HookMacros.h"
#include "dllmain.h"

namespace {
    void ApplyPatch(const unsigned char* bytes, size_t size, void* target, const char* name) {
        if (!target) return;  // FIND_SIG already logged the failure
        PatchMemoryBytes(target, bytes, size);
        LOG_DEBUG("Applied patch: {}", name);
    }
}

namespace Patches {
    void Apply() {
        if (!diversion_hMdoule) return;

        static constexpr unsigned char kJmpSharedLibraryStopPlaying[6] = {0xE9, 0x31, 0x02, 0x00, 0x00, 0x90};
        static constexpr unsigned char kJmpFamilyGroupRunningApp[6]    = {0xE9, 0x9D, 0x01, 0x00, 0x00, 0x90};
        static constexpr unsigned char kJmpFamilyGroupRunningApp2[6]   = {0xE9, 0x31, 0x02, 0x00, 0x00, 0x90};
        static constexpr unsigned char kBCanRemotePlayTogether[5]      = {0xB0, 0x01, 0xC3, 0x90, 0x90};

        ApplyPatch(kJmpSharedLibraryStopPlaying, sizeof(kJmpSharedLibraryStopPlaying),
                   FIND_SIG(diversion_hMdoule, SharedLibraryStopPlayingPatch), "SharedLibraryStopPlaying");
        ApplyPatch(kJmpFamilyGroupRunningApp, sizeof(kJmpFamilyGroupRunningApp),
                   FIND_SIG(diversion_hMdoule, FamilyGroupRunningAppPatch), "FamilyGroupRunningApp");
        ApplyPatch(kJmpFamilyGroupRunningApp2, sizeof(kJmpFamilyGroupRunningApp2),
                   FIND_SIG(diversion_hMdoule, FamilyGroupRunningApp2Patch), "FamilyGroupRunningApp2");
        ApplyPatch(kBCanRemotePlayTogether, sizeof(kBCanRemotePlayTogether),
                   FIND_SIG(diversion_hMdoule, BCanRemotePlayTogetherPatch), "BCanRemotePlayTogether");
    }
}
