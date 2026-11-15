#pragma once
// online_stats_reload.h (v1507, garage subagent 2026-11-11): the pure decision behind the STAT_CLEAR_SLOT_FOR_RELOAD gate answer in
// online_stats_cloud_hook.inc. Evidence (local-analysis/update-1/subagents-2026-11-11/garage/REPORT.md):
//  - The MP character save file (save_char0001) carries the script MP save block G115227 (startup 0x5e8: START_SAVE_DATA(&G115227, 2399, 0);
//    the PSO map entry of 9596 bytes = 2399 words). Garage vehicles live there (14 entries of 53 words, +42 the model).
//  - maintransition's character load (0x6678d) state 0 resets G115227 to its defaults (0x3b99c), then state 1 reloads the character slot
//    (0x3801: STAT_CLEAR_SLOT_FOR_RELOAD(1), STAT_LOAD(1), wait STAT_SLOT_IS_LOADED). The reload's parse restores G115227 from the file
//    (sub_8295C930 -> sub_8294E0B0(1, tree)).
//  - STAT_CLEAR_SLOT_FOR_RELOAD (sub_82D9F508) asks the cloud gate sub_824A7CB8 (lr 0x82D9F530) before it sets the slot's needs-load bit
//    (0x83A31FAC); offline the gate is 0, so the clear did nothing, STAT_LOAD found nothing to load and G115227 kept the reset defaults
//    (t-onlgstore-B3-20261111-0001: CLEAR -> STAT_LOAD "needs-load 00000000", no second cloud load; the B save then holds no vehicle).
// Answer 1 there only in Online local mode, only for a slot (0..5, r31 at the call) whose saved file exists (the same condition as the
// STAT_LOAD gate: the reload then reads that file); no file = the original (a first visit keeps v1088's local defaults).
#include <cstdint>
namespace recomp::online_stats_reload {
constexpr std::uint32_t LrClearReload=0x82D9F530u;   // sub_82D9F508 (STAT_CLEAR_SLOT_FOR_RELOAD) -> sub_824A7CB8
// -1 = keep the original handler; 1 = answer the gate with 1
inline int answer(bool localOn,bool routeOff,bool reloadOff,std::uint32_t lr,int slot,bool fileExists) noexcept {
    if(lr!=LrClearReload || !localOn || routeOff || reloadOff)return -1;
    if(slot<0 || slot>5 || !fileExists)return -1;
    return 1;
}
}
