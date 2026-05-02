#include "Hooks_IPC.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/AppTicket.h"
#include "Utils/Hash.h"
#include "Hooks_Misc.h"
#include "Steam/Enums.h"

namespace {
    // ════════════════════════════════════════════════════════════════
    //  IPC InterfaceCall packet layout
    //
    //  offset 0:  cmd          (1 byte, EIPCCommand)
    //  offset 1:  interfaceID  (1 byte, EIPCInterface)
    //  offset 2:  hSteamUser   (4 bytes)
    //  offset 6:  funcHash     (4 bytes)   ← per-function dispatch
    //  offset 10: args[]       (variable, function-specific)
    //  offset N:  fencepost    (4 bytes, function-specific, basic validation)
    // ════════════════════════════════════════════════════════════════
    constexpr int OFFSET_CMD          = 0;
    constexpr int OFFSET_INTERFACE_ID = 1;
    constexpr int OFFSET_FUNC_HASH    = 6;
    constexpr int OFFSET_ARGS         = 10;
    constexpr int IPC_HEADER_SIZE     = 10;

    // All Steam IPC responses begin with this byte.
    constexpr uint8 RESPONSE_PREFIX = 0x0B;

    // ════════════════════════════════════════════════════════════════
    //  Function hashes (extracted from steamclient64.dll).
    //  Naming: HASH_<Interface>_<Method>.
    // ════════════════════════════════════════════════════════════════
    constexpr uint32 HASH_IClientUser_GetSteamID                        = 0xD6FC3200;
    constexpr uint32 HASH_IClientUser_GetAppOwnershipTicketExtendedData = 0xC7E71245;

    // ════════════════════════════════════════════════════════════════
    //  CServerPipe layout
    //
    //  +32  clientPID    (uint32)
    //  +40  processName  (char*)
    // ════════════════════════════════════════════════════════════════
    constexpr int PIPE_OFFSET_CLIENT_PID   = 32;
    constexpr int PIPE_OFFSET_PROCESS_NAME = 40;

    // ════════════════════════════════════════════════════════════════
    //  Pipe helpers
    // ════════════════════════════════════════════════════════════════

    using GetPipeClient_t = void*(__fastcall*)(void* pEngine, uint32 hSteamPipe);
    inline GetPipeClient_t oGetPipeClient = nullptr;

    static uint32 g_SteamPID = 0;

    // Pre-computed FNV-1a hashes for known steam internal processes.
    constexpr uint32 HASH_PROC_steamwebhelper   = Fnv1aHash("steamwebhelper.exe");
    constexpr uint32 HASH_PROC_gameoverlayui64  = Fnv1aHash("gameoverlayui64.exe");
    constexpr uint32 HASH_PROC_gameoverlayui    = Fnv1aHash("gameoverlayui.exe");

    // Cache IsSteamInternal results keyed by process name hash.
    static std::unordered_map<uint32, bool> g_IsSteamInternalCache;

    static void* GetPipe(void* pServer, uint32 hSteamPipe) {
        return oGetPipeClient ? oGetPipeClient(pServer, hSteamPipe) : nullptr;
    }

    static uint32 GetPipeClientPID(void* pipe) {
        if (!pipe) return 0;
        return *reinterpret_cast<uint32*>(
            reinterpret_cast<uint8*>(pipe) + PIPE_OFFSET_CLIENT_PID);
    }

    // May return nullptr — Steam leaves the processName slot null for pipes
    // whose owner hasn't been resolved yet. Callers MUST null-check.
    static const char* GetPipeProcessName(void* pipe) {
        if (!pipe) return nullptr;
        return *reinterpret_cast<const char**>(
            reinterpret_cast<uint8*>(pipe) + PIPE_OFFSET_PROCESS_NAME);
    }

    // True when the request originated from steam internals — never spoof these.
    static bool IsSteamInternal(void* pipe, uint32 pid) {
        if (pid == 0 || pid == g_SteamPID) return true;
        if (!pipe) return false;

        const char* name = GetPipeProcessName(pipe);
        if (!name) return false;  // unresolved, check again later

        const uint32 nameHash = Fnv1aHash(name);
        auto it = g_IsSteamInternalCache.find(nameHash);
        if (it != g_IsSteamInternalCache.end()) return it->second;

        bool isInternal = nameHash == HASH_PROC_steamwebhelper
                       || nameHash == HASH_PROC_gameoverlayui64
                       || nameHash == HASH_PROC_gameoverlayui;

        g_IsSteamInternalCache[nameHash] = isInternal;
        return isInternal;
    }

