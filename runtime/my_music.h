#pragma once
// My Music (Update 1 P6, lane my-music-android, 2026-11-06; design: docs/fable/UPDATE-1-FEATURES-DESIGN.md section 3, renamed from
// "Self Radio": that is the game's own feature name). The player's own music plays in the app (Android MediaPlayer, iOS AVPlayer) as
// a separate stream; the game silences its own music through the console's custom-soundtrack rule, which it already obeys:
//   - the audio object's poll sub_83136648 (ppc_recomp.253.cpp, v855) asks the music player for its status (XMsg app 0xFA message
//     70009, XMPGetStatus) and stores (status == 1) into byte +13156 of the object;
//   - the mixer sub_8314BCF8 (ppc_recomp.254.cpp) reads that byte and zeroes the volumes of the music groups (group flag +104 > 0x7F).
//   - the poll is called with the static audio object 0x83B91890 (sub_8274E5F0: lis r9,-31815 / addi r3,r9,6288 / bl 0x83136648,
//     ppc_recomp.66.cpp:76996-76998), so the byte is u8 0x83B94EF4 and the control state (+13152) u32 0x83B94EF0. [inference from
//     that call site; the second caller sub_83138A80 passes r31. Used only for the state word's bit 16 and the diagnostic line.]
//   - its control state machine (+13152: 3 take wanted, 2 taken, 1 restore wanted, 0 restored) sends 7001A (XMPSetPlaybackController)
//     with {client 2, request 0 (Game), locked 1} through sub_836A8FE8 to take the music for itself (both kernel paths pass locked 1,
//     r5 = 1), and {client 2, request 4 (Restore), locked 0} through sub_836A90C0 to give it back.
//
// What this header adds to the runtime (all of it read by guest_import_registry.cpp's existing handlers; nothing else changes):
//   - the 70009 answer: today the constant 0 (idle). With My Music it is answer(): the app's last report (0 idle, 1 playing, 2 paused:
//     the XMP_STATE values; the game tests "== 1" only) unless the report is older than StaleMs (a stopped app never keeps the game's
//     music muted), or the game holds the music (then "paused": the console's music player paused for the title, and so do we).
//     Never called by the app = 0 = today's answer bit for bit.
//   - the 7001A handler records hold = locked of a Game request and clears it on Restore (noteControl). The 7001B answer is NOT
//     changed (controller 0, locked 0 as today): the game's old-kernel path reads it before it takes the music, and nothing here
//     needs another answer. [decision; the design proposed answering 7001B from the record]
//   - recomp_my_music_v1(state): the app's report (0 / 1 / 2; anything else -2, nothing changes). Returns 0.
//   - recomp_my_music_state_v1(): one word for the app's 100 ms poll (stateWord below), or -1 before guest memory exists. Relaxed
//     aligned big-endian loads only (touch_context.h's readers and the station chain of GET_PLAYER_RADIO_STATION_INDEX); no guest code
//     runs, no guest memory is written, no lock is taken.
// Cost on the game's side: the 70009 handler does one relaxed atomic load more; only when the app has reported something it also reads
// the monotonic clock (one vDSO call). No allocation, no lock, no wait. The 7001A handler does one relaxed store more.
//
// The exports are defined in exactly one translation unit: the one compiled with -DRECOMP_MY_MUSIC_EXPORT=1 (guest_import_registry.cpp
// through the attempt's per-file defines, as play_mode.h's RECOMP_PLAY_MODE_EXPORT). The state lives in one inline variable, merged
// across the library.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include "touch_context.h"
#include "finish_trap_diag.h" // v1670: the guest-memory lease (v1620's Lease / LeaseRead)

