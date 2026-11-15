#pragma once
// door_chain_stats.h (v1510, subagent doors 2026-11-11): the tutorial / help stats of the race -> garage cutscene chain (hashes and
// the evidence in door_chain_probe.h's header comment), with no dependency: online_freemode_gate_hook.inc (renderer_boundary_trace.cpp)
// logs each change of the value the game itself got from STAT_GET_BOOL / STAT_GET_INT. Read only.
#include <atomic>
#include <cstdint>

namespace recomp::doorchain {
// The tutorial / help stats of the chain (slot 0 = MP0; the probe's character slot), for the stat hook's change log.
struct ChainStat {std::uint32_t hash;const char* name;};
constexpr ChainStat Stats[]={{0x0C9DAB8Fu,"b120.TRIGTUT"},{0x58862927u,"b124.RACETUT"},{0x07B91ECEu,"b121.holdup-tut"},{0x93B408C4u,"b122.CMODTUT"},
                             {0x176D95F4u,"b85.mission"},{0x4CA56002u,"i1177.help"},{0xE26E2539u,"i1179.help"},{0xF0B7C1CCu,"i1180.help"},
                             {0x16BCDF82u,"i1287.FM_CUT_DONE"}};
constexpr unsigned StatCount=sizeof Stats/sizeof Stats[0];
inline int statIndex(std::uint32_t h) noexcept {for(unsigned i=0;i<StatCount;++i)if(Stats[i].hash==h)return int(i);return -1;}
// true once per new (ok, value) of a chain stat: the first read and every change.
class StatWatch {
public:
    bool changed(std::uint32_t h,bool ok,std::uint32_t out) noexcept {
        const int i=statIndex(h);if(i<0)return false;
        const std::uint64_t v=(std::uint64_t(ok?1u:0u)<<32)|out,seen=(std::uint64_t(1)<<40)|v;
        if(last_[i].load(std::memory_order_relaxed)==seen)return false;
        last_[i].store(seen,std::memory_order_relaxed);return true;}
private:
    std::atomic<std::uint64_t> last_[StatCount]{};
};
inline StatWatch& statWatch() noexcept {static StatWatch w;return w;}

}  // namespace recomp::doorchain
