#pragma once
// audio_route.h (v1710, lane candidate-v1710 2026-11-13; notes local-analysis/update-1/subagents-2026-11-13/candidate-v1710/NOTES.md).
// An audio route change (Bluetooth headphones connect / disconnect) no longer stops the game.
//
// The bug (Thor, v1670 runs 0107 and 0117; same code in v1565 and the public app): the owner's AirPods connected (A2DP available
// 03:44:10.678) / disconnected (03:53:44.767); ~0 s / ~1 s later the native audio worker's poll returned Demand::Failed and
// trace_runner.cpp threw "Native audio demand/counter/backend failure" -> EXEC_FIRST_STOP reason=8.
// Why (code): the output stream is an AAudio SHARED / PERFORMANCE_MODE_NONE stream (the legacy AudioTrack path) opened on the default
// route. When the routed device changes, AAudio DISCONNECTS such a stream: its state becomes AAUDIO_STREAM_STATE_DISCONNECTED, writes
// return AAUDIO_ERROR_DISCONNECTED and the stream's error callback (when one is set) is called with AAUDIO_ERROR_DISCONNECTED; AAudio
// never reroutes it. v1670 set no error callback. NativeAudioRenderClient::pollLocked's first test is sameConfiguration(), which asks
// AheadBackend -> AAudioOutputStream::configuration(); that returns nullopt for every state but OPEN/STARTING/STARTED, so the first
// poll after the disconnect is fail() -> Demand::Failed (sticky); a drain racing it fails the same way (writeStereo -> INVALID_STATE or
// -899 -> NativeAudioQueue failed_). Either way the client is failed for good and the worker throws.
//
// The fix (the standard AAudio way): Router is the NativeAudioPlaybackBackend in place of AAudioOutputStream. It owns one AAudio stream
// at a time. A disconnect, seen by whichever comes first (the stream's error callback, a DISCONNECTED state on any query, a -899
// write), DETACHES the stream at once (under the router's leaf lock, no blocking call); a helper thread (never the audio worker, never
// the error callback) closes it and opens a new stream on the default route (= the new device), retrying with backoff. Meanwhile:
//   - configuration() keeps reporting the configuration of the first stream (the client compares it on every poll; the new stream must
//     be 48 kHz stereo float again, its own capacity/burst are logged);
//   - writeStereo() returns 0 (backpressure): the client keeps its queued packets (<= 8) and the worker waits as it does when AAudio is
//     full; no guest packet is dropped or written twice by the router;
//   - framesWritten() = frames the client wrote (old streams' totals + the current stream's own AAudio counter: a real counter fault is
//     still a failure); framesRead() = frames retired. At a detach the old stream's written-but-unread frames (<= its capacity: what
//     the dead route still held) are counted as retired and logged as lost_frames: they will never play. That is the reopen gap.
//   - the new stream starts after its first accepted write (primed, as v431's first start).
// A real backend failure is still a stop: a state other than usable/disconnected (stopped, closed, ...), a write error other than
// -899, an invalid configuration, no new stream within ReopenBudgetNs, or more than StormLimit reroutes within a minute make the router
// FAILED: configuration() and the counters return nullopt, so the client's next poll fails exactly as before.
//
// diag-audio-route-sim (files/private-trace/diag-audio-route-sim; TEST ONLY, default off, read once when the first stream opens; it
// never acts without the file): up to 4 times in seconds after that open (text "40 150"; an empty file = 30). At each time the helper
// makes the current AAudio stream behave as a disconnected one (state DISCONNECTED, writes -899) and calls the stream's own error
// callback with AAUDIO_ERROR_DISCONNECTED, as AAudio does; from there the real path runs (detach, close, a real reopen).
//
// Log lines (tag RecompRenderer): RENDER_AUDIO_ROUTE open / disconnect source=<callback|state|write|frames> / reopened / after-reopen
// (+2 s and +10 s: counters rising) / sim / FAILED.
// Host test: local-analysis/update-1/subagents-2026-11-13/candidate-v1710/tests/audio_route_test.cpp (fake AAudio; v1670's
// AAudioOutputStream fails it).
#include "guest_audio_backend.h"
#include "native_audio_render_client.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace recomp_audio_route {
using Configuration = recomp::bootstrap::NativeAudioConfiguration;
constexpr std::int32_t ErrDisconnected = -899;   // AAUDIO_ERROR_DISCONNECTED
constexpr std::int32_t ErrIllegalArgument = -898; // AAUDIO_ERROR_ILLEGAL_ARGUMENT
constexpr std::int32_t ErrInvalidState = -895;    // AAUDIO_ERROR_INVALID_STATE
constexpr std::int32_t ErrOutOfRange = -882;      // AAUDIO_ERROR_OUT_OF_RANGE

