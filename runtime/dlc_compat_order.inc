// dlc_compat_order.inc (v1356, update-1 session 2026-11-11). Included by renderer_boundary_trace.cpp. dlc_compat_order.h has the
// evidence and the rule. LINK CONDITION: the attempt's redirect script without sub_829E6768 (redirect-v1356-dlcorder.ld).
// "RENDER_DLC_ORDER" lines prove the wrapper ran; with the switch off they only report what the 1.0 check will decide.
#include "dlc_compat_order.h"
namespace dlc_order_glue {
struct GuestMem {std::uint8_t* base;
    std::uint32_t load(std::uint32_t a) noexcept {return PPC_LOAD_U32(a);}
    void store(std::uint32_t a,std::uint32_t v) noexcept {PPC_STORE_U32(a,v);}};
// DEFAULT-ON (update-1, after the Fold DLC runs): the rule only renumbers COMPAT packs, so without DLC it changes nothing.
// files/private-trace/diag-dlc-compat-order-off is the way back (the 1.0 order check as shipped).
inline bool switchOn() noexcept {static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-dlc-compat-order-off"),F_OK)!=0;return on;}
inline std::atomic<unsigned> calls{0};
inline void deviceName(std::uint8_t* base,std::uint32_t entry,char (&name)[40]) noexcept {
    const std::uint32_t p=PPC_LOAD_U32(entry+8u);const unsigned len=PPC_LOAD_U16(entry+12u);
    if(p<0x40000000u || p>=0xC0000000u)return;
    for(unsigned i=0;i<len && i+1<sizeof name;++i){const char ch=char(PPC_LOAD_U8(p+i));if(!ch)break;name[i]=(ch>=32 && ch<127)?ch:'?';}
}
inline void after(std::uint8_t* base,std::uint32_t b,std::uint32_t e) noexcept {
    const bool on=switchOn();
    GuestMem m{base};::recomp::dlc_order::Change ch[16];unsigned seen=0;
    const unsigned changed=::recomp::dlc_order::renumber(m,b,e,on,ch,16,&seen);
    const unsigned c=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(c>8 || seen==0)return;
    for(unsigned i=0;i<seen && i<16;++i){
        char name[40]{};deviceName(base,b+ch[i].index*::recomp::dlc_order::EntrySize,name);
        __android_log_print(ANDROID_LOG_INFO,"RecompRenderer","RENDER_DLC_ORDER call=%u entry=%u device=%s type=%u order=%u -> %u (1.0 check: %s) switch=%d",
            c,ch[i].index,name[0]?name:"?",ch[i].type,ch[i].before,ch[i].after,
            ::recomp::dlc_order::wouldMount(ch[i].type,ch[i].after,ch[i].index)?"mount":"skip",on?1:0);
    }
    if(changed)__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","RENDER_DLC_ORDER call=%u renumbered %u of %u COMPAT order(s) to their sorted index",c,changed,seen);
}
}
PPC_FUNC_IMPL(__imp__sub_829E6768);
PPC_FUNC(sub_829E6768) {const std::uint32_t b=ctx.r3.u32,e=ctx.r4.u32;__imp__sub_829E6768(ctx,base);dlc_order_glue::after(base,b,e);}
