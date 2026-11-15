// signin_spoof_bridge.inc (lane upd1-spoof/signin-spoof): __imp__XampXAuthStartup, moved out of
// remaining_xbox_import_guarded_stops.cpp. Included at the end of generated_guest_import_bridge.cpp, after signin_ui_bridge.inc.
//
// Why it is needed with the Sandbox sign-in answers: the online-services state machine sub_8251ED60 (chunk 33, state word
// 0x8383E8D0, run every frame by sub_822B14D8 once rline is up and its enable byte 0x83C06A47 is set by sub_83271F28) leaves
// state 1 as soon as XamUserGetSigninState(i) == 2 for some user, and state 2 calls sub_836DE640 -> XampXAuthStartup
// (thunk 0x8375408C). Without an answer the Sandbox sign-in would stop the game there.
// Answer under the open gate: E_FAIL (Live authentication unavailable). sub_836DE640 frees its 0x7D000 buffer and returns the
// failure; the state machine stores 5, cleans up (no XHttp, no XAuth shutdown: their started-bytes stay 0) and starts over at 0.
// No guest memory is written. Never success, never a token.
// Closed gate (Story, Unset, no diag-online-local): exactly the generated guarded stop it replaces (same argument capture, same
// call, same name).
// Marker (once per process): ONLINE_LOCAL xauth=unavailable n=1
#include "signin_spoof.h"
#include "online_local_glue.h" // v968 (B2): the one gate is the session glue latch (decided once, Sandbox-only file read)
#include "recomp_app_dir.h"
extern "C" [[noreturn]] void recomp_guest_execution_unimplemented_import(const char* importName);

void __imp__XampXAuthStartup(PPCContext& ctx, std::uint8_t* base) {
    (void)base;
    if (::recomp::online_local_glue::enabled() /* v968 B2: one latch */) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "ONLINE_LOCAL xauth=unavailable n=1");
        }
        ctx.r3.u64 = ::recomp::online_local::XAuthUnavailable;
        return;
    }
    recomp::bootstrap::GuestExecutionGuard::captureImportArguments({ctx.r1.u64, {{
        ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64,
        ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64,
    }}});
    recomp_guest_execution_unimplemented_import("__imp__XampXAuthStartup");
}