namespace recomp::mymusic {

enum XmpState : std::uint32_t { Idle = 0, Playing = 1, Paused = 2 };
/** A report older than this (ms) counts as idle: the app polls every 100 ms; a stopped or crashed app gives the music back. */
constexpr std::uint32_t StaleMs = 1000;

// ---- guest addresses (v855 / v721 / v507 codegen of the same disc executable; re-derive if the codegen input changes)
constexpr std::uint32_t RadioObject = 0x838CA8E8u, RadioStationPtr = 68;   // GET_PLAYER_RADIO_STATION_INDEX -> sub_82D549A0 / sub_8231CC20
constexpr std::uint32_t StationTableBase = 0x838D0464u, StationCount = 0x838A5D7Cu; // sub_82688498 (u32 pointer, u32 count)
constexpr std::uint32_t MaxStations = 64;                                    // bound of the search (the disc has fewer)
constexpr std::uint32_t AudioObject = 0x83B91890u, MuteByte = 13156, ControlState = 13152; // [inference, see above]
constexpr std::uint32_t NoStation = 255;

// ---- the shared state (one per process)
struct Shared {
    std::atomic<std::uint32_t> appState{Idle};   // the app's last report
    std::atomic<std::uint32_t> appMs{0};         // monotonic ms (low 32 bits) of that report; 0 = never
    std::atomic<std::uint32_t> hold{0};          // 1 while the game holds the music (7001A Game + locked, until Restore)
    std::atomic<std::uint32_t> lastAnswer{Idle}; // the last 70009 answer (for the state word)
    std::atomic<std::uint8_t*> guestBase{nullptr};
    // diagnostic counters (only counted while the diagnostic line is on)
    std::atomic<std::uint32_t> statusCalls{0}, playingAnswers{0}, takes{0}, restores{0}, rejects{0};
    std::atomic<std::uint32_t> lastLogMs{0};
};
inline Shared shared{};

inline std::uint32_t nowMs() noexcept {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    const std::uint32_t ms = static_cast<std::uint32_t>(static_cast<std::uint64_t>(t.tv_sec) * 1000u + static_cast<std::uint64_t>(t.tv_nsec) / 1000000u);
    return ms == 0 ? 1u : ms; // (0 means "never reported")
}

/**
 * The 70009 answer. app / appMs: the app's last report and its time; hold: the game holds the music; now: the clock; forced: the
 * diagnostic switch diag-xmp-playing (E-SR1) answers playing whatever the app says.
 */
constexpr std::uint32_t answer(std::uint32_t app, std::uint32_t appMs, bool hold, std::uint32_t now, bool forced) noexcept {
    if (forced) return Playing;
    if (app != Playing && app != Paused) return Idle;
    if (appMs == 0 || now - appMs > StaleMs) return Idle; // (unsigned: wraps correctly; a report "in the future" is > StaleMs too)
    if (hold) return Paused;
    return app;
}

/** The handler's call: reads the clock only when the app reported something. Records the answer for the state word. */
inline std::uint32_t answerNow(bool forced) noexcept {
    const std::uint32_t app = shared.appState.load(std::memory_order_acquire);
    std::uint32_t a = Idle;
    if (forced) a = Playing;
    else if (app != Idle) a = answer(app, shared.appMs.load(std::memory_order_relaxed), shared.hold.load(std::memory_order_relaxed) != 0, nowMs(), false);
    shared.lastAnswer.store(a, std::memory_order_relaxed);
    return a;
}

/** The app's report. 0 on success, -2 for a state that is not 0, 1 or 2 (nothing changes). */
inline int report(int state, std::uint32_t now) noexcept {
    if (state < 0 || state > 2) return -2;
    shared.appMs.store(now == 0 ? 1u : now, std::memory_order_relaxed);
    shared.appState.store(static_cast<std::uint32_t>(state), std::memory_order_release);
    return 0;
}

/** 7001A, after the handler accepted it (client 2, request 0 with locked 0/1, or request 4 with locked 0). */
inline void noteControl(std::uint32_t request, std::uint32_t locked) noexcept {
    shared.hold.store(request == 0 && locked == 1 ? 1u : 0u, std::memory_order_relaxed);
}

// v1670 (Update 1 candidate; the v1620 touch-context lease extended to My Music): the app's 100 ms poll (stateNow) and the diagnostic
// line read guest memory only through this lease. publishBase (the game's first 70009 of a run, a guest thread) publishes it; trace_runner.cpp
// calls retireBase() when the run ends, after the guest threads and before ~GuestAddressSpace munmaps the space: revoke() waits for a
// reader inside, and a later poll sees null and returns -1 as before guest memory existed (v1565 kept the stale base: the poll after a
// guest stop read unmapped memory, the same fault as the touch poll's in run-v1575-11121954).
inline ::recomp::fintrap::Lease& musicLease() noexcept {static ::recomp::fintrap::Lease l;return l;}
inline void publishBase(std::uint8_t* base) noexcept {
    if (base && !shared.guestBase.load(std::memory_order_relaxed)) {
        musicLease().publish(base);
        shared.guestBase.store(base, std::memory_order_release);
    }
}
inline void retireBase() noexcept {
    musicLease().revoke();
    shared.guestBase.store(nullptr, std::memory_order_release);
}

/** The player's radio station index (GET_PLAYER_RADIO_STATION_INDEX's chain), NoStation when none or not found. */
template<class M> std::uint32_t stationIndex(const M& m) noexcept {
    const std::uint32_t station = m.u32(RadioObject + RadioStationPtr);
    if (!touchctx::guestPointer(station)) return NoStation;
    const std::uint32_t table = m.u32(StationTableBase), count = m.u32(StationCount);
    if (!touchctx::guestPointer(table) || count == 0) return NoStation;
    const std::uint32_t n = count < MaxStations ? count : MaxStations;
    for (std::uint32_t i = 0; i < n; ++i) if (m.u32(table + 4u * i) == station) return i;
    return NoStation;
}

// ---- the state word (recomp_my_music_state_v1)
enum Bits : std::uint32_t {
    StationMask = 0xFFu,      // bits 0-7: station index, 255 none
    InVehicle = 1u << 8,
    RadioWheel = 1u << 9,
    Frontend = 1u << 10,      // the pause menu or another frontend screen
    Cutscene = 1u << 11,
    FadedOut = 1u << 12,
    GameHolds = 1u << 13,     // the game took the music for itself (7001A Game + locked)
    ControlOff = 1u << 14,
    InWorld = 1u << 15,       // the player's ped exists
    MuteSet = 1u << 16,       // the game's custom-soundtrack mute byte is set (audio object +13156) [inference: the object's address]
    AnswerShift = 24,         // bits 24-25: the last 70009 answer
    Stale = 1u << 26          // the app's last report is older than StaleMs (the game hears idle)
};
template<class M> std::uint32_t stateWord(const M& m, bool hold, std::uint32_t lastAnswer, bool stale) noexcept {
    std::uint32_t w = stationIndex(m) & StationMask;
    const auto where = touchctx::read(m);
    if (where.context != touchctx::None) w |= InWorld;
    if (where.context >= touchctx::Car) w |= InVehicle;
    const int s = touchctx::screens(m);
    if (s & touchctx::RadioWheel) w |= RadioWheel;
    if (s & touchctx::Frontend) w |= Frontend;
    if (s & touchctx::Cutscene) w |= Cutscene;
    if (s & touchctx::FadedOut) w |= FadedOut;
    if (s & touchctx::ControlOff) w |= ControlOff;
    if (hold) w |= GameHolds;
    if (m.u8(AudioObject + MuteByte)) w |= MuteSet;
    w |= (lastAnswer & 3u) << AnswerShift;
    if (stale) w |= Stale;
    return w;
}
inline int stateNow() noexcept {
    if (!shared.guestBase.load(std::memory_order_acquire)) return -1;
    const ::recomp::fintrap::LeaseRead lease(musicLease()); // v1670: held for the whole read; revoked before the space is released
    const std::uint8_t* const base = lease.base;
    if (!base) return -1;
    const std::uint32_t app = shared.appState.load(std::memory_order_acquire), at = shared.appMs.load(std::memory_order_relaxed);
    const bool stale = app != Idle && (at == 0 || nowMs() - at > StaleMs);
    return static_cast<int>(stateWord(touchctx::GuestMemory{base}, shared.hold.load(std::memory_order_relaxed) != 0,
                                      shared.lastAnswer.load(std::memory_order_relaxed), stale) & 0x7FFFFFFFu);
}

// ---- the diagnostic line (E-SR1 / E-SR3 / E-SR4): only while diag-xmp-playing or diag-my-music-log exists
/** Writes "RENDER_MY_MUSIC ..." into out (NUL-terminated, truncated to size); returns the length snprintf reports. */
inline int diagLine(char* out, std::size_t size, std::uint32_t windowMs, std::uint32_t calls, std::uint32_t playing, std::uint32_t takes,
                    std::uint32_t restores, std::uint32_t rejects, std::uint32_t app, bool hold, int mute, long controlState, bool forced) noexcept {
    return std::snprintf(out, size, "RENDER_MY_MUSIC window_ms=%u status_calls=%u playing_answers=%u take=%u restore=%u rejected=%u "
                         "app=%u hold=%d mute=%d control=%ld forced=%d", windowMs, calls, playing, takes, restores, rejects, app,
                         hold ? 1 : 0, mute, controlState, forced ? 1 : 0);
}
/** Counts one status call (and its answer); returns true when a line is due (at least 1000 ms since the last one; the first call
 *  only starts the window), with the window's length in *windowMs. */
inline bool countStatus(std::uint32_t answerGiven, std::uint32_t now, std::uint32_t* windowMs) noexcept {
    shared.statusCalls.fetch_add(1, std::memory_order_relaxed);
    if (answerGiven == Playing) shared.playingAnswers.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t last = shared.lastLogMs.load(std::memory_order_relaxed);
    if (last == 0) { shared.lastLogMs.compare_exchange_strong(last, now, std::memory_order_relaxed); return false; }
    if (now - last < 1000u) return false;
    if (!shared.lastLogMs.compare_exchange_strong(last, now, std::memory_order_relaxed)) return false;
    if (windowMs) *windowMs = now - last;
    return true;
}
/** Formats the due line from the counters and resets them. */
inline int takeDiagLine(char* out, std::size_t size, std::uint32_t windowMs, bool forced) noexcept {
    const std::uint32_t calls = shared.statusCalls.exchange(0, std::memory_order_relaxed), playing = shared.playingAnswers.exchange(0, std::memory_order_relaxed),
            takes = shared.takes.exchange(0, std::memory_order_relaxed), restores = shared.restores.exchange(0, std::memory_order_relaxed),
            rejects = shared.rejects.exchange(0, std::memory_order_relaxed);
    const ::recomp::fintrap::LeaseRead lease(musicLease()); // v1670
    const std::uint8_t* const base = shared.guestBase.load(std::memory_order_acquire) ? lease.base : nullptr;
    int mute = -1;
    long control = -1;
    if (base) {
        const touchctx::GuestMemory m{base};
        mute = m.u8(AudioObject + MuteByte);
        control = static_cast<long>(m.u32(AudioObject + ControlState));
    }
    return diagLine(out, size, windowMs, calls, playing, takes, restores, rejects, shared.appState.load(std::memory_order_relaxed),
                    shared.hold.load(std::memory_order_relaxed) != 0, mute, control, forced);
}

} // namespace recomp::mymusic

#if defined(RECOMP_MY_MUSIC_EXPORT)
extern "C" __attribute__((visibility("default"))) int recomp_my_music_v1(int state) {
    return ::recomp::mymusic::report(state, ::recomp::mymusic::nowMs());
}
extern "C" __attribute__((visibility("default"))) int recomp_my_music_state_v1(void) {
    return ::recomp::mymusic::stateNow();
}
#endif
