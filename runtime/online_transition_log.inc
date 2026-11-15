// online_transition_log.inc (v1112, trainer-mods): READ ONLY logs of the transition / activity-session natives fmmc_launcher uses (t-onlroute-ugc-1811:
// the race corona is up, walking in shows the job intro + "Downloading" (FMMC_DOWNLOAD, corona state machine 0x16e94) for ~70 s and nothing launches).
// Online mode only; each new (script, ip, result, arg 0) once, at most TransLogMax lines per native; the originals always run, nothing answered.
// Included at the end of online_freemode_gate_hook.inc (online_fmgate helpers). Handlers from natives/registrations.json by the public names.
#include "online_transition.h"
// LINK CONDITION: redirect-v1112-translog.ld (= redirect-v1110-ugcv1.ld without these 30 mangled aliases). "RENDER_ONLINE_TRANS" lines prove it.
namespace online_trans {constexpr unsigned TransLogMax=40;}
#define ONLINE_TRANS_READ(SUB,NAME,NA) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    namespace og=online_fmgate; \
    if(!loading_hold_hook::online()){__imp__##SUB(ctx,base);return;} \
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx); \
    std::uint32_t A[3]{};for(unsigned i=0;i<unsigned(NA) && i<3u;++i)A[i]=og::arg(base,c,i); \
    __imp__##SUB(ctx,base); \
    const std::uint32_t r=og::ret(base,c); \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(lines.load(std::memory_order_relaxed)<online_trans::TransLogMax && seen.fresh(og::mix(s,ip,r,A[0]))) { \
        lines.fetch_add(1,std::memory_order_relaxed);char t[160];std::snprintf(t,sizeof t,"(%d,%d,%d) -> %d",int(A[0]),int(A[1]),int(A[2]),int(r)); \
        const char* n=::recomp::online_script::name(s);char l[260];std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS %s %s script=%s(%08x) ip=%x",NAME,t,n?n:"?",s,ip); \
        loading_hold_hook::log(l);} \
}
ONLINE_TRANS_READ(sub_82DA6A80,"NETWORK_APPLY_TRANSITION_PARAMETER",2)
ONLINE_TRANS_READ(sub_82DA6AA0,"NETWORK_APPLY_TRANSITION_PARAMETER_STRING",3)
ONLINE_TRANS_READ(sub_82DA6D88,"NETWORK_ARE_TRANSITION_DETAILS_VALID",1)
ONLINE_TRANS_READ(sub_82DA6CB0,"NETWORK_CHANGE_TRANSITION_SLOTS",2)
ONLINE_TRANS_READ(sub_82D99B80,"NETWORK_CLEAR_TRANSITION_CREATOR_HANDLE",0)
ONLINE_TRANS_READ(sub_82D99B70,"NETWORK_CLOSE_TRANSITION_MATCHMAKING",0)
// v1130: the engine's own transition script events (online_transition.h), built as sub_830625B8 builds them (a 24-byte object on the stack:
// vtable, 0, 0, byte, data, 0), added with the network group's add (vtable [1]), then the base vtable and the two destructor calls.
PPC_FUNC(sub_82295FD8);   // the script network event group
PPC_FUNC(sub_82377528);
PPC_FUNC(sub_82377518);
namespace online_trans_events {
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-events-off"),F_OK)==0;return o;}
inline void raise(PPCContext& ctx,std::uint8_t* base,std::uint32_t vt,std::uint32_t data,const char* what) {
    namespace ft=::recomp::fm_transition;
    const PPCContext saved=ctx;
    const std::uint32_t sp=(saved.r1.u32-512u)&~15u;PPC_STORE_U32(sp,saved.r1.u32);ctx.r1.u64=sp;
    const std::uint32_t obj=sp+128u;for(unsigned i=0;i<6u;++i)PPC_STORE_U32(obj+4u*i,0u);
    PPC_STORE_U32(obj,vt);PPC_STORE_U32(obj+16u,data);
    sub_82295FD8(ctx,base);const std::uint32_t grp=ctx.r3.u32;
    const std::uint32_t before=grp?PPC_LOAD_U32(grp+412u):0u;
    std::uint32_t addResult=0xFFFFFFFFu;   // v1132: the add's r3 (the queued copy, 0 = refused)
    if(grp) {
        ctx.r3.u64=grp;ctx.r4.u64=obj;ctx.ctr.u64=PPC_LOAD_U32(PPC_LOAD_U32(grp)+4u);PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
        addResult=ctx.r3.u32;   // v1132
    }
    const std::uint32_t after=grp?PPC_LOAD_U32(grp+412u):0u;
    PPC_STORE_U32(obj,ft::EventBaseVt);ctx.r3.u64=obj;sub_82377528(ctx,base);ctx.r3.u64=obj;sub_82377518(ctx,base);
    ctx=saved;
    char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS event raised: %s (vtable %08x data %u) group=%08x count %u -> %u add=%08x pending=%u",what,vt,data,grp,before,after,
        addResult,grp?unsigned(PPC_LOAD_U32(grp+416u+412u)):0u);
    loading_hold_hook::log(l);
}
}
// v1114: QUICKMATCH answered "started" for fmmc on the route (online_transition.h); every call counted, the first answers logged
PPC_FUNC_IMPL(__imp__sub_82DA6750);
PPC_FUNC(sub_82DA6750) {   // NETWORK_DO_TRANSITION_QUICKMATCH
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6750(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    std::uint32_t A[4]{};for(unsigned i=0;i<4u;++i)A[i]=og::arg(base,c,i);
    __imp__sub_82DA6750(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerQuickmatch(true,::recomp::fm_route::jobsOn(),off,s,r);
    const bool soloBefore=ft::solo.load(std::memory_order_acquire);   // v1133
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);ft::solo.store(true,std::memory_order_release);}
    if(ft::raiseNoResults(ans,online_trans_events::off()))online_trans_events::raise(ctx,base,ft::SessionEventVt,ft::NoResultsCode,"195 sub-code 25 (the quickmatch found nobody)");   // v1130
    if(ft::raiseSoloStarted(ans,soloBefore,online_trans_events::off()))online_trans_events::raise(ctx,base,ft::TransitionStartedVt,1u,"194 success 1 (the solo transition we host started)");   // v1133
    static std::atomic<unsigned> calls{0};const unsigned n=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(n<=6 || (n%20)==0){char t[200];std::snprintf(t,sizeof t,"(%d,%d,%d,%d) -> %d%s call #%u",int(A[0]),int(A[1]),int(A[2]),int(A[3]),ans?1:int(r),
        ans?" (ANSWERED: matchmaking started, the engine refused offline)":"",n);
        const char* nm=::recomp::online_script::name(s);char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_DO_TRANSITION_QUICKMATCH %s script=%s(%08x) ip=%x",t,nm?nm:"?",s,ip);
        loading_hold_hook::log(l);}
}
ONLINE_TRANS_READ(sub_82DA68F0,"NETWORK_DO_TRANSITION_TO_GAME",2)
ONLINE_TRANS_READ(sub_82DA7270,"NETWORK_GET_THIS_SCRIPT_IS_NETWORK_SCRIPT",0)
ONLINE_TRANS_READ(sub_82DA6D10,"NETWORK_HAS_PLAYER_STARTED_TRANSITION",1)
// v1130: NETWORK_HOST_TRANSITION - the original; refused for fmmc on the route -> reads 1 (hosted, the solo transition) and 194/1 is raised
PPC_FUNC_IMPL(__imp__sub_82DA6740);
PPC_FUNC(sub_82DA6740) {
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6740(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    std::uint32_t A[6]{};for(unsigned i=0;i<6u;++i)A[i]=og::arg(base,c,i);
    __imp__sub_82DA6740(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerHost(true,::recomp::fm_route::jobsOn(),off,s,r) && !online_trans_events::off();
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);ft::solo.store(true,std::memory_order_release);
            online_trans_events::raise(ctx,base,ft::TransitionStartedVt,1u,"194 success 1 (the transition hosted)");}
    static std::atomic<unsigned> calls{0};const unsigned n=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(n<=6 || (n%20)==0){char t[200];std::snprintf(t,sizeof t,"(%d,%d,%d,%d,%d,%d) -> %d%s call #%u",int(A[0]),int(A[1]),int(A[2]),int(A[3]),int(A[4]),int(A[5]),
        ans?1:int(r),ans?" (ANSWERED: hosted, 194 raised)":"",n);
        const char* nm=::recomp::online_script::name(s);char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_HOST_TRANSITION %s script=%s(%08x) ip=%x",t,nm?nm:"?",s,ip);
        loading_hold_hook::log(l);}
}
ONLINE_TRANS_READ(sub_82DA6E70,"NETWORK_IS_ACTIVITY_SESSION",0)
ONLINE_TRANS_READ(sub_82DA6358,"NETWORK_IS_IN_SESSION",0)
PPC_FUNC_IMPL(__imp__sub_82DA6B78);
PPC_FUNC(sub_82DA6B78) {   // NETWORK_IS_IN_TRANSITION: v1116 the solo transition (online_transition.h)
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6B78(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA6B78(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerSolo(true,::recomp::fm_route::jobsOn(),off,ft::solo.load(std::memory_order_acquire),s,r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);}
    static og::Seen seen;static std::atomic<unsigned> lines{0};
    if(lines.load(std::memory_order_relaxed)<online_trans::TransLogMax && seen.fresh(og::mix(s,ip,ans?1u:r,ans?0x5050u:0u))) {
        lines.fetch_add(1,std::memory_order_relaxed);const char* n=::recomp::online_script::name(s);char l[260];
        std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_IS_IN_TRANSITION -> %d%s script=%s(%08x) ip=%x",ans?1:int(r),ans?" (ANSWERED: the solo transition)":"",n?n:"?",s,ip);
        loading_hold_hook::log(l);}
}
ONLINE_TRANS_READ(sub_82DA6BD8,"NETWORK_IS_TRANSITION_BUSY",0)
PPC_FUNC_IMPL(__imp__sub_82DA6B30);
PPC_FUNC(sub_82DA6B30) {   // NETWORK_IS_TRANSITION_HOST: v1116 the solo transition (online_transition.h)
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6B30(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA6B30(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerSolo(true,::recomp::fm_route::jobsOn(),off,ft::solo.load(std::memory_order_acquire),s,r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);}
    static og::Seen seen;static std::atomic<unsigned> lines{0};
    if(lines.load(std::memory_order_relaxed)<online_trans::TransLogMax && seen.fresh(og::mix(s,ip,ans?1u:r,ans?0x5050u:0u))) {
        lines.fetch_add(1,std::memory_order_relaxed);const char* n=::recomp::online_script::name(s);char l[260];
        std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_IS_TRANSITION_HOST -> %d%s script=%s(%08x) ip=%x",ans?1:int(r),ans?" (ANSWERED: the solo transition)":"",n?n:"?",s,ip);
        loading_hold_hook::log(l);}
}
ONLINE_TRANS_READ(sub_82DA6C00,"NETWORK_IS_TRANSITION_OPEN_TO_MATCHMAKING",0)
ONLINE_TRANS_READ(sub_82DA6A60,"NETWORK_IS_TRANSITION_TO_GAME",0)
ONLINE_TRANS_READ(sub_82DA6DC8,"NETWORK_JOIN_TRANSITION",1)
PPC_FUNC_IMPL(__imp__sub_82DA6828);
PPC_FUNC(sub_82DA6828) {   // NETWORK_LEAVE_TRANSITION: the original; v1116 ends the solo transition
    namespace og=online_fmgate;
    if(!loading_hold_hook::online()){__imp__sub_82DA6828(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA6828(ctx,base);
    const bool was=::recomp::fm_transition::solo.exchange(false,std::memory_order_acq_rel);
    ::recomp::fm_transition::launched.store(false,std::memory_order_release);   // v1134
    static std::atomic<unsigned> lines{0};if(!was && lines.fetch_add(1,std::memory_order_relaxed)>=20)return;
    const std::uint32_t r=og::ret(base,c);const char* n=::recomp::online_script::name(s);char l[220];
    std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_LEAVE_TRANSITION -> %d (original; solo transition %s) script=%s(%08x) ip=%x",int(r),was?"ENDED":"was off",n?n:"?",s,ip);loading_hold_hook::log(l);
}
ONLINE_TRANS_READ(sub_82D99B60,"NETWORK_OPEN_TRANSITION_MATCHMAKING",0)
ONLINE_TRANS_READ(sub_82DA6AD0,"NETWORK_SEND_TRANSITION_GAMER_INSTRUCTION",5)
ONLINE_TRANS_READ(sub_82DA5EE0,"NETWORK_SESSION_ADD_ACTIVE_MATCHMAKING_GROUP",1)
ONLINE_TRANS_READ(sub_82DA5EA8,"NETWORK_SESSION_SET_MATCHMAKING_GROUP_MAX",2)
ONLINE_TRANS_READ(sub_82DA5F08,"NETWORK_SESSION_SET_UNIQUE_CREW_LIMIT",1)
ONLINE_TRANS_READ(sub_82DA6818,"NETWORK_SET_GAMER_INVITED_TO_TRANSITION",1)
ONLINE_TRANS_READ(sub_82DA6C40,"NETWORK_SET_TRANSITION_ACTIVITY_ID",1)
ONLINE_TRANS_READ(sub_82DA6E08,"NETWORK_SET_TRANSITION_CREATOR_HANDLE",1)
#undef ONLINE_TRANS_READ

// v1132 (read only): the network event group (1) as the scripts read it in Online mode - GET_NUMBER_OF_EVENTS a3525d60 (sub_82DD82B0),
// GET_EVENT_AT_INDEX b49c1442 (sub_82DD8390), GET_EVENT_DATA 4280f92f (sub_82DD8408): each new (script, native, group-1 index 0..3, type /
// result) once, at most EventLogMax lines; every event 194 / 195 seen, with its data words. LINK CONDITION: redirect-v1132-events.ld
// (= redirect-v1129-nettime.ld without these three binds). "RENDER_ONLINE_EVENTS" proves it.
namespace online_events_log {
constexpr unsigned EventLogMax=80;
inline std::atomic<unsigned> lines{0};
inline void say(const char* what,std::uint32_t s,std::uint32_t ip,const char* t) noexcept {
    const char* nm=::recomp::online_script::name(s);char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_EVENTS %s %s script=%s(%08x) ip=%x",what,t,nm?nm:"?",s,ip);loading_hold_hook::log(l);}
}
PPC_FUNC_IMPL(__imp__sub_82DD82B0);
PPC_FUNC(sub_82DD82B0) {   // GET_NUMBER_OF_EVENTS
    namespace og=online_fmgate;namespace el=online_events_log;
    if(!loading_hold_hook::online()){__imp__sub_82DD82B0(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32,g=og::arg(base,c,0);
    __imp__sub_82DD82B0(ctx,base);
    if(g!=1u)return;
    const std::uint32_t r=og::ret(base,c);if(r==0u)return;
    const std::uint32_t s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    static og::Seen seen;
    if(el::lines.load(std::memory_order_relaxed)<el::EventLogMax && seen.fresh(og::mix(s,ip,r,0))){el::lines.fetch_add(1,std::memory_order_relaxed);
        char t[80];std::snprintf(t,sizeof t,"GET_NUMBER_OF_EVENTS(1) -> %u",r);el::say("count",s,ip,t);}
}
PPC_FUNC_IMPL(__imp__sub_82DD8390);
PPC_FUNC(sub_82DD8390) {   // GET_EVENT_AT_INDEX
    namespace og=online_fmgate;namespace el=online_events_log;
    if(!loading_hold_hook::online()){__imp__sub_82DD8390(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32,g=og::arg(base,c,0),i=og::arg(base,c,1);
    __imp__sub_82DD8390(ctx,base);
    if(g!=1u)return;
    const std::uint32_t r=og::ret(base,c),s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    static og::Seen seen;const bool key=r==194u || r==195u;
    if((key || el::lines.load(std::memory_order_relaxed)<el::EventLogMax) && seen.fresh(og::mix(s,ip,r,i))){el::lines.fetch_add(1,std::memory_order_relaxed);
        char t[80];std::snprintf(t,sizeof t,"GET_EVENT_AT_INDEX(1, %u) -> %d",i,int(r));el::say("type",s,ip,t);}
}
PPC_FUNC_IMPL(__imp__sub_82DD8408);
PPC_FUNC(sub_82DD8408) {   // GET_EVENT_DATA
    namespace og=online_fmgate;namespace el=online_events_log;
    if(!loading_hold_hook::online()){__imp__sub_82DD8408(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32,g=og::arg(base,c,0),i=og::arg(base,c,1),n=og::arg(base,c,3);
    const std::uint32_t argp=PPC_LOAD_U32(c+8u),dst=argp?PPC_LOAD_U32(argp+8u):0u;
    __imp__sub_82DD8408(ctx,base);
    if(g!=1u)return;
    const std::uint32_t r=og::ret(base,c),s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    const std::uint32_t d0=dst>=0x10000u?PPC_LOAD_U32(dst):0u,d1=(dst>=0x10000u && n>1u)?PPC_LOAD_U32(dst+4u):0u;
    static og::Seen seen;
    if(el::lines.load(std::memory_order_relaxed)<el::EventLogMax && seen.fresh(og::mix(s,ip,r,(d0<<8)^n))){el::lines.fetch_add(1,std::memory_order_relaxed);
        char t[120];std::snprintf(t,sizeof t,"GET_EVENT_DATA(1, %u, size %u) -> %d data %u,%u",i,n,int(r),d0,d1);el::say("data",s,ip,t);}
}

// v1134: NETWORK_LAUNCH_TRANSITION (sub_82DA6888) / NETWORK_IS_TRANSITION_STARTED (sub_82DA6BA8): the solo transition's launch (online_transition.h);
// were v1112 read logs (same unbound link).
PPC_FUNC_IMPL(__imp__sub_82DA6888);
PPC_FUNC(sub_82DA6888) {   // NETWORK_LAUNCH_TRANSITION
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6888(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA6888(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerLaunch(true,::recomp::fm_route::jobsOn(),off,ft::solo.load(std::memory_order_acquire),s,r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);ft::launched.store(true,std::memory_order_release);}
    static std::atomic<unsigned> calls{0};const unsigned n=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(n<=6 || (n%20)==0){const char* nm=::recomp::online_script::name(s);char l[260];std::snprintf(l,sizeof l,
        "RENDER_ONLINE_TRANS NETWORK_LAUNCH_TRANSITION -> %d%s call #%u script=%s(%08x) ip=%x",ans?1:int(r),ans?" (ANSWERED: the solo transition launched)":"",n,nm?nm:"?",s,ip);
        loading_hold_hook::log(l);}
}
PPC_FUNC_IMPL(__imp__sub_82DA6BA8);
PPC_FUNC(sub_82DA6BA8) {   // NETWORK_IS_TRANSITION_STARTED
    namespace og=online_fmgate;namespace ft=::recomp::fm_transition;
    if(!loading_hold_hook::online()){__imp__sub_82DA6BA8(ctx,base);return;}
    static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-fm-trans-answer-off"),F_OK)==0;
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    __imp__sub_82DA6BA8(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    const bool ans=ft::answerStarted(true,::recomp::fm_route::jobsOn(),off,ft::launched.load(std::memory_order_acquire),s,r);
    if(ans){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);}
    static og::Seen seen;static std::atomic<unsigned> lines{0};
    if(lines.load(std::memory_order_relaxed)<online_trans::TransLogMax && seen.fresh(og::mix(s,ip,ans?1u:r,0))){lines.fetch_add(1,std::memory_order_relaxed);
        const char* nm=::recomp::online_script::name(s);char l[260];std::snprintf(l,sizeof l,"RENDER_ONLINE_TRANS NETWORK_IS_TRANSITION_STARTED -> %d%s script=%s(%08x) ip=%x",
        ans?1:int(r),ans?" (ANSWERED: the launched solo transition)":"",nm?nm:"?",s,ip);loading_hold_hook::log(l);}
}
