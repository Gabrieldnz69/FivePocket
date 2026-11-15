#pragma once
// online_log_key.h (v1098, trainer-mods; lead 12:16, update-1/FREEMODE-GATE-3-2026-11-08.md A2 / A3): the read-only logs' dedup and script filter,
// pure for the host test. v1096's stats readLog deduped on (native, slot, result) and ignored the caller, so T17's STAT_LOAD(1) -> 1 at 0x389f and
// IS_LOADED(1) -> 1 at 0x38bb were silent repeats; the key is now (slot, result, script, ip). The maintransition filter is checked BEFORE any
// dedup, so other scripts' calls of hot natives cost one compare and leave no entry.
#include <cstdint>
#include <initializer_list>
namespace recomp::online_log {
constexpr std::uint32_t Maintransition=0x5FF2841Fu;
inline std::uint32_t mix(std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d) noexcept {
    std::uint32_t h=2166136261u;for(std::uint32_t v:{a,b,c,d}){h=(h^v)*16777619u;}return h;}
// a small first-seen set (main / script thread only); full = nothing new is "fresh" (the line caps bound the log anyway)
template<unsigned N> struct Seen {
    std::uint32_t key[N]{};unsigned n=0;
    bool fresh(std::uint32_t k) noexcept {for(unsigned i=0;i<n;++i)if(key[i]==k)return false;if(n<N){key[n++]=k;return true;}return false;}
    bool has(std::uint32_t k) const noexcept {for(unsigned i=0;i<n;++i)if(key[i]==k)return true;return false;}
};
inline std::uint32_t statKey(std::uint32_t slot,std::uint32_t result,std::uint32_t script,std::uint32_t ip) noexcept {return mix(slot,result,script,ip);}
inline bool mtCaller(bool onlineMode,std::uint32_t script) noexcept {return onlineMode && script==Maintransition;}
}
