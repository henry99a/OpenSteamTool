#pragma once

namespace Hooks_Manifest {
    // GetManifestRequestCode hook: serves request codes for our injected
    // depots from manifest.steam.run, with an in-memory cache and bounded
    // WinHTTP timeouts so a slow/dead network never stalls the host.
    void Install();
    void Uninstall();
}