enum class StreamState { Usable, Disconnected, Other }; // Usable = OPEN / STARTING / STARTED

// The error callback's target. Leaked on purpose (never freed): AAudio may call a stream's error callback late; the tag stays valid.
struct Signal {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<std::uint32_t, std::int32_t>> posted; // (stream generation, error)
    bool kicked = false, quit = false;
    void post(std::uint32_t generation, std::int32_t error) {
        { std::lock_guard l(m); posted.emplace_back(generation, error); }
        cv.notify_all();
    }
    void kick() {
        { std::lock_guard l(m); kicked = true; }
        cv.notify_all();
    }
};
struct CallbackTag { Signal* signal; std::uint32_t generation; }; // one per opened stream (leaked, 16 bytes)

// One opened native stream (the device: AAudio below; host tests: a fake AAudio).
class Stream {
public:
    virtual ~Stream() = default; // closes the stream
    virtual StreamState state() const noexcept = 0;
    virtual std::optional<Configuration> configuration() const noexcept = 0; // nullopt unless Usable and valid
    virtual std::int32_t write(const float* interleaved, std::uint32_t frames) noexcept = 0; // >= 0 accepted, < 0 error
    virtual std::optional<std::uint64_t> framesWritten() const noexcept = 0;
    virtual std::optional<std::uint64_t> framesRead() const noexcept = 0;
    virtual bool requestStart() noexcept = 0;
    virtual int deviceId() const noexcept { return 0; }
    virtual void simulateDisconnect() noexcept {}    // diag-audio-route-sim only
};
using Opener = std::function<std::unique_ptr<Stream>(CallbackTag* tag, std::int32_t* error)>;
using LogFn = std::function<void(const char*)>;

struct Limits {
    std::int64_t reopenBudgetNs = 5'000'000'000;  // no usable new stream within this: a real failure
    std::int64_t firstRetryNs = 20'000'000, maxRetryNs = 500'000'000;
    unsigned stormLimit = 12;                      // reroutes per 60 s before it counts as a failure
    std::int64_t afterReopenNs[2] = {2'000'000'000, 10'000'000'000};
};

inline std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// diag-audio-route-sim: up to 4 times (s) from the file; empty = one at 30 s; absent = none.
inline int readSimTimes(const char* path, double* out, int max) noexcept {
    if (!path) return 0;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    char text[128]{};
    const std::size_t n = std::fread(text, 1, sizeof text - 1, f);
    std::fclose(f);
    text[n] = 0;
    int k = 0;
    const char* p = text;
    while (k < max && *p) {
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) { ++p; continue; }
        if (std::isfinite(v) && v >= 0 && v < 86400) out[k++] = v;
        p = end;
    }
    if (!k) out[k++] = 30.0;
    std::sort(out, out + k);
    return k;
}

