// online_freemode_gate_hook.inc (v1093, trainer-mods; lead 10:28, update-1/FREEMODE-GATE-2026-11-08.md). Included by renderer_boundary_trace.cpp
// after online_econ_log.inc. Online mode only for every answer; the logs name the calling script (online_script_name.h, the VM's r23+4)
// and its IP (r23+12, stored by the VM NATIVE handler before the call), so a line maps to maintransition's own addresses (0x36700 ...).
//  1. STAT_GET_BOOL 28a3dd2b sub_82DDC770 (hash, &out, p2) -> bool and STAT_GET_INT 1c6fe43e sub_82DDC6E0 (hash, &out, p2) -> bool:
//     online_freemode_gate.h decides (later visits only: MP0_CHAR_ISACTIVE -> 1 / out 1, MPPLY_LAST_MP_CHAR -> 1 / out 0, answered in memory,
//     the stat store untouched); every read of MP0/MP1_CHAR_ISACTIVE / MPPLY_LAST_MP_CHAR is logged (each new (script, ip, result) once).
//  2. READ ONLY, the next gates: IS_PLAYER_ONLINE 9fab6729 sub_82DA57A0, IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED 74c16879 sub_82DB6718,
//     d87f3a9e sub_82D64E08 (the go-online byte, sub_82232ED0), 6f203c6e sub_82D67C68, NETWORK_CAN_ENTER_MULTIPLAYER 4a23b9c9 sub_82DA5AF8,
//     NETWORK_SESSION_HOST 6716460f sub_82DA5CC0, NETWORK_SESSION_ENTER 543cd2be sub_82DA5B38, NETWORK_SET_RICH_PRESENCE 932a6ced
//     sub_82DFDB28 (audit: whether the newly reached path calls it): arguments (up to 3) and the result, each new (script, ip, result) once,
//     at most GateLogMax lines per native. The originals run unchanged.
// LINK CONDITION: redirect-v1093-fmgate.ld (= redirect-v1089-statsave.ld without these ten wrappers); v1096: redirect-v1096-ugcoff.ld also drops the six v1096 read wrappers. "RENDER_ONLINE_FMGATE" lines prove it.
#include <atomic>
#include "online_freemode_gate.h"
#include "online_script_name.h"
#include "online_log_key.h"   // v1098: the maintransition filter (A2)
#include "online_route_switch.h"   // v1101: the experiments default-off
#include "online_mt_adopt.h"   // v1102: stage C
#include "property_unlock_answer.h"   // v1501: the TEST-ONLY unlock-60 answer + stat 1287 / XP read log (online-doors)
#include "contact_missions.h"   // v1508: the TEST-ONLY rank-7 XP answer (contact-missions REPORT.md)
#include "door_chain_stats.h"   // v1510: the race -> garage cutscene chain stats, logged on change (doors REPORT.md)
#include "ammo_restore_diag.h"   // v1565 (diagnostic, default off): the scripts' reads of the five pistol ammo stats
namespace online_fmgate {
constexpr unsigned GateLogMax=40;
constexpr unsigned StatLogMax=120;   // v1098: every maintransition stat read is logged now (MP2..4, row 126 at 0x36700)
// v1101: DEFAULT-OFF: the answers need files/private-trace/diag-fm-route-on (online_route_switch.h); diag-fm-seed-off still turns them off
inline bool seedOffFile() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fm-seed-off"),F_OK)==0;return o;}
// v1140 DIAGNOSTIC (test runs only; the public apps never write diag-*): files/private-trace/diag-racetut-test answers MP0_FM_RACETUT_DONE 1 in
// memory, so a shop / menu trace run reaches the unlocked shops without driving Lamar's race first. Not a play path.
inline bool racetutTest() noexcept {static const bool t=::access(RECOMP_APP_PATH("/private-trace/diag-racetut-test"),F_OK)==0;return t;}
// v1143 DIAGNOSTIC (test runs only; lead 18:1x option C): files/private-trace/diag-trigtut-test answers MP0_FM_TRIGTUT_DONE (enum 120) 1 in
// memory, so a shop trace run reaches the tutorial stage after the drug deal (the owner's "Purchase some clothes") without the mission. Not a play path.
inline bool trigtutTest() noexcept {static const bool t=::access(RECOMP_APP_PATH("/private-trace/diag-trigtut-test"),F_OK)==0;return t;}
// v1146 DIAGNOSTIC (test runs only): files/private-trace/diag-rank-test answers MP0_CHAR_XP_FM (int enum 625) 3000 = rank 3 when lower, so
// freemode's unlock refresh 0xe029c (item 22 = the clothes shops needs rank 3) opens them as for a character with the race + drug deal RP. Not a play path.
inline bool rankTest() noexcept {static const bool t=::access(RECOMP_APP_PATH("/private-trace/diag-rank-test"),F_OK)==0;return t;}
// v1501 DIAGNOSTIC (test runs only; online-doors REPORT.md 4): files/private-trace/diag-unlock60-test answers MP0_CHAR_XP_FM / the enum-625 slot hashes
// with rank-5 XP (property_unlock_answer.h TestXp) and stat enum 1287 with bit 0 (the MP_INTRO_MCS_11 garage cutscene seen) in memory, so freemode's own
// 0x28789 / unlock 60 path opens the Dynasty 8 purchase and am_mp_property_ext's doors. Not a play path; never written by the public apps.
inline bool unlock60Test() noexcept {static const bool t=::access(RECOMP_APP_PATH("/private-trace/diag-unlock60-test"),F_OK)==0;return t;}
// v1508 DIAGNOSTIC (test runs only; contact-missions REPORT.md): files/private-trace/diag-contact-rank-test answers an XP stat read lower than rank 7
// (contact_missions.h RankXp7, the disc's tunables RANK_XP_7) with that value in memory, so freemode's own unlock refresh opens unlock 27 (am_npc_invites)
// and 97 (Lamar's "Request Job"). Checked before the unlock-60 branch (which only lifts XP to rank 5). Not a play path; never written by the public apps.
inline bool contactRankTest() noexcept {static const bool t=::access(RECOMP_APP_PATH("/private-trace/diag-contact-rank-test"),F_OK)==0;return t;}
// The game's stat-hash table words (property_unlock_answer.h statHashGlobal), through the VM's global block table; 0 = not readable.
inline std::uint32_t globalWord(std::uint8_t* base,std::uint32_t gl) noexcept {
    const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,bs=PPC_LOAD_U32(0x83C5CE48u+4u*b),sz=PPC_LOAD_U32(0x83C5CF50u+4u*b);
    return (bs>=0x10000u && !(bs&3u) && w<sz)?PPC_LOAD_U32(bs+4u*w):0u;}
inline void unlockHashes(std::uint8_t* base,::recomp::propunlock::StatRead& r) noexcept {
    namespace pu=::recomp::propunlock;
    for(std::uint32_t k=0;k<pu::Slots;++k){r.xpHashes[k]=globalWord(base,pu::statHashGlobal(pu::XpEnum,k));r.cutHashes[k]=globalWord(base,pu::statHashGlobal(pu::CutEnum,k));}}
inline ::recomp::propunlock::Decision unlockDecide(std::uint8_t* base,::recomp::propunlock::StatRead& r,std::uint32_t h,bool ok,std::uint32_t out) noexcept {
    unlockHashes(base,r);r.hash=h;r.readOk=ok;r.out=out;return ::recomp::propunlock::decide(true,true,r);}
constexpr std::uint32_t Mp0CharXpFm=0x3E68C21Fu,RankTestXp=3000u;
inline bool seedOff() noexcept {return ::recomp::fm_route::seedOff(::recomp::fm_route::routeOn(),seedOffFile());}
// v1105 (FREEMODE-GATE-6 C3): files/private-trace/diag-fm-intro-answer-off (read once) turns the intro answer off (diag-fm-seed-off does too, through seedOff())
inline bool introAnswerOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fm-intro-answer-off"),F_OK)==0;return o;}
struct Seen {std::uint32_t key[160]{};unsigned n=0;
    bool fresh(std::uint32_t k) noexcept {for(unsigned i=0;i<n;++i)if(key[i]==k)return false;if(n<160)key[n++]=k;return true;}};
inline std::uint32_t mix(std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d) noexcept {
    std::uint32_t h=2166136261u;for(std::uint32_t v:{a,b,c,d}){h=(h^v)*16777619u;}return h;}
inline std::uint32_t scriptOf(std::uint8_t* base,const PPCContext& c) noexcept {const std::uint32_t t=c.r23.u32;return t>=0x40000000u?PPC_LOAD_U32(t+4u):0u;}
inline std::uint32_t ipOf(std::uint8_t* base,const PPCContext& c) noexcept {const std::uint32_t t=c.r23.u32;return t>=0x40000000u?PPC_LOAD_U32(t+12u):0u;}
inline std::uint32_t arg(std::uint8_t* base,std::uint32_t ctxp,unsigned i) noexcept {const std::uint32_t a=PPC_LOAD_U32(ctxp+8u);return a?PPC_LOAD_U32(a+4u*i):0u;}
inline std::uint32_t ret(std::uint8_t* base,std::uint32_t ctxp) noexcept {const std::uint32_t r=PPC_LOAD_U32(ctxp);return r?PPC_LOAD_U32(r):0xFFFFFFFFu;}
inline void say(const char* what,std::uint32_t script,std::uint32_t ip,const char* tail) noexcept {
    const char* n=::recomp::online_script::name(script);
    char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_FMGATE %s %s script=%s(%08x) ip=%x",what,tail,n?n:"?",script,ip);loading_hold_hook::log(l);}
}
// ---- 1. the two stat reads
#define ONLINE_FMGATE_STAT(SUB,ISBOOL,NAME) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    namespace og=online_fmgate;namespace fg=::recomp::freemode_gate; \
    const std::uint32_t c=ctx.r3.u32,h=og::arg(base,c,0),outp=og::arg(base,c,1),s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx); \
    const bool on=loading_hold_hook::online(); \
    const fg::Answer a=fg::decide(on,og::seedOff(),fg::savedCharacter.load(std::memory_order_relaxed),h,ISBOOL); \
    std::uint32_t r=0,out=0;const char* how="original"; \
    ::recomp::propunlock::StatRead pur{};::recomp::propunlock::Decision pud{};   /* v1501 */ \
    ::recomp::contactmissions::Decision cmd{};   /* v1508 */ \
    if(a!=fg::Answer::Original) { \
        out=a==fg::Answer::Bool1?1u:0u;if(outp>=0x10000u && !(outp&3u))PPC_STORE_U32(outp,out); \
        const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;how="ANSWERED in memory (later visit)"; \
    } else { \
        __imp__##SUB(ctx,base);r=og::ret(base,c);out=(outp>=0x10000u && !(outp&3u))?PPC_LOAD_U32(outp):0xFFFFFFFFu; \
        if(fg::decideIntro(on,og::seedOff(),og::introAnswerOff(),fg::savedCharacter.load(std::memory_order_relaxed),fg::introRecord.load(std::memory_order_relaxed), \
                           h,ISBOOL,r!=0u,out==0xFFFFFFFFu?0u:out) && outp>=0x10000u && !(outp&3u)) {   /* v1105 (FREEMODE-GATE-6 C) */ \
            PPC_STORE_U32(outp,1u);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=1;how="ANSWERED in memory (intro record)"; \
        } \
        else if(on && ISBOOL && h==fg::Mp0FmRacetutDone && out!=1u && outp>=0x10000u && !(outp&3u) && og::racetutTest()) {   /* v1140 DIAGNOSTIC (test runs only) */ \
            PPC_STORE_U32(outp,1u);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=1;how="DIAGNOSTIC diag-racetut-test"; \
        } \
        else if(on && ISBOOL && h==fg::Mp0FmTrigtutDone && out!=1u && outp>=0x10000u && !(outp&3u) && og::trigtutTest()) {   /* v1143 DIAGNOSTIC (test runs only) */ \
            PPC_STORE_U32(outp,1u);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=1;how="DIAGNOSTIC diag-trigtut-test"; \
        } \
        else if(on && !ISBOOL && outp>=0x10000u && !(outp&3u) && og::contactRankTest() \
                && (og::unlockHashes(base,pur),pur.hash=h,true) \
                && (cmd=::recomp::contactmissions::decideXp(true,true,::recomp::propunlock::isXp(pur),r!=0u,out)).write) {   /* v1508 DIAGNOSTIC (test runs only) */ \
            PPC_STORE_U32(outp,cmd.value);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=cmd.value;how=cmd.why; \
        } \
        else if(on && !ISBOOL && outp>=0x10000u && !(outp&3u) && og::unlock60Test() \
                && (pud=og::unlockDecide(base,pur,h,r!=0u,out)).write) {   /* v1501 DIAGNOSTIC (test runs only) */ \
            PPC_STORE_U32(outp,pud.value);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=pud.value;how=pud.why; \
        } \
        else if(on && !ISBOOL && h==og::Mp0CharXpFm && (out==0xFFFFFFFFu || out<og::RankTestXp) && outp>=0x10000u && !(outp&3u) && og::rankTest()) {   /* v1146 DIAGNOSTIC (test runs only) */ \
            PPC_STORE_U32(outp,og::RankTestXp);const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);r=1;out=og::RankTestXp;how="DIAGNOSTIC diag-rank-test"; \
        } \
    } \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(on && !ISBOOL && pur.hash==0u && lines.load(std::memory_order_relaxed)<og::StatLogMax){og::unlockHashes(base,pur);pur.hash=h;}   /* v1501: stat 1287 / XP reads logged */ \
    if(on && !::recomp::ammo_diag::ownCall && (fg::watched(h) || h==og::Mp0CharXpFm || (!ISBOOL && (::recomp::propunlock::isXp(pur) || ::recomp::propunlock::isCut(pur))) || s==::recomp::online_log::Maintransition) && lines.load(std::memory_order_relaxed)<og::StatLogMax && seen.fresh(og::mix(h,s,ip,r^(out<<8)))) { \
        lines.fetch_add(1,std::memory_order_relaxed);char t[160];std::snprintf(t,sizeof t,"hash=%08x -> %d out=%d (%s, saved character %d)",h,int(r),int(out),how, \
            fg::savedCharacter.load(std::memory_order_relaxed));og::say(NAME,s,ip,t);} \
    if(!ISBOOL && ::recomp::ammo_diag::on() && !::recomp::ammo_diag::ownCall) {   /* v1565 (diagnostic, read only): the value the script got */ \
        char l[240];if(::recomp::ammo_diag::statRead(h,s,ip,r,out,l,sizeof l))loading_hold_hook::log(l);} \
    if(on && ::recomp::doorchain::statWatch().changed(h,r!=0u,out)) {   /* v1510: read only (the value the game got), each change once */ \
        char t[160];std::snprintf(t,sizeof t,"RENDER_ONLINE_DOORCHAIN stat %s hash=%08x -> %d out=%d (%s) script=",::recomp::doorchain::Stats[::recomp::doorchain::statIndex(h)].name,h,int(r),int(out),how); \
        const char* n=::recomp::online_script::name(s);char l[240];std::snprintf(l,sizeof l,"%s%s(%08x) ip=%x",t,n?n:"?",s,ip);loading_hold_hook::log(l);} \
}
ONLINE_FMGATE_STAT(sub_82DDC770,true,"STAT_GET_BOOL")
ONLINE_FMGATE_STAT(sub_82DDC6E0,false,"STAT_GET_INT")
#undef ONLINE_FMGATE_STAT
// ---- 2. read-only, the next gates
#define ONLINE_FMGATE_READ(SUB,NAME,NA) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    namespace og=online_fmgate; \
    if(!loading_hold_hook::online()){__imp__##SUB(ctx,base);return;} \
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx); \
    std::uint32_t A[3]{};for(unsigned i=0;i<unsigned(NA) && i<3u;++i)A[i]=og::arg(base,c,i); \
    __imp__##SUB(ctx,base); \
    const std::uint32_t r=og::ret(base,c); \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(s,ip,r,A[0]))) { \
        lines.fetch_add(1,std::memory_order_relaxed);char t[120];std::snprintf(t,sizeof t,"(%d,%d,%d) -> %d",int(A[0]),int(A[1]),int(A[2]),int(r));og::say(NAME,s,ip,t);} \
}
ONLINE_FMGATE_READ(sub_82DA57A0,"IS_PLAYER_ONLINE",0)
ONLINE_FMGATE_READ(sub_82DB6718,"IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED",0)
ONLINE_FMGATE_READ(sub_82D64E08,"d87f3a9e(go-online byte)",0)
ONLINE_FMGATE_READ(sub_82D67C68,"6f203c6e",0)
ONLINE_FMGATE_READ(sub_82DA5AF8,"NETWORK_CAN_ENTER_MULTIPLAYER",0)
// v1102 (round 5 stage C, FREEMODE-GATE-5-2026-11-08.md D): maintransition's T18 NETWORK_SESSION_ENTER (ip 0x666b1, a0 == 0) and HOST (its 6 ips)
// adopt our L1 host IN THE NATIVE'S FRAME (online_mt_adopt.h decides; online_l0.h hostForGame refuses unless its gates hold) and return the
// REAL host result through [[r3]] (the native's own result store). Only with diag-fm-route-on and not diag-mt-adopt-off; every other caller,
// ip or state: the original and v1096's read-only log.
#define ONLINE_FMGATE_ADOPT(SUB,NAME,NA) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    namespace og=online_fmgate;namespace ma=::recomp::mt_adopt; \
    if(!loading_hold_hook::online()){__imp__##SUB(ctx,base);return;} \
    const std::uint32_t c=ctx.r3.u32,s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx); \
    std::uint32_t A[3]{};for(unsigned i=0;i<unsigned(NA) && i<3u;++i)A[i]=og::arg(base,c,i); \
    int mt=-1; \
    if(s==ma::Maintransition){const std::uint32_t gl=ma::MtGlobal,b=gl>>18,w=gl&0x3FFFFu,bs=PPC_LOAD_U32(0x83C5CE48u+4u*b),sz=PPC_LOAD_U32(0x83C5CF50u+4u*b); \
                              if(bs>=0x10000u && !(bs&3u) && w<sz)mt=int(PPC_LOAD_U32(bs+4u*w));} \
    const ma::Adopt k=ma::decide(::recomp::fm_route::routeOn(),ma::off(),s,ip,mt,online_l0_hook::sequence().awaitingGame(),A[0]); \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(k==ma::Adopt::Original) { \
        __imp__##SUB(ctx,base); \
        const std::uint32_t r=og::ret(base,c); \
        if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(s,ip,r,A[0]))) { \
            lines.fetch_add(1,std::memory_order_relaxed);char t[120];std::snprintf(t,sizeof t,"(%d,%d,%d) -> %d",int(A[0]),int(A[1]),int(A[2]),int(r));og::say(NAME,s,ip,t);} \
        return; \
    } \
    online_l0_hook::GuestMem m(base);online_l0_hook::GuestOps ops(ctx,base); \
    const std::uint32_t frame=PPC_LOAD_U32(0x83C5D10Cu); \
    const std::uint32_t r=online_l0_hook::sequence().hostForGame(m,ops,online_l0_hook::log,online_l0_hook::nowMs(),frame,ip,recompL0TidNow()); \
    const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,r); \
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(s,ip,r,0xADADu))) { \
        lines.fetch_add(1,std::memory_order_relaxed);const std::uint32_t netobj=m.u32(recomp::online_l0::NetObjectWord); \
        char t[200];std::snprintf(t,sizeof t,"ADOPTED -> %u (local host: state=%u mt=%d args=(%d,%d,%d))",r,netobj?m.u32(netobj+recomp::online_l0::StateOffset):0u,mt, \
            int(A[0]),int(A[1]),int(A[2]));og::say(NAME,s,ip,t);} \
    if(r==1u){ma::entered.store(true,std::memory_order_release);ma::enteredMs.store(online_l0_hook::nowMs(),std::memory_order_release);ma::enteredIp.store(ip,std::memory_order_release); \
        char l2[120];std::snprintf(l2,sizeof l2,"RENDER_ONLINE_MODE adopt: maintransition T18 entered the local session at ip %x",ip);loading_hold_hook::log(l2);} \
}
ONLINE_FMGATE_ADOPT(sub_82DA5CC0,"NETWORK_SESSION_HOST",3)
ONLINE_FMGATE_ADOPT(sub_82DA5B38,"NETWORK_SESSION_ENTER",3)
#undef ONLINE_FMGATE_ADOPT
ONLINE_FMGATE_READ(sub_82DFDB28,"NETWORK_SET_RICH_PRESENCE",3)
// v1096 (lead 11:21, FREEMODE-GATE-2: the busy test 0x6590f and the T18 / session-end gates, read only)
ONLINE_FMGATE_READ(sub_82DBD5F0,"798d6c27(ugc busy)",0)
ONLINE_FMGATE_READ(sub_82DA6320,"NETWORK_IS_SESSION_ACTIVE",0)
ONLINE_FMGATE_READ(sub_82DA6400,"NETWORK_CAN_SESSION_END",0)
ONLINE_FMGATE_READ(sub_82DA5D58,"NETWORK_SESSION_END",2)
ONLINE_FMGATE_READ(sub_82DB4AC0,"STAT_CLEAR_SLOT_FOR_RELOAD",1)
ONLINE_FMGATE_READ(sub_82DA5F38,"NETWORK_SESSION_VALIDATE_JOIN",1)
// v1097 (lead 11:27): am_pi_menu's close check 0x138f6: 0x741(PLAYER_ID, 1, 1) = NETWORK_IS_PLAYER_ACTIVE (ip ~0x74c) && IS_PLAYER_PLAYING (~0x75a)
// && freemode's per-player flag G1338499.f3[player]; then !IS_PLAYER_CONTROL_ON (~0x1392b) unless static 68 bit 9. Logged ONLY when am_pi_menu
// (06f27211) calls them, each new (ip, answer, arg 0) once, at most GateLogMax: which one answers false names the closing condition.
#define ONLINE_FMGATE_PIM(SUB,NAME,NA) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    namespace og=online_fmgate; \
    const std::uint32_t s=loading_hold_hook::online()?og::scriptOf(base,ctx):0u; \
    if(s!=0x06F27211u){__imp__##SUB(ctx,base);return;} \
    const std::uint32_t c=ctx.r3.u32,ip=og::ipOf(base,ctx);std::uint32_t A[2]{};for(unsigned i=0;i<unsigned(NA) && i<2u;++i)A[i]=og::arg(base,c,i); \
    __imp__##SUB(ctx,base); \
    const std::uint32_t r=og::ret(base,c); \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(ip,r,A[0],0u))) { \
        lines.fetch_add(1,std::memory_order_relaxed);char t[96];std::snprintf(t,sizeof t,"(%d) -> %d",int(A[0]),int(r));og::say(NAME,s,ip,t);} \
}
ONLINE_FMGATE_PIM(sub_82DA7578,"pim NETWORK_IS_PLAYER_ACTIVE",1)
ONLINE_FMGATE_PIM(sub_82DB1198,"pim IS_PLAYER_PLAYING",1)
ONLINE_FMGATE_PIM(sub_82DB1CE0,"pim IS_PLAYER_CONTROL_ON",1)
#undef ONLINE_FMGATE_PIM
// v1098 (lead 12:16, FREEMODE-GATE-3 A2): T17's step 2 (0x63124, the character lineup builder, slots 0..4) never returns 1; which native fails
// inside 0x5686 -> 0x3638c is unknown. READ ONLY, maintransition only: the script is checked BEFORE the dedup (other scripts: one compare, no
// entry); then the original; each new (ip, answer, arg 0, arg 1) once, at most GateLogMax lines per native.
#define ONLINE_FMGATE_MTREAD(SUB,NAME,NA) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    story_local_glue::pre_##SUB(ctx,base);   /* v1352: story_create_local.inc (only CREATE_PED's acts; the others are empty) */ \
    namespace og=online_fmgate; \
    const std::uint32_t s=loading_hold_hook::online()?og::scriptOf(base,ctx):0u; \
    if(!::recomp::online_log::mtCaller(loading_hold_hook::online(),s)){__imp__##SUB(ctx,base);return;} \
    const std::uint32_t c=ctx.r3.u32,ip=og::ipOf(base,ctx);std::uint32_t A[2]{};for(unsigned i=0;i<unsigned(NA) && i<2u;++i)A[i]=og::arg(base,c,i); \
    __imp__##SUB(ctx,base); \
    const std::uint32_t r=og::ret(base,c); \
    static og::Seen seen;static std::atomic<unsigned> lines{0}; \
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(ip,r,A[0],A[1]))) { \
        lines.fetch_add(1,std::memory_order_relaxed);char t[110];std::snprintf(t,sizeof t,"(%08x,%08x) -> %d",A[0],A[1],int(r));og::say(NAME,s,ip,t);} \
}
ONLINE_FMGATE_MTREAD(sub_82DB5E68,"mt REQUEST_MODEL",1)
ONLINE_FMGATE_MTREAD(sub_82DB5E98,"mt HAS_MODEL_LOADED",1)
ONLINE_FMGATE_MTREAD(sub_82DAE978,"mt CREATE_PED",2)
ONLINE_FMGATE_MTREAD(sub_82D5C598,"mt DOES_ENTITY_EXIST",1)
ONLINE_FMGATE_MTREAD(sub_82D77C78,"mt IS_ENTITY_DEAD",1)
ONLINE_FMGATE_MTREAD(sub_82DD3C50,"mt HAS_PED_HEAD_BLEND_FINISHED",1)
ONLINE_FMGATE_MTREAD(sub_82DD1348,"mt IS_PED_INJURED",1)
ONLINE_FMGATE_MTREAD(sub_82D8A300,"mt GET_ENTITY_MODEL",1)
ONLINE_FMGATE_MTREAD(sub_82DB4BA8,"mt STAT_SAVE_PENDING",0)
#undef ONLINE_FMGATE_MTREAD
// v1127: STAT_SAVE_PENDING_OR_REQUESTED (sub_82DB4BE0) - the v1098 read log for maintransition, plus online_save_wait.h's answer (any caller).
#include "online_save_wait.h"
PPC_FUNC_IMPL(__imp__sub_82DB4BE0);
PPC_FUNC(sub_82DB4BE0) {
    namespace og=online_fmgate;namespace sw=::recomp::save_wait;
    const bool on=loading_hold_hook::online();
    if(!on){__imp__sub_82DB4BE0(ctx,base);return;}
    const std::uint32_t s=og::scriptOf(base,ctx),c=ctx.r3.u32,ip=og::ipOf(base,ctx);
    __imp__sub_82DB4BE0(ctx,base);
    const std::uint32_t r=og::ret(base,c);
    static const bool real=::access(RECOMP_APP_PATH("/private-trace/diag-stats-save-wait-real"),F_OK)==0;
    const std::uint32_t req=PPC_LOAD_U32(sw::Request+sw::RequestedOff),done=PPC_LOAD_U32(sw::Request+sw::DoneOff);
    const bool task=PPC_LOAD_U8(sw::Request+sw::TaskOff)!=0u,running=(PPC_LOAD_U8(sw::Request+sw::RunningOff)&0x80u) && PPC_LOAD_U8(sw::Request+sw::ActiveOff);
    const bool zero=sw::answerZero(online_stats_hook::localOn(),::recomp::fm_route::routeOn(),real,r,req,done,task,running);
    if(zero){const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,0u);}
    static og::Seen seen;static std::atomic<unsigned> lines{0};
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(ip,r,zero?1u:0u,s))) {
        lines.fetch_add(1,std::memory_order_relaxed);char t[160];
        std::snprintf(t,sizeof t,"(00000000,00000000) -> %d%s (requested %08x done %08x task %d running %d)",zero?0:int(r),zero?" ANSWERED 0: a queued save nothing services":"",req,done,task?1:0,running?1:0);
        og::say("mt STAT_SAVE_PENDING_OR_REQUESTED",s,ip,t);}
}
// v1129: GET_NETWORK_TIME (sub_82DA96E8) / GET_NETWORK_TIME_ACCURATE (sub_82DA9720): online_net_time.h's answer (any caller). LINK CONDITION:
// redirect-v1129-nettime.ld (= redirect-v1301-pollstack.ld without these two binds). "RENDER_ONLINE_NETTIME" proves it.
#include "online_net_time.h"
#include "local_session_flag.h"
namespace online_nettime {
inline std::atomic<unsigned> answered{0};
inline void run(PPCContext& ctx,std::uint8_t* base,void(*orig)(PPCContext&,std::uint8_t*),const char* name) {
    namespace nt=::recomp::net_time;
    if(!loading_hold_hook::online()){orig(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32;
    orig(ctx,base);
    const std::uint32_t rp=PPC_LOAD_U32(c);if(!rp)return;
    const std::uint32_t r=PPC_LOAD_U32(rp),obj=PPC_LOAD_U32(nt::NetTimeObjPtr);
    const bool running=obj>=0x10000u && PPC_LOAD_U32(obj+nt::ReadyOff)!=0u;
    static const bool real=::access(RECOMP_APP_PATH("/private-trace/diag-net-time-real"),F_OK)==0;
    if(!nt::answer(online_stats_hook::localOn(),::recomp::fm_route::routeOn(),real,::recomp::local_session::active(),running,r))return;
    const std::uint32_t t=PPC_LOAD_U32(nt::GameTimer);PPC_STORE_U32(rp,t);
    const unsigned k=answered.fetch_add(1,std::memory_order_relaxed)+1;
    if(k==1 || k==100 || k%20000==0){char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_NETTIME %s answered from the game timer (network clock not running): %u ms (answer #%u)",name,t,k);loading_hold_hook::log(l);}
}
}
PPC_FUNC_IMPL(__imp__sub_82DA96E8);
PPC_FUNC(sub_82DA96E8) {online_nettime::run(ctx,base,__imp__sub_82DA96E8,"GET_NETWORK_TIME");}
PPC_FUNC_IMPL(__imp__sub_82DA9720);
PPC_FUNC(sub_82DA9720) {online_nettime::run(ctx,base,__imp__sub_82DA9720,"GET_NETWORK_TIME_ACCURATE");}
// v1100 (lead, round 4 stage B1, FREEMODE-GATE-4-2026-11-08.md): STAT_GET_MASKED_INT e9d9b70f sub_82DB4D50 (key, &out, bit, nbits, p4) -> ok
// (the handler passes args 0..3 to sub_82AB0170 and stores r3 & 0xFF to the result). Not Online mode: the original, one branch. Other keys:
// v1098's read-only maintransition log, now with the value read. MP0_PSTAT_INT0 (0x724BAE22): the original, then decideMasked (online_freemode_gate.h:
// bit 0 -> 9 / bit 16 -> the saved sex, blanks only, later visits only); on an answer *out and *result are written (memory answer, no store);
// every Online reader gets it (freemode reads 386 too); each new (script, ip, bit, value) logged once, at most GateLogMax.
PPC_FUNC_IMPL(__imp__sub_82DB4D50);
PPC_FUNC(sub_82DB4D50) {
    namespace og=online_fmgate;namespace fg=::recomp::freemode_gate;
    if(!loading_hold_hook::online()){__imp__sub_82DB4D50(ctx,base);return;}
    const std::uint32_t c=ctx.r3.u32,key=og::arg(base,c,0),outp=og::arg(base,c,1),bit=og::arg(base,c,2),nb=og::arg(base,c,3);
    const std::uint32_t s=og::scriptOf(base,ctx),ip=og::ipOf(base,ctx);
    const bool outOk=outp>=0x10000u && !(outp&3u);
    __imp__sub_82DB4D50(ctx,base);
    const std::uint32_t r=og::ret(base,c),out=outOk?PPC_LOAD_U32(outp):0xFFFFFFFFu;
    if(key!=fg::Mp0PstatInt0) {   // v1098's read-only path (maintransition only), + the value read
        if(!::recomp::online_log::mtCaller(true,s))return;
        static og::Seen seen;static std::atomic<unsigned> lines{0};
        if(lines.load(std::memory_order_relaxed)<og::StatLogMax && seen.fresh(og::mix(ip,r,key,bit))) {   // v1102: the B2 inventory (cap 120)
            lines.fetch_add(1,std::memory_order_relaxed);char t[120];std::snprintf(t,sizeof t,"(%08x,bit %u,%u) -> %d out=%d",key,bit,nb,int(r),int(out));og::say("mt STAT_GET_MASKED_INT",s,ip,t);}
        return;
    }
    static std::atomic<bool> typeAnswered{false};
    const bool origOk=(r&0xFFu)!=0u;
    const fg::Masked m=fg::decideMasked(true,og::seedOff(),fg::savedCharacter.load(std::memory_order_relaxed),fg::savedSexSeed.load(std::memory_order_relaxed),
                                        key,bit,nb,origOk,outOk?out:0u,typeAnswered.load(std::memory_order_relaxed));
    if(m.answer) {
        if(outOk)PPC_STORE_U32(outp,m.value);
        const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);
        if(bit==0u)typeAnswered.store(true,std::memory_order_relaxed);
    }
    static og::Seen seen;static std::atomic<unsigned> lines{0};
    const std::uint32_t shown=m.answer?m.value:out;
    if(lines.load(std::memory_order_relaxed)<og::GateLogMax && seen.fresh(og::mix(s,ip,bit,shown^(m.answer?0x80000000u:0u)))) {
        lines.fetch_add(1,std::memory_order_relaxed);char t[200];
        if(m.answer)std::snprintf(t,sizeof t,"ANSWERED in memory hash=724bae22 bit=%u orig=(%d,%d) -> %u (packed %d, saved character %d)",bit,origOk?1:0,int(out),m.value,384+int(bit/8u),
                                  fg::savedCharacter.load(std::memory_order_relaxed));
        else std::snprintf(t,sizeof t,"original hash=724bae22 bit=%u nbits=%u -> (%d,%d)",bit,nb,origOk?1:0,int(out));
        og::say("STAT_GET_MASKED_INT",s,ip,t);
    }
}
#undef ONLINE_FMGATE_READ

#include "online_tut_session_hook.inc"   // v1107: the tutorial-session natives on the freemode route
#include "online_ugc_offline_hook.inc"   // v1110: the offline job list (disc v1 layout) on the freemode route
#include "online_transition_log.inc"   // v1112: read-only transition-native logs (the corona's Downloading)
