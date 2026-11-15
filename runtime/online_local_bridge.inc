#include <array>   // v974
#include <cstring>   // v974
#include <mutex>   // v974
// online_local_bridge.inc (Update 1; lane upd1-spoof/session-stubs, on lane upd1-online/local-session-stubs). PROTOTYPE,
// NOT IN ANY BUILD. Patch target: v963.
// Included at the END of generated_guest_import_bridge.cpp, after signin_ui_bridge.inc (patch 0002): boundRegistry,
// boundGuestBase, bridgeTrap, HostActivityScope, recomp_imports_fast, recomp_v312_release and
// recomp::bootstrap::guestImportEntryHook are that file's. It REPLACES the guarded stops of
// remaining_xbox_import_guarded_stops.cpp for the 42 imports below (patch 0003 removes exactly those definitions;
// tests/check_patches.py verifies the list).
//
// Switch off or Story mode: each function makes exactly the two calls the generated guarded stop made
// (captureImportArguments, then recomp_guest_execution_unimplemented_import(name)), so behaviour is unchanged.
// Switch on in Sandbox: the same sequence as dispatchBoundedImport (v226 activity scope, v211 entry hook, registry
// dispatch, PRIVATE_IMPORT_REJECT line with the diagnostic as hex, then the stop), so a refusal's reason (for example
// "blocking datagram receive ... would block forever") reaches the log. The registry answers through
// online_local_registry.inc; an unanswered call stops as an unimplemented or rejected import, with its name.
// Host-tested end to end (bridge -> fake registry -> registry include -> Service) in tests/glue_harness.cpp.
// No host socket API is used here or in online_local.h (tests/static_no_socket.py).
#include "online_local_glue.h"
#include <unistd.h>
extern "C" [[noreturn]] void recomp_guest_execution_unimplemented_import(const char* importName);

