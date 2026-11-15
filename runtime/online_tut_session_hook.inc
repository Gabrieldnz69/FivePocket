// online_tut_session_hook.inc (v1107, trainer-mods): the tutorial-session natives on the freemode route (online_tut_session.h). Included at the end of
// online_freemode_gate_hook.inc (its online_fmgate helpers: the calling script / IP from the VM's r23, the argument block, the return word).
// NETWORK_ALLOW_GANG_TO_JOIN_TUTORIAL_SESSION ffb2ada1 sub_82DA9D78 (a, b), NETWORK_END_TUTORIAL_SESSION ba57e53e sub_82DA9D88,
// NETWORK_IS_IN_TUTORIAL_SESSION 34dd7b28 sub_82DA9DD0 -> BOOL, NETWORK_IS_TUTORIAL_SESSION_CHANGE_PENDING a003c40b sub_82DA9E38 -> BOOL.
// The originals always run; only the two reads may be answered. Every new (native, script, ip, result) logged, at most TutLogMax lines.
// LINK CONDITION: redirect-v1108-tutsess.ld (= redirect-v1098-mtlineup.ld without the four MANGLED aliases _Z12sub_<x>R10PPCContextPh = __imp__sub_<x>;
// v1107 linked without that and its overrides were not live - the ld assignment wins over a C++ definition). "RENDER_ONLINE_TUT" lines prove it.
#include "online_tut_session.h"
namespace online_tut {
constexpr unsigned TutLogMax=80;
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fm-tut-answer-off"),F_OK)==0;return o;}
inline bool route() noexcept {return ::recomp::fm_route::routeOn() && ::recomp::mt_adopt::entered.load(std::memory_order_acquire);}
inline online_fmgate::Seen seen;inline std::atomic<unsigned> lines{0};
inline void say(const char* what,std::uint32_t s,std::uint32_t ip,const char* tail,std::uint32_t key) noexcept {
    if(lines.load(std::memory_order_relaxed)>=TutLogMax || !seen.fresh(online_fmgate::mix(key,s,ip,0x7475u)))return;
    lines.fetch_add(1,std::memory_order_relaxed);
    char t[200];std::snprintf(t,sizeof t,"%s route=%d asked=%d ended=%d",tail,route()?1:0,::recomp::tut_session::asked.load()?1:0,::recomp::tut_session::ended.load()?1:0);
    char l[300];const char* n=::recomp::online_script::name(s);std::snprintf(l,sizeof l,"RENDER_ONLINE_TUT %s %s script=%s(%08x) ip=%x",what,t,n?n:"?",s,ip);loading_hold_hook::log(l);
}
}
PPC_FUNC_IMPL(__imp__sub_82DA9D78);
PPC_FUNC(sub_82DA9D78) {   // NETWORK_ALLOW_GANG_TO_JOIN_TUTORIAL_SESSION
    if(!loading_hold_hook::online()){__imp__sub_82DA9D78(ctx,base);return;}
    namespace og=online_fmgate;namespace ts=::recomp::tut_session;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx),a0=og::arg(base,c,0),a1=og::arg(base,c,1);
    __imp__sub_82DA9D78(ctx,base);
    if(online_tut::route()){ts::asked.store(true);ts::ended.store(false);}
    char t[80];std::snprintf(t,sizeof t,"(%d,%d) original ran",int(a0),int(a1));online_tut::say("ALLOW_GANG_TO_JOIN_TUTORIAL_SESSION",s,ip,t,1u);
}
PPC_FUNC_IMPL(__imp__sub_82DA9D88);
PPC_FUNC(sub_82DA9D88) {   // NETWORK_END_TUTORIAL_SESSION
    if(!loading_hold_hook::online()){__imp__sub_82DA9D88(ctx,base);return;}
    namespace og=online_fmgate;namespace ts=::recomp::tut_session;
    const std::uint32_t s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA9D88(ctx,base);
    if(ts::asked.load())ts::ended.store(true);
    online_tut::say("END_TUTORIAL_SESSION",s,ip,"original ran",2u);
}
PPC_FUNC_IMPL(__imp__sub_82DA9DD0);
PPC_FUNC(sub_82DA9DD0) {   // NETWORK_IS_IN_TUTORIAL_SESSION
    if(!loading_hold_hook::online()){__imp__sub_82DA9DD0(ctx,base);return;}
    namespace og=online_fmgate;namespace ts=::recomp::tut_session;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA9DD0(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ts::answerIn(online_tut::route(),online_tut::off(),ts::asked.load(),ts::ended.load(),r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);}
    char t[80];std::snprintf(t,sizeof t,"-> %d%s",ans?1:int(r),ans?" (ANSWERED in memory: the solo tutorial session)":"");online_tut::say("IS_IN_TUTORIAL_SESSION",s,ip,t,ans?0x13u:0x10u^r);
}
PPC_FUNC_IMPL(__imp__sub_82DA9E38);
PPC_FUNC(sub_82DA9E38) {   // NETWORK_IS_TUTORIAL_SESSION_CHANGE_PENDING
    if(!loading_hold_hook::online()){__imp__sub_82DA9E38(ctx,base);return;}
    namespace og=online_fmgate;namespace ts=::recomp::tut_session;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA9E38(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ts::answerPending(online_tut::route(),online_tut::off(),ts::asked.load(),ts::ended.load(),r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,0u);}
    char t[80];std::snprintf(t,sizeof t,"-> %d%s",ans?0:int(r),ans?" (ANSWERED in memory)":"");online_tut::say("IS_TUTORIAL_SESSION_CHANGE_PENDING",s,ip,t,ans?0x23u:0x20u^r);
}
