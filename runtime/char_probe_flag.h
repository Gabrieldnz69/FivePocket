#pragma once
// char_probe_flag.h (v1082, trainer-mods): the player ped handle and model as the trainer tick last read them (every 15 frames in Online
// mode), for char_probe_hook.inc's lines (renderer TU) to mark the player's own ped (the creator's YOU ped is the question). Read only.
#include <atomic>
#include <cstdint>
namespace recomp::char_probe {
inline std::atomic<std::uint32_t> playerPed{0},playerModel{0};
}