class Router final : public recomp::audio::NativeAudioPlaybackBackend {
public:
    struct Stats {
        std::uint64_t reroutes = 0, reopened = 0, lostFrames = 0, simFired = 0;
        bool attached = false, failed = false;
        std::uint32_t generation = 0;
    };
    Router(Opener opener, LogFn log, Limits limits = {}, const char* simSwitchPath = nullptr)
        : opener_(std::move(opener)), log_(std::move(log)), limits_(limits), simPath_(simSwitchPath), signal_(new Signal) {}
    ~Router() override {
        { std::lock_guard l(signal_->m); signal_->quit = true; }
        signal_->cv.notify_all();
        if (helper_.joinable()) helper_.join();
        std::unique_ptr<Stream> s, r;
        { std::lock_guard l(mutex_); s = std::move(stream_); r = std::move(retired_); }
    }
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    // The run's setup (synchronous, as AAudioOutputStream::open): false = no usable first stream (setup fails as before).
    bool open() {
        std::lock_guard l(mutex_);
        if (stream_ || opened_) return false;
        std::int32_t error = 0;
        auto s = opener_(new CallbackTag{signal_, generation_}, &error);
        const auto c = s ? s->configuration() : std::nullopt;
        if (!s || !c || !recomp::bootstrap::validNativeAudioConfiguration(*c)) { lastError_ = error; return false; }
        frozen_ = *c;
        stream_ = std::move(s);
        opened_ = true;
        openNs_ = nowNs();
        simCount_ = readSimTimes(simPath_, simTimes_, 4);
        say("RENDER_AUDIO_ROUTE open rate=%u ch=%u cap=%u burst=%u device=%d sim=%d%s", c->sampleRate, c->channels,
            c->bufferCapacityFrames, c->framesPerBurst, stream_->deviceId(), simCount_,
            simCount_ ? " (diag-audio-route-sim: TEST ONLY)" : "");
        helper_ = std::thread([this] { helperLoop(); });
        return true;
    }

