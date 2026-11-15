#pragma once
// online_cloud_site.h (v1096, trainer-mods; lead 11:21, update-1/FREEMODE-GATE-2-2026-11-08.md): NETWORK_IS_CLOUD_AVAILABLE answered per site.
// maintransition's UGC prefetch 0x63299 chooses its route at 0x6326e by CALL 0x65cc7 = NOT NETWORK_IS_CLOUD_AVAILABLE (the native at ip
// 0x65ccc): cloud (our v1057 answer 1) loops for ever in case 2 (UGC_GET_GET_BY_CATEGORY needs ROS credentials and would issue a real rlUgc
// request); the game's own OFFLINE route (G1572867.f4 = 1, set by the game at 0x63275) loads common:/data/ugc/*.ugc locally and finishes
// (G1332397 = 1). So: in Online mode (not diag-cloud-real), ONLY maintransition at ip 0x65ccc gets 0; every other caller keeps 1.
// files/private-trace/diag-ugc-cloud restores 1 there (A/B). The inputs are read once per process, so the answer is constant for the whole
// maintransition instance (0x63230 resets the machine whenever it flips). Pure: the decision, for the host test.
#include <cstdint>
namespace recomp::cloud_site {
constexpr std::uint32_t Maintransition=0x5FF2841Fu,OfflineUgcIp=0x65CCCu;
// -1 = the original handler (not Online mode, or diag-cloud-real); else the answer to write (0 or 1)
inline int answer(bool onlineMode,bool cloudReal,bool ugcCloud,std::uint32_t threadCtx,std::uint32_t script,std::uint32_t ip) noexcept {
    if(!onlineMode || cloudReal)return -1;
    if(!ugcCloud && threadCtx>=0x40000000u && script==Maintransition && ip==OfflineUgcIp)return 0;
    return 1;
}
}
