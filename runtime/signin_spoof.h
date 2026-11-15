#pragma once
// signin_spoof.h (Update 1, lane upd1-spoof/signin-spoof, 2026-11-06; owner decision "just remove the script or spoof it"):
// inside the OFFLINE Sandbox mode only, user 0 answers as signed in online with the multiplayer privileges, so the disc's own
// online scripts take their signed-in branches. Purely local: no socket, no HTTP, no service, nothing authenticated. Story Mode is
// unchanged: every answer below is used only while the play-mode latch says Sandbox AND files/private-trace/diag-online-local exists.
//
// What the game reads (our recompiled corpus, android-private-trace-window/api26/validated-ppc; lane NOTES.md §2 has the chain):
//  - NETWORK_IS_SIGNED_ONLINE = byte 0x83B13BA7, written only by sub_82FD68A8 = sub_824FE468(local gamer info 0x83B26DB8) = bit 0x40
//    of the info's +92 byte. That bit is set by sub_8327EA08 from sub_8326D420(user) = sub_8253D060(user) && XamUserGetSigninState == 2.
//  - sub_8253D060 -> sub_82520750 = XNetGetEthernetLinkStatus(1) != 0 && a cached XNADDR MAC (sub_83250A18) && a cached XNADDR ina
//    != 0 / != 0xFFFFFFFF (sub_8324DAE0), both from XNetGetTitleXnAddr with a result that is neither NONE (1) nor TROUBLESHOOT
//    (0x8000). So the link and an address are part of "signed in online" on this executable: answered with loopback values.
//  - Privileges: sub_82F9AA18 fills the per-user tables 0x83B1394C (254 via sub_8326C200), 0x83B13940 (246/247 via sub_8326C330)
//    and 0x83B13918 (251/252 via sub_8326C280) for users whose profile slot has bit 0x10 (online). XUserCheckPrivilege (sub_8363A2A0)
//    asks XAM directly once the user is not in state 1.
//  - No game site reads an online XUID for these gates: the gamer handle (sub_83274640) prefers the online XUID only when it is
//    non-zero and otherwise uses the offline one. The only XUIDs here are the profile's offline-marked one (0xE... prefix).
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include "play_mode.h"

namespace recomp::online_local {

// ---- Gate --------------------------------------------------------------------------------------------------------------------
inline constexpr const char* SwitchName = "diag-online-local";
// Pure: open only for a Sandbox latch (fresh or not) with the switch present. Unset and Story stay closed.
inline constexpr bool gateOpen(std::uint8_t latch, bool switchPresent) noexcept {
    return latch == ::recomp::bootstrap::play_mode::LatchedOnline   // v1040: Online mode needs no switch file
        || (switchPresent && (latch == ::recomp::bootstrap::play_mode::LatchedSandbox
                              || latch == ::recomp::bootstrap::play_mode::LatchedSandboxFresh)); // [mutant:gate-latch]
}
// The switch file, read ONCE per process (RECOMP_PRIVATE_TRACE_DIR replaces files/private-trace, as for diag-signin-ui-stop).
inline bool switchPresent(const char* deviceDirectory) noexcept {
    static const bool present = [deviceDirectory] {
        const char* dir = std::getenv("RECOMP_PRIVATE_TRACE_DIR");
        const std::string path = std::string(dir ? dir : deviceDirectory) + "/" + SwitchName;
        return ::access(path.c_str(), F_OK) == 0;
    }();
    return present;
}
inline bool enabled(const char* deviceDirectory) noexcept {
    return gateOpen(::recomp::bootstrap::play_mode::latch.load(std::memory_order_acquire), switchPresent(deviceDirectory));
}

// ---- XAM answers -------------------------------------------------------------------------------------------------------------
inline constexpr std::uint32_t SigninStateOnline = 2;            // eXUserSigninState_SignedInToLive [mutant:state]
inline constexpr std::uint32_t ErrorSuccess = 0, ErrorInvalidParameter = 87;
inline constexpr std::uint32_t SigninInfoOnlineXuidOnly = 1, SigninInfoOfflineXuidOnly = 2;

// Privileges granted to the local user (XPRIVILEGE_* ids from the public XDK headers). Everything the online character, free roam
// and jobs need; NOT purchase (245), trade (238), video (234/235), dedicated server (226) or social-network sharing (220): those
// lead to store / upload paths whose imports stay guarded stops. Unknown ids are denied.
inline constexpr std::uint32_t GrantedPrivileges[] = {
    254, // MULTIPLAYER_SESSIONS (sub_8326C200 -> NETWORK_HAVE_ONLINE_PRIVILEGES)
    252, 251, // COMMUNICATIONS, COMMUNICATIONS_FRIENDS_ONLY (sub_8326C280 -> NETWORK_HAVE_COMMUNICATION_PRIVILEGES)
    249, 248, // PROFILE_VIEWING, PROFILE_VIEWING_FRIENDS_ONLY
    247, 246, // USER_CREATED_CONTENT, USER_CREATED_CONTENT_FRIENDS_ONLY (sub_8326C330 -> NETWORK_HAVE_USER_CONTENT_PRIVILEGES)
    244, 243, // PRESENCE, PRESENCE_FRIENDS_ONLY
};
inline constexpr bool privilegeGranted(std::uint32_t privilege) noexcept {
    for (const auto p : GrantedPrivileges) { if (p == privilege) { return true; } } // [mutant:priv-loop]
    return false;
}
// XamUserCheckPrivilege(user, privilege, BOOL* result): for a signed-in Live user XAM returns ERROR_SUCCESS and writes the BOOL.
struct PrivilegeAnswer { std::uint32_t returnValue; bool write; std::uint32_t value; };
inline constexpr PrivilegeAnswer checkPrivilege(std::uint32_t privilege, bool outputPresent) noexcept {
    if (!outputPresent) { return {ErrorInvalidParameter, false, 0}; }
    return {ErrorSuccess, true, privilegeGranted(privilege) ? 1U : 0U};
}

inline void storeBe32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
}
inline void storeBe64(std::uint8_t* p, std::uint64_t v) noexcept { storeBe32(p, std::uint32_t(v >> 32)); storeBe32(p + 4, std::uint32_t(v)); }

