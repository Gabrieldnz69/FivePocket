// online_spoof_hook.inc (v1057, trainer-mods; lead after t-onlctl3-20261107-2051: maintransition's own alert "The Rockstar cloud servers are
// unavailable right now. Only disc content will be available and saving will be disabled. Are you sure you would like to proceed?" appeared
// on the boot-card Online path; answer the check locally, at the check). Included by renderer_boundary_trace.cpp after loading_hold_hook.inc.
// Evidence (the disc, freemode-route lane): maintransition calls NETWORK_IS_CLOUD_AVAILABLE 23 times and builds its alerts from TRAN_CLOUDDWN /
// HUD_CLOUDOFFLIN / HUD_CLOUDFAILMSG / CONT_CLOUDFAIL. The native's handler sub_82DA5818 returns sub_824A7CB8() = signed online (sub_824A7D50)
// && byte 0x83B13BA7 && the user's flag in 0x83B12D10[user]: no Rockstar cloud here, so false.
// - sub_82DA5818 (NETWORK_IS_CLOUD_AVAILABLE, c7ff5afc): Online mode (unless files/private-trace/diag-cloud-real): the answer is 1 (written
//   through the call context's return pointer, word 0 of r3, as the original does); logged once. Story / Sandbox: the original.
//   Not answered here (next, the Stats / Economy backing): the MP stats' cloud slot load (STAT_CLOUD_SLOT_LOAD_FAILED, 40 calls in maintransition)
//   and the MP saves (STAT_SAVE / STAT_SAVE_PENDING): whatever the game asks next is shown by the warning-message log below.
// - sub_82D63340 (SET_WARNING_MESSAGE_WITH_HEADER, 2db9eab5): read only, every mode: each new (header, line 1, line 2) text label triple the
//   scripts put up is logged (the arguments: word 2 of r3 = the argument block; arg 0 header, arg 1 line 1, arg 3 line 2), at most WarnLogMax
//   lines, then the original runs unchanged.
// LINK CONDITION: redirect-v1057-cloud.ld (= v1055's without both wrappers). "RENDER_ONLINE_MODE cloud" / "RENDER_WARNING" prove it live.
#include <atomic>
#include "online_cloud_site.h"
#include "online_route_switch.h"   // v1101
namespace online_spoof_hook {
inline bool cloudReal() noexcept {static const bool real=::access(RECOMP_APP_PATH("/private-trace/diag-cloud-real"),F_OK)==0;return real;}
inline std::atomic<bool> cloudLogged{false};
inline std::atomic<bool> siteLogged{false};   // v1096
inline bool ugcCloudFile() noexcept {static const bool c=::access(RECOMP_APP_PATH("/private-trace/diag-ugc-cloud"),F_OK)==0;return c;}   // v1096 A/B
// v1101: DEFAULT-OFF: the 0x65ccc answer needs files/private-trace/diag-fm-route-on (online_route_switch.h); without it every caller keeps 1
inline bool ugcCloud() noexcept {return ::recomp::fm_route::ugcCloud(::recomp::fm_route::routeOn(),ugcCloudFile());}
constexpr unsigned WarnLogMax=48;
inline std::atomic<unsigned> warnLines{0};
inline char lastWarn[96]{};
inline void guestText(std::uint8_t* base,std::uint32_t at,char* out,int cap) {
    int k=0;
    if(at>=0x40000000u)for(;k<cap-1;++k){const char c=char(PPC_LOAD_U8(at+std::uint32_t(k)));if(!c)break;out[k]=(c>=32 && c<127)?c:'?';}
    out[k]=0;
}
}
PPC_FUNC_IMPL(__imp__sub_82DA5818);
PPC_FUNC(sub_82DA5818) {
    // v1096 (lead 11:21, FREEMODE-GATE-2-2026-11-08.md): maintransition's UGC prefetch route switch (ip 0x65ccc) gets 0: the game's offline UGC
    // route (online_cloud_site.h); every other caller keeps 1. The calling thread's script hash / IP are read inline from the VM's r23
    // (online_fmgate::scriptOf / ipOf come from a later include).
    const std::uint32_t t=ctx.r23.u32,sc=t>=0x40000000u?PPC_LOAD_U32(t+4u):0u,ip=t>=0x40000000u?PPC_LOAD_U32(t+12u):0u;
    const int a=::recomp::cloud_site::answer(loading_hold_hook::online(),online_spoof_hook::cloudReal(),online_spoof_hook::ugcCloud(),t,sc,ip);
    if(a>=0) {
        const std::uint32_t ret=PPC_LOAD_U32(ctx.r3.u32);
        if(ret)PPC_STORE_U32(ret,std::uint32_t(a));
        if(a==0) {
            if(!online_spoof_hook::siteLogged.exchange(true,std::memory_order_relaxed))
                loading_hold_hook::log("RENDER_ONLINE_MODE cloud prefetch site 0x65ccc answered 0 (offline UGC route) script=maintransition (diag-ugc-cloud restores 1)");
        } else if(!online_spoof_hook::cloudLogged.exchange(true,std::memory_order_relaxed))
            loading_hold_hook::log("RENDER_ONLINE_MODE cloud NETWORK_IS_CLOUD_AVAILABLE answered 1 (Online mode; diag-cloud-real restores the real answer)");
        return;
    }
    __imp__sub_82DA5818(ctx,base);
}
PPC_FUNC_IMPL(__imp__sub_82D63340);
PPC_FUNC(sub_82D63340) {
    if(online_spoof_hook::warnLines.load(std::memory_order_relaxed)<online_spoof_hook::WarnLogMax) {
        const std::uint32_t args=PPC_LOAD_U32(ctx.r3.u32+8u);
        char h[24]{},a[24]{},b[24]{};
        if(args){online_spoof_hook::guestText(base,PPC_LOAD_U32(args),h,int(sizeof h));online_spoof_hook::guestText(base,PPC_LOAD_U32(args+4u),a,int(sizeof a));
                 online_spoof_hook::guestText(base,PPC_LOAD_U32(args+12u),b,int(sizeof b));}
        char key[96];std::snprintf(key,sizeof key,"%s|%s|%s",h,a,b);
        if(std::strcmp(key,online_spoof_hook::lastWarn)!=0) {
            std::snprintf(online_spoof_hook::lastWarn,sizeof online_spoof_hook::lastWarn,"%s",key);
            online_spoof_hook::warnLines.fetch_add(1,std::memory_order_relaxed);
            char l[200];std::snprintf(l,sizeof l,"RENDER_WARNING header=%s line1=%s line2=%s mode=%s lr=%08x",h,a,b,loading_hold_hook::online()?"online":"story/sandbox",std::uint32_t(ctx.lr));
            loading_hold_hook::log(l);
        }
    }
    __imp__sub_82D63340(ctx,base);
}
