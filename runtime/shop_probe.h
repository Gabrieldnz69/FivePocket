#pragma once
// shop_probe.h (v1039, trainer-mods; android-qol's ask, read only, developer switch files/private-trace/diag-shop-probe): why shop_controller never
// starts the _mp shop scripts in the local session (t-shops-tpz-barber-20261107-1819: gip 1, 40 s inside the barber, no hairdo_shop_* launch; the
// _sp one started 1 s after the leave). shop_controller's launch 0x258c(idx): name = 0x2cf5(idx) (_mp when NETWORK_IS_GAME_IN_PROGRESS), then
// REQUEST_SCRIPT -> HAS_SCRIPT_LOADED -> GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE(2024) > n -> !NETWORK_IS_SCRIPT_ACTIVE(name, idx, 1, 0) -> ... ->
// START_NEW_SCRIPT_WITH_ARGS(name, args, 2, 2024). The shop table is G(80929) (GLOBAL_U24 013c21, confirmed on the disc at 0x25c6): parallel arrays
// of one word per shop, field f of shop i = word 80929 + f + 1 + i (count at 80929 + f); fields 17 launched, 107 requested, 1015 bits, 243.
// While the local session is up, every EveryFrames: one line with HAS_SCRIPT_LOADED of both _mp names and the free 2024 stacks, one line with
// NETWORK_IS_SCRIPT_ACTIVE for each shop index (hairdo_shop_mp for 0..6, clothes_shop_mp for 7..21), and one line per table field. Counts from
// shop_controller's own NATIVE instructions (HAS_SCRIPT_LOADED 1 -> 1, GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE 1 -> 1, NETWORK_IS_SCRIPT_ACTIVE
// 4 -> 1). No write.
#include "trainer.h"
#include "local_session_flag.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::shop_probe {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t Loaded=joaat("HAS_SCRIPT_LOADED"),   // 1 -> 1 (char*)
    FreeStacks=joaat("GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE"),   // 1 -> 1
    ScriptActive=joaat("NETWORK_IS_SCRIPT_ACTIVE");               // 4 -> 1
}
// v1142: + the four natives the v1141 context / shop menu probe (trainer_glue.inc) calls by name, so the trainer records their handlers
inline constexpr std::uint32_t UsedNatives[]{n::Loaded,n::FreeStacks,n::ScriptActive,
    n::joaat("IS_CONTROL_RELEASED"),n::joaat("IS_HUD_COMPONENT_ACTIVE"),n::joaat("IS_COMMERCE_STORE_OPEN"),n::joaat("IS_SYSTEM_UI_BEING_DISPLAYED")};
constexpr std::uint32_t Table=80929u,EveryFrames=60;constexpr int Shops=22,Barbers=7;
inline constexpr std::uint32_t Fields[4]{17,107,1015,243};
using LogFn=void(*)(const char*);
class Probe {
public:
    explicit Probe(LogFn log) noexcept:log_(log) {}
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed) noexcept {
        if(!armed || !log_ || !::recomp::local_session::active())return;
        if(started_ && frame-last_<EveryFrames)return;
        started_=true;last_=frame;
        const std::uint32_t hm=nat.text("hairdo_shop_mp"),cm=nat.text("clothes_shop_mp");
        char l[400];
        std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL shop-probe frame=%u loaded hairdo_shop_mp=%d clothes_shop_mp=%d free_stacks_2024=%d",frame,
                      one(nat,n::Loaded,hm),one(nat,n::Loaded,cm),one(nat,n::FreeStacks,2024u));
        log_(l);
        int k=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL shop-probe active");
        for(int i=0;i<Shops && k>0 && k<int(sizeof l)-8;++i){
            const std::uint32_t nm=i<Barbers?hm:cm;std::uint32_t out[3]{};const std::uint32_t a[4]{nm,std::uint32_t(i),1u,0u};
            const int v=(nm && nat.call(n::ScriptActive,a,4,out))?int(out[0]):-1;
            k+=std::snprintf(l+k,sizeof l-std::size_t(k)," %d",v);
        }
        log_(l);
        for(std::uint32_t fld:Fields){
            k=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL shop-probe f%u count=%d",fld,readGlobal(nat,Table+fld));
            for(int i=0;i<Shops && k>0 && k<int(sizeof l)-14;++i)k+=std::snprintf(l+k,sizeof l-std::size_t(k)," %d",readGlobal(nat,Table+fld+1u+std::uint32_t(i)));
            log_(l);
        }
    }
private:
    static int one(recomp::trainer::Natives& nat,std::uint32_t h,std::uint32_t a0) noexcept {std::uint32_t out[3]{};return (a0 && nat.call(h,&a0,1,out))?int(out[0]):-1;}
    static int readGlobal(recomp::trainer::Natives& nat,std::uint32_t gl) noexcept {   // the VM's block table (as activities.h)
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    LogFn log_;bool started_=false;std::uint32_t last_=0;
};
}
