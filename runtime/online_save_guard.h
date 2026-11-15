#pragma once
// online_save_guard.h (v967, wave-3 S6; lead 2026-11-06 "block it with the save hold"): while an online character is the player in
// Sandbox (recomp_sandbox_state_v1 bit 4: the save scripts are held), a saved-game content create/overwrite is refused at
// XamContentCreateEx (guest_saves_dispatch.inc), so the phone's own Quick Save (or any other save path the script hold misses) cannot
// write a save holding the freemode ped. Loading stays allowed: disposition 3 (OPEN_EXISTING) only reads. The others create or truncate:
// 1 CREATE_NEW, 2 CREATE_ALWAYS, 4 OPEN_ALWAYS (creates when absent), 5 TRUNCATE_EXISTING. Refused with ERROR_ACCESS_DENIED (5): the game
// reports a failed save. Sandbox saves never reach the story root anyway (wave-3 S7); this keeps the Sandbox copy clean too.
// Placement (2026-11-06 23:2x): in the save STORE's openContent (guest_save_store.h), not in the import dispatcher: the registry is
// built with -fprofile-use and a new branch in its dispatcher would drop that hot function's profile (and its line count is pinned).
// The store reads the hold through saveHoldProbe, which trainer_glue.inc points at recomp_sandbox_state_v1 (no probe: never refused,
// e.g. host builds of the store without the trainer).
#include <atomic>
#include <cstdint>
namespace recomp::sandbox {
inline constexpr std::uint32_t SavesHeldBit = 16u, OpenExisting = 3u, ErrorAccessDenied = 5u;
inline bool onlineSaveWriteRefused(std::uint32_t sandboxState, std::uint32_t disposition) noexcept {
    return (sandboxState & SavesHeldBit) != 0 && disposition != OpenExisting;
}
inline std::atomic<unsigned (*)()> saveHoldProbe{nullptr};
inline bool onlineSaveWriteRefusedNow(std::uint32_t disposition) noexcept {
    const auto probe = saveHoldProbe.load(std::memory_order_acquire);
    return probe != nullptr && onlineSaveWriteRefused(probe(), disposition);
}
} // namespace recomp::sandbox
