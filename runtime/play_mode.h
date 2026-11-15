#pragma once

// Play mode (Update 1, Sandbox design SB §1.2; review W1-4). The launcher says whether this process plays Story Mode or
// Sandbox before the game thread starts. save_sandbox.h's filesRootFor() reads the mode once, when the saves state is
// first made, and from then on the mode cannot change in this process.
//
// recomp_play_mode_v1(mode), exported for the apps (dlsym, like recomp_trainer_v1):
//   mode 0 = Story, 1 = Sandbox, 2 = Sandbox in a fresh generation ("Start a fresh sandbox"), 3 = Online (v1040; ALLOCATION section 18:
//   the launcher's own "Online" entry; a Sandbox-like mode with its own save root <base>/online-mode/g<N> and the local online session
//   path on without the diag files; recomp_play_mode_online_v1 is exported only by libraries that implement it).
//   Returns:
//      0  this process is now latched to that mode, or already was (asking twice for the same mode is fine);
//     -1  the process is latched to another mode: an earlier call asked for one, or the saves state already exists
//         (filesRootFor latched Story because nothing was asked). The launcher must refuse the start (review T9);
//     -2  the mode is not 0, 1, 2 or 3. Nothing changes.
// Never called = Story, which is today's behaviour bit for bit: filesRootFor returns what it always returned.
// v955 (Update 1 P8 transport, trainer-mods): the argument may carry the launcher's sandbox_flags start extra (ALLOCATION section 3)
// in bits 8-15: value = mode | flags << 8. 0, 1 and 2 mean exactly what they meant. Flags with mode 0 (Story), or any bit from 16 up,
// answer -2 and change nothing. The Sandbox core hands the flags to the online world at its first tick (recomp_online_set_flags).
// An app that sends flags to an older library gets -2 and should start again with the plain mode (and say the extra is unsupported).
//
// The export is defined in exactly one translation unit: the one compiled with -DRECOMP_PLAY_MODE_EXPORT=1 (on Android
// guest_import_registry.cpp, through the attempt's per-file defines). Every other includer, including the iOS shell's
// copy of save_sandbox.h, sees only the inline state below. One process holds one instance of it, because the inline
// variable is merged across the library.

#include <atomic>
#include <cstdint>

namespace recomp::bootstrap::play_mode {

enum class PlayMode : std::uint8_t { Story, Sandbox, SandboxFresh, Online };

// The latch: Unset until the first request or the first saves state, then one of the other three for good.
enum : std::uint8_t { Unset = 0, LatchedStory = 1, LatchedSandbox = 2, LatchedSandboxFresh = 3, LatchedOnline = 4 };
inline std::atomic<std::uint8_t> latch{Unset};
// v1040: Online runs the whole Sandbox machinery (trainer, activities, the local session); every "is this Sandbox" test uses this.
inline constexpr bool sandboxLike(std::uint8_t l) noexcept { return l == LatchedSandbox || l == LatchedSandboxFresh || l == LatchedOnline; }
inline bool online() noexcept { return latch.load(std::memory_order_acquire) == LatchedOnline; }

inline std::atomic<std::uint32_t> sandboxFlags{0};

inline int request(int mode) noexcept {
    if (mode < 0 || mode > 3) { return -2; }
    const auto wanted = static_cast<std::uint8_t>(mode + 1);
    std::uint8_t seen = Unset;
    if (latch.compare_exchange_strong(seen, wanted, std::memory_order_acq_rel)) { return 0; }
    return seen == wanted ? 0 : -1;
}

// v955: mode | flags << 8 (see the header comment). Only the call that latches stores the flags; a later call for the same mode
// answers 0 and leaves them (the launcher's start and the developer switch may both ask). Story never stores any.
inline int requestValue(int value) noexcept {
    if (value < 0 || (value >> 16) != 0) { return -2; }
    const int mode = value & 0xFF;
    const auto flags = static_cast<std::uint32_t>((value >> 8) & 0xFF);
    if (mode > 3 || (mode == 0 && flags != 0)) { return -2; }
    const auto wanted = static_cast<std::uint8_t>(mode + 1);
    std::uint8_t seen = Unset;
    if (latch.compare_exchange_strong(seen, wanted, std::memory_order_acq_rel)) {
        if (mode != 0) { sandboxFlags.store(flags, std::memory_order_release); }   // (read at the first Sandbox tick, much later)
        return 0;
    }
    return seen == wanted ? 0 : -1;
}

// For filesRootFor (save_sandbox.h): the mode the saves state is made in. Latches Story when nothing was requested, so
// a request that comes after the saves state exists is refused.
inline PlayMode settle() noexcept {
    std::uint8_t seen = Unset;
    if (latch.compare_exchange_strong(seen, LatchedStory, std::memory_order_acq_rel)) { return PlayMode::Story; }
    return seen == LatchedSandbox ? PlayMode::Sandbox : seen == LatchedSandboxFresh ? PlayMode::SandboxFresh : seen == LatchedOnline ? PlayMode::Online : PlayMode::Story;
}

// For a launcher in the same process as the library (the iOS / Duo shells): the player's choice is stored before the
// start, and the start latches it as its first step, before anything can make the saves state (the shell's bot-root
// check calls filesRootFor, which would otherwise settle Story). Android's activity calls recomp_play_mode_v1 instead.
//   choose(mode): 0, 1 or 2 stored (returns 0), anything else refused (-2, nothing stored). It can change until the
//                 start; never called = Story.
//   latchChosen(): request(the stored choice): 0, or -1 when this process already runs another mode (refuse the start).
inline std::atomic<int> chosen{0};
inline int choose(int mode) noexcept {
    if (mode < 0 || mode > 3) { return -2; }
    chosen.store(mode, std::memory_order_release);
    return 0;
}
inline int latchChosen() noexcept { return request(chosen.load(std::memory_order_acquire)); }

} // namespace recomp::bootstrap::play_mode

#if defined(RECOMP_PLAY_MODE_EXPORT)
extern "C" __attribute__((visibility("default"))) int recomp_play_mode_v1(int mode) {
    return ::recomp::bootstrap::play_mode::requestValue(mode);   // v955: mode | sandbox_flags << 8
}
// v1040 (ALLOCATION section 18): present = this library implements mode 3 (Online); the apps show the "Online" entry only then.
extern "C" __attribute__((visibility("default"))) int recomp_play_mode_online_v1(void) { return 1; }
#endif
