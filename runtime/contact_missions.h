#pragma once
// contact_missions.h (v1508, subagent contact-missions 2026-11-11; local-analysis/update-1/subagents-2026-11-11/contact-missions/REPORT.md).
// GTA Online contact missions (Gerald, Lamar, ...) as this disc's own scripts offer them, for two uses in Online mode:
//  1. Probe (READ ONLY, Online mode; files/private-trace/diag-contact-probe-off turns it off): one line when any of these change (looked at
//     every EveryFrames): unlock 27 (freemode 0x5926a launches am_npc_invites only with it; rank 6), unlock 97 (am_contact_requests 0x4285:
//     Lamar's "Request Job" MPCT_LJOB, else MPCT_LOCKED; rank 7), freemode's am_npc_invites launched flag / thread (G1346303 +1546 / +1547),
//     the contact job list count G1330476 (fmmc_launcher 0x16e38 / 0xe2cd), the phone's call state G10433.f1 (10 = dialling, the value
//     freemode 0x2ecf2 needs) and called character G1477, the XP mirrors G1374086[0..1] and the rank 6 / 7 thresholds G266557[6] / [7].
//     Guest loads only; no native, no write.
//  2. decideXp() (TEST ONLY, files/private-trace/diag-contact-rank-test, default off): an XP stat read (MP0_CHAR_XP_FM or the enum-625 slot
//     hashes, property_unlock_answer.h isXp) lower than rank 7 reads RankXp7, so freemode's own unlock refresh opens unlocks 27 and 97 (the
//     owner's test character holds 7000 XP = rank 6, 100 short). In memory only, through the native's out pointer; the stat store is untouched
//     (a later STAT_SET by the game may save it into the TEST copy's Online root). Not a play path; never written by the public apps.
// Disc evidence (decoded scripts, REPORT.md): rank = the largest i with G266557[i] <= XP (freemode 0xd123); the table is the disc's
// common:/data/tune/tunables.json BASE_GLOBALS RANK_XP_n (5: 3600, 6: 5200, 7: 7100; 5 and 6 match RENDER_ONLINE_PROPUNLOCK on the Thor);
// the rank of an unlock is freemode 0x2605f (27 -> 6, 97 -> 7, 29 -> 9, 60 -> 5); unlock n = word 1573429 + 1 + n/32, bit n%32.
#include "property_unlock_answer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::contactmissions {
namespace pu=::recomp::propunlock;
constexpr std::uint32_t NpcInvitesUnlock=27u,LamarJobUnlock=97u;
constexpr std::uint32_t NpcBlock=1346303u,NpcLaunched=1546u,NpcThread=1547u,JobListCount=1330476u,CallState=10433u,CalledChar=1477u;
constexpr std::uint32_t RankXp6=5200u,RankXp7=7100u;   // tunables.json BASE_GLOBALS RANK_XP_6 / RANK_XP_7 (this disc)
constexpr std::uint32_t EveryFrames=30u;

// The TEST-ONLY XP answer. isXp: the read's hash is an XP stat (pu::isXp); readOk / out: the original native's result.
struct Decision {bool write=false;std::uint32_t value=0;const char* why="original";};
constexpr Decision decideXp(bool online,bool testSwitch,bool isXp,bool readOk,std::uint32_t out) noexcept {
    if(!online || !testSwitch || !isXp)return {};
    if(readOk && out>=RankXp7)return {};
    return {true,RankXp7,"DIAGNOSTIC diag-contact-rank-test xp (rank 7)"};
}

// The probe's words, in the order logged. -1 = not readable.
struct Words {int v[11];};
inline bool unlockSet(int word,std::uint32_t id) noexcept {return word!=-1 && ((std::uint32_t(word)>>pu::unlockBit(id))&1u)!=0u;}
inline int unlockState(int word,std::uint32_t id) noexcept {return word==-1?-1:(unlockSet(word,id)?1:0);}
// The line for one set of words (pure; the host test checks the decoded bits and fields).
inline int format(char* out,std::size_t cap,std::uint32_t frame,const Words& w) noexcept {
    return std::snprintf(out,cap,"RENDER_ONLINE_CONTACT frame=%u unlock27=%d unlock97=%d npc_launched=%d npc_thread=%d joblist=%d call=%d char=%d xp=%d,%d rank6xp=%d rank7xp=%d "
                                 "(read only; -1 = not readable)",
                         frame,unlockState(w.v[0],NpcInvitesUnlock),unlockState(w.v[1],LamarJobUnlock),w.v[2],w.v[3],w.v[4],w.v[5],w.v[6],w.v[7],w.v[8],w.v[9],w.v[10]);
}
template<class Load> Words read(Load&& global) noexcept {
    Words w{};
    w.v[0]=global(pu::unlockWord(NpcInvitesUnlock));w.v[1]=global(pu::unlockWord(LamarJobUnlock));
    w.v[2]=global(NpcBlock+NpcLaunched);w.v[3]=global(NpcBlock+NpcThread);w.v[4]=global(JobListCount);
    w.v[5]=global(CallState+1u);w.v[6]=global(CalledChar);
    w.v[7]=global(pu::element(pu::XpMirror,0u,1u));w.v[8]=global(pu::element(pu::XpMirror,1u,1u));
    w.v[9]=global(pu::element(pu::RankTable,6u,1u));w.v[10]=global(pu::element(pu::RankTable,7u,1u));
    return w;
}
using LogFn=void(*)(const char*);
class Probe {
public:
    explicit Probe(LogFn log) noexcept:log_(log) {}
    // load(addr) -> the guest word at addr (Natives::load); the VM's global block table as property_unlock.h reads it.
    template<class Nat> void tick(Nat& nat,std::uint32_t frame,bool armed) noexcept {
        if(!armed || !log_ || lines_>=MaxLines)return;
        if(started_ && frame-last_<EveryFrames)return;
        last_=frame;
        const Words w=read([&](std::uint32_t gl){
            const std::uint32_t b=gl>>18,i=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
            return (base && !(base&3u) && i<size)?int(nat.load(base+4u*i)):-1;});
        if(started_ && std::memcmp(w.v,prev_.v,sizeof w.v)==0)return;
        started_=true;prev_=w;++lines_;
        char l[320];format(l,sizeof l,frame,w);log_(l);
    }
    unsigned lines() const noexcept {return lines_;}
    static constexpr unsigned MaxLines=400;
private:
    LogFn log_;bool started_=false;std::uint32_t last_=0;Words prev_{};unsigned lines_=0;
};
}  // namespace recomp::contactmissions
