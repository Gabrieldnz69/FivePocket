#pragma once

#include "guest_address_space.h"

#include <mutex>
#include <string>

namespace recomp::bootstrap {

enum class RuntimeState {
    Cold,
    Ready,
    Running,
    Suspended,
    Stopped,
    Failed,
};

enum class RendererState {
    NoSurface,
    Probing,
    Ready,
    Presented,
    Unsupported,
    Failed,
};

class SmokeRuntime final {
public:
    bool initialize(std::string dataDirectory, std::string cacheDirectory);
    void resume();
    void suspend();
    void shutdown();

    void surfaceAttached(int width, int height);
    void rendererReady(std::string diagnostic);
    void framePresented(std::string diagnostic);
    void rendererUnsupported(std::string diagnostic);
    void rendererFailed(std::string diagnostic);
    void surfaceDetached();

    [[nodiscard]] RuntimeState state() const;
    [[nodiscard]] RendererState rendererState() const;
    [[nodiscard]] std::string describe(const char* abi) const;

private:
    mutable std::mutex mutex_;
    RuntimeState state_{RuntimeState::Cold};
    RendererState rendererState_{RendererState::NoSurface};
    std::string dataDirectory_;
    std::string cacheDirectory_;
    std::string rendererDiagnostic_{"Waiting for an Android Surface"};
    GuestAddressSpace guestAddressSpace_;
    int surfaceWidth_{0};
    int surfaceHeight_{0};
};

[[nodiscard]] const char* stateName(RuntimeState state) noexcept;
[[nodiscard]] const char* rendererStateName(RendererState state) noexcept;

} // namespace recomp::bootstrap
