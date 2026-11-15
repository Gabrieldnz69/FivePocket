#pragma once

#include <memory>
#include <cstdint>
#include <vector>

struct ANativeWindow;

namespace recomp::bootstrap {

class SmokeRuntime;

// Owns the ANativeWindow reference passed to attachWindow until detachWindow.
// Vulkan work is deliberately synchronous: this bootstrap presents one clear
// frame whenever a surface is attached or resized.
class VulkanSmokeRenderer final {
public:
    explicit VulkanSmokeRenderer(SmokeRuntime& runtime);
    ~VulkanSmokeRenderer();

    VulkanSmokeRenderer(const VulkanSmokeRenderer&) = delete;
    VulkanSmokeRenderer& operator=(const VulkanSmokeRenderer&) = delete;

    bool attachWindow(ANativeWindow* ownedWindow, int width, int height);
    bool resize(int width, int height);
    void detachWindow();
    // Before attaching a surface only. Independent owned opaque720p pixels;
    // empty resets to the asset-free clear. Does not execute/acknowledge a guest.
    bool configureRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback=false);
    // Reuse the attached device/swapchain for the next owned image. Completes
    // the previous transfer before updating its mapped upload buffer.
    bool updateRgbaDiagnostic(std::vector<std::uint8_t> rgba,bool rejectReadback=false);
    // Sticky for this renderer's lifetime, set immediately after QueuePresent
    // accepts work, BEFORE fallible diagnostics/queue-idle cleanup. Not vblank.
    [[nodiscard]] bool hasQueuedPresentation() const noexcept;
    // Counts actual QueuePresent acceptances, including an acceptance followed
    // by a later failure. Consumers compare before/after for each attempt.
    [[nodiscard]] std::uint64_t queuedPresentationCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace recomp::bootstrap