namespace online_local_bridge {
[[noreturn]] inline void guardedStop(const char* name, PPCContext& ctx) {
    recomp::bootstrap::GuestExecutionGuard::captureImportArguments({ctx.r1.u64, {{
        ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64}}});
    recomp_guest_execution_unimplemented_import(name);
}
inline void call(const char* name, PPCContext& ctx, std::uint8_t* base) {
    if (!::recomp::online_local_glue::enabled()) guardedStop(name, ctx);
    { // v974: the numbered first-use log of every bridged import (the whole path of a run, in order); fixed table, no allocation
        static std::mutex seenMutex; static std::array<const char*, 160> seen{}; static unsigned seenCount = 0;
        std::lock_guard lock(seenMutex);
        bool fresh = true;
        for (unsigned i = 0; i < seenCount; ++i) if (std::strcmp(seen[i], name) == 0) { fresh = false; break; }
        if (fresh && seenCount < seen.size()) {
            seen[seenCount++] = name;
            char line[200]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL first-use #%u import=%s lr=%08x r3=%08x r4=%08x",
                                          seenCount, name, unsigned(ctx.lr), unsigned(ctx.r3.u32), unsigned(ctx.r4.u32));
            ::recomp::online_local_glue::log(line);
        }
    }
    auto* registry = boundRegistry.load(std::memory_order_acquire);
    if (registry == nullptr || base == nullptr || base != boundGuestBase.load(std::memory_order_acquire)) bridgeTrap();
    recomp::bootstrap::GuestImportCall call{};
    call.stackPointer = static_cast<std::uint32_t>(ctx.r1.u64);
    call.arguments = {{ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64}};
    ::recomp::online_local_glue::pendingWaitMicros = 0;
    std::optional<HostActivityScope> activity;
    if (!(::recomp_imports_fast.load(std::memory_order_relaxed) && ::recomp_v312_release.load(std::memory_order_relaxed)))
        activity.emplace("import"); // v226, as dispatchBoundedImport
    if (auto* hook = recomp::bootstrap::guestImportEntryHook.load(std::memory_order_acquire)) hook(); // v211, as every bridged import
    const auto result = registry->dispatch(name, call);
    activity.reset();
    if (!result.handled()) {
        std::array<char, 513> diagnostic{};
        constexpr char hex[] = "0123456789abcdef";
        const auto count = result.diagnostic.size() < 256 ? result.diagnostic.size() : 256;
        for (std::size_t i = 0; i < count; ++i) {
            const auto byte = static_cast<unsigned char>(result.diagnostic[i]);
            diagnostic[i * 2] = hex[byte >> 4]; diagnostic[i * 2 + 1] = hex[byte & 15];
        }
        std::printf("PRIVATE_IMPORT_REJECT name=%s state=%u diagnostic_hex=%s\n", name,
                    static_cast<unsigned>(result.state), diagnostic.data());
        recomp::bootstrap::GuestExecutionGuard::captureImportArguments({ctx.r1.u64, call.arguments});
        if (result.state == recomp::bootstrap::GuestImportDispatchState::NotRegistered)
            recomp::bootstrap::GuestExecutionGuard::stopForUnimplementedImport(name);
        recomp::bootstrap::GuestExecutionGuard::stopForRejectedImport(name);
    }
    if (::recomp::online_local_glue::traceOn()) {   // v1009: the L1 import trace window (2 s after Host)
        char line[200]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL l1 trace bridge import=%s lr=%08x r3=%08x r4=%08x r5=%08x -> %08x",
                                      name, unsigned(ctx.lr), unsigned(ctx.r3.u32), unsigned(ctx.r4.u32), unsigned(ctx.r5.u32), unsigned(call.returnValue));
        ::recomp::online_local_glue::log(line);
    }
    ctx.r3.u64 = call.returnValue;
    // select() with a timeout and nothing ready: a bounded wait (<= 4 ms, an early timeout: online_local.h header) outside
    // the registry lock, so a polling guest thread does not spin a core. Only in Sandbox with the switch on.
    if (const auto us = ::recomp::online_local_glue::pendingWaitMicros) { ::recomp::online_local_glue::pendingWaitMicros = 0; ::usleep(us); }
}
} // namespace online_local_bridge

