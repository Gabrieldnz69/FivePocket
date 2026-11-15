#include "smoke_runtime.h"

#include <sstream>
#include <utility>

namespace recomp::bootstrap {

const char* stateName(RuntimeState state) noexcept {
    switch (state) {
    case RuntimeState::Cold:
        return "cold";
    case RuntimeState::Ready:
        return "ready";
    case RuntimeState::Running:
        return "running";
    case RuntimeState::Suspended:
        return "suspended";
    case RuntimeState::Stopped:
        return "stopped";
    case RuntimeState::Failed:
        return "failed";
    }

    return "unknown";
}

const char* rendererStateName(RendererState state) noexcept {
    switch (state) {
    case RendererState::NoSurface:
        return "waiting-for-surface";
    case RendererState::Probing:
        return "probing";
    case RendererState::Ready:
        return "ready";
    case RendererState::Presented:
        return "presented";
    case RendererState::Unsupported:
        return "unsupported";
    case RendererState::Failed:
        return "failed";
    }

    return "unknown";
}

bool SmokeRuntime::initialize(std::string dataDirectory, std::string cacheDirectory) {
    std::lock_guard lock(mutex_);

    if (dataDirectory.empty() || cacheDirectory.empty()) {
        dataDirectory_.clear();
        cacheDirectory_.clear();
        state_ = RuntimeState::Failed;
        return false;
    }

    dataDirectory_ = std::move(dataDirectory);
    cacheDirectory_ = std::move(cacheDirectory);
    if (!guestAddressSpace_.reserve()) {
        dataDirectory_.clear();
        cacheDirectory_.clear();
        state_ = RuntimeState::Failed;
        return false;
    }
    state_ = RuntimeState::Ready;
    rendererState_ = RendererState::NoSurface;
    rendererDiagnostic_ = "Waiting for an Android Surface";
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;
    return true;
}

void SmokeRuntime::resume() {
    std::lock_guard lock(mutex_);
    if (state_ == RuntimeState::Ready || state_ == RuntimeState::Suspended) {
        state_ = RuntimeState::Running;
    }
}

void SmokeRuntime::suspend() {
    std::lock_guard lock(mutex_);
    if (state_ == RuntimeState::Running) {
        state_ = RuntimeState::Suspended;
    }
}

void SmokeRuntime::shutdown() {
    std::lock_guard lock(mutex_);
    dataDirectory_.clear();
    cacheDirectory_.clear();
    guestAddressSpace_.release();
    state_ = RuntimeState::Stopped;
    rendererState_ = RendererState::NoSurface;
    rendererDiagnostic_ = "Renderer stopped";
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;
}

void SmokeRuntime::surfaceAttached(int width, int height) {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::Probing;
    rendererDiagnostic_ = "Checking Vulkan surface support";
    surfaceWidth_ = width;
    surfaceHeight_ = height;
}

void SmokeRuntime::rendererReady(std::string diagnostic) {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::Ready;
    rendererDiagnostic_ = std::move(diagnostic);
}

void SmokeRuntime::framePresented(std::string diagnostic) {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::Presented;
    rendererDiagnostic_ = std::move(diagnostic);
}

void SmokeRuntime::rendererUnsupported(std::string diagnostic) {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::Unsupported;
    rendererDiagnostic_ = std::move(diagnostic);
}

void SmokeRuntime::rendererFailed(std::string diagnostic) {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::Failed;
    rendererDiagnostic_ = std::move(diagnostic);
}

void SmokeRuntime::surfaceDetached() {
    std::lock_guard lock(mutex_);
    rendererState_ = RendererState::NoSurface;
    rendererDiagnostic_ = "Waiting for an Android Surface";
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;
}

RuntimeState SmokeRuntime::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

RendererState SmokeRuntime::rendererState() const {
    std::lock_guard lock(mutex_);
    return rendererState_;
}

std::string SmokeRuntime::describe(const char* abi) const {
    std::lock_guard lock(mutex_);
    std::ostringstream result;
    result << "Native library loaded\n"
           << "ABI: " << (abi == nullptr ? "unknown" : abi) << '\n'
           << "Lifecycle: " << stateName(state_) << '\n'
           << "Renderer: " << rendererStateName(rendererState_) << '\n'
           << "Surface: ";
    if (surfaceWidth_ > 0 && surfaceHeight_ > 0) {
        result << surfaceWidth_ << 'x' << surfaceHeight_;
    } else {
        result << "detached";
    }
    result << '\n'
           << "Vulkan: " << rendererDiagnostic_ << '\n'
           << "Guest address space: "
           << (guestAddressSpace_.isReserved() ? "reserved (4 GiB virtual)" : "unavailable")
           << '\n'
           << "Game executable: not loaded; no guest execution";
    return result.str();
}

} // namespace recomp::bootstrap
