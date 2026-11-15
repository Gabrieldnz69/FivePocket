#pragma once
// door_chain_probe.h (v1510, subagent doors 2026-11-11; local-analysis/update-1/subagents-2026-11-11/doors/REPORT.md).
// READ ONLY, Online mode (files/private-trace/diag-doorchain-probe-off turns it off): GTA Online's own road from Lamar's race to the
// garage cutscene MP_INTRO_MCS_11 (stat enum 1287 bit 0 -> unlock 60 -> Dynasty 8 / property doors), as the disc's freemode keeps it.
// No native call, no write: guest loads of script globals through the VM's global block table (as property_unlock.h), plus the
// tutorial / help stats the game itself reads (logged by online_freemode_gate_hook.inc through statChanged()).
// Disc evidence (decoded freemode; offsets in its code segment; doors/REPORT.md "Verified"):
//  - rank = the largest i with G266557[i] <= XP (0xd123); defaults (social_controller 0x2ba95) 1:0 2:400 3:900 4:1600 5:3600 6:5200.
//  - unlocks G1573429 (word 1 + n/32, bit n%32), rank per unlock 0x2605f: 59 (FM_MOD text, the LSC shops 39-43) rank 3; 60 and 102
//    (FM_SIM text) rank 5. The unlock-message dispatcher 0x306b1 (case 102 at 0x30ec7) needs G1346303+1559 bit 30 (set by the help
//    sequence 0x5e9dc after FM_IHELP_FFR, which needs +1560 bit 2) and sets its pending flag f_177; the common tail 0x33631 then sets
//    G1346303+1560 bit 28 when not busy (0x34bde / 0x34c04 false).
//  - 0x270bd runs the cutscene state machine 0x270fd while +1560 bit 28 is set and bit 29 clear. State 0 starts MP_INTRO_MCS_11 when the
//    cutscene is not yet seen (0x28789: +1560 bit 6, G1374080[slot] bit 0, or the debug G1344092), the player is alive, the screen is faded
//    in and NO Los Santos Customs menu is open (0x2842b: G80929+1015 runtime shop bits 39-43, bit 6 = "browsing", set by carmod_shop
//    0x25b9 on menu entry; the prior report read this bit as the trigger: it is a blocker). The end (0x27316..) sets +1560 bit 6 and
//    stat 1287 bit 0 (0x19ea2), after which 0x29d99(60) may set unlock 60.
//  - tutorial bool stats (freemode_init's table G1372273): 119 MP0_FM_INTRO_MISS_DONE 0b19abf2, 120 MP0_FM_TRIGTUT_DONE 0c9dab8f,
//    124 MP0_FM_RACETUT_DONE 58862927, 121 07b91ece (set only by fm_hold_up_tut 0x1ff8; needed by unlock 59's text, 0x29f37),
//    122 MP0_FM_CMODTUT_DONE 93b408c4 (am_car_mod_tut 0x878), 85 176d95f4 (fm_mission_controler 0x6086f). Help int stats
//    (G1363889) 1177 4ca56002, 1179 e26e2539, 1180 f0b7c1cc; 1287 MP0_FM_CUT_DONE 16bcdf82.
#include "door_chain_stats.h"
#include "trainer.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::doorchain {
constexpr std::uint32_t XpMirror=1374086u,CutMirror=1374080u,RankTable=266557u,UnlockBits=1573429u,FmFlags=1346303u,Shops=80929u;
constexpr std::uint32_t FlagFirst=1555u,FlagCount=6u,UnlockWords=4u,ShopBits=1015u,LscFirst=39u,LscCount=5u;
constexpr std::uint32_t Busy=1332465u,DebugAll=1344092u,RankMax=12u,EveryFrames=60u;
constexpr std::uint32_t element(std::uint32_t array,std::uint32_t index,std::uint32_t size) noexcept {return array+1u+index*size;}
// rank as freemode 0xd123 computes it, from the table words t[0..n): the first i with t[i] > xp, minus 1 (n when none is larger).
inline int rankOf(const int* t,unsigned n,int xp) noexcept {for(unsigned i=0;i<n;++i)if(t[i]>xp)return int(i)-1;return int(n)-1;}
inline bool bit(std::uint32_t w,unsigned b) noexcept {return (w>>b)&1u;}

