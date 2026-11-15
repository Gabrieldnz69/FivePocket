#pragma once
// online_stats_local.h (v1088, trainer-mods): the pure decision behind online_stats_hook.inc's STAT_LOAD answer. needs-load / failed masks
// at 0x83A31F80 +44 / +56 (1 << slot, slots 0..5). Mark the slot loaded locally only when it still needs a load and no request for it runs.
#include <cstdint>
namespace recomp::online_stats {
struct Local {bool mark;std::uint32_t need,failed;};
inline Local decide(std::uint32_t slot,std::uint32_t need,std::uint32_t failed,bool requestRunning) noexcept {
    if(slot>5u || requestRunning)return {false,need,failed};
    const std::uint32_t bit=1u<<slot;
    if(!(need&bit))return {false,need,failed};
    return {true,need&~bit,failed&~bit};
}
}
