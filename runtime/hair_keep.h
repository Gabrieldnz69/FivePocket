#pragma once
// hair_keep.h (v1106 INTERIM, trainer-mods; owner: the creator PREVIEW stays bald while the hair is chosen). t-onlcre104's probe, on the
// preview ped: SET_PED_COMPONENT_VARIATION comp 2 d=1 t=2 (the catalogue's item 3) then d=0 t=0 in the same tick. In Online mode a comp-2
// (hair) d=0 call from maintransition on a ped whose comp 2 got a non-zero drawable from maintransition in the SAME game frame is not passed on,
// so the hair the creator just applied stays. Hypothesis H-hair (trainer-mods notes): the reset is 0x1fb1d's outfit loop (ip 0x1fb60)
// re-applying packed stat 389 that our Online stats path left 0; char_probe_hook.inc logs the site (script, ip) so a device run tests it.
// Choosing "no hair" (the catalogue's item 0 = d 0, no non-zero set before it in that frame) passes through. Pure: the rule, for the host test.
#include <cstdint>
namespace recomp::hair_keep {
constexpr std::uint32_t Maintransition=0x5FF2841Fu,FrameWord=0x83C5D10Cu;
struct Last {std::uint32_t ped=0,frame=0xFFFFFFFFu;int d=0;};
// One call per SET_PED_COMPONENT_VARIATION: true = do not pass this call on (keep the hair).
inline bool decide(Last& last,bool online,std::uint32_t script,std::uint32_t ped,std::uint32_t comp,std::uint32_t d,std::uint32_t frame) noexcept {
    if(!online || script!=Maintransition || comp!=2u)return false;
    if(d!=0u){last={ped,frame,int(d)};return false;}
    return last.ped==ped && last.frame==frame && last.d!=0;
}
}