void __imp__NetDll_XNetGetConnectStatus(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetGetConnectStatus", ctx, base); }
void __imp__NetDll_socket(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_socket", ctx, base); }
void __imp__NetDll_bind(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_bind", ctx, base); }
void __imp__NetDll_connect(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_connect", ctx, base); }
void __imp__NetDll_listen(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_listen", ctx, base); }
void __imp__NetDll_accept(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_accept", ctx, base); }
void __imp__NetDll_send(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_send", ctx, base); }
void __imp__NetDll_recv(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_recv", ctx, base); }
void __imp__NetDll_recvfrom(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_recvfrom", ctx, base); }
void __imp__NetDll_select(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_select", ctx, base); }
void __imp__NetDll_setsockopt(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_setsockopt", ctx, base); }
void __imp__NetDll_ioctlsocket(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_ioctlsocket", ctx, base); }
void __imp__NetDll_getsockname(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_getsockname", ctx, base); }
void __imp__NetDll_shutdown(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_shutdown", ctx, base); }
void __imp__NetDll_closesocket(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_closesocket", ctx, base); }
void __imp__NetDll___WSAFDIsSet(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll___WSAFDIsSet", ctx, base); }
void __imp__NetDll_XHttpStartup(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpStartup", ctx, base); }
void __imp__NetDll_XHttpShutdown(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpShutdown", ctx, base); }
void __imp__NetDll_XHttpOpen(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpOpen", ctx, base); }
void __imp__NetDll_XHttpConnect(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpConnect", ctx, base); }
void __imp__NetDll_XHttpOpenRequest(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpOpenRequest", ctx, base); }
void __imp__NetDll_XHttpSendRequest(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpSendRequest", ctx, base); }
void __imp__NetDll_XHttpReceiveResponse(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpReceiveResponse", ctx, base); }
void __imp__NetDll_XHttpQueryHeaders(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpQueryHeaders", ctx, base); }
void __imp__NetDll_XHttpReadData(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpReadData", ctx, base); }
void __imp__NetDll_XHttpWriteData(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpWriteData", ctx, base); }
void __imp__NetDll_XHttpSetStatusCallback(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpSetStatusCallback", ctx, base); }
void __imp__NetDll_XHttpDoWork(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpDoWork", ctx, base); }
void __imp__NetDll_XHttpCloseHandle(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XHttpCloseHandle", ctx, base); }
void __imp__NetDll_sendto(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_sendto", ctx, base); }
void __imp__NetDll_inet_addr(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_inet_addr", ctx, base); }
void __imp__NetDll_XNetRegisterKey(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetRegisterKey", ctx, base); }
void __imp__NetDll_XNetUnregisterKey(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetUnregisterKey", ctx, base); }
void __imp__NetDll_XNetXnAddrToInAddr(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetXnAddrToInAddr", ctx, base); }
void __imp__NetDll_XNetInAddrToXnAddr(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetInAddrToXnAddr", ctx, base); }
void __imp__NetDll_XNetUnregisterInAddr(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetUnregisterInAddr", ctx, base); }
void __imp__NetDll_XNetConnect(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetConnect", ctx, base); }
void __imp__NetDll_XNetServerToInAddr(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetServerToInAddr", ctx, base); }
void __imp__NetDll_XNetInAddrToServer(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetInAddrToServer", ctx, base); }
void __imp__NetDll_XNetQosListen(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__NetDll_XNetQosListen", ctx, base); }
void __imp__XamSessionCreateHandle(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamSessionCreateHandle", ctx, base); }
void __imp__XamSessionRefObjByHandle(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamSessionRefObjByHandle", ctx, base); }
// v970 (T1's next stop): XamUserGetAge. Switch off / Story: exactly the old guarded stop. On: the caller's lr and r3..r5 logged once
// (its call site is not reachable statically: no caller of thunk sub_8363DB28 in code or data), then the registry answers it
// (signin_spoof_registry.inc: user 0, ERROR_SUCCESS, age 18 only into a bounded writable r4).
void __imp__XamUserGetAge(PPCContext& ctx, std::uint8_t* base) {
    if (::recomp::online_local_glue::enabled()) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            char line[160]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL first-use stub=XamUserGetAge lr=%08x r3=%08x r4=%08x r5=%08x",
                                          unsigned(ctx.lr), unsigned(ctx.r3.u32), unsigned(ctx.r4.u32), unsigned(ctx.r5.u32));
            ::recomp::online_local_glue::log(line);
        }
    }
    online_local_bridge::call("__imp__XamUserGetAge", ctx, base);
}
// v971 (T1 rerun stop): XamUserGetAgeGroup, as XamUserGetAge (caller lr logged once; switch off / Story = the old stop).
void __imp__XamUserGetAgeGroup(PPCContext& ctx, std::uint8_t* base) {
    if (::recomp::online_local_glue::enabled()) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            char line[160]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL first-use stub=XamUserGetAgeGroup lr=%08x r3=%08x r4=%08x r5=%08x",
                                          unsigned(ctx.lr), unsigned(ctx.r3.u32), unsigned(ctx.r4.u32), unsigned(ctx.r5.u32));
            ::recomp::online_local_glue::log(line);
        }
    }
    online_local_bridge::call("__imp__XamUserGetAgeGroup", ctx, base);
}
// v973 (T1 v971 stop): XamCreateEnumeratorHandle (three XLive wrappers, app 252; caller lr logged once; switch off / Story = the old stop).
void __imp__XamCreateEnumeratorHandle(PPCContext& ctx, std::uint8_t* base) {
    if (::recomp::online_local_glue::enabled()) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            char line[176]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL first-use stub=XamCreateEnumeratorHandle lr=%08x r3=%08x r4=%08x r5=%08x r6=%08x",
                                          unsigned(ctx.lr), unsigned(ctx.r3.u32), unsigned(ctx.r4.u32), unsigned(ctx.r5.u32), unsigned(ctx.r6.u32));
            ::recomp::online_local_glue::log(line);
        }
    }
    online_local_bridge::call("__imp__XamCreateEnumeratorHandle", ctx, base);
}
// v976 (T1 v974 stop): XamAlloc, the XLive request wrappers only (port_xamalloc.py).
void __imp__XamAlloc(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamAlloc", ctx, base); }
// v977: XamFree (frees only XamAlloc blocks).
void __imp__XamFree(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamFree", ctx, base); }
// v974 (bulk 1): five decoded stops (port_bulk1.py), logged by the numbered first-use log above.
void __imp__XamUserAreUsersFriends(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamUserAreUsersFriends", ctx, base); }
void __imp__XamUserGetMembershipTierFromXUID(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamUserGetMembershipTierFromXUID", ctx, base); }
void __imp__XamUserGetOnlineCountryFromXUID(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamUserGetOnlineCountryFromXUID", ctx, base); }
void __imp__XamGetLanguage(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XamGetLanguage", ctx, base); }
void __imp__XNetLogonGetTitleID(PPCContext& ctx, std::uint8_t* base) { online_local_bridge::call("__imp__XNetLogonGetTitleID", ctx, base); }
// v1136 (t-onlrace-post-20261109-0911: the race results' "GAMER CARD (Back)" called it and the guarded stop ended the game): the system gamer-card
// UI does not exist on this device. On the online-local path the call answers ERROR_SUCCESS (r3 = 0) with no UI - the card opens and closes at
// once; the title gets no XN_SYS_UI notification, so it never waits for a closing one. Logged once. Outside that path: the same guarded stop.
void __imp__XamShowGamerCardUIForXUID(PPCContext& ctx, std::uint8_t* base) {
    (void)base;
    if (!::recomp::online_local_glue::enabled()) online_local_bridge::guardedStop("__imp__XamShowGamerCardUIForXUID", ctx);
    static std::atomic<unsigned> calls{0};
    const unsigned n = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 4) { char line[200]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL gamer card UI #%u user=%u xuid=%08x%08x lr=%08x -> ERROR_SUCCESS (no system UI on this device)",
                    n, unsigned(ctx.r3.u32), unsigned(ctx.r4.u32), unsigned(ctx.r5.u32), unsigned(ctx.lr)); ::recomp::online_local_glue::log(line); }
    ctx.r3.u64 = 0;
}
// v1137 (lead 09:2x): the other XamShow*UI imports, as a UI that closed at once (online_xam_ui.h has the evidence and the decisions).
#include "online_xam_ui.h"
extern void __imp__NtSetEvent(PPCContext& ctx, std::uint8_t* base);
namespace online_xam_ui {
inline void note(const char* name, PPCContext& ctx, std::uint32_t result) {
    static std::mutex m; static std::array<const char*, 16> seen{}; static unsigned n = 0;
    std::lock_guard lock(m);
    for (unsigned i = 0; i < n; ++i) if (std::strcmp(seen[i], name) == 0) return;
    if (n < seen.size()) seen[n++] = name;
    char line[200]; std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL system UI %s lr=%08x -> %u (no system UI on this device)", name, unsigned(ctx.lr), unsigned(result));
    ::recomp::online_local_glue::log(line);
}
inline std::uint32_t stackArg9(PPCContext& ctx, std::uint8_t* base) { return PPC_LOAD_U32(ctx.r1.u32 + 0x54u); }
// the overlapped completion (guest_saves_dispatch.inc's layout): [0] result, [1] length, [3] event, [4] routine, [6] extended error
inline void complete(const char* name, PPCContext& ctx, std::uint8_t* base, std::uint32_t overlapped, std::uint32_t result) {
    namespace xu = ::recomp::xam_ui;
    if (overlapped == 0) { note(name, ctx, result); ctx.r3.u64 = result; return; }
    if (PPC_LOAD_U32(overlapped + 16u) != 0u) online_local_bridge::guardedStop(name, ctx);   // APC completion: not modelled
    PPC_STORE_U32(overlapped + 0u, result); PPC_STORE_U32(overlapped + 4u, 0u); PPC_STORE_U32(overlapped + 24u, xu::hresultOf(result));
    const std::uint32_t event = PPC_LOAD_U32(overlapped + 12u);
    if (event != 0u) { PPCContext c = ctx; c.r3.u64 = event; c.r4.u64 = 0; __imp__NtSetEvent(c, base); }
    note(name, ctx, result);
    ctx.r3.u64 = xu::IoPending;
}
inline void plain(const char* name, PPCContext& ctx) {
    if (!::recomp::online_local_glue::enabled()) online_local_bridge::guardedStop(name, ctx);
    const auto a = ::recomp::xam_ui::decide(::recomp::xam_ui::Kind::Plain, 0);
    note(name, ctx, a.result); ctx.r3.u64 = a.result;
}
} // namespace online_xam_ui
void __imp__XamShowMarketplaceUI(PPCContext& ctx, std::uint8_t* base) { (void)base; online_xam_ui::plain("__imp__XamShowMarketplaceUI", ctx); }
void __imp__XamShowFriendRequestUI(PPCContext& ctx, std::uint8_t* base) { (void)base; online_xam_ui::plain("__imp__XamShowFriendRequestUI", ctx); }
void __imp__XamShowPlayerReviewUI(PPCContext& ctx, std::uint8_t* base) { (void)base; online_xam_ui::plain("__imp__XamShowPlayerReviewUI", ctx); }
void __imp__XamShowMessageComposeUI(PPCContext& ctx, std::uint8_t* base) { (void)base; online_xam_ui::plain("__imp__XamShowMessageComposeUI", ctx); }
void __imp__XamShowKeyboardUI(PPCContext& ctx, std::uint8_t* base) {
    constexpr const char* name = "__imp__XamShowKeyboardUI";
    if (!::recomp::online_local_glue::enabled()) online_local_bridge::guardedStop(name, ctx);
    const auto a = ::recomp::xam_ui::decide(::recomp::xam_ui::Kind::Keyboard, 0);
    online_xam_ui::complete(name, ctx, base, ctx.r10.u32, a.result);
}
void __imp__XamShowMessageBoxUI(PPCContext& ctx, std::uint8_t* base) {
    constexpr const char* name = "__imp__XamShowMessageBoxUI";
    if (!::recomp::online_local_glue::enabled()) online_local_bridge::guardedStop(name, ctx);
    const auto a = ::recomp::xam_ui::decide(::recomp::xam_ui::Kind::MessageBox, ctx.r6.u32);
    const std::uint32_t result = ctx.r10.u32;
    if (a.writeButton && result != 0u) PPC_STORE_U32(result, a.button);
    online_xam_ui::complete(name, ctx, base, online_xam_ui::stackArg9(ctx, base), a.result);
}
void __imp__XamShowMarketplaceDownloadItemsUI(PPCContext& ctx, std::uint8_t* base) {
    constexpr const char* name = "__imp__XamShowMarketplaceDownloadItemsUI";
    if (!::recomp::online_local_glue::enabled()) online_local_bridge::guardedStop(name, ctx);
    const auto a = ::recomp::xam_ui::decide(::recomp::xam_ui::Kind::MarketDownload, 0);
    if (a.writeHresult && ctx.r7.u32 != 0u) PPC_STORE_U32(ctx.r7.u32, ::recomp::xam_ui::hresultOf(a.result));
    online_xam_ui::complete(name, ctx, base, ctx.r8.u32, a.result);
}
