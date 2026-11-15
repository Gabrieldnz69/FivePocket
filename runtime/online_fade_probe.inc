// online_fade_probe.inc (v1088, trainer-mods; lead 2026-11-08 03:5x after t-onlcre74-20261108-0337: the female stood on the hill ~3.5 s
// before fm_intro's scaffold shot. Hypothesis to test: on the 360 fm_intro fades out (DO_SCREEN_FADE_OUT), places the player, starts the
// cutscene, then fades in; if our port does not DRAW the game's screen fade while IS_SCREEN_FADED_OUT reports 1, the hill shows = a renderer
// item). READ ONLY, Online mode: every DO_SCREEN_FADE_OUT / DO_SCREEN_FADE_IN call (arg = ms), and each IS_SCREEN_FADED_OUT / _IN /
// IS_SCREEN_FADING_OUT / _IN answer when it changes for a script, with the calling script (online_script_name.h) and the time; at most
// FadeLogMax lines. The original runs unchanged. Compare the times with the frame recorder: "faded out = 1" over a world frame = renderer.
// Handlers (registration table): IS_SCREEN_FADED_OUT 9caa05fa sub_82D590A8, IS_SCREEN_FADED_IN 4f37276d sub_82D590E0, IS_SCREEN_FADING_OUT
// 79275a57 sub_82D59118, IS_SCREEN_FADING_IN c7c82800 sub_82D59150, DO_SCREEN_FADE_IN 66c1bdee sub_82D59188, DO_SCREEN_FADE_OUT 89d01805 sub_82D59198.
// LINK CONDITION: redirect-v1088-stats.ld drops these six wrappers too. "RENDER_ONLINE_FADE" lines prove it.
#include <atomic>
#include "online_script_name.h"
namespace online_fade_probe {
constexpr unsigned FadeLogMax=160;
inline std::atomic<unsigned> lines{0};
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fade-probe-off"),F_OK)==0;return o;}
inline bool want() noexcept {return loading_hold_hook::online() && !off() && lines.load(std::memory_order_relaxed)<FadeLogMax;}
inline std::uint32_t scriptOf(std::uint8_t* base,const PPCContext& ctx) noexcept {const std::uint32_t t=ctx.r23.u32;return t>=0x40000000u?PPC_LOAD_U32(t+4u):0u;}
inline void say(const char* what,std::uint32_t script,const char* tail) noexcept {
    lines.fetch_add(1,std::memory_order_relaxed);const char* n=::recomp::online_script::name(script);
    char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_FADE t=%llu %s script=%s(%08x) %s",(unsigned long long)loading_hold_hook::nowMs(),what,n?n:"?",script,tail);
    loading_hold_hook::log(l);
}
// the IS_* answers: one slot per (native, script) for the last 8 scripts seen
struct Last {std::uint32_t script=0;std::uint32_t v=0xFFFFFFFFu;};
inline Last last[4][8]{};
inline bool changed(unsigned which,std::uint32_t script,std::uint32_t v) noexcept {
    for(auto& e:last[which])if(e.script==script){if(e.v==v)return false;e.v=v;return true;}
    for(auto& e:last[which])if(!e.script){e.script=script;e.v=v;return true;}
    last[which][0]={script,v};return true;
}
}
#define ONLINE_FADE_DO(SUB,NAME) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    if(online_fade_probe::want()){const std::uint32_t a=PPC_LOAD_U32(ctx.r3.u32+8u);char t[48];std::snprintf(t,sizeof t,"ms=%d",a?int(PPC_LOAD_U32(a)):-1); \
        online_fade_probe::say(NAME,online_fade_probe::scriptOf(base,ctx),t);} \
    __imp__##SUB(ctx,base); \
}
#define ONLINE_FADE_IS(SUB,W,NAME) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    const bool w=online_fade_probe::want();const std::uint32_t c=ctx.r3.u32,s=w?online_fade_probe::scriptOf(base,ctx):0u; \
    __imp__##SUB(ctx,base); \
    if(w){const std::uint32_t rp=PPC_LOAD_U32(c),v=rp?PPC_LOAD_U32(rp):0xFFFFFFFFu; \
        if(online_fade_probe::changed(W,s,v)){char t[32];std::snprintf(t,sizeof t,"-> %d",int(v));online_fade_probe::say(NAME,s,t);}} \
}
ONLINE_FADE_DO(sub_82D59198,"DO_SCREEN_FADE_OUT")
ONLINE_FADE_DO(sub_82D59188,"DO_SCREEN_FADE_IN")
ONLINE_FADE_IS(sub_82D590A8,0,"IS_SCREEN_FADED_OUT")
ONLINE_FADE_IS(sub_82D590E0,1,"IS_SCREEN_FADED_IN")
ONLINE_FADE_IS(sub_82D59118,2,"IS_SCREEN_FADING_OUT")
ONLINE_FADE_IS(sub_82D59150,3,"IS_SCREEN_FADING_IN")
#undef ONLINE_FADE_DO
#undef ONLINE_FADE_IS
