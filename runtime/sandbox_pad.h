#pragma once
// sandbox_pad.h (Update 1 P4 + P8, v955; revised v957 after the wave-2 bug hunt, WAVE2-REPORT §2.1 items 1, 2, 6): the character-wheel
// block while an online (freemode) character is the player (recomp_trainer_state_v1 bit 20; review dependency 4, option (b)).
// The Sandbox tick (sandbox_mode.h, script thread) sets wheelBlocked = bit 20 && phone not up; the gamepad reader (trace_runner.cpp,
// user 0, after the synthetic pad and the Thor map tap) passes the buttons through filter(buttons, nowMs):
//  - only D-pad DOWN, and only a HOLD: a press passes for its first HoldMs (a tap still navigates the phone, the pause menu and every
//    script-drawn shop / mod-shop menu); after that, while still held, DOWN is dropped, so the game's wheel (it opens on a held DOWN,
//    E5: a 2.5 s hold) never stays open to pick a character;
//  - never while the pause menu is open, read LIVE from the guest's pause byte (0x83C5D20C, set when the pause menu opens through
//    sub_8259E440 -> sub_8258F000 -> sub_8343D830; WAVE2 §2.1-2): the script gate stops the Sandbox tick during the pause, so a value
//    latched by the tick would go stale. The guest base is published by the Sandbox filter hook (sandbox_launch.inc);
//  - never in Story mode (wheelBlocked is only ever set by the Sandbox tick).
// Option (a), holding `selector`, is not built: main_persistent starts selector once at boot (E3 logs).
#include <atomic>
#include <cstdint>

namespace recomp::sandbox::pad {
constexpr std::uint16_t DpadDown=0x0002;   // XInput wButtons
constexpr std::uint32_t PauseByte=0x83C5D20Cu;
constexpr long long HoldMs=150;
inline std::atomic<bool> wheelBlocked{false};
inline std::atomic<const std::uint8_t*> guestBase{nullptr};
inline std::atomic<long long> downSince{0};   // 0 = DOWN not held at the last read
inline std::atomic<unsigned> droppedReads{0};
inline bool pauseMenuOpen() noexcept {const std::uint8_t* b=guestBase.load(std::memory_order_relaxed);return b && b[PauseByte]!=0;}
inline std::uint16_t filter(std::uint16_t buttons,long long nowMs) noexcept {
    if(!(buttons&DpadDown)){downSince.store(0,std::memory_order_relaxed);return buttons;}
    long long since=downSince.load(std::memory_order_relaxed);
    if(since==0){since=nowMs?nowMs:1;downSince.store(since,std::memory_order_relaxed);}
    if(!wheelBlocked.load(std::memory_order_relaxed) || pauseMenuOpen() || nowMs-since<HoldMs)return buttons;
    droppedReads.fetch_add(1,std::memory_order_relaxed);
    return static_cast<std::uint16_t>(buttons&~DpadDown);
}
} // namespace recomp::sandbox::pad
