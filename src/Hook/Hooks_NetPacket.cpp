#include "Hooks_NetPacket.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/AppTicket.h"
#include "Utils/Hash.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include "proto/steam_messages.pb.h"


namespace {

// ---- global state ----
constexpr uint32 kMaxBodySize   = 8092;  
constexpr uint32 kMaxHdrSize    = 1024;
constexpr uint32 kMaxPacketSize = 8 + kMaxHdrSize + kMaxBodySize;
constexpr int    kPacketPoolSize = 8;

bool   g_NeedReplace = false;
uint8  g_NewBody[kMaxBodySize];
uint32 g_cbNewBody   = 0;
uint8  g_PacketPool[kPacketPoolSize][kMaxPacketSize];
int    g_PacketPoolIdx = 0;

// ---- helper: extract body bytes from CNetPacket ----
inline bool UnpackPacket(CNetPacket* p, const uint8*& pBody, uint32& cbBody,
                          EMsg& eMsg, const uint8*& pHdr, uint32& cbHdr)
{
    if (!p || !p->m_pubData || p->m_cubData < 8) return false;
    eMsg  = static_cast<EMsg>(*(uint16*)(p->m_pubData));
    cbHdr = *(uint32*)(p->m_pubData + 4);
    uint32 off = 8 + cbHdr;
    if (off > p->m_cubData) return false;
    pHdr   = p->m_pubData + 8;
    pBody  = p->m_pubData + off;
    cbBody = p->m_cubData - off;
    return true;
}

// ---- helper: replace body bytes inside CNetPacket (ring-buffer pool) ----
inline void ReplaceBody(CNetPacket* p, const uint8* pNewBody, uint32 cbNewBody)
{
    LOG_NETPACKET_DEBUG("Replacing packet body with new body of size {}...", cbNewBody);
    uint32 hdrLen = 8 + *(uint32*)(p->m_pubData + 4);
    uint32 newSize = hdrLen + cbNewBody;
    if (newSize > sizeof(g_PacketPool[0])) return;

    uint8* buf = g_PacketPool[g_PacketPoolIdx];
    memcpy(buf, p->m_pubData, hdrLen);
    if (cbNewBody > 0)
        memcpy(buf + hdrLen, pNewBody, cbNewBody);
    p->m_pubData = buf;
    p->m_cubData = newSize;

    g_PacketPoolIdx = (g_PacketPoolIdx + 1) % kPacketPoolSize;
}

// ============================================================
// Response handlers
// ============================================================

void HandleEncryptedAppTicketResponse(const uint8* pBody, uint32 cbBody)
{
    LOG_NETPACKET_DEBUG("Handling CMsgClientRequestEncryptedAppTicketResponse...");
    // ---- decode response body ----
    CMsgClientRequestEncryptedAppTicketResponse resp = {};
    pb_istream_t stream = pb_istream_from_buffer(pBody, cbBody);
    if (!pb_decode(&stream, CMsgClientRequestEncryptedAppTicketResponse_fields, &resp)){
        LOG_NETPACKET_WARN("Failed to decode CMsgClientRequestEncryptedAppTicketResponse");
        return;
    }

    // Only intervene when the server returned failure and the app is configured.
    if (resp.eresult == k_EResultOK) return;
    if (!LuaConfig::HasDepot(resp.app_id)) return;

    // ---- get replacement EncryptedAppTicket from registry ----
    auto ticket = AppTicket::GetEncryptedTicketFromRegistry(resp.app_id);
    if (ticket.empty()) return;

    // ---- parse registry blob as EncryptedAppTicket ----
    EncryptedAppTicket newTicket = {};
    pb_istream_t ticketStream = pb_istream_from_buffer(ticket.data(), static_cast<uint32>(ticket.size()));
    if (!pb_decode(&ticketStream, EncryptedAppTicket_fields, &newTicket)){
        LOG_NETPACKET_WARN("Failed to decode EncryptedAppTicket");
        return;
    }

    // ---- replace nested sub-message (shallow struct copy) ----
    resp.encrypted_app_ticket = newTicket;
    resp.has_encrypted_app_ticket = true; // necessary to indicate presence of the new ticket
    resp.eresult = k_EResultOK;

    // ---- encode modified response ----
    pb_ostream_t outStream = pb_ostream_from_buffer(g_NewBody, sizeof(g_NewBody));
    if (!pb_encode(&outStream, CMsgClientRequestEncryptedAppTicketResponse_fields, &resp)){
        LOG_NETPACKET_WARN("Failed to encode modified CMsgClientRequestEncryptedAppTicketResponse");
        return;
    }

    g_cbNewBody = outStream.bytes_written;
    g_NeedReplace = true;
}

void HandleNotifyRunningApps(const uint8* pBody, uint32 cbBody)
{
    (void)pBody; (void)cbBody;
    LOG_NETPACKET_DEBUG("Blocking CFamilyGroupsClient_NotifyRunningApps_Notification...");
    g_cbNewBody = 0;
    g_NeedReplace = true;
}

void HandleSharedLibraryStopPlaying(const uint8* pBody, uint32 cbBody)
{
    (void)pBody; (void)cbBody;
    LOG_NETPACKET_DEBUG("Blocking CMsgClientSharedLibraryStopPlaying...");
    g_cbNewBody = 0;
    g_NeedReplace = true;
}

// ============================================================
// RespServiceJob — dispatch eMsg 147 by target_job_name
// ============================================================
void RespServiceJob(const char* targetJobName, const uint8* pBody, uint32 cbBody)
{
    switch (Fnv1aHash(targetJobName)) {
    case Fnv1aHash("CFamilyGroupsClient_NotifyRunningApps_Notification"):
        HandleNotifyRunningApps(pBody, cbBody);
        break;

    // ---- add new 147 service methods here ----
    // case HashJobName("Xxx"):
    //     HandleXxx(pBody, cbBody);
    //     break;
    }
}

// ============================================================
// RespJob — dispatch by eMsg / target_job_name
// ============================================================
void RespJob(EMsg eMsg, const uint8* pBody, uint32 cbBody,
             const uint8* pHdr, uint32 cbHdr)
{
    g_NeedReplace = false;
    if (cbBody < 1) return;
    
    LOG_NETPACKET_TRACE("Received eMsg {} (cbBody={}, cbHdr={})", static_cast<uint32>(eMsg), cbBody, cbHdr);

    switch (eMsg) {

    case k_EMsgServiceMethodResponse: {     // 147 — dispatch by target_job_name
        CMsgProtoBufHeader hdr = CMsgProtoBufHeader_init_zero;
        pb_istream_t hdrStream = pb_istream_from_buffer(pHdr, cbHdr);
        if (pb_decode(&hdrStream, CMsgProtoBufHeader_fields, &hdr) && hdr.has_target_job_name)
            RespServiceJob(hdr.target_job_name, pBody, cbBody);
        break;
    }

    case k_EMsgClientSharedLibraryStopPlaying:     // 9406
        HandleSharedLibraryStopPlaying(pBody, cbBody);
        break;
        
    // May be we should hanle it in IPC layer? 
    case k_EMsgClientRequestEncryptedAppTicketResponse:     // 5527
        HandleEncryptedAppTicketResponse(pBody, cbBody);
        break;

    default:
        break;
    }
}

// ============================================================
// RecvMultiPkt — intercept all received network packets
// ============================================================
HOOK_FUNC(RecvMultiPkt, void*, void* pThis, CNetPacket* pPacket)
{
    const uint8* pBody = nullptr;
    uint32 cbBody = 0;
    EMsg eMsg;
    const uint8* pHdr = nullptr;
    uint32 cbHdr = 0;

    if (UnpackPacket(pPacket, pBody, cbBody, eMsg, pHdr, cbHdr)) {
        RespJob(eMsg, pBody, cbBody, pHdr, cbHdr);
        if (g_NeedReplace) {
            ReplaceBody(pPacket, g_NewBody, g_cbNewBody);
        }
    }

    return oRecvMultiPkt(pThis, pPacket);
}

} // namespace


namespace Hooks_NetPacket {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_D(RecvMultiPkt);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(RecvMultiPkt);
        UNHOOK_END();
    }
}