    // NativeAudioPlaybackBackend
    std::optional<Configuration> configuration() const noexcept override {
        std::lock_guard l(mutex_);
        if (failed_ || !opened_) return std::nullopt;
        if (stream_) {
            const auto st = stream_->state();
            if (st == StreamState::Disconnected) detachLocked("state", ErrDisconnected);
#if defined(RECOMP_AUDIO_ROUTE_MUTANT_OTHER_IS_REROUTE)
            else if (st == StreamState::Other) detachLocked("state", ErrInvalidState); // mutant: a real failure rerouted
#else
            else if (st == StreamState::Other) { failLocked("stream state not usable (not a disconnect)"); return std::nullopt; }
#endif
        }
        return frozen_;
    }
    bool requestStart() noexcept override {
        std::lock_guard l(mutex_);
        if (failed_ || !opened_) return false;
        startWanted_ = true;
        if (!stream_) return true; // rerouting: the new stream starts after its first write
        if (curStarted_) return true;
        if (stream_->state() == StreamState::Disconnected) { detachLocked("state", ErrDisconnected); return true; }
        if (!stream_->requestStart()) { failLocked("requestStart refused"); return false; }
        curStarted_ = true;
        return true;
    }
    bool started() const noexcept override {
        std::lock_guard l(mutex_);
        return !failed_ && startWanted_;
    }
    std::int32_t writeStereo(const float* data, std::uint32_t frames) noexcept override {
        if (!data || !frames || frames > recomp::audio::AudioFramesPerPacket) return ErrIllegalArgument;
        for (std::size_t i = 0; i < std::size_t(frames) * 2; ++i)
            if (!std::isfinite(data[i]) || data[i] < -1 || data[i] > 1) return ErrOutOfRange;
        std::lock_guard l(mutex_);
        if (failed_ || !opened_) return ErrInvalidState;
#if defined(RECOMP_AUDIO_ROUTE_MUTANT_DROP_IN_GAP)
        if (!stream_) { baseWritten_ += frames; return std::int32_t(frames); } // mutant: packets in the gap accepted and dropped
#endif
        if (!stream_) return 0; // rerouting: backpressure, the client keeps the packet
        const auto r = stream_->write(data, frames);
        if (r == ErrDisconnected) { detachLocked("write", r); return 0; }
        if (r < 0 || std::uint32_t(r) > frames) { failLocked("write error"); lastError_ = r; return r < 0 ? r : ErrInvalidState; }
        curOwn_ += std::uint64_t(r);
        if (r > 0 && startWanted_ && !curStarted_) { // a new stream after a reroute: primed, now started
            if (!stream_->requestStart()) { failLocked("requestStart refused after a reopen"); return ErrInvalidState; }
            curStarted_ = true;
        }
        return r;
    }
    std::optional<std::uint64_t> framesWritten() const noexcept override {
        std::lock_guard l(mutex_);
        if (failed_ || !opened_) return std::nullopt;
        if (!stream_) return baseWritten_;
        const auto w = stream_->framesWritten();
        if (!w) {
            if (stream_->state() == StreamState::Disconnected) { detachLocked("frames", ErrDisconnected); return baseWritten_; }
            failLocked("framesWritten unavailable");
            return std::nullopt;
        }
#if defined(RECOMP_AUDIO_ROUTE_MUTANT_NO_REBASE)
        return *w; // mutant: the new stream's counter is not rebased
#else
        return baseWritten_ + *w;
#endif
    }
    std::optional<std::uint64_t> framesRead() const noexcept override {
        std::lock_guard l(mutex_);
        if (failed_ || !opened_) return std::nullopt;
        if (!stream_) return baseWritten_; // detached: everything written so far is retired (played or lost)
        const auto r = stream_->framesRead();
        if (!r) {
            if (stream_->state() == StreamState::Disconnected) { detachLocked("frames", ErrDisconnected); return baseWritten_; }
            failLocked("framesRead unavailable");
            return std::nullopt;
        }
        const std::uint64_t v = baseWritten_ + *r;
        lastRead_ = v;
        return v;
    }
    std::int32_t lastError() const noexcept { std::lock_guard l(mutex_); return lastError_; }
    Stats stats() const noexcept {
        std::lock_guard l(mutex_);
        return {reroutes_, reopened_, lostFrames_, simFired_, stream_ != nullptr, failed_, generation_};
    }

private:
    template <class... A> void say(const char* fmt, A... a) const noexcept {
        if (!log_) return;
        char text[400];
        std::snprintf(text, sizeof text, fmt, a...);
        log_(text);
    }
    // Under mutex_. No blocking call: the stream moves to retired_ for the helper to close.
    void detachLocked(const char* source, std::int32_t error) const noexcept {
        if (!stream_ || failed_) return;
        std::uint64_t oldRead = lastRead_ > baseWritten_ ? lastRead_ - baseWritten_ : 0;
        if (const auto r = stream_->framesRead(); r && *r >= oldRead && *r <= curOwn_) oldRead = *r;
        const std::uint64_t lost = curOwn_ > oldRead ? curOwn_ - oldRead : 0;
        const int dev = stream_->deviceId();
        baseWritten_ += curOwn_;
        lastRead_ = baseWritten_;
        lostFrames_ += lost;
        ++reroutes_;
        ++generation_; // a late error callback of the old stream is ignored from now on
        curOwn_ = 0;
        curStarted_ = false;
        retired_ = std::move(stream_);
        detachNs_ = nowNs();
        rerouteTimes_.push_back(detachNs_);
        while (!rerouteTimes_.empty() && detachNs_ - rerouteTimes_.front() > 60'000'000'000) rerouteTimes_.pop_front();
        std::snprintf(pending_, sizeof pending_,
            "RENDER_AUDIO_ROUTE disconnect source=%s error=%d device=%d written=%llu retired=%llu lost_frames=%llu reroutes=%llu gen=%u",
            source, int(error), dev, (unsigned long long)baseWritten_, (unsigned long long)baseWritten_, (unsigned long long)lost,
            (unsigned long long)reroutes_, unsigned(generation_));
        if (rerouteTimes_.size() > limits_.stormLimit) failLocked("reroute storm (more than the limit in 60 s)");
        signal_->kick();
    }
    void failLocked(const char* why) const noexcept {
        if (failed_) return;
        failed_ = true;
        std::snprintf(failWhy_, sizeof failWhy_, "%s", why);
        signal_->kick();
    }

    void helperLoop() {
        unsigned simNext = 0;
        bool failLogged = false;
        std::int64_t afterAt[2] = {0, 0};
        for (;;) {
            // 1. wait for a kick, a posted error, a sim time or an after-reopen line
            std::int64_t wake = INT64_MAX;
            if (simNext < unsigned(simCount_)) wake = std::min(wake, openNs_ + std::int64_t(simTimes_[simNext] * 1e9));
            for (auto t : afterAt) if (t) wake = std::min(wake, t);
            std::deque<std::pair<std::uint32_t, std::int32_t>> posted;
            {
                std::unique_lock l(signal_->m);
                const auto pred = [&] { return signal_->quit || signal_->kicked || !signal_->posted.empty(); };
                if (wake == INT64_MAX) signal_->cv.wait(l, pred);
                else {
                    const auto delta = wake - nowNs();
                    if (delta > 0) signal_->cv.wait_for(l, std::chrono::nanoseconds(delta), pred);
                }
                if (signal_->quit) return;
                signal_->kicked = false;
                posted.swap(signal_->posted);
            }
            // 2. error callbacks (the AAudio thread only posted them)
            for (const auto& [gen, err] : posted) {
                std::lock_guard l(mutex_);
                if (gen != generation_ || !stream_) continue; // an old stream's late callback
                if (err == ErrDisconnected || stream_->state() == StreamState::Disconnected) detachLocked("callback", err);
                else std::snprintf(pending_, sizeof pending_, "RENDER_AUDIO_ROUTE error callback error=%d (not a disconnect; the polls decide)", int(err));
            }
            // 3. the sim (TEST ONLY): make the live stream disconnected the way AAudio does
            const std::int64_t now = nowNs();
            while (simNext < unsigned(simCount_) && now >= openNs_ + std::int64_t(simTimes_[simNext] * 1e9)) {
                std::lock_guard l(mutex_);
                if (stream_ && !failed_) {
                    ++simFired_;
                    say("RENDER_AUDIO_ROUTE sim: disconnect injected at +%.1f s (diag-audio-route-sim, TEST ONLY) device=%d",
                        simTimes_[simNext], stream_->deviceId());
                    stream_->simulateDisconnect(); // posts the error callback; the next poll / write / the callback detaches
                } else say("RENDER_AUDIO_ROUTE sim: +%.1f s skipped (no live stream)", simTimes_[simNext]);
                ++simNext;
            }
            // 4. log, close the retired stream, reopen
            std::unique_ptr<Stream> old;
            bool need = false;
            {
                std::lock_guard l(mutex_);
                if (pending_[0]) { if (log_) log_(pending_); pending_[0] = 0; }
                old = std::move(retired_);
                need = !stream_ && opened_ && !failed_;
                if (failed_ && !failLogged) {
                    failLogged = true;
                    say("RENDER_AUDIO_ROUTE FAILED: %s (a real backend failure: the next poll stops the run)", failWhy_);
                }
            }
            old.reset(); // AAudioStream_close, off the worker and off the error callback
            if (need) reopen(afterAt);
            // 5. the after-reopen lines (counters rising)
            for (int k = 0; k < 2; ++k) {
                if (!afterAt[k] || nowNs() < afterAt[k]) continue;
                afterAt[k] = 0;
                std::lock_guard l(mutex_);
                if (!stream_) continue;
                const auto w = stream_->framesWritten(), r = stream_->framesRead();
                say("RENDER_AUDIO_ROUTE after-reopen +%llds written=%llu read=%llu state=%s started=%d device=%d reroutes=%llu lost_frames=%llu",
                    (long long)(limits_.afterReopenNs[k] / 1'000'000'000), (unsigned long long)(baseWritten_ + (w ? *w : 0)),
                    (unsigned long long)(baseWritten_ + (r ? *r : 0)),
                    stream_->state() == StreamState::Usable ? "usable" : stream_->state() == StreamState::Disconnected ? "disconnected" : "other",
                    int(curStarted_), stream_->deviceId(), (unsigned long long)reroutes_, (unsigned long long)lostFrames_);
            }
        }
    }
    void reopen(std::int64_t* afterAt) {
        std::int64_t delay = 0, start = 0;
        { std::lock_guard l(mutex_); start = detachNs_; } // the budget counts from the disconnect
        unsigned attempt = 0;
        for (;;) {
            if (delay) { // interruptible wait (quit)
                std::unique_lock l(signal_->m);
                if (signal_->cv.wait_for(l, std::chrono::nanoseconds(delay), [&] { return signal_->quit; })) return;
            }
            ++attempt;
            std::uint32_t gen;
            { std::lock_guard l(mutex_); if (failed_ || stream_) return; gen = generation_; }
            std::int32_t error = 0;
#if defined(RECOMP_AUDIO_ROUTE_MUTANT_FAIL_AT_DISCONNECT)
            { std::lock_guard l(mutex_); failLocked("mutant: a disconnect is a failure"); return; }
#endif
            auto s = opener_(new CallbackTag{signal_, gen}, &error);
            const auto c = s ? s->configuration() : std::nullopt;
            const bool same = c && recomp::bootstrap::validNativeAudioConfiguration(*c) && c->sampleRate == frozen_.sampleRate &&
                              c->channels == frozen_.channels;
            if (same) {
                std::lock_guard l(mutex_);
                if (failed_) return;
                stream_ = std::move(s);
                curOwn_ = 0;
                curStarted_ = false;
                lastRead_ = baseWritten_;
                ++reopened_;
                say("RENDER_AUDIO_ROUTE reopened attempt=%u gap_ms=%.1f rate=%u ch=%u cap=%u burst=%u device=%d (reported cap=%u burst=%u kept) written=%llu",
                    attempt, double(nowNs() - detachNs_) * 1e-6, c->sampleRate, c->channels, c->bufferCapacityFrames, c->framesPerBurst,
                    stream_->deviceId(), frozen_.bufferCapacityFrames, frozen_.framesPerBurst, (unsigned long long)baseWritten_);
                const auto t = nowNs();
                afterAt[0] = t + limits_.afterReopenNs[0];
                afterAt[1] = t + limits_.afterReopenNs[1];
                return;
            }
            s.reset();
            say("RENDER_AUDIO_ROUTE reopen attempt=%u failed error=%d%s", attempt, int(error), c ? " (configuration differs)" : "");
#if !defined(RECOMP_AUDIO_ROUTE_MUTANT_NO_BUDGET)
            if (nowNs() - start >= limits_.reopenBudgetNs) {
                std::lock_guard l(mutex_);
                failLocked("no usable stream within the reopen budget"); // the helper logs FAILED once
                return;
            }
#endif
            delay = delay ? std::min(delay * 2, limits_.maxRetryNs) : limits_.firstRetryNs;
        }
    }
    Opener opener_;
    LogFn log_;
    Limits limits_;
    const char* simPath_;
    Signal* signal_; // leaked (late callbacks)
    std::thread helper_;
    mutable std::mutex mutex_; // leaf: never held across a blocking call; lock order R > L_audio > this > Signal::m
    // guarded by mutex_
    mutable std::unique_ptr<Stream> stream_, retired_;
    Configuration frozen_{};
    bool opened_ = false;
    mutable bool failed_ = false, curStarted_ = false;
    bool startWanted_ = false;
    mutable std::uint64_t baseWritten_ = 0, curOwn_ = 0, lastRead_ = 0, lostFrames_ = 0, reroutes_ = 0;
    std::uint64_t reopened_ = 0, simFired_ = 0;
    mutable std::uint32_t generation_ = 0;
    mutable std::int64_t detachNs_ = 0;
    mutable std::deque<std::int64_t> rerouteTimes_;
    mutable char pending_[400]{};
    mutable char failWhy_[120]{};
    std::int32_t lastError_ = 0;
    std::int64_t openNs_ = 0;
    double simTimes_[4]{};
    int simCount_ = 0;
};
} // namespace recomp_audio_route

// ---- the AAudio stream (device; host tests compile it against a fake <aaudio/AAudio.h>) ----
#if defined(__ANDROID__) || defined(RECOMP_AUDIO_ROUTE_FAKE_AAUDIO)
#include <aaudio/AAudio.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace recomp_audio_route {
class AAudioStreamImpl final : public Stream {
public:
    static void onError(AAudioStream*, void* user, aaudio_result_t error) {
        auto* tag = static_cast<CallbackTag*>(user);
        if (tag && tag->signal) tag->signal->post(tag->generation, error); // only a post: no AAudio call, no close here
    }
    // v1670's AAudioOutputStream::open builder (48 kHz, stereo, float, SHARED, PERFORMANCE_MODE_NONE) + the error callback.
    static std::unique_ptr<Stream> open(CallbackTag* tag, std::int32_t* error) {
        AAudioStreamBuilder* builder = nullptr;
        aaudio_result_t r = AAudio_createStreamBuilder(&builder);
        if (r != AAUDIO_OK || !builder) { if (error) *error = r; return nullptr; }
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSampleRate(builder, 48000);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
        AAudioStreamBuilder_setErrorCallback(builder, &AAudioStreamImpl::onError, tag);
        AAudioStream* s = nullptr;
        r = AAudioStreamBuilder_openStream(builder, &s);
        AAudioStreamBuilder_delete(builder);
        if (r != AAUDIO_OK || !s) { if (error) *error = r; if (s) AAudioStream_close(s); return nullptr; }
        return std::unique_ptr<Stream>(new AAudioStreamImpl(s, tag));
    }
    ~AAudioStreamImpl() override { if (s_) AAudioStream_close(s_); }
    StreamState state() const noexcept override {
        if (sim_.load(std::memory_order_acquire)) return StreamState::Disconnected;
        const auto st = AAudioStream_getState(s_);
        if (st == AAUDIO_STREAM_STATE_OPEN || st == AAUDIO_STREAM_STATE_STARTING || st == AAUDIO_STREAM_STATE_STARTED) return StreamState::Usable;
        if (st == AAUDIO_STREAM_STATE_DISCONNECTED) return StreamState::Disconnected;
        return StreamState::Other;
    }
    // v1670's AAudioOutputStream::configuration checks, unchanged.
    std::optional<Configuration> configuration() const noexcept override {
        if (state() != StreamState::Usable || AAudioStream_getDirection(s_) != AAUDIO_DIRECTION_OUTPUT ||
            AAudioStream_getFormat(s_) != AAUDIO_FORMAT_PCM_FLOAT) return std::nullopt;
        const auto rate = AAudioStream_getSampleRate(s_), channels = AAudioStream_getChannelCount(s_);
        const auto capacity = AAudioStream_getBufferCapacityInFrames(s_), burst = AAudioStream_getFramesPerBurst(s_);
        if (rate <= 0 || channels <= 0 || capacity <= 0 || burst <= 0) return std::nullopt;
        Configuration c{std::uint32_t(rate), std::uint32_t(channels), std::uint32_t(capacity), std::uint32_t(burst)};
        return recomp::bootstrap::validNativeAudioConfiguration(c) ? std::optional{c} : std::nullopt;
    }
    std::int32_t write(const float* data, std::uint32_t frames) noexcept override {
        if (sim_.load(std::memory_order_acquire)) return AAUDIO_ERROR_DISCONNECTED;
        return AAudioStream_write(s_, data, std::int32_t(frames), 0); // timeout 0: never blocks (v1670)
    }
    std::optional<std::uint64_t> framesWritten() const noexcept override {
        const auto f = AAudioStream_getFramesWritten(s_);
        return f < 0 ? std::nullopt : std::optional{std::uint64_t(f)};
    }
    std::optional<std::uint64_t> framesRead() const noexcept override {
        const auto f = AAudioStream_getFramesRead(s_);
        return f < 0 ? std::nullopt : std::optional{std::uint64_t(f)};
    }
    bool requestStart() noexcept override {
        if (AAudioStream_getState(s_) != AAUDIO_STREAM_STATE_OPEN) return false;
        return AAudioStream_requestStart(s_) == AAUDIO_OK;
    }
    int deviceId() const noexcept override { return int(AAudioStream_getDeviceId(s_)); }
    void simulateDisconnect() noexcept override { // diag-audio-route-sim: what AAudio does at a route change
        sim_.store(true, std::memory_order_release);
        onError(s_, tag_, AAUDIO_ERROR_DISCONNECTED);
    }
private:
    AAudioStreamImpl(AAudioStream* s, CallbackTag* tag) : s_(s), tag_(tag) {}
    AAudioStream* s_;
    CallbackTag* tag_;
    std::atomic<bool> sim_{false};
};
inline std::shared_ptr<Router> makeAAudioRouter(const char* simSwitchPath, Limits limits = {}) {
    return std::make_shared<Router>(&AAudioStreamImpl::open,
#if defined(__ANDROID__)
        [](const char* line) { __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", line); },
#else
        [](const char* line) { std::fprintf(stderr, "%s\n", line); },
#endif
        limits, simSwitchPath);
}
} // namespace recomp_audio_route
#endif
