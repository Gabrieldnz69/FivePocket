#pragma once
// imenu_back_gate.h (v989, E3 fix; trainer-mods 2026-11-07): Back (360 "Select", XINPUT_GAMEPAD_BACK 0x0020) for the Interaction Menu
// style, decided in the library's pad read before the game sees it.
//
// Why (decoded from this disc, validated-ppc): the camera's view-mode cycle sub_82282920 reads the NETWORK_IS_GAME_IN_PROGRESS byte
// 0x83B13AEB. In a network game it cycles on a short RELEASE (sub_82480BE8 + sub_832353B0 with a hold threshold); offline it cycles on
// the PRESS (sub_8244CF58). freemode's opener (fm 0xcb2e5..0xcb3a4) never disables control 0 before 350 ms because online the camera
// waits for the release. In the offline Sandbox the press already changed the camera (E3-hold-open FAIL, imenu-e15-20261107-0554).
// The cycle's next-mode rule (sub_8224A808) uses an internal allowed-mode mask, so a native cannot reproduce it faithfully. Instead:
//   - while the style is on, Back is withheld from the game from the press on;
//   - released before HoldMs (350 ms, freemode's threshold, fm 0xcb253): a tap. Back is handed to the game as a short press (TapMs) so
//     the game's own cycle runs (one tap = one press, as online);
//   - held HoldMs or longer: never handed to the game; the menu reads the hold from rawDown() (its own 350 ms timer) and opens / closes.
// While the menu is not closed at the press, a tap is not handed over either (the open menu owns Back, as freemode disables control 0).
// Style off (the default "FivePocket"), Story mode, or another user index: the snapshot passes unchanged (one relaxed load).
// Thread safety: filter() runs on the game's input thread(s) under one small mutex; the setters and rawDown() are atomics.
#include <atomic>
#include <cstdint>
#include <mutex>

namespace recomp::imenugate {

constexpr std::uint16_t BackBit=0x0020;   // XINPUT_GAMEPAD_BACK
constexpr std::uint32_t HoldMs=350;       // fm 0xcb253 (open) / am 0x13aa3 (close by holding Back again)
constexpr std::uint32_t TapMs=100;        // the handed-over press: at least three 33 ms game frames (the game reads the pad once per frame)

class BackGate {
public:
    // Any thread. on = the Interaction Menu style is active in Sandbox mode.
    void setEnabled(bool on) noexcept {enabled_.store(on,std::memory_order_release);}
    bool enabled() const noexcept {return enabled_.load(std::memory_order_relaxed);}
    // Script thread, every Sandbox tick: is the menu closed (not opening / open / closing / cooling)? Read at each press.
    void setMenuClosed(bool closed) noexcept {menuClosed_.store(closed,std::memory_order_relaxed);}

    // The pad read (XamInputGetState, user 0). buttons is the snapshot's button word, nowMs a monotonic clock. Changes only Back.
    void filter(std::uint16_t& buttons,std::uint64_t nowMs) noexcept {
        if(!enabled_.load(std::memory_order_acquire)) {
            if(active_.load(std::memory_order_relaxed)){std::lock_guard l(m_);down_=false;tapUntil_=0;active_.store(false,std::memory_order_relaxed);}
            rawDown_.store((buttons&BackBit)!=0,std::memory_order_relaxed);
            return;
        }
        std::lock_guard l(m_);
        active_.store(true,std::memory_order_relaxed);
        const bool raw=(buttons&BackBit)!=0;
        rawDown_.store(raw,std::memory_order_relaxed);
        if(raw) {
            if(!down_){down_=true;downAt_=nowMs;tapUntil_=0;closedAtPress_=menuClosed_.load(std::memory_order_relaxed);++presses_;}
            buttons=static_cast<std::uint16_t>(buttons&~BackBit);        // withheld from the press on
            return;
        }
        if(down_) {
            down_=false;
            if(nowMs-downAt_<HoldMs && closedAtPress_){tapUntil_=nowMs+TapMs;++taps_;}
            else ++holds_;
        }
        if(nowMs<tapUntil_)buttons=static_cast<std::uint16_t>(buttons|BackBit);   // the tap, handed to the game
    }
    // Script thread: Back as the player holds it (not as the game sees it), for the menu's 350 ms open / close timers.
    bool rawDown() const noexcept {return rawDown_.load(std::memory_order_relaxed);}
    // Counters (logs, tests).
    unsigned presses() const noexcept {std::lock_guard l(m_);return presses_;}
    unsigned taps() const noexcept {std::lock_guard l(m_);return taps_;}
    unsigned holds() const noexcept {std::lock_guard l(m_);return holds_;}

private:
    std::atomic<bool> enabled_{false},menuClosed_{true},rawDown_{false},active_{false};
    mutable std::mutex m_;
    bool down_=false,closedAtPress_=true;
    std::uint64_t downAt_=0,tapUntil_=0;
    unsigned presses_=0,taps_=0,holds_=0;
};

}  // namespace recomp::imenugate
