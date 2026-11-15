#pragma once
// My Music (Update 1 P6): the registry's hooks, included once by guest_import_registry.cpp (the line that replaces its blank line 166,
// so every original line keeps its number: the registry names its site statics after __LINE__, and no inserted code shares a line
// number with original code in the line tables). Every hook is out of line and the import dispatcher only gains the calls:
//   - recompMyMusicStatus: the 70009 answer (my_music.h answerNow), the guest base for the state word, and the diagnostic line;
//   - recompMyMusicControl: the 7001A hold record;
//   - recompMyMusicRefused: every refusal (rejected / memoryFault) in the four XMsg handlers (XMsgInProcessCall 7001B, XMsgInProcessCall,
//     XMsgStartIORequestEx, XMsgStartIORequest) calls it; with a diagnostic switch on, each refused app-0xFA message is counted and
//     logged (E-SR1: a playlist message 7000D / 70013 / 7002E the game sends after it hears "playing" shows up whichever import
//     carried it). tools/registry_hook_check.py checks that every refusal in those handlers carries the call.
// Needs, before the include: RECOMP_APP_PATH, ::access / F_OK (unistd.h), and android/log.h on Android (all included above line 166).
#include "my_music.h"
#include "my_music_mixer_diag.h" // v957: the mixer line (diagnostic)

namespace {
// diag-xmp-playing (ALLOCATION section 4, E-SR1) answers 70009 "playing" whatever the app says; it and diag-my-music-log (proposed
// allocation) print one RENDER_MY_MUSIC line per second of status calls and each refused music-player message. The public app never
// writes diag-* files. Each switch is looked for once.
// v957 (E-SR1 A/B, diagnostic): with diag-my-music-log present, diag-xmp-playing is looked for again every 500 ms, so one run can
// alternate forced / not forced; without it the switch is read once, exactly as before.
inline bool recompMyMusicForced() {
    static const bool initial = ::access(RECOMP_APP_PATH("/private-trace/diag-xmp-playing"), F_OK) == 0;
    static const bool dynamic = ::access(RECOMP_APP_PATH("/private-trace/diag-my-music-log"), F_OK) == 0;
    if (!dynamic) return initial;
    static std::atomic<std::uint32_t> nextMs{0};
    static std::atomic<bool> on{initial};
    const std::uint32_t now = ::recomp::mymusic::nowMs();
    const std::uint32_t next = nextMs.load(std::memory_order_relaxed);
    if (next == 0 || std::int32_t(now - next) >= 0) {
        nextMs.store(now + 500u, std::memory_order_relaxed);
        on.store(::access(RECOMP_APP_PATH("/private-trace/diag-xmp-playing"), F_OK) == 0, std::memory_order_relaxed);
    }
    return on.load(std::memory_order_relaxed);
}
inline bool recompMyMusicLogged() {
    static const bool on = recompMyMusicForced() || ::access(RECOMP_APP_PATH("/private-trace/diag-my-music-log"), F_OK) == 0;
    return on;
}
[[gnu::noinline, gnu::cold]] void recompMyMusicRefused(const char* via, std::uint32_t app, std::uint32_t message) {
    if (app != 0xFA || !recompMyMusicLogged()) return;
    ::recomp::mymusic::shared.rejects.fetch_add(1, std::memory_order_relaxed);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "RENDER_MY_MUSIC rejected via=%s msg=%08x", via, message);
#else
    (void)via; (void)message;
#endif
}
// The 70009 answer (out of line: the import dispatcher grows by a call, not by this code).
[[gnu::noinline]] std::uint32_t recompMyMusicStatus(std::uint8_t* base) {
    const bool forced = recompMyMusicForced();
    ::recomp::mymusic::publishBase(base);
    const std::uint32_t state = ::recomp::mymusic::answerNow(forced);
    if (recompMyMusicLogged()) {
        std::uint32_t window = 0;
        if (::recomp::mymusic::countStatus(state, ::recomp::mymusic::nowMs(), &window)) {
            char line[256];
            ::recomp::mymusic::takeDiagLine(line, sizeof line, window, forced);
#if defined(__ANDROID__)
            __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", line);
            char mix[256];   // v958: the mixer's output volumes, read from guest memory (my_music_mixer_diag.h)
            if (::recomp::mymusic::mixer::line(mix, sizeof mix, window, ::recomp::mymusic::mixer::read(base, ::recomp::mymusic::AudioObject)) > 0)
                __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", mix);
            static unsigned rawWindows = 0;   // v961: the raw voice/group bytes every 4th window (my_music_mixer_diag.h rawLine)
            if ((rawWindows++ & 3u) == 0) {
                thread_local char raw[1200];
                for (const bool music : {true, false})
                    if (::recomp::mymusic::mixer::rawLine(raw, sizeof raw, base, ::recomp::mymusic::AudioObject, music) > 0)
                        __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", raw);
            }
#endif
        }
    }
    return state;
}
// 7001A accepted: the hold record (and the diagnostic count).
[[gnu::noinline]] void recompMyMusicControl(std::uint32_t request, std::uint32_t locked) {
    ::recomp::mymusic::noteControl(request, locked);
    if (recompMyMusicLogged()) (request == 0 ? ::recomp::mymusic::shared.takes : ::recomp::mymusic::shared.restores).fetch_add(1, std::memory_order_relaxed);
}
// The 70009 handler's two descriptions (one call site, kept here so the handler's lines stay one for one).
inline const char* recompMyMusicStatusText(std::uint32_t state) {
    return state == 0 ? "Native standalone music player is idle; game audio unchanged"
                      : "My Music: the app's music player state (the game mutes its music while it plays)";
}
} // namespace
