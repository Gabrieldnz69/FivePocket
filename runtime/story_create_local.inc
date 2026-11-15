// story_create_local.inc (v1352, update-1 session 2026-11-10). Included by renderer_boundary_trace.cpp BEFORE online_freemode_gate_hook.inc
// (whose v1098 macro calls story_local_glue::pre_<SUB>; CREATE_PED's applies the rule). story_create_local.h has the evidence and the rule. LINK CONDITION:
// the attempt's redirect script without CREATE_VEHICLE sub_82E06618, CREATE_OBJECT sub_82E03B20, CREATE_OBJECT_NO_OFFSET sub_82E03B30
// (v1352: redirect-v1352-storelocal.ld from v1154-natreg; v1353: redirect-v1353-storelocal.ld from v1156-signin2); CREATE_PED sub_82DAE978
// is out since v1098. "RENDER_STORY_LOCAL" proves it.
#include "story_create_local.h"
#include "sandbox_runtime.h"
#include "local_session_flag.h"
namespace story_local_glue {
struct GuestMem {std::uint8_t* base;
    std::uint32_t load(std::uint32_t a) noexcept {return PPC_LOAD_U32(a);}
    void store(std::uint32_t a,std::uint32_t v) noexcept {PPC_STORE_U32(a,v);}};
inline bool switchOn() noexcept {static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-story-local-create-off"),F_OK)!=0;return on;}   // v1165: default ON (lead 03:2x)
inline std::atomic<unsigned> made{0},asked{0};
inline void threadName(std::uint8_t* base,char (&name)[32]) noexcept {
    const std::uint32_t t=PPC_LOAD_U32(::recomp::story_local::CurrentThread);
    if(::recomp::story_local::plausible(t))for(unsigned i=0;i+1<sizeof name;++i){const char ch=char(PPC_LOAD_U8(t+104u+i));if(!ch)break;name[i]=(ch>=32 && ch<127)?ch:'?';}
}
}
PPC_FUNC_IMPL(__imp__sub_82A954D8);   // the engine's "this script's network handler" (NETWORK_GET_THIS_SCRIPT_IS_NETWORK_SCRIPT's helper)
namespace story_local_glue {
inline void pre(PPCContext& ctx,std::uint8_t* base,const ::recomp::story_local::Create& c) noexcept {
    if(!switchOn())return;   // default: one static bool load, nothing else
    GuestMem m{base};int askedNet=-1;
    const bool changed=::recomp::story_local::apply(m,ctx.r3.u32,c,true,::recomp::sandbox::active(),::recomp::bootstrap::play_mode::online(),
        ::recomp::local_session::active(),[&]{const PPCContext saved=ctx;__imp__sub_82A954D8(ctx,base);const bool net=ctx.r3.u32!=0;ctx=saved;return net;},&askedNet);
    if(askedNet<0)return;   // outside the scope / already local: nothing logged
    const unsigned a=asked.fetch_add(1,std::memory_order_relaxed)+1;
    const unsigned k=changed?made.fetch_add(1,std::memory_order_relaxed)+1:made.load(std::memory_order_relaxed);
    if(a<=32 || (changed && (k<=24 || k%200==0))) {
        char name[32]{};threadName(base,name);
        char l[176];std::snprintf(l,sizeof l,"RENDER_STORY_LOCAL %s by %s: network_script=%d -> %s (asked #%u, made #%u)",c.name,name[0]?name:"?",askedNet,
            changed?"made local (Story script, Sandbox session; isNetwork 1 -> 0)":"kept networked",a,k);
        ::recomp::sandbox::log(l);
    }
}
// The v1098 read-log macro of online_freemode_gate_hook.inc calls pre_<SUB> first in each of its nine wrappers: CREATE_PED gets the rule,
// the other eight natives nothing (their invocation lines stay as they were).
inline void pre_sub_82DAE978(PPCContext& ctx,std::uint8_t* base) noexcept {pre(ctx,base,::recomp::story_local::Ped);}
#define STORY_LOCAL_NOPRE(SUB) inline void pre_##SUB(PPCContext&,std::uint8_t*) noexcept {}
STORY_LOCAL_NOPRE(sub_82DB5E68) STORY_LOCAL_NOPRE(sub_82DB5E98) STORY_LOCAL_NOPRE(sub_82D5C598) STORY_LOCAL_NOPRE(sub_82D77C78)
STORY_LOCAL_NOPRE(sub_82DD3C50) STORY_LOCAL_NOPRE(sub_82DD1348) STORY_LOCAL_NOPRE(sub_82D8A300) STORY_LOCAL_NOPRE(sub_82DB4BA8)
#undef STORY_LOCAL_NOPRE
}
PPC_FUNC_IMPL(__imp__sub_82E06618);
PPC_FUNC(sub_82E06618) {story_local_glue::pre(ctx,base,::recomp::story_local::Vehicle);__imp__sub_82E06618(ctx,base);}
PPC_FUNC_IMPL(__imp__sub_82E03B20);
PPC_FUNC(sub_82E03B20) {story_local_glue::pre(ctx,base,::recomp::story_local::Object);__imp__sub_82E03B20(ctx,base);}
PPC_FUNC_IMPL(__imp__sub_82E03B30);
PPC_FUNC(sub_82E03B30) {story_local_glue::pre(ctx,base,::recomp::story_local::ObjectNoOffset);__imp__sub_82E03B30(ctx,base);}