    // ════════════════════════════════════════════════════════════════
    //  Handler dispatch
    //
    //  Each handler receives the request buffer (so it may read its own
    //  variable args) and the response buffer to be rewritten in place via
    //  CUtlBuffer::Put / PutPod. The response prefix (0x0B) is the first
    //  byte of every payload; handlers build [prefix][payload] in a small
    //  local buffer and Put() it.
    //
    //  Args start at reqData + OFFSET_ARGS; each handler decodes them
    //  inline because every method has a different shape (this matches
    //  how Steam's generated stubs read them).
    // ════════════════════════════════════════════════════════════════

    using IpcHandlerFn = void(*)(CUtlBuffer* pWrite,
                                 const uint8* reqData, int32 reqSize,
                                 AppId_t appId);

    struct IpcHandlerEntry {
        EIPCInterface interfaceID;
        uint32        funcHash;
        const char*   name;       // "Interface_Method" — used for logging
        IpcHandlerFn  handler;
    };

    // ── Handler: IClientUser::GetSteamID ──────────────────────────
    //  Request:  no args
    //  Response: [uint8 prefix=0x0B][uint64 SteamID]   (9 bytes)
    //  TODO: We need to find a timing to switch the SteamID that ensures the save files are not affected.
    static void Handler_IClientUser_GetSteamID(CUtlBuffer* pWrite,
                                               const uint8*, int32,
                                               AppId_t appId)
    {
        LOG_IPC_DEBUG(pWrite->DebugString());
        const uint64 spoofed = AppTicket::GetSpoofSteamID(appId);
        if(!spoofed){
            LOG_IPC_WARN("IClientUser::GetSteamID: AppId={} no valid ticket - cannot spoof", appId);
            return;
        }
        uint8* base = pWrite->m_Memory.m_pMemory;
        base[0] = RESPONSE_PREFIX;
        memcpy(base + 1, &spoofed, sizeof(spoofed));
        LOG_IPC_DEBUG("IClientUser::GetSteamID: AppId={} -> Spoofed: 0x{:X}({})", appId, spoofed, spoofed);
    }

    // ── Handler: IClientUser::GetAppOwnershipTicketExtendedData ───
    //  Request:  [uint32 nAppID][int32 cbBufferLength]
    //  Response (IPC serialization from CClientUser_RunInterface):
    //  uint32 GetAppOwnershipTicketData( uint32 nAppID, void *pvBuffer, uint32 cbBufferLength, uint32 *piAppId, uint32 *piSteamId, uint32 *piSignature, uint32 *pcbSignature )
    //    [uint8  prefix     ]  0x0B
    //    [uint32 returnValue]  actual ticket bytes written
    //    [bufSize bytes     ]  ticket data padded with zeros to bufSize (pvBuffer)
    //    [uint32 piAppId    ]  byte offset of AppID   in ticket (V4 = 16)
    //    [uint32 piSteamId  ]  byte offset of SteamID in ticket (V4 = 8)
    //    [uint32 piSignature]  byte offset of signature = ownershipTicketLength
    //    [uint32 pcbSignature] signature size in bytes (RSA-1024 = 128)
    static void Handler_IClientUser_GetAppOwnershipTicketExtendedData(
        CUtlBuffer* pWrite, const uint8* reqData, int32 reqSize, AppId_t appId)
    {
        LOG_IPC_DEBUG(pWrite->DebugString());
        if (reqSize < OFFSET_ARGS + 8) return;
        const uint8* args = reqData + OFFSET_ARGS;
        const uint32 reqAppID   = *reinterpret_cast<const uint32*>(args);
        const int32  reqBufSize = *reinterpret_cast<const int32*>(args + 4);

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: req AppID={} bufSize={}",
                  reqAppID, reqBufSize);

        std::vector<uint8_t> ticket = AppTicket::GetAppOwnershipTicketFromRegistry(reqAppID);
        if (ticket.empty() || ticket.size() < 4) return;

        const uint32 ticketSize = static_cast<uint32>(ticket.size());
        const uint32 sigOffset  = *reinterpret_cast<const uint32*>(ticket.data());

        const uint32 totalSize = 1 + 4 + reqBufSize + 16;
        if (static_cast<uint32>(pWrite->m_Put) < totalSize) return;

        uint8* base = pWrite->m_Memory.m_pMemory;