using LogFn=void(*)(const char*);
class Probe {
public:
    explicit Probe(LogFn log) noexcept:log_(log) {}
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed) noexcept {
        if(!armed || !log_)return;
        if(started_ && frame-last_<EveryFrames)return;
        last_=frame;
        Snap s{};
        s.xp=global(nat,element(XpMirror,0u,1u));s.cut=global(nat,element(CutMirror,0u,1u));
        for(unsigned i=0;i<RankMax;++i)s.table[i]=global(nat,RankTable+1u+i);
        for(unsigned i=0;i<UnlockWords;++i)s.unlock[i]=global(nat,element(UnlockBits,i,1u));
        for(unsigned i=0;i<FlagCount;++i)s.flags[i]=global(nat,FmFlags+FlagFirst+i);
        for(unsigned i=0;i<LscCount;++i)s.lsc[i]=global(nat,element(Shops+ShopBits,LscFirst+i,1u));
        s.browse290=global(nat,Shops+290u);s.browse291=global(nat,Shops+291u);s.busy=global(nat,Busy);s.debugAll=global(nat,DebugAll);
        if(started_ && std::memcmp(&s,&prev_,sizeof s)==0)return;
        started_=true;prev_=s;++lines_;
        log_(format(frame,s).text);
    }
    unsigned lines() const noexcept {return lines_;}
    struct Snap {int xp,cut,table[RankMax],unlock[UnlockWords],flags[FlagCount],lsc[LscCount],browse290,browse291,busy,debugAll;};
    struct Line {char text[520];};
    static Line format(std::uint32_t frame,const Snap& s) noexcept {
        Line l{};
        const std::uint32_t u1=std::uint32_t(s.unlock[1]),u3=std::uint32_t(s.unlock[3]),f60=std::uint32_t(s.flags[5]),f59=std::uint32_t(s.flags[4]);
        int lscBrowse=0;for(unsigned i=0;i<LscCount;++i)if(s.lsc[i]!=-1 && bit(std::uint32_t(s.lsc[i]),6))lscBrowse|=1<<i;
        std::snprintf(l.text,sizeof l.text,"RENDER_ONLINE_DOORCHAIN frame=%u xp=%d rank=%d u59=%d u60=%d u102=%d unlock=%08x,%08x,%08x,%08x "
                      "f1555..1560=%08x,%08x,%08x,%08x,%08x,%08x trig28=%d cutdone29=%d seen6=%d ffr1559.30=%d stat1287=%08x lscbrowse=%d "
                      "browse290/291=%d,%d busy=%d debugall=%d rank3/5=%d/%d (read only; -1 = not readable)",
                      frame,s.xp,rankOf(s.table,RankMax,s.xp),int(bit(u1,27)),int(bit(u1,28)),int(bit(u3,6)),unsigned(s.unlock[0]),u1,unsigned(s.unlock[2]),u3,
                      unsigned(s.flags[0]),unsigned(s.flags[1]),unsigned(s.flags[2]),unsigned(s.flags[3]),f59,f60,
                      int(bit(f60,28)),int(bit(f60,29)),int(bit(f60,6)),int(bit(f59,30)),unsigned(s.cut),lscBrowse,s.browse290,s.browse291,s.busy,s.debugAll,
                      s.table[3],s.table[5]);
        return l;
    }
private:
    static int global(recomp::trainer::Natives& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    LogFn log_;bool started_=false;std::uint32_t last_=0;Snap prev_{};unsigned lines_=0;
};
}  // namespace recomp::doorchain
