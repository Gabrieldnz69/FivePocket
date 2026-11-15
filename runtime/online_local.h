#pragma once
// online_local.h (Update 1; lane upd1-spoof/session-stubs, 2026-11-06; built on lane upd1-online/local-session-stubs).
// PROTOTYPE, NOT IN ANY BUILD. Patch target: v963.
//
// Local "online" answers for the locally signed-in profile, behind the boot switch files/private-trace/diag-online-local
// (off by default) AND the Sandbox play mode (play-mode latch Sandbox or SandboxFresh, the same test as
// recomp::sandbox::active()). In Story mode, or with the switch off, every entry point returns Kind::NotMine and the caller
// keeps today's behaviour exactly (registry path or guarded stop). Owner decision 2026-11-06 ("just remove the script or
// spoof it"): the game's own online content may run locally in Sandbox. Nothing here sends anything anywhere: no host
// socket, no DNS, no HTTP, no file; every answer is computed in-process (tests/static_no_socket.py).
//
// Pure: no guest memory except through GuestMemory, no platform calls. The glue (online_local_glue.h +
// online_local_registry.inc) supplies guest memory, the per-thread last-error field (KTHREAD+0x160), the XOVERLAPPED
// completion (event signal, as guest_saves_dispatch.inc does it) and the log.
//
// CORPUS CORRECTION (this lane): v963 links 374 recompiled chunks, 339 from .attempts/v963/src and 35 from the validated
// corpus (build/link.d). The first lane scanned only the attempt's own chunks and so missed the XDK network/session library
// in chunks 336+ (0x836AB0xx-0x836AC3xx). In the full link set: XamSessionCreateHandle 1 call site, XamSessionRefObjByHandle
// 13, XMsgStartIORequest 27 (14 of them XGI session messages), NetDll_sendto/inet_addr/XNetGetTitleXnAddr/XNetRegisterKey/
// XNetXnAddrToInAddr/... one XDK wrapper each (NOTES.md §1).
//
// Already in the standing lineage (v963 registry + guest_saves_dispatch.inc) and NOT duplicated: user 0 signed in locally
// (state 1), offline XUID, gamertag, sign-in info, profile settings, XGI 0xB0006/7/8, saved-game content under the save root
// (<base>/sandbox-mode/g<N> in Sandbox), XamUserCheckPrivilege = 1245 with *result = 0 (the one privilege-254 site accepts
// it for a locally signed-in user). The signed-online byte 0x83B13BA7 is the signin-spoof lane's, not this file's.
//
// Stubs (S-numbers kept from the first lane; S1 retired):
//   S2  XGI 0xB0041, two record shapes (v989, lead-0607 session.md; the XDK wrappers, validated-ppc ppc_recomp.329.cpp):
//       XUserGetContext sub_8363DCA0: {user, 0, 0(8), 0, pContext at +20, 0}; XUserGetProperty sub_8363DD28: {user, 0, 0(8),
//       pcb at +16, 0, pProperty at +24} (the wrapper requires *pcb >= 24 and points a string / binary value at pProperty + 24).
//       Both only from the read step sub_83291318. Answer: the value a prior 0xB0006 set, else 0, always SUCCESS (records
//       0x800B / 0x800A must succeed for step 0); synchronous or completed through the XOVERLAPPED (997). v968..v988 read the
//       context pointer at +16 (wrong: a fault for GetContext, a write to pcb + 4 for GetProperty).
//   S3  NetDll_XNetGetEthernetLinkStatus -> ACTIVE|100MBPS|FULL_DUPLEX.
//   S4  NetDll_XNetGetConnectStatus -> CONNECTED for a loopback IN_ADDR, IDLE otherwise.
//   S5  Loopback sockets (17 imports incl. sendto and inet_addr): an in-process datagram/stream fabric. Only 127.0.0.0/8,
//       INADDR_ANY binds and the limited broadcast stay inside the process; a datagram to any other address is DROPPED
//       (counted, never sent); a stream connect to any other address fails WSAENETUNREACH. Blocking reads on an empty
//       queue are REFUSED (the glue stops with the name) rather than blocking a guest thread forever.
//       DEVIATION (documented, tested): select() with a timeout and nothing ready returns 0 after a host wait of at most
//       SelectWaitCapMicros (4 ms), an EARLY timeout (the bridge cannot park a guest thread; callers accept 0).
//       inet_addr parses a dotted quad only (no name resolution).
//   S6  XHttp (13 imports) fail clean: handles are created as WinHTTP does offline; SendRequest fails with
//       ERROR_WINHTTP_CANNOT_CONNECT; nothing leaves the process.
//   S7  Local XGI sessions, WIRED (derived from the XDK wrappers in the game, NOTES.md §2): XamSessionCreateHandle,
//       XamSessionRefObjByHandle, ObDereferenceObject / NtClose of those objects and handles, and the 12 XGI (app 0xFB)
//       session messages the code sends: 0xB0010 create (host only), 0xB0011 delete, 0xB0012 join (local user 0 only;
//       remote XUIDs fail: no peers exist), 0xB0013 leave, 0xB0014 start, 0xB0015 end, 0xB0018 modify, 0xB001C search (no
//       results), and fail-clean answers for 0xB001A arbitration, 0xB001D details, 0xB001E migrate, 0xB0025 write stats.
//   S8  XNet local addressing: XNetGetTitleXnAddr -> ETHERNET|STATIC with IN_ADDR 127.0.0.1, no online address, a locally
//       administered MAC (never ONLINE); key register/unregister bookkeeping (4 keys); XnAddr<->InAddr only for the title's
//       own XNADDR and loopback; XNetConnect / UnregisterInAddr for loopback; ServerToInAddr / InAddrToServer fail (no
//       Live server); QosListen accepted (no peer can probe).
// Never: a Live sign-in state (2), an online XUID, tokens, XAuth, machine IDs, console signatures, DNS, Live enumerations,
// XnpLogonGetStatus. Those stay guarded stops so a device run names them.
//
// Cost with the service ON: Service::claims() is a name prefilter (prefix compare, plus a 16-bit tag compare of r3 for
// ObDereferenceObject / NtClose) run before any lock; only claimed names take the service mutex.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

namespace recomp::online_local {

struct GuestMemory {
    virtual ~GuestMemory() = default;
    virtual bool read(std::uint32_t at, void* out, std::uint32_t n) const = 0;
    virtual bool write(std::uint32_t at, const void* in, std::uint32_t n) = 0;
};
inline bool rd32(const GuestMemory& m, std::uint32_t at, std::uint32_t& v) {
    std::uint8_t b[4]; if (!m.read(at, b, 4)) return false;
    v = std::uint32_t(b[0]) << 24 | std::uint32_t(b[1]) << 16 | std::uint32_t(b[2]) << 8 | b[3]; return true;
}
inline bool rd16(const GuestMemory& m, std::uint32_t at, std::uint16_t& v) {
    std::uint8_t b[2]; if (!m.read(at, b, 2)) return false; v = std::uint16_t(b[0] << 8 | b[1]); return true;
}
inline bool wr32(GuestMemory& m, std::uint32_t at, std::uint32_t v) {
    const std::uint8_t b[4]{std::uint8_t(v >> 24), std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v)};
    return m.write(at, b, 4);
}
inline bool wr16(GuestMemory& m, std::uint32_t at, std::uint16_t v) {
    const std::uint8_t b[2]{std::uint8_t(v >> 8), std::uint8_t(v)}; return m.write(at, b, 2);
}