// X_USER_SIGNIN_INFO (40 bytes, big-endian; layout in guest_local_profile.h). State 2. The XUID field holds the profile's
// offline-marked XUID for an offline or unfiltered query and 0 for ONLINE_XUID_ONLY: no online XUID is ever produced. Info flags
// stay 0 (the one game caller, sub_8326D378 via sub_8363DB38, reads only +0).
inline std::array<std::uint8_t, 40> encodeSigninInfo(std::uint64_t offlineXuid, const std::string& gamertag,
                                                     std::uint32_t queryFlags) noexcept {
    std::array<std::uint8_t, 40> out{};
    if (queryFlags != SigninInfoOnlineXuidOnly) { storeBe64(out.data(), offlineXuid); } // [mutant:online-xuid]
    storeBe32(out.data() + 12, SigninStateOnline);
    const std::size_t n = gamertag.size() < 15 ? gamertag.size() : 15;
    std::memcpy(out.data() + 24, gamertag.data(), n);
    return out;
}
inline constexpr bool isOfflineMarkedXuid(std::uint64_t xuid) noexcept { return (xuid >> 60) == 0xE; }

// ---- XNet answers (loopback only; no transport exists behind them) -----------------------------------------------------------
inline constexpr std::uint32_t EthernetLinkActive = 0x1;          // XNET_ETHERNET_LINK_ACTIVE [mutant:link]
// XNET_GET_XNADDR_ETHERNET | XNET_GET_XNADDR_STATIC: an address is configured; NOT ONLINE (0x80), NOT PENDING (0), NOT NONE (1).
inline constexpr std::uint32_t TitleXnAddrFlags = 0x2 | 0x4;
inline constexpr std::uint32_t LoopbackIna = 0x7F000001;          // 127.0.0.1
// XNADDR (36 bytes): +0 ina, +4 inaOnline (0: no online address, so sub_8324DB80 / sub_8324DA70 stay off), +8 wPortOnline (0),
// +10 abEnet[6] (02:00:00:00:00:01, a locally administered MAC), +16 abOnline[20] (zero: no Live address).
inline std::array<std::uint8_t, 36> loopbackXnAddr() noexcept {
    std::array<std::uint8_t, 36> out{};
    storeBe32(out.data(), LoopbackIna);                            // [mutant:ina]
    const std::uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0x01};
    std::memcpy(out.data() + 10, mac, 6);
    return out;
}

// ---- XAuth (the online-services state machine sub_8251ED60 reaches it once a user is in state 2) -------------------------------
// XampXAuthStartup is answered with a failure: Live authentication is unavailable. sub_836DE640 frees its buffer and the state
// machine goes to state 5 (no XHttp, no token). It is never answered with success.
inline constexpr std::uint32_t XAuthUnavailable = 0x80004005U;    // E_FAIL [mutant:xauth]

} // namespace recomp::online_local
