#pragma once
// v1308 (bugfix-stability, Discord D4 "audio is faulty"): the audio threads' executor permit domain.
// Since v22 the audio engine update (guest thread 64141000, ~0.5 core) and the native XAudio PCM callback worker (64184000) share permit
// domain 1: one permit, so they take turns. On a CPU-bound phone (Fold4, 44 ms frames) the callback renders ~170 packets/s against the
// 187.5/s that 48 kHz / 256 frames needs; AAudio then holds one packet or none (fold-touch2 runs v1175 / v1178), i.e. underruns.
// Hypothesis under test: the callback misses its 5.3 ms deadlines while the engine thread holds domain 1's permit.
// v1311: own domain by default; files/private-trace/diag-audio-callback-domain-off (read once, before any worker starts) = v1175. v1308 read
// files/private-trace/diag-audio-callback-domain (default absent = v1175 exactly), which gave the
// callback worker domain CallbackOwnDomain, which no other thread can get: the v171 diag-domain-each range then stops below it.
#include <cstdint>

namespace recomp_audio_domain {
inline constexpr std::uint32_t EngineToken=0x64141000u,CallbackToken=0x64184000u;
inline constexpr unsigned SharedDomain=1u,CallbackOwnDomain=63u;
inline constexpr unsigned NotAudio=~0u;
// The audio threads' domain; NotAudio for every other token (the caller's policy decides those).
constexpr unsigned domainFor(std::uint32_t token,bool callbackOwn) noexcept {
    if(token==CallbackToken)return callbackOwn?CallbackOwnDomain:SharedDomain;
    if(token==EngineToken)return SharedDomain;
    return NotAudio;
}
// v171's diag-domain-each hands out domains 8 .. eachEnd()-1.
constexpr unsigned eachEnd(bool callbackOwn) noexcept {return callbackOwn?CallbackOwnDomain:64u;}
} // namespace recomp_audio_domain