enum class Kind : std::uint8_t { NotMine, Handled, Refuse, MemoryFault };
struct Result {
    Kind kind = Kind::NotMine;
    std::uint32_t ret = 0;             // r3
    bool setLastError = false;         // write lastError to the calling thread's last-error field
    std::uint32_t lastError = 0;
    std::uint32_t waitMicros = 0;      // select with a timeout and nothing ready: the glue may wait this long (capped)
    // XOVERLAPPED completion the glue performs (result, length, extended error; event signalled), as the saves path does.
    bool complete = false;
    std::uint32_t overlapped = 0, ovlResult = 0, ovlLength = 0, ovlExtended = 0;
    const char* why = "";
};
struct Call { std::array<std::uint32_t, 8> a{}; }; // r3..r10 (low 32 bits)

enum class Stub : std::uint8_t { XgiGetContext, LinkStatus, ConnectStatus, Sockets, Http, Session, XnetAddress, Count };
inline constexpr const char* StubName[]{"S2-xgi-get-context", "S3-link-status", "S4-connect-status",
                                        "S5-loopback-sockets", "S6-xhttp-fail-clean", "S7-local-session", "S8-xnet-local-address"};

// Constants (public Xbox 360 / Winsock / WinHTTP values).
inline constexpr std::uint32_t LinkActive = 0x01, Link100Mbps = 0x02, LinkFullDuplex = 0x08;
inline constexpr std::uint32_t ConnectIdle = 0, ConnectConnected = 2;
inline constexpr std::uint32_t SocketError = 0xFFFFFFFFu, InvalidSocket = 0xFFFFFFFFu;
inline constexpr std::uint32_t WsaEBadf = 10009, WsaEFault = 10014, WsaEInval = 10022, WsaEWouldBlock = 10035,
    WsaENotSock = 10038, WsaEMsgSize = 10040, WsaEProtoNoSupport = 10043, WsaESockTNoSupport = 10044,
    WsaEOpNotSupp = 10045, WsaEAfNoSupport = 10047, WsaEAddrInUse = 10048, WsaENetUnreach = 10051,
    WsaEIsConn = 10056, WsaENotConn = 10057, WsaEConnRefused = 10061, WsaEShutdown = 10058;
inline constexpr std::uint32_t AfInet = 2, SockStream = 1, SockDgram = 2, IpprotoTcp = 6, IpprotoUdp = 17, IpprotoVdp = 254;
inline constexpr std::uint32_t FionBio = 0x8004667E, FionRead = 0x4004667F;
inline constexpr std::uint32_t XEFail = 0x80004005u;
inline constexpr std::uint32_t HttpCannotConnect = 12029, HttpIncorrectHandleState = 12019, HttpInvalidHandle = 6;
inline constexpr std::uint32_t MaxSockets = 64, MaxQueuedDatagrams = 64, MaxDatagram = 65507, MaxStreamBytes = 1u << 20;
inline constexpr std::uint32_t SelectWaitCapMicros = 4000;
// S7 (handles and objects are tags no registry kernel handle can take: kernel handles stay <= 0x7FFFFFFC).
inline constexpr std::uint32_t SessionHandleTag = 0xF0510000u, SessionObjectTag = 0xF0520000u, MaxSessions = 8;
inline constexpr std::uint32_t IoPending = 997, ErrorFunctionFailed = 1627, ErrorInvalidHandle = 6, StatusSuccess = 0;
inline constexpr std::uint32_t XsessionSinglePlayerFlags = 0x706;   // v999: sub_83271798 mode 0 with gamer-record bit 0x10
inline constexpr std::uint32_t XsessionCreateHost = 0x1;            // XSESSION_CREATE_HOST (public); the wrapper validates it
inline constexpr std::uint32_t XsessionInfoBytes = 60;              // XNKID 8 + XNADDR 36 + XNKEY 16
// XGI session messages the game's XDK wrappers send (app 0xFB; wrapper address, buffer length; NOTES.md §2.2).
inline constexpr std::uint32_t MsgCreate = 0xB0010, MsgDelete = 0xB0011, MsgJoin = 0xB0012, MsgLeave = 0xB0013, MsgStart = 0xB0014,
    MsgEnd = 0xB0015, MsgModify = 0xB0018, MsgArbitration = 0xB001A, MsgSearch = 0xB001C, MsgDetails = 0xB001D,
    MsgMigrate = 0xB001E, MsgWriteStats = 0xB0025;
inline constexpr bool isSessionMessage(std::uint32_t m) noexcept {
    return m == MsgCreate || m == MsgDelete || m == MsgJoin || m == MsgLeave || m == MsgStart || m == MsgEnd || m == MsgModify
        || m == MsgArbitration || m == MsgSearch || m == MsgDetails || m == MsgMigrate || m == MsgWriteStats;
}
// S8
inline constexpr std::uint32_t XnaddrEthernet = 0x02, XnaddrStatic = 0x04; // XNET_GET_XNADDR_* (public); never ONLINE (0x80)
inline constexpr std::uint32_t MaxKeys = 4, WsaEAlready = 10037, WsaENoBufs = 10055, WsaEHostUnreach = 10065;
inline constexpr std::uint32_t Loopback = 0x7F000001u, InaddrNone = 0xFFFFFFFFu;
inline constexpr std::uint8_t OwnMac[6]{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}; // locally administered, not a real interface

inline bool loopbackAddress(std::uint32_t a) { return (a >> 24) == 127; }

class Service {
public:
    using LogFn = void (*)(const char*);
    // switchOn: files/private-trace/diag-online-local present. sandbox: recomp::sandbox::active(). Both are latched by
    // the glue once per process; the service never re-reads them.
    Service(LogFn log, bool switchOn, bool sandbox) noexcept : log_(log), enabled_(switchOn && sandbox) {}
    bool enabled() const noexcept { return enabled_; }
    std::uint32_t firstUseMask() const noexcept { return firstUse_; }
    std::uint64_t droppedDatagrams() const noexcept { return dropped_; }

    // Prefilter, run before any lock (the registry glue calls it first). True only for what this service can answer: every
    // NetDll_ import (registry-side XNetGetEthernetLinkStatus / XNetGetTitleXnAddr and the bridged ones), XMsgStartIORequest,
    // the two XamSession imports, and ObDereferenceObject / NtClose only for this service's own object / handle tags.
    static bool claims(std::string_view import, std::uint32_t r3) noexcept {
        if (import.substr(0, 14) == "__imp__NetDll_" || import == "__imp__XMsgStartIORequest") return true;
        if (import.substr(0, 17) == "__imp__XamSession") return true;
        if ((r3 & 0xFFFF0000u) == SessionObjectTag && import == "__imp__ObDereferenceObject") return true;
        if ((r3 & 0xFFFF0000u) == SessionHandleTag && import == "__imp__NtClose") return true;
        return false;
    }

