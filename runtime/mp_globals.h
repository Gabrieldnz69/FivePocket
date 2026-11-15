#pragma once
// mp_globals.h (v985, trainer-mods 2026-11-07): the MP script-global blocks the online boot sets up, for Sandbox + diag-online-local.
// T4 (v981) refused "globals-block-5": the disc creator route reads G1344033 / G1344034 / G1338478 (block 5) and the VM's block table
// (0x83C5CE48[5]) was null. Decoded (script headers, word +0x1C = globals count | block << 18, all 144 decoded scripts): block 5 is
// owned by mp_registration (66001 globals), block 6 by mp_fm_registration, block 1 by tunables_registration, block 3 by
// ugc_global_registration (block 4 mp_prop_global_block: 70 natives, not taken). Those four have NO native calls at all (332 / 69 / 8
// / 8 bytes of code): they only carry the blocks' initial values. CORRECTION (v989, lead-0607 block5.md): startup itself starts
// MP_registration, MP_FM_registration, tunables_registration, SC_LB_GLOBAL_BLOCK and mp_prop_global_block at 0x5a3..0x5d1
// (0x28cc7: REQUEST_SCRIPT / HAS_SCRIPT_LOADED / START_NEW_SCRIPT); the v986 device log shows the blocks placed in the Sandbox
// (0xAC5..0xACC). This setup is kept as the guard ("only if null"); v989 logs the table, size and owner of blocks 0..7 at its first tick. So, once, in Sandbox + diag-online-local only: REQUEST_SCRIPT each, wait HAS_SCRIPT_LOADED, start it through
// the game's own start-script function (its program load allocates and fills the block), release it, and confirm the block-table entry
// is non-null. Natives + the starter only; nothing written by us into the blocks.
#include "trainer.h"
#include <array>
#include <cstdint>
#include <cstdio>

namespace recomp::mpglobals {
using trainer::Natives;using trainer::joaat;
inline constexpr std::uint32_t BlockTable=0x83C5CE48u;   // the VM's global-block table (creator_route.h GlobalsBlockTable)
struct Reg {const char* name;std::uint32_t block;};
inline constexpr std::array<Reg,4> Regs{{{"mp_registration",5},{"mp_fm_registration",6},{"tunables_registration",1},{"ugc_global_registration",3}}};
inline constexpr std::uint32_t Stack=1024,LoadFramesMax=300,ReadyFramesMax=60;
namespace n {
constexpr std::uint32_t RequestScript=joaat("REQUEST_SCRIPT"),ScriptLoaded=joaat("HAS_SCRIPT_LOADED"),NoLongerNeeded=joaat("SET_SCRIPT_AS_NO_LONGER_NEEDED");
}
struct Named {const char* name;std::uint32_t hash;};
inline constexpr Named Used[]{{"REQUEST_SCRIPT",n::RequestScript},{"HAS_SCRIPT_LOADED",n::ScriptLoaded},{"SET_SCRIPT_AS_NO_LONGER_NEEDED",n::NoLongerNeeded}};
inline constexpr std::uint32_t UsedNatives[]{n::RequestScript,n::ScriptLoaded,n::NoLongerNeeded};
using StartFn=std::uint32_t(*)(void* ctx,std::uint32_t nameAddr,std::uint32_t stack);   // the glue's starter (0 = refused)
using LogFn=void(*)(const char*);

class Setup {
public:
    enum class St:std::uint8_t {Idle,Loading,Waiting,Done,Failed};
    St state() const noexcept {return st_;}
    // Every Sandbox tick with the online-local latch on. Returns true once every block is present (also when they already were).
    bool tick(Natives& nat,std::uint32_t frame,void* starterCtx,StartFn start,LogFn log) noexcept {
        if(st_==St::Done || st_==St::Failed)return st_==St::Done;
        if(st_==St::Idle) {
            if(log){   // v989 (diagnostic, reads only): what sub_83438C20 placed, and the placement counter 0x83C5CB30
                for(std::uint32_t b=0;b<8;++b){char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL mp-globals table block=%u at=%08x size=%u owner=%08x",
                    b,nat.load(0x83C5CE48u+4u*b),nat.load(0x83C5CF50u+4u*b),nat.load(0x83C5CA30u+4u*b));log(l);}
                char l[80];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL mp-globals placed-counter=%u",nat.load(0x83C5CB30u));log(l);
            }
            if(allPresent(nat)){st_=St::Done;say(log,"RENDER_ONLINE_LOCAL mp-globals already present");return true;}
            for(const auto& r:Regs)if(!present(nat,r.block)){const std::uint32_t s=nat.text(r.name);if(s)call0(nat,n::RequestScript,{s});}
            st_=St::Loading;since_=frame;return false;
        }
        if(st_==St::Loading) {
            bool all=true;
            for(std::size_t i=0;i<Regs.size();++i) {
                if(started_[i] || present(nat,Regs[i].block))continue;
                const std::uint32_t s=nat.text(Regs[i].name);std::uint32_t out[3]{};
                if(s && nat.call(n::ScriptLoaded,&s,1,out) && out[0]) {
                    const std::uint32_t id=start?start(starterCtx,s,Stack):0;started_[i]=true;
                    call0(nat,n::NoLongerNeeded,{s});
                    char l[112];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL mp-globals start name=%s block=%u id=%u",Regs[i].name,Regs[i].block,id);say(log,l);
                } else all=false;
            }
            if(all){st_=St::Waiting;since_=frame;}
            else if(frame-since_>LoadFramesMax){fail(log,"load-timeout");}
            return false;
        }
        // Waiting: the blocks appear once the programs are loaded and run
        if(allPresent(nat)) {
            st_=St::Done;
            for(const auto& r:Regs){char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL mp-globals block=%u ready (%s) at=%08x",r.block,r.name,
                nat.load(BlockTable+4u*r.block));say(log,l);}   // v986: + the block's address
            return true;
        }
        if(frame-since_>ReadyFramesMax)fail(log,"block-not-allocated");
        return false;
    }
    static bool present(Natives& nat,std::uint32_t block) noexcept {return nat.load(BlockTable+4u*block)!=0;}
    static bool allPresent(Natives& nat) noexcept {for(const auto& r:Regs)if(!present(nat,r.block))return false;return true;}
private:
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);}
    static void say(LogFn log,const char* l) noexcept {if(log)log(l);}
    void fail(LogFn log,const char* why) noexcept {
        st_=St::Failed;char l[160];int n=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL mp-globals failed why=%s missing=",why);
        for(const auto& r:Regs)if(n>0 && n<int(sizeof l)-8)n+=std::snprintf(l+n,sizeof l-std::size_t(n),"%u,",r.block);
        say(log,l);
    }
    St st_=St::Idle;std::uint32_t since_=0;std::array<bool,Regs.size()> started_{};
};
} // namespace recomp::mpglobals
