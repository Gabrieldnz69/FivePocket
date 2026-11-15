#pragma once
// property_unlock_answer.h (v1501, subagent online-doors 2026-11-10): the constants and the TEST-ONLY STAT_GET_INT decision of
// property_unlock.h, with no dependency (included by renderer_boundary_trace.cpp through online_freemode_gate_hook.inc, and by
// property_unlock.h for the probe). The evidence and the rules are in property_unlock.h's header comment.
#include <cstdint>

namespace recomp::propunlock {
constexpr std::uint32_t UnlockBits=1573429u,OwnedProperty=1374092u,CutMirror=1374080u,XpMirror=1374086u,StatHashes=1363889u,RankTable=266557u;
constexpr std::uint32_t PropertyUnlock=60u,CutEnum=1287u,XpEnum=625u,Slots=5u,EnumWords=6u;
constexpr std::uint32_t Mp0CharXpFm=0x3E68C21Fu;   // joaat("MP0_CHAR_XP_FM") (online_freemode_gate_hook.inc's v1146 constant)
constexpr std::uint32_t TestXp=4000u;   // v1170 (lead 10:1x): rank 5 = 3600 on this disc (was 7000)
constexpr std::uint32_t EveryFrames=90u;

constexpr std::uint32_t element(std::uint32_t array,std::uint32_t index,std::uint32_t size) noexcept {return array+1u+index*size;}
constexpr std::uint32_t unlockWord(std::uint32_t id) noexcept {return element(UnlockBits,id/32u,1u);}
constexpr std::uint32_t unlockBit(std::uint32_t id) noexcept {return id%32u;}
constexpr std::uint32_t statHashGlobal(std::uint32_t statEnum,std::uint32_t slot) noexcept {return element(element(StatHashes,statEnum,EnumWords),slot,1u);}

// One STAT_GET_INT seen by the hook, with the hashes the game's table holds for the two enums (0 = not readable).
struct StatRead {
    std::uint32_t hash=0;
    bool readOk=false;          // the original native returned true (out holds the stored value)
    std::uint32_t out=0;
    std::uint32_t xpHashes[Slots]{};
    std::uint32_t cutHashes[Slots]{};
};
struct Decision {bool write=false;std::uint32_t value=0;const char* why="original";};
inline bool listed(std::uint32_t h,const std::uint32_t (&a)[Slots]) noexcept {
    if(h==0u)return false;
    for(std::uint32_t x:a)if(x==h)return true;
    return false;
}
inline bool isXp(const StatRead& r) noexcept {return r.hash==Mp0CharXpFm || listed(r.hash,r.xpHashes);}
inline bool isCut(const StatRead& r) noexcept {return listed(r.hash,r.cutHashes);}
inline Decision decide(bool online,bool testSwitch,const StatRead& r) noexcept {
    if(!online || !testSwitch)return {};
    if(isXp(r) && (!r.readOk || r.out<TestXp))return {true,TestXp,"DIAGNOSTIC diag-unlock60-test xp"};
    if(isCut(r) && (!r.readOk || !(r.out&1u)))return {true,(r.readOk?r.out:0u)|1u,"DIAGNOSTIC diag-unlock60-test stat1287.bit0"};
    return {};
}

}  // namespace recomp::propunlock
