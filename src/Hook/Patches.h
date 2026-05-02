#pragma once

namespace Patches {
    // One-shot byte patches in steamclient (family-sharing / remote-play
    // bypasses). No-op for un-found patterns — failures are logged.
    void Apply();
}
