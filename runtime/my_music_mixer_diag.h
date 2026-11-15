#pragma once
// my_music_mixer_diag.h (Update 1 P6, diagnostic only; v958 replaces v957's wrappers, which were never on the path: the mixer subtree is
// called only as direct generated-body calls, see memory wrapper-call-site-routing). It reads the mixer's OUTPUT state from guest memory
// once a second (the RENDER_MY_MUSIC cadence, diag-my-music-log only). Decoded from v957's codegen (host, read-only):
//  - sub_8314BCF8 (the mixer's per-group update, ppc_recomp.254.cpp:33900-34950) walks 96 slots: group pointer = u32 at audio object
//    + 12768 + 4*i (0 = none), voice = engine + 6688 + 336*i, engine = u32 at 0x83B8E210; a music group has byte group+104 & 0x80;
//  - its channel volumes go out as queued commands (sub_8313CCC8 type 4 with the vector / sub_8313CC50 type 8 zero); the audio thread's
//    voice handler sub_8314CCE8 (ppc_recomp.254.cpp:36773) stores them: voice + 48 + 32*channel = 4 big-endian floats (type 8 = zeros);
//    channels 0..4 (a route byte group+80+ch with (b & 0xFE) == 254 is unused and skipped, as the mixer skips it).
// The audio object is my_music.h's AudioObject (the same object whose byte +13156 is the custom-soundtrack mute the mixer reads).
// Line (once a second): RENDER_MY_MUSIC mixer window_ms=.. groups=.. music_groups=.. music_live=.. music_max=.. other_live=.. other_max=..
//  live = channels with a non-zero volume; max = the largest |volume| over those channels.
// PASS for E-SR1 A/B: forced phases music_live = 0 / music_max = 0; off phases (radio audible) music_live > 0; other_* about equal.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::mymusic::mixer {
constexpr std::uint32_t EngineWord = 0x83B8E210u, Groups = 12768, Slots = 96, VoiceBase = 6688, VoiceStride = 336;
constexpr std::uint32_t GroupFlag = 104, MusicBit = 0x80, Route = 80, Volumes = 48, ChannelStride = 32, Channels = 5;

struct Snapshot { unsigned groups = 0, musicGroups = 0, musicLive = 0, otherLive = 0; float musicMax = 0, otherMax = 0; bool ok = false; };

inline bool plausible(std::uint32_t a) noexcept { return a >= 0x10000u && a < 0xFFFF0000u && (a & 3u) == 0; }
inline std::uint32_t be32(const std::uint8_t* base, std::uint32_t a) noexcept {
    std::uint32_t v; std::memcpy(&v, base + a, 4); return __builtin_bswap32(v);
}
inline float bef(const std::uint8_t* base, std::uint32_t a) noexcept { const std::uint32_t b = be32(base, a); float f; std::memcpy(&f, &b, 4); return f; }

// audio = the audio object (my_music.h AudioObject). Reads only; skips anything implausible.
inline Snapshot read(const std::uint8_t* base, std::uint32_t audio) noexcept {
    Snapshot s;
    if (!base || !plausible(audio)) return s;
    const std::uint32_t engine = be32(base, EngineWord);
    if (!plausible(engine)) return s;
    for (std::uint32_t i = 0; i < Slots; ++i) {
        const std::uint32_t g = be32(base, audio + Groups + 4 * i);
        if (!plausible(g)) continue;
        ++s.groups;
        const bool music = (base[g + GroupFlag] & MusicBit) != 0;
        if (music) ++s.musicGroups;
        const std::uint32_t voice = engine + VoiceBase + VoiceStride * i;
        for (std::uint32_t ch = 0; ch < Channels; ++ch) {
            if ((base[g + Route + ch] & 0xFEu) == 254u) continue;
            float m = 0;
            for (int k = 0; k < 4; ++k) { const float f = std::fabs(bef(base, voice + Volumes + ChannelStride * ch + 4 * k)); if (std::isfinite(f) && f > m) m = f; }
            if (m > 0) { if (music) { ++s.musicLive; if (m > s.musicMax) s.musicMax = m; } else { ++s.otherLive; if (m > s.otherMax) s.otherMax = m; } }
        }
    }
    s.ok = true;
    return s;
}
inline int line(char* out, std::size_t size, std::uint32_t windowMs, const Snapshot& s) noexcept {
    if (!s.ok) return std::snprintf(out, size, "RENDER_MY_MUSIC mixer window_ms=%u unreadable=1", windowMs);
    return std::snprintf(out, size, "RENDER_MY_MUSIC mixer window_ms=%u groups=%u music_groups=%u music_live=%u music_max=%.3f other_live=%u other_max=%.3f",
                         windowMs, s.groups, s.musicGroups, s.musicLive, double(s.musicMax), s.otherLive, double(s.otherMax));
}
// v961 (raw dump, diagnostic): v958's read showed music_max ~0 and other_max ~1.8e16, so the "4 floats per channel" decode is not
// confirmed. rawLine dumps, for the first plausible slot of one kind (music = group+104 & 0x80, or not), the whole 336-byte voice and the
// group's first 128 bytes as big-endian hex, with the slot index, the group/voice addresses, the flag byte and the five route bytes.
// RENDER_MY_MUSIC mixer_raw kind=music|other slot=.. group=........ flag=.. route=.......... voice=........ v=<672 hex> g=<256 hex>
// Reads only; "none" when no slot of that kind exists.
constexpr std::uint32_t RawGroupBytes = 128;
inline int rawLine(char* out, std::size_t size, const std::uint8_t* base, std::uint32_t audio, bool wantMusic) noexcept {
    const char* kind = wantMusic ? "music" : "other";
    if (!base || !plausible(audio)) return std::snprintf(out, size, "RENDER_MY_MUSIC mixer_raw kind=%s unreadable=1", kind);
    const std::uint32_t engine = be32(base, EngineWord);
    if (!plausible(engine)) return std::snprintf(out, size, "RENDER_MY_MUSIC mixer_raw kind=%s unreadable=1", kind);
    for (std::uint32_t i = 0; i < Slots; ++i) {
        const std::uint32_t g = be32(base, audio + Groups + 4 * i);
        if (!plausible(g) || ((base[g + GroupFlag] & MusicBit) != 0) != wantMusic) continue;
        const std::uint32_t voice = engine + VoiceBase + VoiceStride * i;
        int n = std::snprintf(out, size, "RENDER_MY_MUSIC mixer_raw kind=%s slot=%u group=%08x flag=%02x route=%02x%02x%02x%02x%02x voice=%08x v=",
                              kind, i, g, base[g + GroupFlag], base[g + Route], base[g + Route + 1], base[g + Route + 2], base[g + Route + 3],
                              base[g + Route + 4], voice);
        static const char hex[] = "0123456789abcdef";
        const auto put = [&](std::uint32_t from, std::uint32_t count) {
            for (std::uint32_t k = 0; k < count && n >= 0 && std::size_t(n) + 3 < size; ++k) {
                const std::uint8_t b = base[from + k]; out[n++] = hex[b >> 4]; out[n++] = hex[b & 15];
            }
            if (n >= 0 && std::size_t(n) < size) out[n] = 0;
        };
        put(voice, VoiceStride);
        if (n >= 0 && std::size_t(n) + 4 < size) { std::memcpy(out + n, " g=", 3); n += 3; out[n] = 0; }
        put(g, RawGroupBytes);
        return n;
    }
    return std::snprintf(out, size, "RENDER_MY_MUSIC mixer_raw kind=%s none=1", kind);
}
} // namespace recomp::mymusic::mixer