    // Registry-side entry (inside dispatchUnderRegistryLock, before guest_saves_dispatch.inc).
    Result dispatchRegistry(std::string_view import, const Call& c, GuestMemory& m) {
        if (!enabled_ || !claims(import, c.a[0])) return {};
        if (import == "__imp__XMsgStartIORequest" && c.a[0] != 0xFB) return {}; // only XGI; decided before the lock
        std::lock_guard l(mutex_);
        if (import == "__imp__XMsgStartIORequest") return xgi(c, m);
        if (import == "__imp__NetDll_XNetGetEthernetLinkStatus") {
            if (c.a[0] != 1) return {};
            first(Stub::LinkStatus);
            return ok(LinkActive | Link100Mbps | LinkFullDuplex, "loopback link up (no host interface used)");
        }
        if (import == "__imp__NetDll_XNetGetTitleXnAddr") {
            if (c.a[0] != 1) return {};
            first(Stub::XnetAddress);
            std::uint8_t a[36]; ownXnAddr(a);
            if (!m.write(c.a[1], a, 36)) return fault("XNADDR output is not a bounded writable 36-byte record");
            return ok(XnaddrEthernet | XnaddrStatic, "local title address 127.0.0.1 (ETHERNET|STATIC, never ONLINE)");
        }
        if (import == "__imp__ObDereferenceObject") {
            auto* s = byObject(c.a[0]);
            if (!s) return {};
            if (s->refs == 0) return refuse("unbalanced local-session object dereference");
            --s->refs; reap(); return ok(0, "released a local-session object reference");
        }
        if (import == "__imp__NtClose") {
            auto* s = byHandle(c.a[0]);
            if (!s) return {};
            s->handleOpen = false; reap(); return ok(StatusSuccess, "closed a local-session handle");
        }
        return {};
    }
    // Bridge-side entry (imports that are guarded stops today). NotMine = keep the guarded stop.
    Result dispatchBridge(std::string_view import, const Call& c, GuestMemory& m) {
        if (!enabled_) return {};
        if (import == "__imp__XamSessionCreateHandle" || import == "__imp__XamSessionRefObjByHandle") {
            std::lock_guard l(mutex_);
            return sessionImport(import, c, m);
        }
        if (import.substr(0, 14) != "__imp__NetDll_") return {};
        std::lock_guard l(mutex_);
        if (import == "__imp__NetDll_XNetGetConnectStatus") {
            if (c.a[0] != 1) return {};
            first(Stub::ConnectStatus);
            // IN_ADDR passed by value (r4).
            return ok(loopbackAddress(c.a[1]) ? ConnectConnected : ConnectIdle, "loopback connect status");
        }
        if (import.find("XHttp") != std::string_view::npos) return http(import, c, m);
        if (import.substr(0, 18) == "__imp__NetDll_XNet") return xnet(import.substr(14), c, m);
        if (import == "__imp__NetDll___WSAFDIsSet") { first(Stub::Sockets); return fdIsSet(c, m); }
        if (import == "__imp__NetDll_inet_addr") { first(Stub::Sockets); return inetAddr(c, m); }
        return socketCall(import.substr(14), c, m);
    }

    // ---- S7 inspection (tests and the log) ------------------------------------------------------------------------
    enum class SessionState : std::uint8_t { Allocated, Created, Started, Ended, Deleted };
    struct LocalSession {
        std::uint32_t handle = 0, object = 0, refs = 0; bool handleOpen = true;
        bool createRef = false; // the create message hands its object reference to the session service until delete
        SessionState state = SessionState::Allocated;
        std::uint32_t flags = 0, hostUser = 0, publicSlots = 0, privateSlots = 0;
        std::vector<std::uint32_t> members; // local user indices (only user 0 exists)
    };
    const LocalSession* session(std::uint32_t handle) const {
        for (const auto& [id, s] : sessions_) if (s.handle == handle) return &s;
        return nullptr;
    }
    std::size_t liveSessions() const noexcept { return sessions_.size(); }
    std::size_t registeredKeys() const noexcept { return keys_.size(); }

private:
    struct Datagram { std::uint32_t fromAddr; std::uint16_t fromPort; std::vector<std::uint8_t> bytes; };
    struct Socket {
        std::uint32_t type = 0, protocol = 0; bool nonBlocking = false, bound = false, listening = false, connected = false;
        bool shutRead = false, shutWrite = false;
        std::uint32_t addr = 0; std::uint16_t port = 0;           // bound
        std::uint32_t peerAddr = 0; std::uint16_t peerPort = 0;   // connected
        std::deque<Datagram> datagrams; std::deque<std::uint8_t> stream; std::deque<std::uint32_t> pendingAccept;
        std::uint32_t peer = 0;                                   // stream peer socket handle
    };
    enum class HttpKind : std::uint8_t { Session, Connect, Request };
    struct HttpHandle { HttpKind kind; std::uint32_t callback = 0; };

    LogFn log_; const bool enabled_;
    std::mutex mutex_;
    std::uint32_t firstUse_ = 0;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> contexts_; // (user, id) -> value
    std::map<std::uint32_t, Socket> sockets_; std::uint32_t nextSocket_ = 0x100; std::uint16_t nextEphemeral_ = 49152;
    std::map<std::uint32_t, HttpHandle> http_; std::uint32_t nextHttp_ = 0;
    std::map<std::uint32_t, LocalSession> sessions_; std::uint32_t nextSession_ = 0; // id (low 16 bits of handle/object)
    std::vector<std::array<std::uint8_t, 24>> keys_;  // registered XNKID (8) + XNKEY (16)
    std::uint64_t dropped_ = 0; bool droppedLogged_ = false;

