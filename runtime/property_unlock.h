#pragma once
// property_unlock.h (v1501, subagent online-doors 2026-11-10; local-analysis/update-1/subagents-2026-11-10/online-doors/REPORT.md).
// GTA Online's property unlock as the disc's own scripts keep it, for two uses in Online mode:
//  1. Probe (READ ONLY, Online mode, files/private-trace/diag-propunlock-probe-off turns it off): one line when any of these change
//     (looked at every EveryFrames): unlock 60 (the bit appinternet's Dynasty 8 purchase 0x1ba1a needs, else "LAR_IRANK", and
//     am_mp_property_ext's door loop 0x2e1 needs), the owned property G1374092[slot], stat enum 1287's mirror G1374080[slot]
//     (bit 0 = the garage cutscene MP_INTRO_MCS_11 seen, freemode 0x270fd), the XP mirror G1374086[slot] (stat enum 625) and the
//     rank table's rank 5 / 6 thresholds G266557[5] / [6] (freemode 0xd123). Guest loads only; no native, no write.
//  2. decide() (TEST ONLY, files/private-trace/diag-unlock60-test, default off): what a STAT_GET_INT answer becomes so freemode's own
//     unlock 60 path runs for a character that has not played that far: XP (MP0_CHAR_XP_FM, or the slot hashes of enum 625) raised to
//     TestXp (rank 5) when lower, and enum 1287's value with bit 0 set. In memory only, through the native's out pointer; the stat store
//     is untouched (a later STAT_SET by the game itself may save it into the TEST copy's Online root). Not a play path.
// Disc evidence (decoded freemode / appinternet / am_mp_property_ext, REPORT.md sections 2-4; offsets in the decoded code segment):
//  - VM arrays: word 0 = count, element i of size s at +1 + i*s (ARRAY_U8 / ARRAY_U8_LOAD). G1573429 = the unlock bitset: unlock n is
//    word 1573429 + 1 + n/32, bit n%32 (freemode 0x29e20 SET_BIT, appinternet 0x1c279 / am_mp_property_ext 0xc6f2 IS_BIT_SET).
//  - freemode 0xce414(enum, slot) = STAT_GET_INT(G1363889[enum][slot], &v, -1): G1363889 has 6 words per enum (count + 5 slots), so the
//    hash of (enum, slot) is word 1363889 + 1 + enum*6 + 1 + slot. 0xce198..0xce1ed: G1374080[slot] = 0xce414(1287, slot);
//    0xce24c..0xce29d / 0xe1cb2: G1374092[slot] = 0xce414(1262, slot); G1374086[slot] holds the XP (0xd16c).
//  - unlock 60 is set by 0x29d99(60, x) only when 0x28789 holds: G1346303+1560 bit 6, or G1374080[slot] bit 0, or the debug G1344092.
//    The rank refresh 0xe029c clears the bitset and re-applies every unlock whose rank (0x2605f: 60 -> rank 5) is reached; the one-off
//    0x5cdf6 applies 60 directly with the help text FM_IHELP_GAR.
//  - rank = the largest i with G266557[i] <= XP (0xd123). TestXp 7000 is rank 5 for the GTA Online table (5: 6100, 6: 9500), checked by
//    the probe's rank5xp / rank6xp fields on the device; v1146's diag-rank-test used 3000 = rank 3 on the same table.
#include "property_unlock_answer.h"
#include "trainer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::propunlock {
using LogFn=void(*)(const char*);
// The read-only probe. Natives::load only (no call): the VM's global block table, as fmi_probe.h / activities.h read it.
class Probe {
public:
    explicit Probe(LogFn log) noexcept:log_(log) {}
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed,bool testSwitch) noexcept {
        if(!armed || !log_)return;
        if(started_ && frame-last_<EveryFrames)return;
        last_=frame;
        int v[10]{};
        const int uw=global(nat,unlockWord(PropertyUnlock));
        v[0]=uw;
        v[1]=global(nat,element(OwnedProperty,0u,1u));v[2]=global(nat,element(OwnedProperty,1u,1u));
        v[3]=global(nat,element(CutMirror,0u,1u));v[4]=global(nat,element(CutMirror,1u,1u));
        v[5]=global(nat,element(XpMirror,0u,1u));v[6]=global(nat,element(XpMirror,1u,1u));
        v[7]=global(nat,element(RankTable,5u,1u));v[8]=global(nat,element(RankTable,6u,1u));
        v[9]=testSwitch?1:0;
        if(started_ && std::memcmp(v,prev_,sizeof v)==0)return;
        started_=true;std::memcpy(prev_,v,sizeof v);++lines_;
        const int u60=uw==-1?-1:int((std::uint32_t(uw)>>unlockBit(PropertyUnlock))&1u);
        char l[300];
        std::snprintf(l,sizeof l,"RENDER_ONLINE_PROPUNLOCK frame=%u unlock60=%d unlockword1=%08x owned=%d,%d stat1287=%08x,%08x(bit0 %d,%d) xp=%d,%d "
                                 "rank5xp=%d rank6xp=%d test=%d (read only; -1 = not readable)",
                      frame,u60,unsigned(uw),v[1],v[2],unsigned(v[3]),unsigned(v[4]),v[3]==-1?-1:(v[3]&1),v[4]==-1?-1:(v[4]&1),v[5],v[6],v[7],v[8],v[9]);
        log_(l);
    }
    unsigned lines() const noexcept {return lines_;}
private:
    static int global(recomp::trainer::Natives& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    LogFn log_;bool started_=false;std::uint32_t last_=0;int prev_[10]{};unsigned lines_=0;
};
}  // namespace recomp::propunlock