        // prefix
        base[0] = RESPONSE_PREFIX;
        // returnValue = ticketSize
        memcpy(base + 1, &ticketSize, 4);
        // ticket buffer: copy ticket then zero-fill the rest
        const uint32 copySize = (ticketSize < static_cast<uint32>(reqBufSize))
                              ? ticketSize : static_cast<uint32>(reqBufSize);
        memcpy(base + 5, ticket.data(), copySize);
        if (copySize < static_cast<uint32>(reqBufSize))
            memset(base + 5 + copySize, 0, reqBufSize - copySize);
        // 4 output parameters (ISteamAppTicket::GetAppOwnershipTicketData)
        const uint32 piAppId      = 16;   // offset of uint32 appID   in V4 ticket
        const uint32 piSteamId    = 8;    // offset of uint64 steamID in V4 ticket
        const uint32 piSignature  = sigOffset;
        const uint32 pcbSignature = 128;  // RSA-1024
        const uint32 outOff = 5 + reqBufSize;
        memcpy(base + outOff,      &piAppId,      4);
        memcpy(base + outOff + 4,  &piSteamId,    4);
        memcpy(base + outOff + 8,  &piSignature,  4);
        memcpy(base + outOff + 12, &pcbSignature,  4);

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} -> {} bytes "
                  "(sigOffset={})", appId, ticketSize, sigOffset);
    }

    static const IpcHandlerEntry g_Handlers[] = {
        { EIPCInterface::IClientUser, HASH_IClientUser_GetSteamID,
          "IClientUser::GetSteamID",
          Handler_IClientUser_GetSteamID },
        { EIPCInterface::IClientUser, HASH_IClientUser_GetAppOwnershipTicketExtendedData,
          "IClientUser::GetAppOwnershipTicketExtendedData",
          Handler_IClientUser_GetAppOwnershipTicketExtendedData },
    };

    static const IpcHandlerEntry* FindHandler(EIPCInterface iface, uint32 funcHash) {
        for (auto& e : g_Handlers) {
            if (e.interfaceID == iface && e.funcHash == funcHash) return &e;
        }
        return nullptr;
    }

    // Decode the request header and return the dispatch entry, or nullptr if
    // the packet is not an InterfaceCall we know how to spoof.
    static const IpcHandlerEntry* DecodeRequest(const CUtlBuffer* pRead) {
        const int32 size = pRead->m_Put;
        if (size < IPC_HEADER_SIZE) return nullptr;

        const uint8* data = pRead->m_Memory.m_pMemory;
        const auto cmd = static_cast<EIPCCommand>(data[OFFSET_CMD]);
        if (cmd != EIPCCommand::InterfaceCall) {
            LOG_IPC_TRACE("ipc: cmd={} size={} (skipped)", static_cast<int>(cmd), size);
            return nullptr;
        }

        const auto iface = static_cast<EIPCInterface>(data[OFFSET_INTERFACE_ID]);
        const uint32 funcHash = *reinterpret_cast<const uint32*>(data + OFFSET_FUNC_HASH);
        const IpcHandlerEntry* entry = FindHandler(iface, funcHash);
        if (!entry) {
            LOG_IPC_TRACE("ipc: unhandled iface={} hash=0x{:08X}",
                      static_cast<int>(iface), funcHash);
            return nullptr;
        }

        LOG_IPC_TRACE("ipc: dispatch -> {}", entry->name);
        return entry;
    }

    // ════════════════════════════════════════════════════════════════
    //  Main hook
    //
    //  1. Decode the packet header. If it isn't an InterfaceCall we care
    //     about, just forward to the original implementation.
    //  2. Forward to original (it produces the real response in pWrite).
    //  3. Skip if the request originated from the steam client itself
    //     (only game traffic is spoofed).
    //  4. Resolve the target AppId via Hooks_Misc — prefer the running
    //     pipe's app, fall back to the initial-running-game hint.
    // ════════════════════════════════════════════════════════════════
    HOOK_FUNC(IPCProcessMessage, bool,
              void* pServer, uint32 hSteamPipe,
              CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        const IpcHandlerEntry* entry = DecodeRequest(pRead);
        const int32  reqSize = pRead->m_Put;
        const uint8* reqData = pRead->m_Memory.m_pMemory;

        const bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);
        if (!result || !entry) return result;

        void* pipe = GetPipe(pServer, hSteamPipe);
        const uint32 clientPID = GetPipeClientPID(pipe);
        if (IsSteamInternal(pipe, clientPID)) {
            LOG_IPC_TRACE("ipc: {} from steam — passthrough", entry->name);
            return result;
        }

        AppId_t appId = Hooks_Misc::GetAppIDForCurrentPipe();
        if (appId == 0)
            appId = Hooks_Misc::GetAppIDFromInitialRunningGame();
        if (appId == 0 || !LuaConfig::HasDepot(appId)) {
            LOG_IPC_WARN("ipc: {} pid={} no spoof config (appId={})",
                      entry->name, clientPID, appId);
            return result;
        }

        entry->handler(pWrite, reqData, reqSize, appId);
        return result;
    }
}

namespace Hooks_IPC {
    void Install() {
        g_SteamPID = GetCurrentProcessId();
        RESOLVE_D(GetPipeClient);
        HOOK_BEGIN();
        INSTALL_HOOK_D(IPCProcessMessage);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(IPCProcessMessage);
        UNHOOK_END();
        oGetPipeClient            = nullptr;
        g_SteamPID                = 0;
        g_IsSteamInternalCache.clear();
    }
}
