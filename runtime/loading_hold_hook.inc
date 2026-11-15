// loading_hold_hook.inc (v1050, trainer-mods; online_mode.h step 1; v1054: held at the loading screens' own shutdown): Online mode holds the
// game's boot loading screen. Included once by renderer_boundary_trace.cpp before world_load_hooks.inc (whose update-list #33 hook calls frame()
// once per frame, on the main thread).
// v1054 (t-onlctl-20261107-2029, v1050): the script native's override (sub_82D9EBC8) never fired, yet the boot card stayed up until the
// session and fm_intro: the screen is taken down by another caller. sub_825C7E08 (the shutdown, r3 = 1 from every caller) has three callers
// besides the native handler: sub_8295D948 (twice) and sub_8256FC30. So the hold sits at sub_825C7E08 itself; every call is logged with its
// caller (first CallLogMax), in every mode, so the device shows which path takes the screen down.
// LINK CONDITION: the attempt links update-1/redirect/redirect-v1054-loadhold.ld (= v462 without sub_825C7E08; the v462 script binds its weak
// wrapper straight to the body, which would bypass this override silently). "RENDER_ONLINE_MODE shutdown" lines prove it is live.
// Story / Sandbox: the original, unchanged (after the log line). Online mode, until released: not run; counted. frame(): once the controller
// asks (or the fallbacks in online_mode.h), the held shutdown runs once with r3 = 1, the registers saved and restored around it.
#include "online_mode.h"
#include <atomic>
#include <chrono>
namespace loading_hold_hook {
inline void log(const char* line) noexcept {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);
#else
    std::fputs(line,stderr);std::fputc('\n',stderr);
#endif
}
inline bool online() noexcept {return ::recomp::bootstrap::play_mode::online();}
inline std::uint64_t nowMs() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline std::uint64_t firstHoldMs=0;
constexpr unsigned CallLogMax=16;
inline std::atomic<unsigned> calls{0};
}

PPC_FUNC_IMPL(__imp__sub_825C7E08);
PPC_FUNC(sub_825C7E08) {   // the loading screens' shutdown (r3 = 1)
    const bool on=loading_hold_hook::online();
    const bool hold=::recomp::online_mode::holdShutdown(on);
    const unsigned k=loading_hold_hook::calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(k<=loading_hold_hook::CallLogMax) {
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE shutdown #%u r3=%u lr=%08x mode=%s -> %s",k,std::uint32_t(ctx.r3.u32),std::uint32_t(ctx.lr),
                                  on?"online":"story/sandbox",hold?"held (the loading screen stays up)":"run");
        loading_hold_hook::log(l);
    }
    if(hold)return;
    __imp__sub_825C7E08(ctx,base);
}

namespace loading_hold_hook {
inline void frame(PPCContext& ctx,std::uint8_t* base) {   // not noexcept: a guest stop unwinds to the executor
    if(!online())return;
    if(!::recomp::online_mode::shutdownDue(true,nowMs(),firstHoldMs))return;
    const PPCContext saved=ctx;
    ctx.r3.s64=1;
    __imp__sub_825C7E08(ctx,base);
    ctx=saved;
    char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE release ran the shutdown (held=%u, arrival phase %d, %s)",
                              ::recomp::online_mode::deferredShutdowns.load(std::memory_order_acquire),::recomp::online_mode::arrivalPhase.load(std::memory_order_acquire),
                              ::recomp::online_mode::releaseRequested.load(std::memory_order_acquire)?"controller":"hard limit (online character confirmed)");
    log(l);
}
}