    void first(Stub s) {
        const auto bit = 1u << unsigned(s);
        if (firstUse_ & bit) return;
        firstUse_ |= bit;
        if (log_) { char line[96]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL first-use stub=%s", StubName[unsigned(s)]); log_(line); }
    }
    static Result ok(std::uint32_t r, const char* why) { Result x; x.kind = Kind::Handled; x.ret = r; x.why = why; return x; }
    static Result fail(std::uint32_t r, std::uint32_t err, const char* why) {
        Result x; x.kind = Kind::Handled; x.ret = r; x.setLastError = true; x.lastError = err; x.why = why; return x;
    }
    static Result refuse(const char* why) { Result x; x.kind = Kind::Refuse; x.why = why; return x; }
    static Result fault(const char* why) { Result x; x.kind = Kind::MemoryFault; x.why = why; return x; }
    LocalSession* byHandle(std::uint32_t h) {
        if ((h & 0xFFFF0000u) != SessionHandleTag) return nullptr;
        auto it = sessions_.find(h & 0xFFFFu); return it == sessions_.end() || !it->second.handleOpen ? nullptr : &it->second;
    }
    LocalSession* byObject(std::uint32_t o) {
        if ((o & 0xFFFF0000u) != SessionObjectTag) return nullptr;
        auto it = sessions_.find(o & 0xFFFFu); return it == sessions_.end() ? nullptr : &it->second;
    }
    // A session is freed once its handle is closed and no object reference is held (the kernel object rule).
    void reap() {
        for (auto it = sessions_.begin(); it != sessions_.end();) it = (!it->second.handleOpen && it->second.refs == 0) ? sessions_.erase(it) : std::next(it);
    }
    static std::uint32_t hresult(std::uint32_t win32) { return win32 == 0 ? 0u : (0x80070000u | (win32 & 0xFFFFu)); }
    // XGI result delivery, matching the XDK wrappers (sub_836AB6E8 & co.): synchronous (no XOVERLAPPED): a negative return
    // makes the wrapper answer 1627 and a non-negative one makes it read the thread's last error (sub_823F4E70 reads
    // KTHREAD+0x160): so success = 0 with last error 0, failure = HRESULT_FROM_WIN32. Asynchronous: ERROR_IO_PENDING and
    // the XOVERLAPPED completed now (result, length 0, extended HRESULT), which the glue writes and signals.
    static Result xgiDone(std::uint32_t overlapped, std::uint32_t win32, const char* why) {
        Result x; x.kind = Kind::Handled; x.why = why;
        if (overlapped == 0) {
            x.ret = hresult(win32);
            if (win32 == 0) { x.setLastError = true; x.lastError = 0; }
            return x;
        }
        x.ret = IoPending; x.complete = true; x.overlapped = overlapped; x.ovlResult = win32; x.ovlExtended = hresult(win32);
        return x;
    }
    static void ownXnAddr(std::uint8_t (&a)[36]) {
        std::memset(a, 0, sizeof a);
        a[0] = 127; a[3] = 1;                               // ina 127.0.0.1; inaOnline 0, wPortOnline 0
        std::memcpy(a + 10, OwnMac, 6);                     // abEnet; abOnline (20 bytes) zero: no online identity
    }

    // ---- S2 ----
    Result xgi(const Call& c, GuestMemory& m) {
        const auto message = c.a[1], overlapped = c.a[2], buffer = c.a[3], length = c.a[4];
        if (message == 0x000B0006) { // observe XUserSetContext {user, ?, 0(8), id, value}; the saves path answers it
            std::uint32_t user = 0, id = 0, value = 0;
            if ((length == 0 || length == 0x18) && rd32(m, buffer, user) && rd32(m, buffer + 16, id) && rd32(m, buffer + 20, value))
                contexts_[{user, id}] = value;
            return {};
        }
        if (isSessionMessage(message)) return xgiSession(c, m); // only the 12 the code sends; others stay today's (named stop)
        if (message != 0x000B0041) return {};
        std::uint32_t user = 0, f16 = 0, f20 = 0, f24 = 0;
        if ((length != 0 && length != 0x20) || !rd32(m, buffer, user) || !rd32(m, buffer + 16, f16) || !rd32(m, buffer + 20, f20)
                || !rd32(m, buffer + 24, f24))
            return fault("XUserGetContext / XUserGetProperty request is not a bounded 32-byte record");
        if (f20 != 0 && f16 == 0 && f24 == 0) { // XUserGetContext: pContext at +20 = XUSER_CONTEXT {id, value}
            std::uint32_t id = 0;
            if (!rd32(m, f20, id)) return fault("XUserGetContext context record is not readable");
            first(Stub::XgiGetContext);
            const auto it = contexts_.find({user, id});
            if (!wr32(m, f20 + 4, it == contexts_.end() ? 0 : it->second)) return fault("context value is not writable");
            return xgiDone(overlapped, 0, it == contexts_.end() ? "context never set: 0 (success)" : "context value the title set earlier");
        }
        if (f16 != 0 && f20 == 0 && f24 != 0) { // XUserGetProperty: pcb at +16, pProperty at +24 = XUSER_PROPERTY {id, XUSER_DATA}
            std::uint32_t cb = 0, id = 0;
            if (!rd32(m, f16, cb) || cb < 24 || !rd32(m, f24, id)) return fault("XUserGetProperty size / property record is not readable");
            first(Stub::XgiGetContext);
            // XUSER_DATA at +8: type byte, value union at +16 (8 bytes); type = the id's top nibble (the wrapper's own test).
            // Unset = the type with a zero value: numbers 0, a string / binary of 0 bytes (an empty string: one zero wchar).
            const std::uint32_t type = (id >> 28) & 0xFu;
            std::uint32_t data = 0;
            if (!wr32(m, f24 + 8, type << 24) || !wr32(m, f24 + 12, 0)) return fault("property type is not writable");
            if (type == 4 || type == 6) {
                if (!rd32(m, f24 + 20, data)) return fault("property data pointer is not readable");
                const bool str = type == 4 && data != 0 && cb >= 26;
                if (!wr32(m, f24 + 16, str ? 2 : 0)) return fault("property size is not writable");
                if (str && !wr16(m, data, 0)) return fault("property string is not writable");
            } else if (!wr32(m, f24 + 16, 0) || !wr32(m, f24 + 20, 0)) return fault("property value is not writable");
            return xgiDone(overlapped, 0, "property never set: its type with value 0 (success)");
        }
        return fault("0xB0041 record is neither XUserGetContext (+20) nor XUserGetProperty (+16 / +24)");
    }
    // ---- S7 ----
    Result sessionImport(std::string_view import, const Call& c, GuestMemory& m) {
        first(Stub::Session);
        if (import == "__imp__XamSessionCreateHandle") { // (PHANDLE out) -> 0
            if (sessions_.size() >= MaxSessions) return ok(ErrorFunctionFailed, "local-session table full (8)");
            std::uint32_t id = 0;
            for (int i = 0; i < 0x10000 && !id; ++i) { nextSession_ = (nextSession_ + 1) & 0xFFFFu; if (nextSession_ && !sessions_.count(nextSession_)) id = nextSession_; }
            if (!id) return ok(ErrorFunctionFailed, "no free local-session id");
            if (!wr32(m, c.a[0], SessionHandleTag | id)) return fault("session handle output is not writable guest memory");
            LocalSession s; s.handle = SessionHandleTag | id; s.object = SessionObjectTag | id; sessions_[id] = s;
            return ok(0, "local session handle (no XAM session service)");
        }
        // XamSessionRefObjByHandle(HANDLE, PVOID* out) -> 0. The object is a tag: the XDK wrappers only store it in the XGI
        // buffer and pass it back to ObDereferenceObject (tests/check_wrappers.py checks all 13 call sites).
        auto* s = byHandle(c.a[0]);
        if (!s) return ok(ErrorInvalidHandle, "not a local-session handle");
        if (!wr32(m, c.a[1], s->object)) return fault("session object output is not writable guest memory");
        ++s->refs; return ok(0, "referenced a local-session object");
    }
    Result xgiSession(const Call& c, GuestMemory& m) {
        const auto message = c.a[1], overlapped = c.a[2], buffer = c.a[3], length = c.a[4];
        first(Stub::Session);
        const auto field = [&](std::uint32_t off, std::uint32_t& v) { return rd32(m, buffer + off, v); };
        const auto need = [&](std::uint32_t len) { return length == len; };
        if (message == MsgSearch) { // {index, user, maxResults, u16 props, u16 contexts, props*, contexts*, cbResults, results*, ?}
            std::uint32_t results = 0;
            if (!need(36) || !field(28, results)) return fault("session search request is not a bounded 36-byte record");
            const std::uint8_t zero[8]{};
            if (!results || !m.write(results, zero, 8)) return fault("session search results are not writable guest memory");
            return xgiDone(overlapped, 0, "session search: no other session exists locally (0 results)");
        }
        if (message == MsgArbitration) return xgiDone(overlapped, ErrorFunctionFailed, "arbitration is a Live service: not available locally");
        if (message == MsgDetails) return xgiDone(overlapped, ErrorFunctionFailed, "session details unavailable (the wrapper's own pre-2.0.52457 answer)");
        if (message == MsgMigrate) return xgiDone(overlapped, ErrorFunctionFailed, "host migration needs remote peers: none exist");
        if (message == MsgWriteStats) return xgiDone(overlapped, ErrorFunctionFailed, "leaderboard stats are a Live service: not written");
        std::uint32_t object = 0;
        if (!field(0, object)) return fault("session request is not readable guest memory");
        auto* s = byObject(object);
        if (!s || s->refs == 0) return xgiDone(overlapped, ErrorInvalidHandle, "not a referenced local-session object");
        if (s->state == SessionState::Deleted) return xgiDone(overlapped, ErrorFunctionFailed, "local session already deleted");
        if (message == MsgCreate) { // {obj, flags, publicSlots, privateSlots, user, sessionInfo*, nonce*}
            std::uint32_t flags = 0, pub = 0, priv = 0, user = 0, info = 0, nonce = 0;
            if (!need(28) || !field(4, flags) || !field(8, pub) || !field(12, priv) || !field(16, user) || !field(20, info) || !field(24, nonce))
                return fault("session create request is not a bounded 28-byte record");
            if (s->state != SessionState::Allocated) return xgiDone(overlapped, ErrorFunctionFailed, "session object already created");
            // The create wrapper references the object and never dereferences it (the service keeps it): on a refusal the
            // reference is dropped here, as the service would, so the closed handle can be reaped.
            const auto refuseCreate = [&](const char* why) { --s->refs; return xgiDone(overlapped, ErrorFunctionFailed, why); };
            // v999 (L1, lead-0607 session.md corrected proposal item 3): the offline host (task mode 0) creates WITHOUT the HOST bit;
            // sub_83271798 builds exactly 0x706 or 0 for mode 0 (no peer 0x20, matchmaking 0x08 or arbitration 0x10). Only those two
            // single-player shapes are accepted without HOST; anything else is still a remote join.
            const bool singlePlayer = flags == XsessionSinglePlayerFlags || flags == 0;
            if (!(flags & XsessionCreateHost) && !singlePlayer) return refuseCreate("joining a remote session: none exists locally");
            if (user != 0) return refuseCreate("only user 0 is signed in (locally)");
            // XSESSION_INFO for the host: a local session id (XNKID class bits 0: not an online id), the title's own XNADDR and
            // a key that protects nothing (no traffic leaves the process). Deterministic from the session id; not a secret.
            std::uint8_t out[XsessionInfoBytes]{};
            const std::uint32_t id = s->object & 0xFFFFu;
            out[0] = 0x00; out[4] = std::uint8_t(id >> 8); out[5] = std::uint8_t(id); out[7] = 0x4C; // "L"ocal
            std::uint8_t a[36]; ownXnAddr(a); std::memcpy(out + 8, a, 36);
            for (int i = 0; i < 16; ++i) out[44 + i] = std::uint8_t(0x5A ^ (id + i));
            std::uint8_t n[8]{0, 0, 0, 0, 0, 0, std::uint8_t(id >> 8), std::uint8_t(id)};
            if (!m.write(info, out, XsessionInfoBytes) || !m.write(nonce, n, 8)) return fault("session info / nonce output is not writable guest memory");
            s->state = SessionState::Created; s->flags = flags; s->hostUser = user; s->publicSlots = pub; s->privateSlots = priv; s->createRef = true;
            return xgiDone(overlapped, 0, (flags & XsessionCreateHost) ? "local session created (host, user 0)" : "local session created (single-player, user 0)");
        }
        if (message == MsgDelete) { // {obj, 0, ?, ?}
            if (!need(16)) return fault("session delete request is not a 16-byte record");
            s->state = SessionState::Deleted; s->members.clear();
            if (s->createRef) { s->createRef = false; --s->refs; } // the service lets go of the create reference
            return xgiDone(overlapped, 0, "local session deleted");
        }
        if (s->state == SessionState::Allocated) return xgiDone(overlapped, ErrorFunctionFailed, "session object was never created");
        if (message == MsgJoin || message == MsgLeave) { // {obj, count, xuids* (remote), indices* (local), privateSlots* | 0}
            std::uint32_t count = 0, xuids = 0, indices = 0;
            if (!need(20) || !field(4, count) || !field(8, xuids) || !field(12, indices)) return fault("session join/leave request is not a bounded 20-byte record");
            if (count > 4 + 32) return xgiDone(overlapped, ErrorFunctionFailed, "member count out of range");
            if (xuids && count) return xgiDone(overlapped, ErrorFunctionFailed, "remote members: no peers exist locally");
            std::vector<std::uint32_t> users(count);
            for (std::uint32_t i = 0; i < count; ++i) if (!rd32(m, indices + 4 * i, users[i])) return fault("local member list is not readable");
            for (auto u : users) if (u != 0) return xgiDone(overlapped, ErrorFunctionFailed, "only user 0 is signed in (locally)");
            if (message == MsgJoin) {
                for (auto u : users) if (std::find(s->members.begin(), s->members.end(), u) == s->members.end()) {
                    if (s->members.size() >= s->publicSlots + s->privateSlots) return xgiDone(overlapped, ErrorFunctionFailed, "session is full");
                    s->members.push_back(u);
                }
                return xgiDone(overlapped, 0, "local user joined the local session");
            }
            for (auto u : users) {
                auto it = std::find(s->members.begin(), s->members.end(), u);
                if (it == s->members.end()) return xgiDone(overlapped, ErrorFunctionFailed, "user is not a member");
                s->members.erase(it);
            }
            return xgiDone(overlapped, 0, "local user left the local session");
        }
        if (message == MsgStart) {
            if (!need(16)) return fault("session start request is not a 16-byte record");
            if (s->state != SessionState::Created && s->state != SessionState::Ended) return xgiDone(overlapped, ErrorFunctionFailed, "session not in a startable state");
            s->state = SessionState::Started; return xgiDone(overlapped, 0, "local session started");
        }
        if (message == MsgEnd) {
            if (!need(16)) return fault("session end request is not a 16-byte record");
            if (s->state != SessionState::Started) return xgiDone(overlapped, ErrorFunctionFailed, "session not started");
            s->state = SessionState::Ended; return xgiDone(overlapped, 0, "local session ended");
        }
        if (message == MsgModify) { // {obj, flags, publicSlots, privateSlots}
            std::uint32_t flags = 0, pub = 0, priv = 0;
            if (!need(16) || !field(4, flags) || !field(8, pub) || !field(12, priv)) return fault("session modify request is not a bounded 16-byte record");
            if (pub + priv < s->members.size()) return xgiDone(overlapped, ErrorFunctionFailed, "fewer slots than members");
            s->flags = (s->flags & XsessionCreateHost) | (flags & ~XsessionCreateHost); s->publicSlots = pub; s->privateSlots = priv;
            return xgiDone(overlapped, 0, "local session modified");
        }
        return {}; // another 0xB00xx message: not one the code sends; today's path (rejected, named)
    }
    // ---- S8 ----
    bool keyIndex(const GuestMemory& m, std::uint32_t xnkid, std::size_t& index) {
        std::uint8_t id[8]; if (!m.read(xnkid, id, 8)) return false;
        for (index = 0; index < keys_.size(); ++index) if (std::memcmp(keys_[index].data(), id, 8) == 0) return true;
        return false;
    }
    Result xnet(std::string_view name, const Call& c, GuestMemory& m) {
        if (c.a[0] != 1) return {}; // XNCALLER_TITLE only
        static constexpr std::string_view Handled[]{"XNetRegisterKey", "XNetUnregisterKey", "XNetXnAddrToInAddr", "XNetInAddrToXnAddr",
            "XNetUnregisterInAddr", "XNetConnect", "XNetServerToInAddr", "XNetInAddrToServer", "XNetQosListen"};
        bool known = false; for (auto h : Handled) known = known || h == name;
        if (!known) return {};
        first(Stub::XnetAddress);
        if (name == "XNetRegisterKey") { // (caller, XNKID*, XNKEY*) -> 0 | error
            std::array<std::uint8_t, 24> k{};
            if (!m.read(c.a[1], k.data(), 8) || !m.read(c.a[2], k.data() + 8, 16)) return ok(WsaEFault, "key arguments not readable");
            std::size_t i = 0; if (keyIndex(m, c.a[1], i)) return ok(WsaEAlready, "key already registered");
            if (keys_.size() >= MaxKeys) return ok(WsaENoBufs, "key table full (4)");
            keys_.push_back(k); return ok(0, "key registered locally (protects no traffic: none leaves the process)");
        }
        if (name == "XNetUnregisterKey") {
            std::size_t i = 0; if (!keyIndex(m, c.a[1], i)) return ok(WsaEInval, "key not registered");
            keys_.erase(keys_.begin() + std::ptrdiff_t(i)); return ok(0, "key unregistered");
        }
        if (name == "XNetXnAddrToInAddr") { // (caller, XNADDR*, XNKID*, IN_ADDR*)
            std::uint8_t a[36], own[36]; ownXnAddr(own); std::size_t i = 0;
            if (!m.read(c.a[1], a, 36)) return ok(WsaEFault, "XNADDR not readable");
            if (std::memcmp(a, own, 36) != 0) return ok(WsaEInval, "not the title's own address: no remote peers exist");
            if (!keyIndex(m, c.a[2], i)) return ok(WsaEInval, "key not registered");
            if (!wr32(m, c.a[3], Loopback)) return ok(WsaEFault, "IN_ADDR output not writable");
            return ok(0, "own XNADDR -> 127.0.0.1");
        }
        if (name == "XNetInAddrToXnAddr") { // (caller, IN_ADDR, XNADDR*, XNKID*)
            if (!loopbackAddress(c.a[1])) return ok(WsaEInval, "not a loopback address: no remote peers exist");
            std::uint8_t own[36]; ownXnAddr(own);
            if (c.a[2] && !m.write(c.a[2], own, 36)) return ok(WsaEFault, "XNADDR output not writable");
            if (c.a[3]) { std::uint8_t id[8]{}; if (!keys_.empty()) std::memcpy(id, keys_.front().data(), 8);
                          if (!m.write(c.a[3], id, 8)) return ok(WsaEFault, "XNKID output not writable"); }
            return ok(0, "loopback -> own XNADDR");
        }
        if (name == "XNetUnregisterInAddr") return ok(0, "nothing to release");
        if (name == "XNetConnect") return loopbackAddress(c.a[1]) ? ok(0, "loopback: already connected") : ok(WsaEInval, "no remote peers exist");
        if (name == "XNetServerToInAddr") return ok(WsaEHostUnreach, "no Live server exists locally");
        if (name == "XNetInAddrToServer") return ok(WsaEInval, "not a Live server address");
        (void)m; return ok(0, "QoS listen accepted: no peer can probe");
    }
    Result inetAddr(const Call& c, GuestMemory& m) { // (const char*) -> IN_ADDR in network order, dotted quad only
        char text[16]{}; std::uint32_t n = 0;
        for (; n < 16; ++n) { if (!m.read(c.a[0] + n, &text[n], 1)) return ok(InaddrNone, "unreadable string: INADDR_NONE"); if (!text[n]) break; }
        if (n == 16) return ok(InaddrNone, "not a dotted quad: INADDR_NONE");
        std::uint32_t v = 0, part = 0, parts = 0, digits = 0;
        for (std::uint32_t i = 0; i <= n; ++i) {
            const char ch = text[i];
            if (ch >= '0' && ch <= '9') { part = part * 10 + std::uint32_t(ch - '0'); if (++digits > 3 || part > 255) return ok(InaddrNone, "not a dotted quad: INADDR_NONE"); continue; }
            if ((ch != '.' && ch != 0) || !digits) return ok(InaddrNone, "not a dotted quad (no name resolution): INADDR_NONE");
            v = v << 8 | part; ++parts; part = 0; digits = 0;
            if (ch == 0) break;
        }
        if (parts != 4) return ok(InaddrNone, "not a dotted quad: INADDR_NONE");
        return ok(v, "dotted quad parsed (no name resolution)");
    }
    // ---- S6 ----
    Result http(std::string_view import, const Call& c, GuestMemory& m) {
        if (c.a[0] != 1) return {};
        first(Stub::Http);
        const auto name = import.substr(14);
        const auto handleOf = [&](std::uint32_t h, HttpKind k) -> HttpHandle* {
            auto it = http_.find(h); return it == http_.end() || it->second.kind != k ? nullptr : &it->second; };
        const auto make = [&](HttpKind k) -> Result {
            if (http_.size() >= 64) return fail(0, 8, "XHttp handle table full");
            const std::uint32_t h = 0xF0480000u | ++nextHttp_; http_[h] = HttpHandle{k}; return ok(h, "offline WinHTTP-style handle");
        };
        if (name == "XHttpStartup" || name == "XHttpShutdown") return ok(1, "XHttp library state only");
        if (name == "XHttpOpen") return make(HttpKind::Session);
        if (name == "XHttpConnect") return handleOf(c.a[1], HttpKind::Session) ? make(HttpKind::Connect)
                                                                               : fail(0, HttpInvalidHandle, "bad session handle");
        if (name == "XHttpOpenRequest") return handleOf(c.a[1], HttpKind::Connect) ? make(HttpKind::Request)
                                                                                   : fail(0, HttpInvalidHandle, "bad connect handle");
        if (name == "XHttpSetStatusCallback") {
            auto it = http_.find(c.a[1]);
            if (it == http_.end()) return fail(0xFFFFFFFFu, HttpInvalidHandle, "bad handle"); // WINHTTP_INVALID_STATUS_CALLBACK
            const auto previous = it->second.callback; it->second.callback = c.a[2]; return ok(previous, "callback stored; never invoked");
        }
        if (name == "XHttpSendRequest") return handleOf(c.a[1], HttpKind::Request)
                ? fail(0, HttpCannotConnect, "no connection: request never leaves the process")
                : fail(0, HttpInvalidHandle, "bad request handle");
        if (name == "XHttpReceiveResponse" || name == "XHttpQueryHeaders" || name == "XHttpReadData" || name == "XHttpWriteData")
            return handleOf(c.a[1], HttpKind::Request) ? fail(0, HttpIncorrectHandleState, "no response: request was never sent")
                                                       : fail(0, HttpInvalidHandle, "bad request handle");
        if (name == "XHttpDoWork") return http_.count(c.a[1]) ? ok(1, "nothing pending") : fail(0, HttpInvalidHandle, "bad handle");
        if (name == "XHttpCloseHandle") {
            if (!http_.erase(c.a[1])) return fail(0, HttpInvalidHandle, "bad handle");
            return ok(1, "closed");
        }
        (void)m;
        return {};
    }
    // ---- S5: loopback sockets ----
    Socket* sock(std::uint32_t s) { auto it = sockets_.find(s); return it == sockets_.end() ? nullptr : &it->second; }
    bool readSockaddr(const GuestMemory& m, std::uint32_t at, std::uint32_t len, std::uint32_t& addr, std::uint16_t& port) {
        std::uint16_t family = 0;
        if (len < 16 || !rd16(m, at, family) || !rd16(m, at + 2, port) || !rd32(m, at + 4, addr)) return false;
        return family == AfInet;
    }
    bool writeSockaddr(GuestMemory& m, std::uint32_t at, std::uint32_t lenPtr, std::uint32_t addr, std::uint16_t port) {
        std::uint32_t len = 0;
        if (!at) return true;
        if (!lenPtr || !rd32(m, lenPtr, len) || len < 16) return false;
        const std::uint8_t zero[8]{};
        return wr16(m, at, AfInet) && wr16(m, at + 2, port) && wr32(m, at + 4, addr) && m.write(at + 8, zero, 8) && wr32(m, lenPtr, 16);
    }
    bool portInUse(std::uint32_t type, std::uint16_t port, std::uint32_t except) const {
        for (const auto& [h, s] : sockets_) if (h != except && s.bound && s.type == type && s.port == port) return true;
        return false;
    }
    std::uint16_t ephemeral(std::uint32_t type) {
        for (int i = 0; i < 16384; ++i) { const auto p = nextEphemeral_; nextEphemeral_ = std::uint16_t(nextEphemeral_ == 65535 ? 49152 : nextEphemeral_ + 1);
            if (!portInUse(type, p, 0)) return p; }
        return 0;
    }
    std::uint32_t findBound(std::uint32_t type, std::uint16_t port, bool listening) const {
        for (const auto& [h, s] : sockets_) if (s.bound && s.type == type && s.port == port && (!listening || s.listening)) return h;
        return 0;
    }
    bool readable(const Socket& s) const {
        if (s.listening) return !s.pendingAccept.empty();
        return s.type == SockDgram ? !s.datagrams.empty() : (!s.stream.empty() || (s.connected && !s.peer));
    }
    Result deliverDatagram(Socket& src, std::uint32_t toAddr, std::uint16_t toPort, std::vector<std::uint8_t> bytes) {
        if (!src.bound) { src.bound = true; src.addr = 0; src.port = ephemeral(SockDgram); }
        const auto n = std::uint32_t(bytes.size());
        const bool broadcast = toAddr == 0xFFFFFFFFu;
        if (!loopbackAddress(toAddr) && !broadcast) {
            ++dropped_;
            if (!droppedLogged_ && log_) { droppedLogged_ = true; log_("RENDER_ONLINE_LOCAL datagram to a non-loopback address dropped (never sent)"); }
            return ok(n, "non-loopback datagram dropped in-process");
        }
        for (auto& [handle, s] : sockets_) {
            (void)handle;
            // the sender itself is included: a datagram to its own port, or a broadcast on it, loops back as on a BSD stack
            if (!s.bound || s.type != SockDgram || s.port != toPort || s.shutRead) continue;
            if (!(s.addr == 0 || s.addr == toAddr || broadcast)) continue;
            if (s.datagrams.size() < MaxQueuedDatagrams) s.datagrams.push_back({0x7F000001u, src.port, bytes});
            if (!broadcast) break;
        }
        return ok(n, "datagram queued in-process");
    }
    Result socketCall(std::string_view name, const Call& c, GuestMemory& m) {
        if (c.a[0] != 1) return {}; // XNCALLER_TITLE only
        static constexpr std::string_view Handled[]{"socket", "bind", "connect", "listen", "accept", "send", "recv", "recvfrom",
            "sendto", "select", "setsockopt", "ioctlsocket", "getsockname", "shutdown", "closesocket"};
        bool known = false; for (auto h : Handled) known = known || h == name;
        if (!known) return {};
        first(Stub::Sockets);
        if (name == "socket") {
            const auto af = c.a[1], type = c.a[2], proto = c.a[3];
            if (af != AfInet) return fail(InvalidSocket, WsaEAfNoSupport, "only AF_INET");
            if (type != SockStream && type != SockDgram) return fail(InvalidSocket, WsaESockTNoSupport, "only stream/datagram");
            if (proto != 0 && proto != IpprotoTcp && proto != IpprotoUdp && proto != IpprotoVdp)
                return fail(InvalidSocket, WsaEProtoNoSupport, "protocol");
            if (sockets_.size() >= MaxSockets) return fail(InvalidSocket, 10024, "too many sockets");
            const auto h = nextSocket_; nextSocket_ += 4;
            sockets_[h] = Socket{}; sockets_[h].type = type; sockets_[h].protocol = proto;
            return ok(h, "in-process socket (no host socket)");
        }
        if (name == "select") return select(c, m);
        Socket* s = sock(c.a[1]);
        if (!s) return fail(SocketError, WsaENotSock, "not a socket");
        if (name == "closesocket") {
            if (s->peer) if (auto* p = sock(s->peer)) p->peer = 0;
            for (auto pending : s->pendingAccept) sockets_.erase(pending);
            sockets_.erase(c.a[1]); return ok(0, "closed");
        }
        if (name == "bind") {
            std::uint32_t addr = 0; std::uint16_t port = 0;
            if (!readSockaddr(m, c.a[2], c.a[3], addr, port)) return fail(SocketError, WsaEFault, "bad sockaddr");
            if (s->bound) return fail(SocketError, WsaEInval, "already bound");
            if (addr != 0 && !loopbackAddress(addr)) return fail(SocketError, 10049, "only INADDR_ANY or loopback"); // WSAEADDRNOTAVAIL
            if (port == 0) port = ephemeral(s->type);
            if (port == 0 || portInUse(s->type, port, c.a[1])) return fail(SocketError, WsaEAddrInUse, "port in use");
            s->bound = true; s->addr = addr; s->port = port; return ok(0, "bound in-process");
        }
        if (name == "getsockname") {
            if (!s->bound) return fail(SocketError, WsaEInval, "not bound");
            if (!writeSockaddr(m, c.a[2], c.a[3], s->addr, s->port)) return fail(SocketError, WsaEFault, "bad name buffer");
            return ok(0, "bound address");
        }
        if (name == "setsockopt") return ok(0, "option accepted (no host effect)");
        if (name == "ioctlsocket") {
            const auto cmd = c.a[2], arg = c.a[3];
            if (cmd == FionBio) { std::uint32_t v = 0; if (!rd32(m, arg, v)) return fail(SocketError, WsaEFault, "argp"); s->nonBlocking = v != 0; return ok(0, "FIONBIO"); }
            if (cmd == FionRead) {
                std::uint32_t n = s->type == SockDgram ? (s->datagrams.empty() ? 0 : std::uint32_t(s->datagrams.front().bytes.size()))
                                                       : std::uint32_t(s->stream.size());
                if (!wr32(m, arg, n)) return fail(SocketError, WsaEFault, "argp"); return ok(0, "FIONREAD");
            }
            return fail(SocketError, WsaEInval, "ioctl command not modelled");
        }
        if (name == "shutdown") { const auto how = c.a[2]; if (how > 2) return fail(SocketError, WsaEInval, "how");
            if (how != 1) s->shutRead = true; if (how != 0) s->shutWrite = true; return ok(0, "shutdown"); }
        if (name == "listen") {
            if (s->type != SockStream) return fail(SocketError, WsaEOpNotSupp, "listen on datagram");
            if (!s->bound) return fail(SocketError, WsaEInval, "listen before bind");
            s->listening = true; return ok(0, "listening in-process");
        }
        if (name == "accept") {
            if (!s->listening) return fail(InvalidSocket, WsaEInval, "not listening");
            if (s->pendingAccept.empty()) {
                if (s->nonBlocking) return fail(InvalidSocket, WsaEWouldBlock, "no pending connection");
                return refuse("blocking accept with no local peer would block forever");
            }
            const auto h = s->pendingAccept.front(); s->pendingAccept.pop_front();
            Socket* a = sock(h);
            if (!a || !writeSockaddr(m, c.a[2], c.a[3], 0x7F000001u, a->peerPort)) return fail(InvalidSocket, WsaEFault, "bad addr buffer");
            return ok(h, "accepted in-process connection");
        }
        if (name == "connect") {
            std::uint32_t addr = 0; std::uint16_t port = 0;
            if (!readSockaddr(m, c.a[2], c.a[3], addr, port)) return fail(SocketError, WsaEFault, "bad sockaddr");
            if (s->type == SockDgram) { s->connected = true; s->peerAddr = addr; s->peerPort = port; return ok(0, "datagram peer set"); }
            if (s->connected) return fail(SocketError, WsaEIsConn, "already connected");
            if (!loopbackAddress(addr)) return fail(SocketError, WsaENetUnreach, "only loopback peers exist");
            const auto listener = findBound(SockStream, port, true);
            if (!listener) return fail(SocketError, WsaEConnRefused, "no local listener");
            if (!s->bound) { s->bound = true; s->port = ephemeral(SockStream); }
            if (sockets_.size() >= MaxSockets) return fail(SocketError, 10055, "no buffer space");
            const auto server = nextSocket_; nextSocket_ += 4;
            Socket srv; srv.type = SockStream; srv.protocol = s->protocol; srv.bound = true; srv.addr = 0x7F000001u; srv.port = port;
            srv.connected = true; srv.peer = c.a[1]; srv.peerAddr = 0x7F000001u; srv.peerPort = s->port;
            sockets_[server] = std::move(srv);
            s = sock(c.a[1]); // map may have rehashed nothing (std::map), kept for clarity
            s->connected = true; s->peer = server; s->peerAddr = addr; s->peerPort = port;
            sock(listener)->pendingAccept.push_back(server);
            return ok(0, "connected in-process");
        }
        if (name == "send" || name == "sendto") {
            const auto buf = c.a[2], len = c.a[3];
            if (s->shutWrite) return fail(SocketError, WsaEShutdown, "shut down");
            std::vector<std::uint8_t> bytes(len);
            if (len && !m.read(buf, bytes.data(), len)) return fail(SocketError, WsaEFault, "bad buffer");
            if (s->type == SockDgram) {
                if (len > MaxDatagram) return fail(SocketError, WsaEMsgSize, "datagram too large");
                std::uint32_t addr = s->peerAddr; std::uint16_t port = s->peerPort;
                if (name == "sendto" && c.a[5]) { if (!readSockaddr(m, c.a[5], c.a[6], addr, port)) return fail(SocketError, WsaEFault, "bad to"); }
                else if (!s->connected) return fail(SocketError, WsaENotConn, "no destination");
                return deliverDatagram(*s, addr, port, std::move(bytes));
            }
            if (!s->connected) return fail(SocketError, WsaENotConn, "not connected");
            Socket* p = sock(s->peer);
            if (!p) return fail(SocketError, 10054, "peer closed"); // WSAECONNRESET
            if (p->stream.size() + len > MaxStreamBytes) {
                if (s->nonBlocking) return fail(SocketError, WsaEWouldBlock, "peer buffer full");
                return refuse("blocking send to a full in-process peer would block forever");
            }
            p->stream.insert(p->stream.end(), bytes.begin(), bytes.end());
            return ok(len, "stream bytes queued in-process");
        }
        if (name == "recv" || name == "recvfrom") {
            const auto buf = c.a[2], len = c.a[3];
            const auto fromAt = name == "recvfrom" ? c.a[5] : 0u, fromLen = name == "recvfrom" ? c.a[6] : 0u;
            if (s->shutRead) return ok(0, "shut down: end of data");
            if (s->type == SockDgram) {
                if (!s->bound) return fail(SocketError, WsaEInval, "recv on unbound datagram socket");
                if (s->datagrams.empty()) {
                    if (s->nonBlocking) return fail(SocketError, WsaEWouldBlock, "no datagram");
                    return refuse("blocking datagram receive with no local sender would block forever");
                }
                auto d = std::move(s->datagrams.front()); s->datagrams.pop_front();
                const auto n = std::uint32_t(d.bytes.size()) < len ? std::uint32_t(d.bytes.size()) : len;
                if (n && !m.write(buf, d.bytes.data(), n)) return fail(SocketError, WsaEFault, "bad buffer");
                if (!writeSockaddr(m, fromAt, fromLen, d.fromAddr, d.fromPort)) return fail(SocketError, WsaEFault, "bad from");
                if (n < d.bytes.size()) return fail(SocketError, WsaEMsgSize, "datagram truncated");
                return ok(n, "datagram received in-process");
            }
            if (!s->connected) return fail(SocketError, WsaENotConn, "not connected");
            if (s->stream.empty()) {
                if (!s->peer) return ok(0, "peer closed: end of data");
                if (s->nonBlocking) return fail(SocketError, WsaEWouldBlock, "no data");
                return refuse("blocking stream receive with no local sender would block forever");
            }
            const auto n = std::uint32_t(s->stream.size()) < len ? std::uint32_t(s->stream.size()) : len;
            std::vector<std::uint8_t> out(s->stream.begin(), s->stream.begin() + n);
            if (n && !m.write(buf, out.data(), n)) return fail(SocketError, WsaEFault, "bad buffer");
            s->stream.erase(s->stream.begin(), s->stream.begin() + n);
            if (!writeSockaddr(m, fromAt, fromLen, s->peerAddr, s->peerPort)) return fail(SocketError, WsaEFault, "bad from");
            return ok(n, "stream bytes received in-process");
        }
        return {};
    }
    // fd_set: u32 count, then up to 64 u32 sockets.
    bool readSet(const GuestMemory& m, std::uint32_t at, std::vector<std::uint32_t>& out) {
        std::uint32_t n = 0; if (!rd32(m, at, n) || n > 64) return false;
        out.resize(n); for (std::uint32_t i = 0; i < n; ++i) if (!rd32(m, at + 4 + 4 * i, out[i])) return false;
        return true;
    }
    bool writeSet(GuestMemory& m, std::uint32_t at, const std::vector<std::uint32_t>& v) {
        if (!wr32(m, at, std::uint32_t(v.size()))) return false;
        for (std::size_t i = 0; i < v.size(); ++i) if (!wr32(m, at + 4 + 4 * std::uint32_t(i), v[i])) return false;
        return true;
    }
    Result select(const Call& c, GuestMemory& m) {
        const std::uint32_t sets[3]{c.a[2], c.a[3], c.a[4]}; const auto timeout = c.a[5];
        std::vector<std::uint32_t> in[3], out[3]; std::uint32_t ready = 0;
        for (int k = 0; k < 3; ++k) {
            if (!sets[k]) continue;
            if (!readSet(m, sets[k], in[k])) return fail(SocketError, WsaEFault, "bad fd_set");
            for (auto h : in[k]) {
                Socket* s = sock(h); if (!s) return fail(SocketError, WsaENotSock, "fd_set holds a non-socket");
                const bool r = k == 0 ? readable(*s) : k == 1 ? (s->type == SockDgram || s->connected) : false;
                if (r) { out[k].push_back(h); ++ready; }
            }
        }
        for (int k = 0; k < 3; ++k) if (sets[k] && !writeSet(m, sets[k], out[k])) return fail(SocketError, WsaEFault, "bad fd_set");
        Result x = ok(ready, ready ? "ready sockets" : "nothing ready");
        if (!ready) {
            if (!timeout) return refuse("infinite select with no local traffic would block forever");
            std::uint32_t sec = 0, usec = 0;
            if (!rd32(m, timeout, sec) || !rd32(m, timeout + 4, usec)) return fail(SocketError, WsaEFault, "bad timeval");
            const std::uint64_t want = std::uint64_t(sec) * 1000000 + usec;
            // DEVIATION (header): at most SelectWaitCapMicros of host wait, then 0 = timeout, even when more was asked.
            x.waitMicros = std::uint32_t(want < SelectWaitCapMicros ? want : SelectWaitCapMicros);
        }
        return x;
    }
    Result fdIsSet(const Call& c, GuestMemory& m) {
        std::vector<std::uint32_t> set;
        if (!readSet(m, c.a[1], set)) return fault("fd_set is not readable guest memory");
        for (auto h : set) if (h == c.a[0]) return ok(1, "in set");
        return ok(0, "not in set");
    }
};

} // namespace recomp::online_local
