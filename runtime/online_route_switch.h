#pragma once
// online_route_switch.h (v1101, trainer-mods; lead, the owner's rule "always promote the newest stable build"): every freemode-route
// EXPERIMENT is DEFAULT-OFF, so the standing build's Online mode behaves like v1092-v1097 (character select / T4 hold, the 60 s sweep fallback
// -> Freemode, phone, leave). files/private-trace/diag-fm-route-on (read once per process) turns the experiments on for the freemode work:
//   - the in-memory MP character answers (MP0_CHAR_ISACTIVE / MPPLY_LAST_MP_CHAR, v1093),
//   - NETWORK_IS_CLOUD_AVAILABLE 0 at maintransition ip 0x65ccc (the offline UGC route, v1096),
//   - the MP0_PSTAT_INT0 type / gender answers (B1, v1100).
// The older per-experiment switches keep working on top (diag-fm-seed-off, diag-ugc-cloud). The read-only logs, the Online clock, the curtain
// fix and the leave are NOT behind this switch. Pure parts for the host test.
#include <unistd.h>
namespace recomp::fm_route {
// the effective "answers off" for the stats answers: off unless the route switch is on, and off again with diag-fm-seed-off
constexpr bool seedOff(bool routeOn,bool seedOffFile) noexcept {return !routeOn || seedOffFile;}
// the effective "keep the cloud answer 1 at 0x65ccc": unless the route switch is on, and again with diag-ugc-cloud
constexpr bool ugcCloud(bool routeOn,bool ugcCloudFile) noexcept {return !routeOn || ugcCloudFile;}
#ifdef RECOMP_APP_PATH
// v1124 (owner via the lead 2026-11-09 02:5x: "Get freeroam working"): the route is DEFAULT-ON in Online mode (every caller is behind an Online-mode
// check); files/private-trace/diag-fm-route-off turns it off (diag-fm-route-on is no longer needed and is ignored).
inline bool routeOn() noexcept {static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-fm-route-off"),F_OK)!=0;return on;}
// v1124: the JOB parts of the route (the disc job list v1110 / v1111 and the race transition answers v1114 / v1116) stay behind their own switch
// until a job launches: with them on, the job coronas appear and walking into one leaves the player in fmmc's lobby (frozen, hidden; corona
// state 6). files/private-trace/diag-fm-jobs-on turns them on (with the route).
// v1135 (lead 09:0x): the job parts (offline job list, transition answers and events, launch) DEFAULT-ON on the route - Lamar's race runs to its
// end and the player returns to free roam (t-onlrace-drive-20261109-0844, t-onlrace-post); files/private-trace/diag-fm-jobs-off turns them off.
inline bool jobsOn() noexcept {static const bool on=routeOn() && ::access(RECOMP_APP_PATH("/private-trace/diag-fm-jobs-off"),F_OK)!=0;return on;}
#endif
}