// v1055 (lead's 360 reference, online-reference/LOADING-FLOW.md: the 360 joins Online FROM THE BOOT CARD: "Online (X)" pressed turns
// "Loading Story Mode" into "Loading Online"; the game then goes online itself, independent of the Story save). Decoded:
// - sub_825B9BE0 = the boot card's button handler, called every loading-screen update (sub_825D94F0, sub_825F9EA8). On a new press (edge byte
//   0x837861C8) it toggles the "go online" byte 0x83B13AEC (CNetwork, next to the game-in-progress byte 0x83B13AEB) through the setter
//   sub_82F91D18(r3) and swaps the prompts with sub_825B9AA0(show, id): X branch = set 1, hide 30 / 31 ("Online (X)"), show 44.
// - The byte is what the scripts read: native d87f3a9e (handler sub_82D64E08 -> sub_82232ED0 = lbz 15084) is called by startup and by
//   selector, whose boot latch (0x42d4, static 158) then sets static 45 f39 = 1, f7 = 3 and takes the maintransition branch (the game's own
//   SP -> MP transition). Native c0b971ea (sub_82D64E50) is the scripts' setter.
// Online mode (unless files/private-trace/diag-boot-online-off): on the first call the X branch runs once exactly as the game does it (the
// byte set to 1, prompts 30 / 31 hidden, 44 shown; v1059: v1055-v1058 hid 44 too and the card kept "Loading Story Mode"), and the handler is
// skipped from then on (a stray press cannot toggle back to Story).
// LINK CONDITION: redirect-v1055-bootonline.ld (= v1054's without sub_825B9BE0). "RENDER_ONLINE_MODE boot-card" proves it live.
PPC_FUNC(sub_82F91D18);   // the "go online" byte's setter (r3)
PPC_FUNC(sub_825B9AA0);   // the boot card prompt (r3 = show, r4 = id)
PPC_FUNC(sub_825C8060);   // v1072: the card's choice freeze (show 44 by the byte, then 0x83895C13 = 1)
namespace loading_hold_hook {
inline bool bootOnlineOff() noexcept {static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-boot-online-off"),F_OK)==0;return off;}
inline std::atomic<bool> bootOnlineDone{false};
// v1072: the card's choice. The loading screen's periodic eligibility check sub_825C80C0 (from sub_825D94F0 on every update once its timer
// 0x83895C00 passes 0x83895C24, and on the screen's init) runs while byte 0x83895C13 is 0; its first branch (ROS byte 0x83B13BA7, the
// privilege check sub_8259A2B8, the go-online byte still 0) offers "Online (X)"; every other case, the go-online byte already 1 included,
// hides "Story Mode (A)", clears the byte (sub_82F91D18(0)) and sets 0x83895C20. The game freezes the choice with sub_825C8060 (show 44 when
// the byte is 1, then 0x83895C13 = 1): startup calls it first (native b03bccdf, pc 1071), the screen's init clears 0x83895C13 again, and
// sub_8256C230 / sub_825CD738 call it later. Online mode, once the card is ready and the byte set: the hook calls that freeze, as the game
// does, so the check cannot take the choice back (v1071 rep1: cleared twice in 32 s; the card's "Loading Story Mode" from ~65 s in every run).
inline bool freezeOff() noexcept {static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-boot-freeze-off"),F_OK)==0;return off;}
inline unsigned freezes=0;   // main thread only (the loading-screen update)
constexpr unsigned FreezeLogMax=8,SetterLogMax=32;
inline std::atomic<unsigned> setterCalls{0};
inline unsigned u8at(std::uint8_t* base,std::uint32_t a) noexcept {return PPC_LOAD_U8(a);}
}
PPC_FUNC_IMPL(__imp__sub_825B9BE0);
PPC_FUNC(sub_825B9BE0) {
    if(!loading_hold_hook::online() || loading_hold_hook::bootOnlineOff()){__imp__sub_825B9BE0(ctx,base);return;}
    // v1063 (t-onlctl7-20261107-2138: the card kept "Loading Story Mode"): sub_825B9AA0 acts only while the card state 0x83895C64 == 1 and its
    // movie handle 0x837861C4 is valid; the v1059 one-shot ran on the handler's first call, before that, and was dropped. The byte is set once;
    // the two SET_BUTTONS calls are repeated on every card update until the card has been ready for ButtonFrames updates. The handler itself
    // stays skipped in Online mode (a stray press cannot toggle back to Story).
    constexpr unsigned ButtonFrames=30;
    static unsigned readyFrames=0;
    if(readyFrames>=ButtonFrames && loading_hold_hook::bootOnlineDone.load(std::memory_order_acquire)) {
        // v1072: keep the choice frozen (a later init of the screen clears 0x83895C13): re-freeze whenever it is open again, the byte set.
        if(!loading_hold_hook::freezeOff() && !::recomp::online_mode::skipGoOnline.load(std::memory_order_acquire) &&
           PPC_LOAD_U8(0x83895C13u)==0u && PPC_LOAD_U8(0x83B13AECu)==1u) {
            const PPCContext keep=ctx;sub_825C8060(ctx,base);ctx=keep;
            ctx.r3.s64=0;ctx.r4.s64=44;sub_825B9AA0(ctx,base);ctx=keep;   // v1080: the prompt stays hidden
            ::recomp::online_mode::choiceFrozen.store(PPC_LOAD_U8(0x83895C13u)!=0u,std::memory_order_release);   // v1075
            if(++loading_hold_hook::freezes<=loading_hold_hook::FreezeLogMax){char l[160];std::snprintf(l,sizeof l,
                "RENDER_ONLINE_MODE boot-card choice frozen again #%u (sub_825C8060; 5c13=%u golive=%u)",loading_hold_hook::freezes,
                loading_hold_hook::u8at(base,0x83895C13u),loading_hold_hook::u8at(base,0x83B13AECu));loading_hold_hook::log(l);}
        }
        return;
    }
    const PPCContext saved=ctx;
    // v1066: the byte (selector's maintransition branch) only once the controller decided, and not on a first visit: then the creator route
    // starts its own maintransition (a second one from selector would make the route refuse "already-running"). The text is sent either way.
    if(::recomp::online_mode::goOnlineDecided.load(std::memory_order_acquire) && !loading_hold_hook::bootOnlineDone.exchange(true,std::memory_order_acq_rel)) {
        if(::recomp::online_mode::skipGoOnline.load(std::memory_order_acquire)) {
            loading_hold_hook::log("RENDER_ONLINE_MODE boot-card go-online byte left 0 (first visit: the creator route starts maintransition itself)");
        } else {
            const unsigned before=PPC_LOAD_U8(0x83B13AECu);
            ctx.r3.s64=1;sub_82F91D18(ctx,base);
            char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE boot-card go-online byte 83b13aec %u -> %u (the boot card's X branch)",before,unsigned(PPC_LOAD_U8(0x83B13AECu)));
            loading_hold_hook::log(l);
        }
    }
    const bool ready=PPC_LOAD_U32(0x83895C64u)==1u && PPC_LOAD_U32(0x837861C4u)!=0xFFFFFFFFu;
    // exactly the game's X branch (loc_825B9D64): SET_BUTTONS(0, byte 0x837861C9 ? 30 : 31), SET_BUTTONS(1, 44) (sub_825B9AA0 -> the movie's
    // SET_BUTTONS, method name at 0x82017388)
    ctx.r3.s64=0;ctx.r4.s64=PPC_LOAD_U8(0x837861C9u)?30:31;sub_825B9AA0(ctx,base);
    ctx.r3.s64=1;ctx.r4.s64=44;sub_825B9AA0(ctx,base);
    ctx=saved;
    if(ready && ++readyFrames==ButtonFrames) {
        loading_hold_hook::log("RENDER_ONLINE_MODE boot-card SET_BUTTONS(0, 30|31) + SET_BUTTONS(1, 44) sent while the card was ready (30 updates)");
        // v1072: the game's own freeze of the choice (see freezeOff above), once, with the byte set; Story's choice (byte 0) is never frozen here.
        const unsigned b13=PPC_LOAD_U8(0x83895C13u),go=PPC_LOAD_U8(0x83B13AECu);
        if(!loading_hold_hook::freezeOff() && !::recomp::online_mode::skipGoOnline.load(std::memory_order_acquire) && go==1u && b13==0u) {
            sub_825C8060(ctx,base);ctx=saved;++loading_hold_hook::freezes;
            // v1080 (lead: the footer kept "Story Mode (A)" beside "Loading Online"; the 360 reference shows only "Loading Online" from its
            // second card, t0086, and A does nothing in Online mode here): the prompt hidden with the game's own call (SET_BUTTONS(0, 44)).
            ctx.r3.s64=0;ctx.r4.s64=44;sub_825B9AA0(ctx,base);ctx=saved;
            loading_hold_hook::log("RENDER_ONLINE_MODE boot-card Story Mode (A) prompt hidden after the freeze (SET_BUTTONS(0, 44))");
            ::recomp::online_mode::choiceFrozen.store(PPC_LOAD_U8(0x83895C13u)!=0u,std::memory_order_release);   // v1075
        }
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE boot-card choice %s (5c13 %u -> %u, 5c20=%u, golive=%u)",
            loading_hold_hook::freezeOff()?"not frozen (diag-boot-freeze-off)":go!=1u?"not frozen (byte 0)":b13?"already frozen":"frozen by sub_825C8060",
            b13,loading_hold_hook::u8at(base,0x83895C13u),loading_hold_hook::u8at(base,0x83895C20u),loading_hold_hook::u8at(base,0x83B13AECu));
        loading_hold_hook::log(l);
    }
}
// v1072: every write through the go-online byte's setter (stb r3 -> 0x83B13AEC), logged with its caller (first SetterLogMax), every mode;
// the original runs unchanged. LINK CONDITION: redirect-v1072-gosetter.ld (= v1057's without sub_82F91D18).
PPC_FUNC_IMPL(__imp__sub_82F91D18);
PPC_FUNC(sub_82F91D18) {
    const unsigned k=loading_hold_hook::setterCalls.fetch_add(1,std::memory_order_relaxed)+1;
    if(k<=loading_hold_hook::SetterLogMax) {
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE go-online setter #%u %u -> %u lr=%08x 5c13=%u 5c20=%u mode=%s",k,
            loading_hold_hook::u8at(base,0x83B13AECu),unsigned(ctx.r3.u8),std::uint32_t(ctx.lr),loading_hold_hook::u8at(base,0x83895C13u),
            loading_hold_hook::u8at(base,0x83895C20u),loading_hold_hook::online()?"online":"story/sandbox");
        loading_hold_hook::log(l);
    }
    __imp__sub_82F91D18(ctx,base);
}
// v1082 (lead, t-onlrev77-20261108-0210: "Story Mode (A)" still on the cards at 50 s and 85 s although v1080's hide ran at the freeze): the
// card prompt call itself. Every call is logged (first PromptLogMax: show, id, caller, the card word 0x83895C5C), every mode. Online mode,
// once the choice is frozen: a request to SHOW 44 ("Story Mode (A)") becomes a hide (A does nothing here; the 360's later cards read only
// "Loading Online"), logged. Everything else runs unchanged.
// LINK CONDITION: redirect-v1082-charprobe2.ld without sub_825B9AA0's line (checked by the batch).
namespace loading_hold_hook {constexpr unsigned PromptLogMax=40;inline std::atomic<unsigned> promptCalls{0},promptHeld{0};}
PPC_FUNC_IMPL(__imp__sub_825B9AA0);
PPC_FUNC(sub_825B9AA0) {
    const unsigned show=ctx.r3.u32,id=ctx.r4.u32;
    const bool keepHidden=loading_hold_hook::online() && ::recomp::online_mode::choiceFrozen.load(std::memory_order_acquire) && id==44u && show!=0u;
    // the card hook's own ~60 pre-freeze calls must not use up the log: 4 lines before the freeze, PromptLogMax after it
    const bool frozen=::recomp::online_mode::choiceFrozen.load(std::memory_order_acquire);
    static std::atomic<unsigned> before{0};
    const unsigned k=frozen?loading_hold_hook::promptCalls.fetch_add(1,std::memory_order_relaxed)+1:before.fetch_add(1,std::memory_order_relaxed)+1;
    if(k<=(frozen?loading_hold_hook::PromptLogMax:4u)) {
        char l[180];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE card-prompt %s#%u SET_BUTTONS(%u, %u) lr=%08x c5c=%u%s",frozen?"frozen ":"",k,show,id,std::uint32_t(ctx.lr),
            unsigned(PPC_LOAD_U32(0x83895C5Cu)),keepHidden?" -> kept hidden (Online, choice frozen)":"");
        loading_hold_hook::log(l);
    } else if(keepHidden && loading_hold_hook::promptHeld.load(std::memory_order_relaxed)%30u==0u) {
        loading_hold_hook::log("RENDER_ONLINE_MODE card-prompt show 44 kept hidden (later calls)");
    }
    if(keepHidden){loading_hold_hook::promptHeld.fetch_add(1,std::memory_order_relaxed);ctx.r3.s64=0;}
    __imp__sub_825B9AA0(ctx,base);
}
