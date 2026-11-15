#include "ppc_config.h"
#include "ppc_context.h"
#include "ppc_recomp_shared.h"

#include "generated_guest_import_bridge.h"
#include "guest_address_space.h"
#include "guest_clock.h"
#include "guest_execution_guard.h"
#include "guest_image_loader.h"
#include "guest_import_registry.h"
#if defined(RECOMP_NATIVE_XMA)
#include "native_xma_context_pool.h"
#include <android/log.h>
#include <cstdio>
#endif
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE)
#include "renderer_boundary_trace.h"
#include "native_tile_scene_handoff.h"
#endif
#include "guest_cooperative_executor.h"
#include "guest_thread_entry.h"
#include "guest_title_environment.h"
#include "guest_thread_environment.h"
#include "guest_xex_header.h"
#include "guest_data_variable_binding.h"
#include "guest_kernel_version.h"
#include "guest_native_read_only_directory.h"
#include "guest_video_device_globals.h"
#include "native_scaler_timing.h"
#include "guest_stopped_gpu_submission.h"
#include "xenos_indirect_inspection.h"
#include "stopped_submission_capture.h"
#include "xenos_dma_triangle_draw.h"
#include <fstream>
#include "xenos_stopped_work.h"
#include "xenos_stopped_shader_capture.h"
#include "native_deferred_shader_capture.h"
#include "xenos_stopped_physical_capture.h"
#include "xenos_stopped_memory.h"
#include "xenos_stopped_word_staging.h"
#include "xenos_copy_shader.h"
#include "xenos_stopped_constant_initialization.h"
#include "native_swap_image_file.h"
#include "private_trace_api.h"
#include "private_display_sequence.h"
#if defined(RECOMP_PRIVATE_TRACE_LIBRARY)
#include <atomic>
#endif
#include "indirect_call_resolver.h"
#if defined(__ANDROID__)
#include "vulkan_engine.h"
#include "aaudio_output_stream.h"
#include "native_color_program_input.h"
#include "native_color_batch_capture.h"
#include "native_deferred_color_capture.h"
#include "native_deferred_color_memory.h"
#include "native_deferred_color_result.h"
#include "native_deferred_color_write_guard.h"
#include "native_color_chain_execution.h"
#include "native_color_scratch_memory.h"
#include "native_color_output_publication.h"
#include "xenos_native_initialization.h"
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::atomic<recomp::bootstrap::GuestImportRegistry*> nativeDiscRegistry{nullptr};
std::uint8_t* nativeDiscBase = nullptr;
struct NativeDiscBridgeScope {
    NativeDiscBridgeScope(recomp::bootstrap::GuestImportRegistry* registry, std::uint8_t* base) {
        nativeDiscBase = base;
        nativeDiscRegistry.store(registry, std::memory_order_release);
    }
    ~NativeDiscBridgeScope() {
        nativeDiscRegistry.store(nullptr, std::memory_order_release);
        nativeDiscBase = nullptr; // Executor has joined every guest writer.
    }
};
}

// Native implementation of the verified SDK handle-to-event helper. Keep its
// surrounding critical section, registered callback and disc API in original
// code. No emulated ExEventObjectType/OBJECT_TYPE structure is needed here.
extern "C" void __imp__sub_8364D680(PPCContext& ctx, std::uint8_t* base);
void sub_8364D680(PPCContext& ctx, std::uint8_t* base) {
    auto* registry = nativeDiscRegistry.load(std::memory_order_acquire);
    if (!registry) { __imp__sub_8364D680(ctx, base); return; }
    const auto event = base == nativeDiscBase
        ? registry->referenceNativeEvent(ctx.r3.u32) : std::nullopt;
    if (!event) {
        recomp::bootstrap::GuestExecutionGuard::captureImportArguments({ctx.r1.u64, {{
            ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64,
            ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64}}});
        recomp::bootstrap::GuestExecutionGuard::stopForRejectedImport("__imp__ObReferenceObjectByHandle");
    }
    ctx.r3.u64 = *event;
}

namespace {

constexpr std::string_view ProtocolHeader{"HOST_GUEST_TRACE_V1"};
constexpr std::uint64_t MaximumStagedXexHeaderSize = 16U * 1024U * 1024U;
constexpr std::uint64_t HeaderReadChunkSize = 1024U * 1024U;
constexpr std::uint64_t PageSize = 0x1000U;
constexpr std::uint64_t ModuleStructureSize = 0x64U;

struct NumericRange {
    std::uint64_t base;
    std::uint64_t size;
};

class NativeReadOnlyFile final : public recomp::bootstrap::GuestReadOnlyFile {
public:
    NativeReadOnlyFile(int descriptor, std::uint64_t size) : descriptor_(descriptor), size_(size) {}
    ~NativeReadOnlyFile() override { close(descriptor_); }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t bytes) const noexcept override {
        static_assert(sizeof(off_t) >= 8);
        if (offset > size_ || bytes > size_ - offset || bytes > 1024 * 1024 || (bytes && !destination)) { return false; }
        struct stat info {};
        if (fstat(descriptor_, &info) != 0 || info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) != size_) { return false; }
        std::size_t done = 0;
        while (done < bytes) {
            const auto count = pread(descriptor_, destination + done, bytes - done, static_cast<off_t>(offset + done));
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { return false; }
            done += static_cast<std::size_t>(count);
        }
        return true;
    }
private:
    int descriptor_;
    std::uint64_t size_;
};

class ScopedFile final {
public:
    explicit ScopedFile(int descriptor) noexcept : descriptor_(descriptor) {}
    ~ScopedFile() {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
    }

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    [[nodiscard]] int get() const noexcept { return descriptor_; }
    [[nodiscard]] int release() noexcept { const auto descriptor = descriptor_; descriptor_ = -1; return descriptor; }

private:
    int descriptor_{-1};
};

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
    value = 0;
    const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parseArguments(
        int argc,
        char** argv,
        std::array<std::uint64_t, 9>& values,
        bool& hasPhysicalArena,
        std::array<std::uint64_t, 3>& physicalValues,
        bool& hasTitleEnvironment,
        std::array<std::uint64_t, 8>& titleValues,
        std::size_t& rawXexArgumentIndex,
        bool& hasThreadEnvironment,
        std::array<std::uint64_t, 7>& threadValues,
        bool& hasThreadPool,
        std::array<std::uint64_t, 2>& threadPoolValues,
        bool& hasThreadType,
        std::array<std::uint64_t, 2>& threadTypeValues) {
    int baseArgumentCount = argc;
    hasThreadType = argc >= 3 && std::string_view(argv[argc - 3]) == "--thread-object-type";
    if (hasThreadType) {
        baseArgumentCount -= 3;
        for (std::size_t i = 0; i < threadTypeValues.size(); ++i) {
            if (!parseUnsigned(argv[baseArgumentCount + 1 + i], threadTypeValues[i])
                    || threadTypeValues[i] > UINT32_MAX) { return false; }
        }
    }
    hasThreadPool = baseArgumentCount >= 3
            && std::string_view(argv[baseArgumentCount - 3]) == "--thread-pool";
    if (hasThreadPool) {
        baseArgumentCount -= 3;
        for (std::size_t i = 0; i < threadPoolValues.size(); ++i) {
            if (!parseUnsigned(argv[baseArgumentCount + 1 + i], threadPoolValues[i])
                    || threadPoolValues[i] > UINT32_MAX) {
                return false;
            }
        }
    }
    hasThreadEnvironment = baseArgumentCount == 28 || baseArgumentCount == 31;
    if ((hasThreadPool || hasThreadType) && !hasThreadEnvironment) { return false; }
    if (hasThreadEnvironment) {
        baseArgumentCount -= 8;
        if (std::string_view(argv[baseArgumentCount]) != "--thread-environment") {
            return false;
        }
        for (std::size_t index = 0; index < threadValues.size(); ++index) {
            if (!parseUnsigned(argv[baseArgumentCount + 1 + index], threadValues[index])
                    || threadValues[index] > UINT32_MAX) {
                return false;
            }
        }
    }
    if (baseArgumentCount != 11 && baseArgumentCount != 14
            && baseArgumentCount != 20 && baseArgumentCount != 23) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!parseUnsigned(argv[index + 2], values[index])) {
            return false;
        }
    }
    hasPhysicalArena = baseArgumentCount == 14 || baseArgumentCount == 23;
    if (hasPhysicalArena) {
        for (std::size_t index = 0; index < physicalValues.size(); ++index) {
            if (!parseUnsigned(argv[index + 11], physicalValues[index])) {
                return false;
            }
        }
    }
    hasTitleEnvironment = baseArgumentCount == 20 || baseArgumentCount == 23;
    rawXexArgumentIndex = hasPhysicalArena ? 14 : 11;
    if (hasTitleEnvironment) {
        for (std::size_t index = 0; index < titleValues.size(); ++index) {
            if (!parseUnsigned(
                        argv[index + rawXexArgumentIndex + 1],
                        titleValues[index])) {
                return false;
            }
        }
    }
    return true;
}

bool fitsGuestRange(std::uint64_t address, std::uint64_t size) {
    constexpr std::uint64_t GuestAddressSpaceSize = 1ULL << 32U;
    return address <= std::numeric_limits<std::uint32_t>::max()
            && size <= GuestAddressSpaceSize
            && address <= GuestAddressSpaceSize - size;
}

bool rangesOverlap(const NumericRange& left, const NumericRange& right) {
    return left.base < right.base + right.size
            && right.base < left.base + left.size;
}

bool titleLayoutIsValid(
        const std::array<std::uint64_t, 9>& values,
        const std::array<std::uint64_t, 8>& titleValues) {
    const NumericRange fullImage{values[0], titleValues[1]};
    const NumericRange heap{values[2], values[3]};
    const NumericRange stack{values[4], values[5]};
    const NumericRange header{titleValues[2], titleValues[3]};
    const NumericRange exportedVariable{titleValues[5], 4};
    const NumericRange module{titleValues[6], ModuleStructureSize};
    const std::array ranges{fullImage, heap, stack, header, exportedVariable, module};
    for (const auto& range : ranges) {
        if (range.size == 0 || !fitsGuestRange(range.base, range.size)) {
            return false;
        }
    }
    for (std::size_t left = 0; left < ranges.size(); ++left) {
        for (std::size_t right = left + 1; right < ranges.size(); ++right) {
            if (rangesOverlap(ranges[left], ranges[right])) {
                return false;
            }
        }
    }
    const auto decodedImageEnd = values[0] + values[1];
    const auto fullImageEnd = fullImage.base + fullImage.size;
    return values[1] <= fullImage.size
            && titleValues[2] != 0
            && titleValues[5] != 0
            && titleValues[6] != 0
            && (titleValues[2] & 3U) == 0
            && (titleValues[5] & 3U) == 0
            && (titleValues[6] & 3U) == 0
            && titleValues[4] >= values[0]
            && titleValues[4] + 4 <= decodedImageEnd
            && titleValues[7] >= fullImage.base
            && titleValues[7] < fullImageEnd;
}

bool stageRawXexHeader(
        recomp::bootstrap::GuestAddressSpace& addressSpace,
        const char* rawXexPath,
        std::uint64_t expectedFileSize,
        std::uint32_t headerAddress,
        std::uint64_t headerSize) {
    if (rawXexPath == nullptr
            || !std::filesystem::path(rawXexPath).is_absolute()
            || headerSize < 24
            || headerSize > MaximumStagedXexHeaderSize
            || !fitsGuestRange(headerAddress, headerSize)) {
        return false;
    }
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    ScopedFile file(open(rawXexPath, flags));
    if (file.get() < 0) {
        return false;
    }
    struct stat information {};
    if (fstat(file.get(), &information) != 0
            || !S_ISREG(information.st_mode)
            || information.st_size < 0
            || static_cast<std::uint64_t>(information.st_size) != expectedFileSize
            || headerSize > expectedFileSize) {
        return false;
    }

    auto* destination = addressSpace.translate(headerAddress);
    std::uint64_t bytesRead = 0;
    while (bytesRead < headerSize) {
        const auto remaining = headerSize - bytesRead;
        const auto request = static_cast<std::size_t>(
                std::min(remaining, HeaderReadChunkSize));
        const auto result = read(file.get(), destination + bytesRead, request);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            std::memset(destination, 0, static_cast<std::size_t>(bytesRead));
            return false;
        }
        bytesRead += static_cast<std::uint64_t>(result);
    }
    const auto declaredHeaderSize =
            (static_cast<std::uint32_t>(destination[8]) << 24U)
            | (static_cast<std::uint32_t>(destination[9]) << 16U)
            | (static_cast<std::uint32_t>(destination[10]) << 8U)
            | static_cast<std::uint32_t>(destination[11]);
    if (destination[0] != 'X'
            || destination[1] != 'E'
            || destination[2] != 'X'
            || destination[3] != '2'
            || declaredHeaderSize != headerSize
            || fstat(file.get(), &information) != 0
            || information.st_size < 0
            || static_cast<std::uint64_t>(information.st_size) != expectedFileSize) {
        std::memset(destination, 0, static_cast<std::size_t>(headerSize));
        return false;
    }
    return true;
}

bool threadInputsMatchHeader(
        const recomp::bootstrap::GuestAddressSpace& addressSpace,
        std::uint32_t headerAddress,
        std::uint64_t headerSize,
        const std::array<std::uint64_t, 7>& threadValues) {
    const auto lookup = recomp::bootstrap::GuestXexHeader::lookup(
            addressSpace, headerAddress, 0x00020104U);
    if (!lookup.found()
            || lookup.kind != recomp::bootstrap::GuestXexHeaderValueKind::DataPointer
            || lookup.value < headerAddress
            || static_cast<std::uint64_t>(lookup.value) + 16
                    > static_cast<std::uint64_t>(headerAddress) + headerSize) {
        return false;
    }
    const auto* data = addressSpace.translate(lookup.value);
    const auto word = [&](std::size_t offset) -> std::uint32_t {
        return (static_cast<std::uint32_t>(data[offset]) << 24U)
                | (static_cast<std::uint32_t>(data[offset + 1]) << 16U)
                | (static_cast<std::uint32_t>(data[offset + 2]) << 8U)
                | static_cast<std::uint32_t>(data[offset + 3]);
    };
    return word(0) == threadValues[6] && word(4) == threadValues[3]
            && word(8) == threadValues[5] && word(12) == threadValues[4];
}

void addSaturating(std::uint64_t& target, std::size_t value) {
    const auto converted = static_cast<std::uint64_t>(value);
    if (converted > std::numeric_limits<std::uint64_t>::max() - target) {
        target = std::numeric_limits<std::uint64_t>::max();
    } else {
        target += converted;
    }
}

} // namespace

static int runTraceMain(int argc, char** argv,const RecompPrivateTraceCallbacksV2* callbacks=nullptr,
        const RecompPrivateTraceCallbacksV3* liveCallbacks=nullptr,
        const RecompPrivateTraceCallbacksV5* inputCallbacks=nullptr) {
    std::array<std::uint64_t, 3> kernelVersionValues{};
    const bool hasKernelVersion = argc >= 5 &&
        std::string_view(argv[argc - 4]) == "--native-kernel-version";
    if (hasKernelVersion) {
        for (unsigned i = 0; i < kernelVersionValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 3 + i], kernelVersionValues[i]) ||
                kernelVersionValues[i] > UINT32_MAX) return 2;
        if (!recomp::bootstrap::guestKernelVersionBytes(kernelVersionValues[2])) return 2;
        argc -= 4;
    }
    const bool liveDeferredColor=argc>=2 && std::string_view(argv[argc-1])=="--experimental-native-deferred-color-prefix";
    if(liveDeferredColor)--argc;
    const bool diagnoseDeferredColor=argc>=2 && std::string_view(argv[argc-1])=="--diagnose-stopped-deferred-color";
    if(diagnoseDeferredColor)--argc;
    const bool diagnosePostOutputColor=argc>=2 && std::string_view(argv[argc-1])=="--diagnose-stopped-post-output-color";
    if(diagnosePostOutputColor)--argc;
    const bool livePostOutputColor=argc>=2 && std::string_view(argv[argc-1])=="--experimental-native-post-output-color";
    if(livePostOutputColor)--argc;
    const bool nativeDisplayOutput=argc>=2 && std::string_view(argv[argc-1])=="--experimental-native-normal-display-output";
    if(nativeDisplayOutput)--argc;
    const bool nativeIdentityScaler=argc>=2 && std::string_view(argv[argc-1])=="--experimental-native-identity-scaler";
    if(nativeIdentityScaler)--argc;
    std::array<std::uint64_t,2> scalerTimingValues{};
    const bool nativeScalerTiming=argc>=4 && std::string_view(argv[argc-3])=="--experimental-native-scaler-timing";
    if(nativeScalerTiming){
        for(unsigned i=0;i<2;++i)if(!parseUnsigned(argv[argc-2+i],scalerTimingValues[i]) || scalerTimingValues[i]>UINT32_MAX)return 2;
        argc-=3;
    }
    if(nativeIdentityScaler && !nativeScalerTiming)return 2;
    if(nativeDisplayOutput && (!nativeIdentityScaler || !liveCallbacks))return 2;
    if(diagnosePostOutputColor && !nativeDisplayOutput)return 2;
    if(livePostOutputColor && (!nativeDisplayOutput || diagnosePostOutputColor))return 2;
    if(diagnoseDeferredColor && !livePostOutputColor)return 2;
    if(liveDeferredColor && (!livePostOutputColor || diagnoseDeferredColor))return 2;
    const bool nativeSwapCommands=argc>=2 && std::string_view(argv[argc-1])=="--experimental-native-swap-commands";
    if(nativeSwapCommands)--argc;
    const bool captureNativeSwapImage=argc>=2 && std::string_view(argv[argc-1])=="--capture-native-swap-image";
    if(captureNativeSwapImage)--argc;
    const bool nativeNoSystemOverlay=argc>=2 && std::string_view(argv[argc-1])=="--native-no-system-overlay";
    if(nativeNoSystemOverlay)--argc;
    if(captureNativeSwapImage && !nativeNoSystemOverlay)return 2;
    if(callbacks && (!nativeNoSystemOverlay || captureNativeSwapImage))return 2;
    if(nativeSwapCommands!=bool(liveCallbacks) || (nativeSwapCommands &&
        (!nativeNoSystemOverlay || captureNativeSwapImage || callbacks)))return 2;
    // Private bounded native-program experiments. Inspection and scratch modes
    // remain post-stop; live publication requires its separately named flag.
    std::string inspectColorDirectory;
    bool renderStoppedColor=false,lockedStoppedColor=false;
    unsigned commitStoppedColor=0;
    bool experimentalOwnedColor=false;
    if(argc>=3 && (std::string_view(argv[argc-2])=="--inspect-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--render-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--render-locked-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--commit-scratch-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--reject-scratch-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--stale-scratch-stopped-color-program" ||
        std::string_view(argv[argc-2])=="--experimental-owned-color-program")){
        experimentalOwnedColor=std::string_view(argv[argc-2])=="--experimental-owned-color-program";
        if(std::string_view(argv[argc-2])=="--commit-scratch-stopped-color-program")commitStoppedColor=1;
        if(std::string_view(argv[argc-2])=="--reject-scratch-stopped-color-program")commitStoppedColor=2;
        if(std::string_view(argv[argc-2])=="--stale-scratch-stopped-color-program")commitStoppedColor=3;
        lockedStoppedColor=std::string_view(argv[argc-2])=="--render-locked-stopped-color-program";
        renderStoppedColor=lockedStoppedColor || std::string_view(argv[argc-2])=="--render-stopped-color-program";
        inspectColorDirectory=argv[argc-1];argc-=2;
        if(inspectColorDirectory.empty() || inspectColorDirectory.front()!='/' || inspectColorDirectory.size()>4096)return 2;
    }
#if defined(__ANDROID__)
    std::optional<recomp::gpu::NativeColorChainProgram> inspectColorProgram;
    if(!inspectColorDirectory.empty()){
        inspectColorProgram=recomp::gpu::readNativeColorProgram(inspectColorDirectory,recomp::gpu::NativeColorCoordinateProfile::Padded1280W1);
        if(!inspectColorProgram){std::cerr<<"trace setup failed: stopped color program rejected\n";return 2;}
    }
#else
    if(!inspectColorDirectory.empty())return 2;
#endif
    recomp::gpu::StoppedPhysicalRanges inspectOwnershipRanges;
    while(argc>=4 && std::string_view(argv[argc-3])=="--inspect-stopped-ownership"){
        std::uint64_t a=0,n=0;
        if(inspectOwnershipRanges.size()==8 || !parseUnsigned(argv[argc-2],a) ||
            !parseUnsigned(argv[argc-1],n) || a>UINT32_MAX || n>UINT32_MAX)return 2;
        inspectOwnershipRanges.push_back({static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(n)});argc-=3;
    }
    std::reverse(inspectOwnershipRanges.begin(),inspectOwnershipRanges.end());
    if(!inspectOwnershipRanges.empty() && !recomp::gpu::validStoppedOwnershipRanges(inspectOwnershipRanges))return 2;
    recomp::gpu::StoppedPhysicalRanges capturePhysicalRanges;
    while (argc >= 4 && std::string_view(argv[argc - 3]) == "--capture-stopped-range") {
        std::uint64_t a = 0, n = 0;
        if (capturePhysicalRanges.size() == 8 || !parseUnsigned(argv[argc - 2],a) ||
                !parseUnsigned(argv[argc - 1],n) || a > UINT32_MAX || n > UINT32_MAX) return 2;
        capturePhysicalRanges.push_back({static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(n)});
        argc -= 3;
    }
    std::reverse(capturePhysicalRanges.begin(),capturePhysicalRanges.end());
    std::uint32_t captureByteBudget=recomp::gpu::DefaultStoppedPhysicalByteBudget;
    if(argc>=3 && std::string_view(argv[argc-2])=="--capture-stopped-byte-budget") {
        std::uint64_t value=0;
        if(!parseUnsigned(argv[argc-1],value) || value>UINT32_MAX)return 2;
        captureByteBudget=static_cast<std::uint32_t>(value);argc-=2;
    }
    if ((!capturePhysicalRanges.empty() && !recomp::gpu::validStoppedPhysicalRanges(capturePhysicalRanges,captureByteBudget)) ||
        (capturePhysicalRanges.empty() && captureByteBudget!=recomp::gpu::DefaultStoppedPhysicalByteBudget)) return 2;
    const bool experimentalIdleCachePair = argc >= 2 && std::string_view(argv[argc - 1]) == "--experimental-idle-cache-pair";
    if (experimentalIdleCachePair) --argc;
    const bool nativeHighPriorityTiers = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-high-priority-tiers";
    if (nativeHighPriorityTiers) --argc;
    const bool nativeCriticalSectionWaits = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-critical-section-waits";
    if (nativeCriticalSectionWaits) --argc;
    const bool nativeCategoryOneUnityGain = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-category1-unity-gain";
    if (nativeCategoryOneUnityGain) --argc;
    const bool nativeEventWakeBoost = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-event-wake-boost";
    if (nativeEventWakeBoost) --argc;
    const bool experimentalNativeAudioRenderer = argc >= 2 && std::string_view(argv[argc - 1]) == "--experimental-native-audio-renderer";
    if (experimentalNativeAudioRenderer) --argc;
    if (nativeCategoryOneUnityGain && !experimentalNativeAudioRenderer) {
        std::cerr << "trace setup failed: category1 gain requires the native PCM renderer\n";return 2;
    }
    const bool experimentalNativeAudio = argc >= 2 && std::string_view(argv[argc - 1]) == "--experimental-native-audio";
    if (experimentalNativeAudio) --argc;
    if (experimentalNativeAudioRenderer && !experimentalNativeAudio) {
        std::cerr << "trace setup failed: renderer requires explicit experimental native audio\n";return 2;
    }
#if !defined(__ANDROID__)
    if (experimentalNativeAudio) { std::cerr << "trace setup failed: native audio currently requires Android\n"; return 2; }
#endif
    const bool disconnectedInput = argc >= 2 && std::string_view(argv[argc - 1]) == "--disconnected-input";
    if (disconnectedInput) --argc;
    const bool nativeInput = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-input";
    if(nativeInput)--argc;
    if(nativeInput && (disconnectedInput || !inputCallbacks))return 2;
    const bool imagePageProtections = argc >= 2 && std::string_view(argv[argc - 1]) == "--xex-image-protections";
    if (imagePageProtections) --argc;
    const bool experimentalGpuClock = argc >= 2 && std::string_view(argv[argc - 1]) == "--experimental-gpu-clock-500mhz";
    if (experimentalGpuClock) --argc;
    const bool captureStoppedPm4 = argc >= 2 && std::string_view(argv[argc - 1]) == "--capture-stopped-pm4";
    if (captureStoppedPm4) --argc;
    const bool captureStoppedShaders = argc >= 2 && std::string_view(argv[argc - 1]) == "--capture-stopped-shaders";
    if (captureStoppedShaders) --argc;
    if(diagnoseDeferredColor && (!captureStoppedPm4 || !captureStoppedShaders || !capturePhysicalRanges.empty()))return 2;
    const bool nativeStateBatches = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-state-batches";
    if (nativeStateBatches) --argc;
    const bool nativeEmptyCacheCoherency = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-empty-cache-coherency";
    if (nativeEmptyCacheCoherency) --argc;
    const bool experimentalNativeInitialization = argc >= 2 && std::string_view(argv[argc - 1]) == "--experimental-native-initialization";
    if (experimentalNativeInitialization) --argc;
    const bool nativeInitialCoherency = argc >= 2 && std::string_view(argv[argc - 1]) == "--native-initial-coherency";
    if (nativeInitialCoherency) --argc;
    if (experimentalNativeInitialization && !nativeInitialCoherency) return 2;
    if (nativeEmptyCacheCoherency && !experimentalNativeInitialization) return 2;
    if (nativeStateBatches && !experimentalNativeInitialization) return 2;
    if (experimentalIdleCachePair && (!nativeStateBatches || !nativeEmptyCacheCoherency)) return 2;
    if(experimentalOwnedColor && !nativeStateBatches)return 2;
    if(nativeNoSystemOverlay && !experimentalOwnedColor)return 2;
    if(nativeScalerTiming && (!nativeSwapCommands || !experimentalGpuClock))return 2;
    if(!inspectColorDirectory.empty() && !nativeStateBatches)return 2;
    const bool hasGraphics = argc >= 3 && std::string_view(argv[argc - 2]) == "--graphics-backend";
    if ((experimentalGpuClock || nativeInitialCoherency || captureStoppedShaders || captureStoppedPm4 ||
            !capturePhysicalRanges.empty() || !inspectOwnershipRanges.empty() || !inspectColorDirectory.empty()) && !hasGraphics) return 2;
    if (hasGraphics) {
#if !defined(__ANDROID__)
        std::cerr << "trace setup failed: Vulkan backend currently requires Android\n"; return 2;
#else
        if (std::string_view(argv[argc - 1]) != "vulkan-headless") return 2;
        argc -= 2;
#endif
    }
    std::array<std::uint64_t, 2> hsioLockValues{};
    const bool hasHsioLock = argc >= 4 && std::string_view(argv[argc - 3]) == "--hsio-lock";
    if (hasHsioLock) {
        for (std::size_t i = 0; i < hsioLockValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i],hsioLockValues[i]) || hsioLockValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::array<std::uint64_t, 2> certMonitorValues{};
    const bool hasNoCertMonitor = argc >= 4 && std::string_view(argv[argc - 3]) == "--no-cert-monitor";
    if (hasNoCertMonitor) {
        for (std::size_t i = 0; i < certMonitorValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i], certMonitorValues[i]) || certMonitorValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::array<std::uint64_t, 2> debugMonitorValues{};
    const bool hasNoDebugMonitor = argc >= 4 && std::string_view(argv[argc - 3]) == "--no-debug-monitor";
    if (hasNoDebugMonitor) {
        for (std::size_t i = 0; i < debugMonitorValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i], debugMonitorValues[i]) || debugMonitorValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::array<std::uint64_t, 2> commandLineValues{};
    const bool hasCommandLine = argc >= 4 && std::string_view(argv[argc - 3]) == "--command-line";
    if (hasCommandLine) {
        for (std::size_t i = 0; i < commandLineValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i], commandLineValues[i]) || commandLineValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::array<std::uint64_t, 2> uptimeValues{};
    const bool hasUptime = argc >= 4 && std::string_view(argv[argc - 3]) == "--uptime-bundle";
    if (hasUptime) {
        for (std::size_t i = 0; i < uptimeValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i], uptimeValues[i]) || uptimeValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::array<std::uint64_t, 3> videoGlobalValues{};
    const bool hasVideoGlobals = argc >= 5 && std::string_view(argv[argc - 4]) == "--video-globals";
    if (hasVideoGlobals) {
        for (std::size_t i = 0; i < videoGlobalValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 3 + i], videoGlobalValues[i]) || videoGlobalValues[i] > UINT32_MAX) return 2;
        argc -= 4;
    }
    std::array<std::uint64_t, 2> hardwareInfoValues{};
    const bool hasHardwareInfo = argc >= 4 && std::string_view(argv[argc - 3]) == "--hardware-info";
    if (hasHardwareInfo) {
        for (std::size_t i = 0; i < hardwareInfoValues.size(); ++i)
            if (!parseUnsigned(argv[argc - 2 + i], hardwareInfoValues[i]) || hardwareInfoValues[i] > UINT32_MAX) return 2;
        argc -= 3;
    }
    std::vector<std::uint32_t> unboundDataSlots;
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--unbound-data-imports") {
        std::string_view remaining(argv[argc - 1]);
        do {
            const auto split = remaining.find(',');
            const auto token = remaining.substr(0, split);
            std::uint64_t address = 0;
            if (unboundDataSlots.size() == 256 || token.empty() ||
                !parseUnsigned(std::string(token).c_str(), address) || address > UINT32_MAX) return 2;
            unboundDataSlots.push_back(static_cast<std::uint32_t>(address));
            if (split == std::string_view::npos) break;
            remaining.remove_prefix(split + 1);
        } while (true);
        argc -= 2;
    }
    const bool hasVirtualVideoMode = argc >= 3 && std::string_view(argv[argc - 2]) == "--virtual-video-mode";
    if (hasVirtualVideoMode) {
        if (std::string_view(argv[argc - 1]) != "720p60") {
            std::cerr << "trace setup failed: unsupported virtual video mode\n"; return 2;
        }
        argc -= 2;
    }
    const bool cooperative = argc >= 3
            && std::string_view(argv[argc - 2]) == "--cooperative-threads";
    if ((experimentalNativeAudioRenderer || nativeEventWakeBoost || nativeCriticalSectionWaits || nativeHighPriorityTiers) && !cooperative) {
        std::cerr << "trace setup failed: native audio/wait scheduling requires serialized cooperative execution\n";return 2;
    }
    std::uint64_t quantum = 32;
    if (cooperative) {
        if (!parseUnsigned(argv[argc - 1], quantum) || quantum == 0 || quantum > 1000000) {
            std::cerr << "trace setup failed: invalid cooperative quantum\n";
            return 2;
        }
        argc -= 2;
    }
    const bool hasNativeXam = argc >= 4 && std::string_view(argv[argc - 3]) == "--native-xam-module";
    std::uint64_t nativeXamAddress = 0, nativeXamVersion = 0;
    if (hasNativeXam) {
        if (!cooperative || !parseUnsigned(argv[argc - 2], nativeXamAddress)
                || !parseUnsigned(argv[argc - 1], nativeXamVersion) || nativeXamVersion >> 28 != 2
                || nativeXamAddress == 0 || nativeXamAddress > UINT32_MAX || (nativeXamAddress & 3U) != 0
                || !fitsGuestRange(nativeXamAddress, 0x64)) {
            std::cerr << "trace setup failed: invalid native XAM module range\n";
            return 2;
        }
        argc -= 3;
    }
    std::unique_ptr<recomp::bootstrap::GuestNativeReadOnlyDirectory> nativeGameCache;
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--native-game-cache-directory") {
        nativeGameCache = std::make_unique<recomp::bootstrap::GuestNativeReadOnlyDirectory>("gamecache:", argv[argc - 1]);
        if (!nativeGameCache->ready()) { std::cerr << "trace setup failed: native game-cache directory is unavailable\n"; return 2; }
        argc -= 2;
    }
    std::unique_ptr<recomp::bootstrap::GuestNativeReadOnlyDirectory> nativeUpdate;
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--native-update-directory") {
        nativeUpdate = std::make_unique<recomp::bootstrap::GuestNativeReadOnlyDirectory>("update:", argv[argc - 1]);
        if (!nativeUpdate->ready()) { std::cerr << "trace setup failed: native update directory is unavailable\n"; return 2; }
        argc -= 2;
    }
    std::unique_ptr<recomp::bootstrap::GuestNativeReadOnlyDirectory> nativeCache;
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--native-cache-directory") {
        nativeCache = std::make_unique<recomp::bootstrap::GuestNativeReadOnlyDirectory>("cache:", argv[argc - 1]);
        if (!nativeCache->ready()) { std::cerr << "trace setup failed: native cache directory is unavailable\n"; return 2; }
        argc -= 2;
    }
    std::vector<std::pair<std::uint32_t, std::filesystem::path>> fileRequests;
    while (argc >= 4 && std::string_view(argv[argc - 3]) == "--read-only-file") {
        std::uint64_t index = 0;
        if (fileRequests.size() == 16 || !parseUnsigned(argv[argc - 2], index) || index == 0 || index > 16384) {
            std::cerr << "trace setup failed: invalid explicit content binding\n"; return 2;
        }
        fileRequests.emplace_back(static_cast<std::uint32_t>(index), argv[argc - 1]);
        argc -= 3;
    }
    const bool hasFileMetadata = argc >= 3 && std::string_view(argv[argc - 2]) == "--file-metadata-catalog";
    recomp::bootstrap::GuestFileMetadataCatalog fileMetadata;
    if (hasFileMetadata) {
        const std::filesystem::path path(argv[argc - 1]);
        ScopedFile input(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat info {};
        if (!path.is_absolute() || input.get() < 0 || fstat(input.get(), &info) != 0
                || !S_ISREG(info.st_mode) || info.st_size < 16
                || static_cast<std::uint64_t>(info.st_size) > fileMetadata.MaximumBytes) {
            std::cerr << "trace setup failed: file metadata catalog is not bounded\n";
            return 2;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto count = read(input.get(), bytes.data() + done, bytes.size() - done);
            if (count <= 0) { std::cerr << "trace setup failed: file metadata read failed\n"; return 2; }
            done += static_cast<std::size_t>(count);
        }
        if (!fileMetadata.load(bytes)) { std::cerr << "trace setup failed: invalid file metadata catalog\n"; return 2; }
        argc -= 2;
    }
    struct InstalledFileRequest { std::uint32_t package, record; std::filesystem::path path; };
    std::vector<InstalledFileRequest> installedFileRequests;
    while (argc >= 5 && std::string_view(argv[argc - 4]) == "--installed-file") {
        std::uint64_t package = 0, record = 0;
        if (installedFileRequests.size() == 32 || !parseUnsigned(argv[argc - 3], package) || !parseUnsigned(argv[argc - 2], record)
                || package == 0 || package > 16 || record == 0 || record > 256) {
            std::cerr << "trace setup failed: invalid installed file request\n"; return 2;
        }
        installedFileRequests.push_back({static_cast<std::uint32_t>(package), static_cast<std::uint32_t>(record), argv[argc - 1]});
        argc -= 4;
    }
    struct InstalledPackage { recomp::bootstrap::GuestFileMetadataCatalog metadata; recomp::bootstrap::GuestImportRegistry::ContentFiles files; };
    std::map<std::uint32_t, InstalledPackage> installedPackages;
    while (argc >= 4 && std::string_view(argv[argc - 3]) == "--installed-package") {
        std::uint64_t index = 0;
        const std::filesystem::path path(argv[argc - 1]);
        if (!parseUnsigned(argv[argc - 2], index) || index == 0 || index > 16 || installedPackages.count(index) || !path.is_absolute()) {
            std::cerr << "trace setup failed: invalid installed package request\n"; return 2;
        }
        ScopedFile input(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat info {};
        if (input.get() < 0 || fstat(input.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 16
                || static_cast<std::uint64_t>(info.st_size) > recomp::bootstrap::GuestFileMetadataCatalog::MaximumBytes) {
            std::cerr << "trace setup failed: installed metadata is not bounded\n"; return 2;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto count = read(input.get(), bytes.data() + done, bytes.size() - done);
            if (count <= 0) { std::cerr << "trace setup failed: installed metadata read failed\n"; return 2; }
            done += static_cast<std::size_t>(count);
        }
        InstalledPackage package;
        if (!package.metadata.load(bytes) || package.metadata.size() > 256 || package.metadata.pathAt(1) != std::optional<std::string>{"content:\\"}) {
            std::cerr << "trace setup failed: installed package metadata rejected\n"; return 2;
        }
        installedPackages.emplace(static_cast<std::uint32_t>(index), std::move(package));
        argc -= 3;
    }
    for (const auto& request : installedFileRequests) {
        auto found = installedPackages.find(request.package);
        if (found == installedPackages.end() || !request.path.is_absolute()) {
            std::cerr << "trace setup failed: installed file lacks package metadata\n"; return 2;
        }
        auto& package = found->second;
        const auto path = package.metadata.pathAt(request.record);
        if (!path || package.files.count(*path)) { std::cerr << "trace setup failed: duplicate/invalid installed record\n"; return 2; }
        const auto entry = package.metadata.query(*path);
        ScopedFile source(open(request.path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat info {};
        if (entry.state != recomp::bootstrap::GuestFileQueryState::Found || entry.metadata.attributes != 1
                || entry.metadata.values[5] > (8ULL << 30) || source.get() < 0 || fstat(source.get(), &info) != 0
                || !S_ISREG(info.st_mode) || info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) != entry.metadata.values[5]) {
            std::cerr << "trace setup failed: installed content size/type mismatch\n"; return 2;
        }
        auto file = std::make_shared<const NativeReadOnlyFile>(source.get(), entry.metadata.values[5]);
        (void)source.release();
        package.files.emplace(*path, std::move(file));
    }
    const bool hasInstalledContent = argc >= 3 && std::string_view(argv[argc - 2]) == "--installed-content-catalog";
    if (!hasInstalledContent && !installedPackages.empty()) { std::cerr << "trace setup failed: installed catalog missing\n"; return 2; }
    recomp::bootstrap::GuestContentCatalog installedContent;
    if (hasInstalledContent) {
        const std::filesystem::path path(argv[argc - 1]);
        ScopedFile input(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat info {};
        if (!path.is_absolute() || input.get() < 0 || fstat(input.get(), &info) != 0
                || !S_ISREG(info.st_mode) || info.st_size < 16
                || static_cast<std::uint64_t>(info.st_size) > installedContent.MaximumBytes) {
            std::cerr << "trace setup failed: installed-content catalog is not bounded\n"; return 2;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto count = read(input.get(), bytes.data() + done, bytes.size() - done);
            if (count <= 0) { std::cerr << "trace setup failed: content catalog read failed\n"; return 2; }
            done += static_cast<std::size_t>(count);
        }
        if (!installedContent.load(bytes)) { std::cerr << "trace setup failed: invalid content catalog\n"; return 2; }
        argc -= 2;
    }
    std::map<std::string, std::shared_ptr<const NativeReadOnlyFile>> fileSources;
    for (const auto& request : fileRequests) {
        const auto guestPath = fileMetadata.pathAt(request.first);
        if (!guestPath || !request.second.is_absolute() || fileSources.count(*guestPath)) {
            std::cerr << "trace setup failed: content record is missing, duplicate or unmounted\n"; return 2;
        }
        const auto metadata = fileMetadata.query(*guestPath);
        const auto descriptor = open(request.second.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info {};
        if (metadata.state != recomp::bootstrap::GuestFileQueryState::Found || metadata.metadata.attributes != 1
                || descriptor < 0 || fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0
                || static_cast<std::uint64_t>(info.st_size) != metadata.metadata.values[5]
                || metadata.metadata.values[5] > (8ULL << 30)) {
            if (descriptor >= 0) { close(descriptor); }
            std::cerr << "trace setup failed: explicit content is not a matching regular file\n"; return 2;
        }
        ScopedFile ownership(descriptor);
        auto source = std::make_shared<const NativeReadOnlyFile>(descriptor, metadata.metadata.values[5]);
        (void)ownership.release();
        fileSources.emplace(*guestPath, std::move(source));
    }
    // argv[1] is the one explicit decoded-image path. All numeric arguments
    // are validated by the Python driver and passed in decimal form:
    // image base/size, heap base/size, stack base/size, budget, label limit,
    // and the separately validated mapping count. An optional physical arena
    // contributes guest base/size and the physical address at that base.
    std::array<std::uint64_t, 9> values{};
    std::array<std::uint64_t, 3> physicalValues{};
    std::array<std::uint64_t, 8> titleValues{};
    std::array<std::uint64_t, 7> threadValues{};
    std::array<std::uint64_t, 2> threadPoolValues{};
    std::array<std::uint64_t, 2> threadTypeValues{};
    bool hasPhysicalArena = false;
    bool hasTitleEnvironment = false;
    bool hasThreadEnvironment = false;
    bool hasThreadPool = false;
    bool hasThreadType = false;
    std::size_t rawXexArgumentIndex = 0;
    if (!parseArguments(
                argc,
                argv,
                values,
                hasPhysicalArena,
                physicalValues,
                hasTitleEnvironment,
                titleValues,
                rawXexArgumentIndex,
                hasThreadEnvironment,
                threadValues,
                hasThreadPool,
                threadPoolValues,
                hasThreadType,
                threadTypeValues)
            || (cooperative && (!hasThreadPool || !hasThreadType))
            || values[0] > UINT32_MAX
            || !fitsGuestRange(values[0], values[1])
            || values[2] > UINT32_MAX
            || values[3] > UINT32_MAX
            || values[4] > UINT32_MAX
            || values[5] > UINT32_MAX
            || values[6] == 0
            || values[7] > values[6]
            || values[7] > std::numeric_limits<std::size_t>::max()
            || values[8] > std::numeric_limits<std::size_t>::max()
            || values[2] == 0
            || values[5] == 0
            || (values[2] % PageSize) != 0
            || (values[3] % PageSize) != 0
            || (values[4] % PageSize) != 0
            || (values[5] % PageSize) != 0
            || values[4] + values[5] > UINT32_MAX
            || ((values[4] + values[5]) & 15U) != 0) {
        std::cerr << "trace setup failed: invalid internal invocation\n";
        return 2;
    }
    const auto imageBase = static_cast<std::uint32_t>(values[0]);
    const auto imageSize = values[1];
    const auto heapBase = static_cast<std::uint32_t>(values[2]);
    const auto heapSize = static_cast<std::uint32_t>(values[3]);
    const auto stackBase = static_cast<std::uint32_t>(values[4]);
    const auto stackSize = static_cast<std::uint32_t>(values[5]);
    const auto functionEntryBudget = values[6];
    const auto functionLabelLimit = static_cast<std::size_t>(values[7]);
    const auto expectedMappingCount = static_cast<std::size_t>(values[8]);

    if (hasPhysicalArena) {
        const NumericRange physicalArena{physicalValues[0], physicalValues[1]};
        const NumericRange physicalAddressRange{physicalValues[2], physicalValues[1]};
        const NumericRange image{imageBase, imageSize};
        const NumericRange heap{heapBase, heapSize};
        const NumericRange stack{stackBase, stackSize};
        if (physicalValues[0] == 0
                || physicalValues[1] == 0
                || physicalValues[0] > UINT32_MAX
                || physicalValues[1] > UINT32_MAX
                || physicalValues[2] > UINT32_MAX
                || (physicalValues[0] % PageSize) != 0
                || (physicalValues[1] % PageSize) != 0
                || (physicalValues[2] % PageSize) != 0
                || !fitsGuestRange(physicalArena.base, physicalArena.size)
                || !fitsGuestRange(
                        physicalAddressRange.base,
                        physicalAddressRange.size)
                || rangesOverlap(physicalArena, image)
                || rangesOverlap(physicalArena, heap)
                || rangesOverlap(physicalArena, stack)) {
            std::cerr << "trace setup failed: invalid physical-memory invocation\n";
            return 2;
        }
    }

    std::size_t validatedMappingCount = 0;
    if (!recomp::bootstrap::initializeGuestIndirectCallResolver(
                expectedMappingCount, validatedMappingCount)) {
        std::cerr << "trace setup failed: generated function mapping rejected\n";
        return 2;
    }

    if (hasTitleEnvironment
            && (titleValues[1] > UINT32_MAX
                || titleValues[2] > UINT32_MAX
                || titleValues[4] > UINT32_MAX
                || titleValues[5] > UINT32_MAX
                || titleValues[6] > UINT32_MAX
                || titleValues[7] > UINT32_MAX
                || !titleLayoutIsValid(values, titleValues))) {
        std::cerr << "trace setup failed: invalid title-environment invocation\n";
        return 2;
    }
    if (hasPhysicalArena && hasTitleEnvironment) {
        const NumericRange physicalArena{physicalValues[0], physicalValues[1]};
        const std::array titleRanges{
                NumericRange{values[0], titleValues[1]},
                NumericRange{titleValues[2], titleValues[3]},
                NumericRange{titleValues[5], 4},
                NumericRange{titleValues[6], ModuleStructureSize},
        };
        if (std::any_of(
                    titleRanges.begin(),
                    titleRanges.end(),
                    [&](const NumericRange& range) {
                        return rangesOverlap(physicalArena, range);
                    })) {
            std::cerr << "trace setup failed: physical-memory/title overlap\n";
            return 2;
        }
    }
    if (hasThreadEnvironment) {
        using ThreadEnvironment = recomp::bootstrap::GuestThreadEnvironment;
        const std::array threadRanges{
                NumericRange{threadValues[0], ThreadEnvironment::PcrSize},
                NumericRange{threadValues[1], ThreadEnvironment::ThreadSize},
                NumericRange{threadValues[2], threadValues[5] + threadValues[6] * 4},
        };
        const std::array otherRanges{
                NumericRange{values[0], titleValues[1]},
                NumericRange{heapBase, heapSize},
                NumericRange{stackBase, stackSize},
                NumericRange{titleValues[2], titleValues[3]},
                NumericRange{titleValues[5], 4},
                NumericRange{titleValues[6], ModuleStructureSize},
        };
        for (const auto& threadRange : threadRanges) {
            if (!fitsGuestRange(threadRange.base, threadRange.size)
                    || std::any_of(
                            otherRanges.begin(), otherRanges.end(),
                            [&](const NumericRange& range) {
                                return rangesOverlap(threadRange, range);
                            })
                    || (hasPhysicalArena && rangesOverlap(
                            threadRange,
                            NumericRange{physicalValues[0], physicalValues[1]}))) {
                std::cerr << "trace setup failed: thread scratch range rejected\n";
                return 2;
            }
        }
    }

    recomp::bootstrap::GuestAddressSpace addressSpace;
    if (hasThreadType) {
        const NumericRange identity{threadTypeValues[1], 4};
        const std::array otherRanges{
                NumericRange{imageBase, titleValues[1]}, NumericRange{heapBase, heapSize},
                NumericRange{stackBase, stackSize}, NumericRange{titleValues[2], titleValues[3]},
                NumericRange{titleValues[5], 4}, NumericRange{titleValues[6], ModuleStructureSize},
                NumericRange{threadValues[0], recomp::bootstrap::GuestThreadEnvironment::PcrSize},
                NumericRange{threadValues[1], recomp::bootstrap::GuestThreadEnvironment::ThreadSize},
                NumericRange{threadValues[2], threadValues[5] + threadValues[6] * 4},
        };
        if (identity.base == 0 || identity.base % 4 != 0 || !fitsGuestRange(identity.base, 4)
                || std::any_of(otherRanges.begin(), otherRanges.end(),
                        [&](const auto& range) { return rangesOverlap(identity, range); })
                || (hasThreadPool && rangesOverlap(identity, {threadPoolValues[0], threadPoolValues[1]}))
                || (hasPhysicalArena && rangesOverlap(identity, {physicalValues[0], physicalValues[1]}))) {
            std::cerr << "trace setup failed: thread type identity range rejected\n";
            return 2;
        }
    }
    if (hasNativeXam) {
        const NumericRange xam{nativeXamAddress, 0x64};
        const std::array others{
                NumericRange{imageBase, titleValues[1]}, NumericRange{heapBase, heapSize}, NumericRange{stackBase, stackSize},
                NumericRange{titleValues[2], titleValues[3]}, NumericRange{titleValues[5], 4}, NumericRange{titleValues[6], ModuleStructureSize},
                NumericRange{threadValues[0], recomp::bootstrap::GuestThreadEnvironment::PcrSize},
                NumericRange{threadValues[1], recomp::bootstrap::GuestThreadEnvironment::ThreadSize},
                NumericRange{threadValues[2], threadValues[5] + threadValues[6] * 4},
                NumericRange{threadPoolValues[0], threadPoolValues[1]}, NumericRange{threadTypeValues[1], 4},
                NumericRange{physicalValues[0], hasPhysicalArena ? physicalValues[1] : 0},
        };
        if (std::any_of(others.begin(), others.end(), [&](const auto& range) { return rangesOverlap(xam, range); })) {
            std::cerr << "trace setup failed: native XAM module overlaps another range\n";
            return 2;
        }
    }
    std::vector<NumericRange> extraDataRanges;
    if (hasKernelVersion) extraDataRanges.push_back({kernelVersionValues[1], 8});
    if (hasHardwareInfo) extraDataRanges.push_back({hardwareInfoValues[1], 16});
    if (hasVideoGlobals) extraDataRanges.push_back({videoGlobalValues[2], 8});
    if (hasUptime) extraDataRanges.push_back({uptimeValues[1], 24});
    if (hasCommandLine) extraDataRanges.push_back({commandLineValues[1], 64});
    if (hasNoDebugMonitor) extraDataRanges.push_back({debugMonitorValues[1], 4});
    if (hasNoCertMonitor) extraDataRanges.push_back({certMonitorValues[1], 4});
    if (hasHsioLock) extraDataRanges.push_back({hsioLockValues[1],28});
    if(nativeScalerTiming)extraDataRanges.push_back({scalerTimingValues[1],4});
    const std::vector<NumericRange> otherDataRanges{
        {imageBase, hasTitleEnvironment ? titleValues[1] : imageSize}, {heapBase, heapSize}, {stackBase, stackSize},
        {titleValues[2], hasTitleEnvironment ? titleValues[3] : 0}, {titleValues[5], hasTitleEnvironment ? 4U : 0U},
        {titleValues[6], hasTitleEnvironment ? ModuleStructureSize : 0U},
        {threadValues[0], hasThreadEnvironment ? recomp::bootstrap::GuestThreadEnvironment::PcrSize : 0U},
        {threadValues[1], hasThreadEnvironment ? recomp::bootstrap::GuestThreadEnvironment::ThreadSize : 0U},
        {threadValues[2], hasThreadEnvironment ? threadValues[5] + threadValues[6] * 4 : 0},
        {threadPoolValues[0], hasThreadPool ? threadPoolValues[1] : 0}, {threadTypeValues[1], hasThreadType ? 4U : 0U},
        {nativeXamAddress, hasNativeXam ? 0x64U : 0U}, {physicalValues[0], hasPhysicalArena ? physicalValues[1] : 0}};
    if ((!extraDataRanges.empty() && !hasTitleEnvironment) || (hasVideoGlobals && imageSize > UINT32_MAX) ||
        (hasHardwareInfo && (!cooperative || !hasInstalledContent))) return 2;
    for (std::size_t i = 0; i < extraDataRanges.size(); ++i) {
        const auto range = extraDataRanges[i];
        if (!range.base || range.base % 4 || !fitsGuestRange(range.base, range.size) ||
            std::any_of(otherDataRanges.begin(), otherDataRanges.end(), [&](auto other) { return other.size && rangesOverlap(range, other); })) return 2;
        for (std::size_t j = 0; j < i; ++j) if (rangesOverlap(range, extraDataRanges[j])) return 2;
    }
    const bool physicalAcAlias=hasPhysicalArena && physicalValues[0]==0xA0000000 &&
        physicalValues[1]==0x20000000 && physicalValues[2]==0;
    if(physicalAcAlias) {
        const NumericRange alias{0xC0000000,0x20000000};
        const auto overlapsAlias=[&](auto range){return range.size && rangesOverlap(range,alias);};
        if(std::any_of(otherDataRanges.begin(),otherDataRanges.end(),overlapsAlias) ||
           std::any_of(extraDataRanges.begin(),extraDataRanges.end(),overlapsAlias)) {
            std::cerr<<"trace setup failed: physical alias overlaps configured guest data\n";
            return 2;
        }
    }
    // Configure coherent physical views before loading any guest bytes. Keep
    // the existing flat layout for other explicitly configured test profiles.
    if (!(physicalAcAlias?addressSpace.reserveWithPhysicalAcAlias():addressSpace.reserve())) {
        std::cerr << "trace setup failed: guest address reservation unavailable\n";
        return 2;
    }
    if(physicalAcAlias)std::cout<<"GUEST_PHYSICAL_AC_ALIAS_V1 shared_backing=1 bytes=536870912 e_view_implemented=0\n";

    recomp::bootstrap::GuestImageLoader loader;
    if (!loader.loadExplicitDecodedImage(
                addressSpace,
                argv[1],
                imageBase,
                imageSize)) {
        std::cerr << "trace setup failed: decoded image rejected\n";
        return 2;
    }

    recomp::bootstrap::GuestTitleEnvironment titleEnvironment;
    if (hasTitleEnvironment) {
        const auto headerBase = static_cast<std::uint32_t>(titleValues[2]);
        if (!stageRawXexHeader(
                    addressSpace,
                    argv[rawXexArgumentIndex],
                    titleValues[0],
                    headerBase,
                    titleValues[3])) {
            std::cerr << "trace setup failed: raw XEX header staging rejected\n";
            return 2;
        }
        const recomp::bootstrap::GuestTitleEnvironmentLayout titleLayout{
                static_cast<std::uint32_t>(titleValues[4]),
                static_cast<std::uint32_t>(titleValues[5]),
                static_cast<std::uint32_t>(titleValues[6]),
                headerBase,
                imageBase,
                static_cast<std::uint32_t>(titleValues[1]),
                static_cast<std::uint32_t>(titleValues[7]),
                hasThreadType ? static_cast<std::uint32_t>(threadTypeValues[0]) : 0U,
                hasThreadType ? static_cast<std::uint32_t>(threadTypeValues[1]) : 0U,
        };
        if (!titleEnvironment.initialize(addressSpace, titleLayout)) {
            std::cerr << "trace setup failed: title environment rejected\n";
            return 2;
        }
    }

    recomp::bootstrap::GuestThreadEnvironment threadEnvironment;
    if (hasThreadEnvironment) {
        if (!threadInputsMatchHeader(
                    addressSpace,
                    static_cast<std::uint32_t>(titleValues[2]),
                    titleValues[3],
                    threadValues)) {
            std::cerr << "trace setup failed: staged TLS metadata mismatch\n";
            return 2;
        }
        recomp::bootstrap::GuestThreadEnvironmentLayout threadLayout{};
        threadLayout.pcrAddress = static_cast<std::uint32_t>(threadValues[0]);
        threadLayout.threadAddress = static_cast<std::uint32_t>(threadValues[1]);
        threadLayout.tlsAddress = static_cast<std::uint32_t>(threadValues[2]);
        threadLayout.imageBase = imageBase;
        threadLayout.imageSize = static_cast<std::uint32_t>(imageSize);
        threadLayout.stackBase = stackBase;
        threadLayout.stackSize = stackSize;
        threadLayout.tlsRawDataAddress = static_cast<std::uint32_t>(threadValues[3]);
        threadLayout.tlsRawDataSize = static_cast<std::uint32_t>(threadValues[4]);
        threadLayout.tlsDataSize = static_cast<std::uint32_t>(threadValues[5]);
        threadLayout.tlsSlotCount = static_cast<std::uint32_t>(threadValues[6]);
        threadLayout.threadId = 1;
        if (!threadEnvironment.initialize(addressSpace, threadLayout)) {
            std::cerr << "trace setup failed: thread environment rejected\n";
            return 2;
        }
    }

    std::uint64_t debugMessageCount = 0;
    std::uint64_t debugMessageBytes = 0;
    recomp::bootstrap::GuestImportRegistry registry;
    if (!registry.initialize(
                addressSpace,
                heapBase,
                heapSize,
                [&](std::string_view message) {
                    ++debugMessageCount;
                    addSaturating(debugMessageBytes, message.size());
                    // Guest text is deliberately not printed or retained.
                })) {
        std::cerr << "trace setup failed: import registry rejected heap range\n";
        return 2;
    }
    if (hasVirtualVideoMode && !registry.bindVirtualVideoMode720p60()) {
        std::cerr << "trace setup failed: virtual video binding rejected\n"; return 2;
    }
    if (imagePageProtections && (!hasTitleEnvironment || !registry.bindImagePageProtections(
            static_cast<std::uint32_t>(titleValues[2]), static_cast<std::uint32_t>(titleValues[3]),
            imageBase, static_cast<std::uint32_t>(titleValues[1])))) {
        std::cerr << "trace setup failed: XEX image page metadata rejected\n";
        return 2;
    }
    if (!registry.bindDpcInitialization([](auto address) {
        return recomp::bootstrap::hasExactGuestFunction(address);
    })) { std::cerr << "trace setup failed: DPC initializer binding rejected\n"; return 2; }
    // The generated runtime hook reads the shared fixed-rate CPU timebase.
    // UTC system time is a separate domain; neither validates GPU dword 044B.
    using FileTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
    const auto utc = recomp::bootstrap::fileTimeFromUnixTicks(
            std::chrono::duration_cast<FileTicks>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto monotonicStart = std::chrono::steady_clock::now();
    if (!utc || !registry.bindClockSources([utc, monotonicStart] {
                return recomp::bootstrap::advanceFileTime(*utc,
                        std::chrono::duration_cast<FileTicks>(std::chrono::steady_clock::now() - monotonicStart).count());
            }, recomp::bootstrap::GuestTimebaseFrequency)) {
        std::cerr << "trace setup failed: clock binding rejected\n";
        return 2;
    }
    const auto guestThreadToken = hasThreadEnvironment
            ? static_cast<std::uint32_t>(threadValues[1])
            : static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(stackBase) + stackSize - 16U);
    recomp::bootstrap::GuestImportCall clockProbe;
    if (!registry.dispatch("__imp__KeQueryPerformanceFrequency", clockProbe).handled()
            || clockProbe.returnValue != recomp::bootstrap::GuestTimebaseFrequency) {
        std::cerr << "trace setup failed: performance frequency disagrees with CPU timebase\n";
        return 2;
    }
    if (!registry.bindGuestThreadToken(guestThreadToken)) {
        std::cerr << "trace setup failed: explicit guest thread token rejected\n";
        return 2;
    }
    if (hasPhysicalArena
            && !registry.bindPhysicalMemoryArena(
                    static_cast<std::uint32_t>(physicalValues[0]),
                    static_cast<std::uint32_t>(physicalValues[1]),
                    static_cast<std::uint32_t>(physicalValues[2]))) {
        std::cerr << "trace setup failed: physical-memory arena binding rejected\n";
        return 2;
    }
#if defined(RECOMP_NATIVE_XMA)
    std::shared_ptr<recomp::audio::NativeXmaContextPool> nativeXma;
    struct XmaRegisterScope { ~XmaRegisterScope() { recomp::bootstrap::resetGuestXmaRegisters(); } } xmaRegisterScope;
    if (hasPhysicalArena) {
        recomp::bootstrap::GuestImportCall allocation;
        allocation.arguments={0,recomp::bootstrap::GuestXmaBackend::PoolBytes,0x204,0,UINT32_MAX,256,0,0};
        if (!registry.dispatch("__imp__MmAllocatePhysicalMemoryEx",allocation).handled() || !allocation.returnValue) return 2;
        const auto v=static_cast<std::uint32_t>(allocation.returnValue);
        recomp::bootstrap::GuestImportCall physical; physical.arguments[0]=v;
        if (!registry.dispatch("__imp__MmGetPhysicalAddress",physical).handled()) return 2;
        nativeXma=std::make_shared<recomp::audio::NativeXmaContextPool>(v,static_cast<std::uint32_t>(physical.returnValue),
            addressSpace.translate(v),[&](auto p,auto bytes,auto writable) { return registry.mapNativeAudioPhysical(p,bytes,writable); });
        if (!nativeXma->ready() || !registry.bindNativeXma(nativeXma) ||
            !recomp::bootstrap::bindGuestXmaRegisters([&](auto address,bool write,std::uint32_t& value) {
                const auto accepted=nativeXma->access(address,write,value);
                if (!accepted) {
                    char words[160]{};
                    for (unsigned i=0;i<16;++i) std::snprintf(words+i*9,10,"%08x,",nativeXma->failureContext()[i]);
                    __android_log_print(ANDROID_LOG_INFO,"RecompNativeXma","STOP address=%08x write=%u value=%08x reason=%s context=%s",
                        address,unsigned(write),value,nativeXma->failure().c_str(),words);
                    std::cerr<<"native XMA stopped: "<<nativeXma->failure()<<" context=";
                    for (auto word:nativeXma->failureContext()) std::cerr<<std::hex<<word<<',';
                    std::cerr<<std::dec<<"\n";
                }
                return accepted;
            })) return 2;
    }
#endif
    if (hasTitleEnvironment
            && !registry.bindExecutableXexHeader(
                    static_cast<std::uint32_t>(titleValues[2]))) {
        std::cerr << "trace setup failed: executable XEX header binding rejected\n";
        return 2;
    }
    if (hasThreadEnvironment
            && !registry.bindGuestThreadTlsStorage(
                    guestThreadToken,
                    threadEnvironment.dynamicTlsAddress(),
                    static_cast<std::uint32_t>(threadValues[6]))) {
        std::cerr << "trace setup failed: dynamic TLS binding rejected\n";
        return 2;
    }

    if (hasThreadPool) {
        const auto stackDefault = recomp::bootstrap::GuestXexHeader::lookup(
                addressSpace, static_cast<std::uint32_t>(titleValues[2]), 0x00020200U);
        if (!stackDefault.found()
                || stackDefault.kind != recomp::bootstrap::GuestXexHeaderValueKind::Immediate) {
            std::cerr << "trace setup failed: title default stack metadata is unavailable\n";
            return 2;
        }
        recomp::bootstrap::GuestThreadStorageConfig config{};
        config.arena = {static_cast<std::uint32_t>(threadPoolValues[0]),
                static_cast<std::uint32_t>(threadPoolValues[1])};
        config.image = {imageBase, static_cast<std::uint32_t>(titleValues[1])};
        config.code = {PPC_CODE_BASE, PPC_CODE_SIZE};
        config.tlsRawDataAddress = static_cast<std::uint32_t>(threadValues[3]);
        config.tlsRawDataSize = static_cast<std::uint32_t>(threadValues[4]);
        config.tlsDataSize = static_cast<std::uint32_t>(threadValues[5]);
        config.tlsSlotCount = static_cast<std::uint32_t>(threadValues[6]);
        config.defaultStackSize = stackDefault.value;
        const std::vector<recomp::bootstrap::GuestThreadMemoryRange> excluded{
                {stackBase, stackSize},
                {static_cast<std::uint32_t>(titleValues[2]), static_cast<std::uint32_t>(titleValues[3])},
                {static_cast<std::uint32_t>(titleValues[5]), 4},
                {static_cast<std::uint32_t>(titleValues[6]), ModuleStructureSize},
                {static_cast<std::uint32_t>(threadValues[0]), recomp::bootstrap::GuestThreadEnvironment::PcrSize},
                {static_cast<std::uint32_t>(threadValues[1]), recomp::bootstrap::GuestThreadEnvironment::ThreadSize},
        };
        if (!registry.bindThreadStorageArena(config, excluded,
                    recomp::bootstrap::hasExactGuestFunction)) {
            std::cerr << "trace setup failed: created-thread arena rejected\n";
            return 2;
        }
    }

    if (hasThreadType && (!registry.bindThreadObjectType(static_cast<std::uint32_t>(threadTypeValues[1]))
            || !registry.bindInitialThreadObject(guestThreadToken, 1))) {
        std::cerr << "trace setup failed: thread object binding rejected\n";
        return 2;
    }

    if (hasNativeXam && !registry.bindNativeXamModule(static_cast<std::uint32_t>(nativeXamAddress),
            static_cast<std::uint32_t>(titleValues[6]), {{stackBase, stackSize},
                {static_cast<std::uint32_t>(titleValues[2]), static_cast<std::uint32_t>(titleValues[3])},
                {static_cast<std::uint32_t>(titleValues[5]), 4},
                {static_cast<std::uint32_t>(threadValues[0]), recomp::bootstrap::GuestThreadEnvironment::PcrSize},
                {static_cast<std::uint32_t>(threadValues[1]), recomp::bootstrap::GuestThreadEnvironment::ThreadSize},
                {static_cast<std::uint32_t>(threadValues[2]), threadEnvironment.tlsStorageSize()}},
                static_cast<std::uint32_t>(nativeXamVersion))) {
        std::cerr << "trace setup failed: native XAM module binding rejected\n";
        return 2;
    }
    if (hasInstalledContent) {
        if (!hasTitleEnvironment) { std::cerr << "trace setup failed: content requires title metadata\n"; return 2; }
        const auto execution = recomp::bootstrap::GuestXexHeader::lookup(
                addressSpace, static_cast<std::uint32_t>(titleValues[2]), 0x00040006);
        if (!execution.found() || execution.kind != recomp::bootstrap::GuestXexHeaderValueKind::DataPointer
                || execution.value < titleValues[2] || std::uint64_t{execution.value} + 24 > titleValues[2] + titleValues[3]) {
            std::cerr << "trace setup failed: content title identity unavailable\n"; return 2;
        }
        const auto* p = addressSpace.translate(execution.value + 12);
        const auto titleId = (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
        if (!registry.bindInstalledContent(installedContent, titleId)) {
            std::cerr << "trace setup failed: content title binding rejected\n"; return 2;
        }
        for (const auto& [index, package] : installedPackages) {
            if (!registry.bindContentPackage(index, package.metadata, package.files)) {
                std::cerr << "trace setup failed: package requires complete matching preopened sources\n"; return 2;
            }
        }
    }
    PPCContext context{};
    recomp::bootstrap::GuestDataVariableBinding hardwareInfo;
    recomp::bootstrap::GuestDataVariableBinding kernelVersionBinding;
    recomp::bootstrap::GuestDataVariableBinding uptimeBinding;
    recomp::bootstrap::GuestDataVariableBinding commandLineBinding;
    recomp::bootstrap::GuestDataVariableBinding debugMonitorBinding;
    recomp::bootstrap::GuestDataVariableBinding certMonitorBinding;
    recomp::bootstrap::GuestDataVariableBinding hsioLockBinding;
    recomp::bootstrap::GuestDataVariableBinding scalerClockBinding;
    recomp::bootstrap::GuestVideoDeviceGlobals videoGlobals;
    std::vector<recomp::bootstrap::GuestDataVariableBinding::Range> hardwareExcluded;
    std::vector<recomp::bootstrap::GuestVideoDeviceGlobals::Range> videoExcluded;
    for (auto range : otherDataRanges) if (range.size) {
        hardwareExcluded.push_back({static_cast<std::uint32_t>(range.base), range.size});
        videoExcluded.push_back({static_cast<std::uint32_t>(range.base), range.size});
    }
    if (hasVideoGlobals) hardwareExcluded.push_back({static_cast<std::uint32_t>(videoGlobalValues[2]), 8});
    if (hasHardwareInfo) videoExcluded.push_back({static_cast<std::uint32_t>(hardwareInfoValues[1]), 16});
    if (hasHardwareInfo && !hardwareInfo.initialize(addressSpace, {imageBase, imageSize},
            static_cast<std::uint32_t>(hardwareInfoValues[0]), 0x156,
            static_cast<std::uint32_t>(hardwareInfoValues[1]),
            {0,0,0,0x20,6,0,0,0,0,0,0,0,0,0,0,0}, hardwareExcluded)) {
        std::cerr << "trace setup failed: virtual hardware-info binding rejected\n"; return 2;
    }
    if (hasVideoGlobals && !videoGlobals.initialize(addressSpace,
            {imageBase, static_cast<std::uint32_t>(imageSize), static_cast<std::uint32_t>(videoGlobalValues[0]),
             static_cast<std::uint32_t>(videoGlobalValues[1]), static_cast<std::uint32_t>(videoGlobalValues[2])}, videoExcluded)) {
        std::cerr << "trace setup failed: video pointer variables rejected\n"; return 2;
    }
    if (hasUptime) {
        // The full request was checked against every other range above. This
        // binding reserves 24 bytes but only the observed +16 word is readable.
        if (!uptimeBinding.initialize(addressSpace, {imageBase, imageSize},
                static_cast<std::uint32_t>(uptimeValues[0]), 0xAD, static_cast<std::uint32_t>(uptimeValues[1]),
                std::vector<std::uint8_t>(24), hardwareExcluded)) {
            std::cerr << "trace setup failed: uptime data binding rejected\n"; return 2;
        }
        const auto uptimeStart = std::chrono::steady_clock::now();
        if (!recomp::bootstrap::bindGuestScalarUptimeClock(static_cast<std::uint32_t>(uptimeValues[1]), [uptimeStart]() -> std::optional<std::uint64_t> {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - uptimeStart).count();
                return elapsed < 0 ? std::nullopt : std::optional<std::uint64_t>(static_cast<std::uint64_t>(elapsed));
            })) return 2;
    }
    if (hasCommandLine) {
        constexpr std::string_view name = "\"default.xex\"";
        std::vector<std::uint8_t> command(64, 0);
        std::copy(name.begin(), name.end(), command.begin());
        if (!commandLineBinding.initialize(addressSpace, {imageBase, imageSize},
                static_cast<std::uint32_t>(commandLineValues[0]), 0x1AE, static_cast<std::uint32_t>(commandLineValues[1]),
                command, hardwareExcluded)) {
            std::cerr << "trace setup failed: launch command line binding rejected\n"; return 2;
        }
    }
    // DATA import -> distinct pointer variable -> null: there is no attached
    // Xbox remote debugger in this native profile. Never replace the slot with
    // null directly, invent a monitor object, or install a callback trampoline.
    if (hasNoDebugMonitor && !debugMonitorBinding.initialize(addressSpace, {imageBase, imageSize},
            static_cast<std::uint32_t>(debugMonitorValues[0]), 0x59,
            static_cast<std::uint32_t>(debugMonitorValues[1]), {0, 0, 0, 0}, hardwareExcluded)) {
        std::cerr << "trace setup failed: no-debug-monitor data binding rejected\n"; return 2;
    }
    // The certification monitor is an optional developer callback service.
    // DATA slot -> a distinct four-byte pointer variable -> null represents
    // its absence. Do not map the descriptor to null or invent a callback.
    if (hasNoCertMonitor && !certMonitorBinding.initialize(addressSpace, {imageBase,imageSize},
            static_cast<std::uint32_t>(certMonitorValues[0]),0x266,
            static_cast<std::uint32_t>(certMonitorValues[1]),{0,0,0,0},hardwareExcluded)) {
        std::cerr << "trace setup failed: no-cert-monitor data binding rejected\n"; return 2;
    }
    // Unlike monitor pointers, this export IS the critical-section object.
    // Use the actual registry synchronization implementation, not zero storage
    // mistaken for a valid unlocked lock. No claim of hardware calibration.
    if (hasHsioLock) {
        if (!hsioLockBinding.initialize(addressSpace,{imageBase,imageSize},
                static_cast<std::uint32_t>(hsioLockValues[0]),0x1C1,
                static_cast<std::uint32_t>(hsioLockValues[1]),std::vector<std::uint8_t>(28),hardwareExcluded)) {
            std::cerr << "trace setup failed: HSIO lock data binding rejected\n"; return 2;
        }
        recomp::bootstrap::GuestImportCall init;
        init.arguments[0] = hsioLockValues[1]; init.arguments[1] = 10000;
        if (!registry.dispatch("__imp__RtlInitializeCriticalSectionAndSpinCount",init).handled() || init.returnValue != 0) {
            std::cerr << "trace setup failed: HSIO critical-section initialization rejected\n"; return 2;
        }
    }
    if(nativeScalerTiming){
        if(!hasVirtualVideoMode || std::count(unboundDataSlots.begin(),unboundDataSlots.end(),scalerTimingValues[0])!=1)return 2;
        const auto& payload=recomp::gpu::NativeScalerTiming::GpuClockBytes;
        if(!scalerClockBinding.initialize(addressSpace,{imageBase,imageSize},static_cast<std::uint32_t>(scalerTimingValues[0]),0x1C0,
            static_cast<std::uint32_t>(scalerTimingValues[1]),std::vector<std::uint8_t>(payload.begin(),payload.end()),hardwareExcluded))return 2;
        unboundDataSlots.erase(std::find(unboundDataSlots.begin(),unboundDataSlots.end(),scalerTimingValues[0]));
    }
    if (hasKernelVersion) {
        const auto slot = static_cast<std::uint32_t>(kernelVersionValues[0]);
        const auto payload = recomp::bootstrap::guestKernelVersionBytes(kernelVersionValues[2]);
        if (!hasNativeXam || !payload ||
            std::count(unboundDataSlots.begin(), unboundDataSlots.end(), slot) != 1 ||
            !kernelVersionBinding.initialize(addressSpace, {imageBase, imageSize}, slot,
                0x158, static_cast<std::uint32_t>(kernelVersionValues[1]),
                std::vector<std::uint8_t>(payload->begin(), payload->end()), hardwareExcluded)) {
            std::cerr << "trace setup failed: native kernel-version profile rejected\n";
            return 2;
        }
        unboundDataSlots.erase(std::find(unboundDataSlots.begin(), unboundDataSlots.end(), slot));
    }
    if (std::any_of(unboundDataSlots.begin(), unboundDataSlots.end(), [&](auto slot) {
            return !hasTitleEnvironment || slot < imageBase || std::uint64_t{slot} + 4 > imageBase + imageSize;
        }) || !recomp::bootstrap::bindGuestScalarDataImportGuard(unboundDataSlots)) {
        std::cerr << "trace setup failed: invalid unresolved data-import guard\n"; return 2;
    }
    if (!registry.bindTitleTerminationNotifications(static_cast<std::uint32_t>(imageBase),
            static_cast<std::uint32_t>(imageSize),
            [](auto callback) { return recomp::bootstrap::hasExactGuestFunction(callback); })) {
        std::cerr << "trace setup failed: title termination metadata binding rejected\n"; return 2;
    }
#if defined(__ANDROID__)
    auto nativeAudio = experimentalNativeAudio ? std::make_shared<recomp::audio::AAudioOutputStream>() : nullptr;
    if (experimentalNativeAudio && (!nativeAudio->open() || !registry.bindExperimentalAudioOutput(nativeAudio))) {
        std::cerr << "trace setup failed: native audio stream binding rejected\n"; return 2;
    }
    auto nativeAudioRenderer = experimentalNativeAudioRenderer
        ?std::make_shared<recomp::audio::NativeAudioRenderClient>(nativeAudio):nullptr;
    auto nativeGraphics = hasGraphics ? std::make_shared<recomp::gpu::VulkanEngine>() : nullptr;
    if (hasGraphics && !registry.bindGraphicsBackend(nativeGraphics,
            static_cast<std::uint32_t>(imageBase), static_cast<std::uint32_t>(imageSize),
            [](auto callback) { return recomp::bootstrap::hasExactGuestFunction(callback); })) {
        std::cerr << "trace setup failed: native graphics backend binding rejected\n"; return 2;
    }
#endif
    std::uint32_t lastFileMetadataIndex = 0;
    if ((hasFileMetadata || nativeCache || nativeUpdate || nativeGameCache) && !registry.bindFileMetadataQuery([&](auto path) {
        if (nativeGameCache && nativeGameCache->owns(path)) {
            lastFileMetadataIndex = 0;
            return nativeGameCache->query(path);
        }
        if (nativeUpdate && nativeUpdate->owns(path)) {
            lastFileMetadataIndex = 0;
            return nativeUpdate->query(path);
        }
        if (nativeCache && nativeCache->owns(path)) {
            lastFileMetadataIndex = 0;
            return nativeCache->query(path);
        }
        lastFileMetadataIndex = fileMetadata.recordIndex(path);
        return fileMetadata.query(path);
    })) {
        std::cerr << "trace setup failed: file metadata binding rejected\n";
        return 2;
    }
    if ((!fileSources.empty() || nativeCache || nativeUpdate || nativeGameCache) && !registry.bindReadOnlyFiles([&](auto path) -> std::shared_ptr<const recomp::bootstrap::GuestReadOnlyFile> {
        if (nativeGameCache && nativeGameCache->owns(path)) return nativeGameCache->open(path);
        if (nativeUpdate && nativeUpdate->owns(path)) return nativeUpdate->open(path);
        if (nativeCache && nativeCache->owns(path)) return nativeCache->open(path);
        const auto found = fileSources.find(std::string(path));
        return found == fileSources.end() ? nullptr : found->second;
    })) { std::cerr << "trace setup failed: read-only content binding rejected\n"; return 2; }
    context.r1.u64 = static_cast<std::uint64_t>(stackBase) + stackSize;
    if (hasThreadEnvironment) {
        context.r13.u64 = threadValues[0];
    }

    if (experimentalGpuClock) {
        const auto epoch = std::chrono::steady_clock::now();
        const auto elapsed=[epoch]() -> std::optional<std::uint64_t> {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - epoch).count();
            if (ns < 0) return std::nullopt;
            return static_cast<std::uint64_t>(ns);
        };
        if (!recomp::bootstrap::bindExperimentalGuestGpuClock([&,elapsed]() -> std::optional<std::uint64_t> {
            if (!registry.graphicsInitialized()) return std::nullopt;
            return elapsed();
        }))return 2;
#if defined(__ANDROID__)
        // Backend calls this under the registry mutex. Share the pure epoch,
        // not the MMIO readiness wrapper which would reenter that mutex.
        if(nativeSwapCommands && (!nativeGraphics || !nativeGraphics->bindNativeSwapCompletionClock(elapsed)))return 2;
#endif
    }
    const auto submitOwnedColor=[&](std::uint32_t word){
#if defined(__ANDROID__)
        if(!experimentalOwnedColor || !nativeGraphics || !inspectColorProgram)return false;
        const auto& state=nativeGraphics->stateBatchSnapshot();
        const recomp::gpu::DrawBinState bins{{{static_cast<std::uint32_t>(state.binMask),static_cast<std::uint32_t>(state.binMask>>32),
            static_cast<std::uint32_t>(state.binSelect),static_cast<std::uint32_t>(state.binSelect>>32)}},state.knownBinWords};
        if(registry.submitExperimentalOwnedColorBatch(*inspectColorProgram,bins,word) ||
            (livePostOutputColor && registry.submitExperimentalPostOutputColorBatch(*inspectColorProgram,bins,word)))return true;
        // Synchronous executor-token scope: every other guest writer is parked.
        // A successful PREFIX does not discharge the enclosing swap/IRQ tail.
        // Keep returning false so the ordinary scalar guard stops at that tail.
        if(liveDeferredColor)(void)registry.executeExperimentalDeferredColorPrefix(*inspectColorProgram,bins,word);
        return false;
#else
        (void)word;return false;
#endif
    };
    std::function<bool(std::uint32_t)> submitScalerInterrupt,submitPostOutputSwap;
    GraphicsSubmissionDiagnostic submissionDiagnostic;
    if (nativeInitialCoherency && (!cooperative || !registry.bindNativeInitialCoherencySubmission() ||
        (experimentalNativeInitialization && !registry.bindExperimentalNativeInitialization()) ||
        (nativeEmptyCacheCoherency && !registry.bindEmptyGuestCacheCoherency()) ||
        (nativeStateBatches && !registry.bindNativeStateBatches()) ||
        (experimentalIdleCachePair && !registry.bindExperimentalIdleCachePair()) ||
        (experimentalOwnedColor && !registry.bindExperimentalOwnedColorBatch()) ||
        (nativeNoSystemOverlay && !registry.bindNativeNoSystemOverlay()) ||
        (nativeSwapCommands && !registry.bindExperimentalNativeSwapCommands([&](const auto& image){
#if defined(__ANDROID__)
            if (nativeGraphics && nativeGraphics->nativeScalerConfigured()) {
                std::vector<std::uint8_t> pixels;
                if (!nativeGraphics->mapNativeScanout(image.rgba.data(),image.rgba.size(),image.request.width,image.request.height,pixels)) return false;
                return liveCallbacks->orderedRgba(liveCallbacks->user,pixels.data(),static_cast<std::uint32_t>(pixels.size()),
                    image.request.width,image.request.height)==1;
            }
#endif
            return liveCallbacks->orderedRgba(liveCallbacks->user,image.rgba.data(),
                static_cast<std::uint32_t>(image.rgba.size()),image.request.width,image.request.height)==1;
        })) ||
        (nativeScalerTiming && !registry.bindExperimentalNativeScalerTiming(static_cast<std::uint32_t>(scalerTimingValues[0]),
            static_cast<std::uint32_t>(scalerTimingValues[1]))) ||
        (nativeIdentityScaler && !registry.bindExperimentalNativeIdentityScaler()) ||
        (nativeIdentityScaler && (!hasVideoGlobals || !registry.bindNativeInterruptDeviceRoot(
            [&](std::uint32_t device) {return videoGlobals.matchesDevice(device);}))) ||
        !recomp::bootstrap::bindGuestGraphicsSubmission([&](auto word) {
            submissionDiagnostic.begin(word);
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE) && defined(__ANDROID__)
            if(const auto captured=submissionDiagnostic.attempt("captureRendererTileSubmission",
                    [&]{return captureRendererTileSubmission(word);})) {
                submissionDiagnostic.finish(*captured);return *captured;
            }
#endif
            // Ordinary paths remain synchronous without guest reentry. The
            // explicit scaler-interrupt path below hands off only to its fresh
            // owned CPU2 worker and directly back; every other writer is parked.
#define TRACE_SUBMISSION(method) submissionDiagnostic.attempt(#method,[&]{return registry.method(word);})
            const bool accepted=TRACE_SUBMISSION(submitNativeInitialCoherency) ||
                (experimentalNativeInitialization && TRACE_SUBMISSION(submitExperimentalNativeInitialization)) ||
                (nativeEmptyCacheCoherency && TRACE_SUBMISSION(submitEmptyGuestCacheCoherency)) ||
                (nativeStateBatches && TRACE_SUBMISSION(submitNativeStateBatch)) ||
                (nativeStateBatches && experimentalIdleCachePair && TRACE_SUBMISSION(submitNativeMaintenancePoints)) ||
                // Disabled per independent audit (audit notes): the
                // verifier accepts real guest-visible effects as side-effect-
                // free (scratch-register writeback, persistent register-state
                // changes with no draw, a malformed DRAW_INDX missing its
                // initiator token) and a prior candidate shipped with an
                // ABI-inconsistent GuestImportRegistry layout across
                // translation units. Do not re-enable until both are fixed
                // and re-verified; the investigation code/tests are kept.
                (false && nativeStateBatches && TRACE_SUBMISSION(submitVerifiedSideEffectFreeBatch)) ||
                submissionDiagnostic.attempt("submitOwnedColor",[&]{return submitOwnedColor(word);}) ||
                (nativeIdentityScaler && TRACE_SUBMISSION(submitNativeScalerStorageCoherency)) ||
                (nativeIdentityScaler && TRACE_SUBMISSION(submitNativePostScalerStorageCoherency)) ||
                (nativeIdentityScaler && TRACE_SUBMISSION(submitNativePostDisplayStorageCoherency)) ||
                (nativeIdentityScaler && TRACE_SUBMISSION(submitNativeDisplayGamma)) ||
                (nativeDisplayOutput && TRACE_SUBMISSION(submitExperimentalNativeDisplayOutput)) ||
                (nativeDisplayOutput && TRACE_SUBMISSION(submitNativePostOutputStorageCoherency)) ||
                (livePostOutputColor && TRACE_SUBMISSION(submitNativePostOutputSwapStorageCoherency)) ||
                (livePostOutputColor && TRACE_SUBMISSION(submitNativeDeferredSwapStorageCoherency)) ||
                (submitPostOutputSwap && submissionDiagnostic.attempt("submitPostOutputSwap",[&]{return submitPostOutputSwap(word);})) ||
                (nativeSwapCommands && (TRACE_SUBMISSION(submitNativeCommandStorageCoherency) ||
                    (experimentalGpuClock && TRACE_SUBMISSION(submitNativeWrappedSwapCommand)) ||
                    TRACE_SUBMISSION(submitExperimentalNativeSwapCommand))) ||
                (submitScalerInterrupt && submissionDiagnostic.attempt("submitScalerInterrupt",[&]{return submitScalerInterrupt(word);})) ||
                // Last fallback: exact runtime command-storage coherency only.
                // It owns a fresh completion and never relaxes startup cursors.
                (nativeDisplayOutput && TRACE_SUBMISSION(submitNativeRuntimeCommandStorageCoherency))
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE) && defined(__ANDROID__)
                || (nativeDisplayOutput && nativeStateBatches && experimentalIdleCachePair &&
                    submissionDiagnostic.attempt("submitRendererRuntimeMaintenance",[&]{return submitRendererRuntimeMaintenance(word);}))
#endif
                ;
#undef TRACE_SUBMISSION
            submissionDiagnostic.finish(accepted);return accepted;
        }))) return 2;
    if (experimentalNativeInitialization && hasHsioLock &&
        !registry.bindNativeEdramTrainingPolicy(static_cast<std::uint32_t>(hsioLockValues[1]))) return 2;
    std::vector<std::string> functionLabels;
    std::size_t nextFunctionLabel = 0;
    std::uint64_t droppedFunctionLabels = 0;
    if (disconnectedInput && !registry.bindDisconnectedInput()) return 2;
    if(nativeInput && !registry.bindNativeGamepad([&](std::uint32_t user,recomp::input::GamepadState& state){
        return inputCallbacks->gamepadState(inputCallbacks->inputUser,user,&state);
    },[&](std::uint32_t user,recomp::input::GamepadState& state){
        return inputCallbacks->gamepadCapabilities(inputCallbacks->inputUser,user,&state);
    }))return 2;
    recomp_bind_generated_guest_import_registry(&registry, addressSpace.base());
    recomp::bootstrap::GuestExecutionGuard guard;
    const auto traceSink = functionLabelLimit == 0
                    ? recomp::bootstrap::GuestExecutionGuard::FunctionTraceSink{}
                    : recomp::bootstrap::GuestExecutionGuard::FunctionTraceSink(
                              [&](std::string_view label) {
#if defined(RECOMP_PRIVATE_CHECKPOINT_OBSERVER)
                                  RECOMP_PRIVATE_CHECKPOINT_OBSERVER(addressSpace.base(), label);
#endif
                                  if (functionLabels.size() < functionLabelLimit) {
                                      functionLabels.emplace_back(label);
                                  } else {
                                      functionLabels[nextFunctionLabel].assign(label);
                                      nextFunctionLabel = (nextFunctionLabel + 1) % functionLabels.size();
                                      ++droppedFunctionLabels;
                                  }
                              });
    recomp::bootstrap::GuestExecutionResult execution;
    recomp::bootstrap::GuestCooperativeResult cooperativeResult;
    if (cooperative) {
        recomp::bootstrap::GuestCooperativeExecutor executor;
        if(livePostOutputColor) submitPostOutputSwap=[&](std::uint32_t word){
            // Admission, CPU storage and the action slot exist before native
            // presentation. Only the exclusive handoff may enter this worker;
            // an ordinary failed submission traps without scheduling peers.
            if(!registry.inspectStoppedPostOutputSwapQueuedInterrupt(word))return false;
            using Action=recomp::gpu::NativePostOutputSwapInterruptSequence::Action;
            auto actionSlot=std::make_shared<const Action*>(nullptr);
            const auto worker=registry.createNativePostOutputSwapInterruptWorker(word,[&](const auto& thread,const auto& initial){
                auto cpu=std::make_shared<PPCContext>();const auto token=thread.layout.threadAddress;
                cpu->r1.u64=initial.stackPointer;cpu->r13.u64=initial.pcrAddress;
                cpu->r3.u64=initial.argument0;cpu->r4.u64=initial.argument1;cpu->lr=0;
                return executor.enqueue(token,[&,cpu,initial,token,word,actionSlot]{
                    if(!*actionSlot)throw std::runtime_error("Second-swap IRQ entered without exclusive native action lease");
                    const auto& action=**actionSlot;
                    if(!registry.runNativePostOutputSwapInterruptScope(word,[&]{
                        if(!registry.beginNativePostOutputSwapInterrupt(word,action))
                            throw std::runtime_error("Second-swap IRQ prefix/ownership rejected");
                        if(!executor.runNativeNonYielding([&]{
                            recomp_host_guest_call_indirect(initial.address,*cpu,addressSpace.base());
                            if(cpu->r1.u64!=initial.stackPointer || cpu->r13.u64!=initial.pcrAddress)
                                throw std::runtime_error("Second-swap IRQ did not preserve owned stack/PCR");
                        },4096))throw std::runtime_error("Second-swap IRQ non-yielding scope rejected");
                    }))throw std::runtime_error("Second-swap IRQ scope/queued result rejected");
                    if(!registry.acknowledgeNativePostOutputSwapInterrupt(word,action))
                        throw std::runtime_error("Second-swap IRQ acknowledgement rejected");
                    if(!registry.markGuestThreadReturned(token,0))
                        throw std::runtime_error("Second-swap IRQ worker return rejected");
                });
            });
            if(!worker)return false;
            return registry.submitNativePostOutputWrappedSwapCommand(word,[&,actionSlot,token=worker->layout.threadAddress](const Action& action){
                if(*actionSlot)return false;
                *actionSlot=&action;
                struct Clear {const Action*& slot;~Clear(){slot=nullptr;}} clear{*actionSlot};
                // Parent remains parked, every peer remains parked, and the
                // native action reference stays alive until this worker returns.
                return executor.runNativeWorkerNext(token,4096);
            });
        };
        if (nativeIdentityScaler) submitScalerInterrupt=[&](std::uint32_t word) {
            const auto worker=registry.createNativeCommandInterruptWorker(word,[&](const auto& thread,const auto& initial) {
                auto cpu=std::make_shared<PPCContext>();const auto token=thread.layout.threadAddress;
                cpu->r1.u64=initial.stackPointer;cpu->r13.u64=initial.pcrAddress;
                cpu->r3.u64=initial.argument0;cpu->r4.u64=initial.argument1;cpu->lr=0;
                return executor.enqueue(token,[&,cpu,initial,token,word] {
                    if (!registry.runNativeCommandInterruptScope(word,[&] {
                        if (!registry.beginNativeScalerInterrupt(word))
                            throw std::runtime_error("Native scaler interrupt prefix/ownership preflight failed");
                        if (!executor.runNativeNonYielding([&] {
                            recomp_host_guest_call_indirect(initial.address,*cpu,addressSpace.base());
                            if (cpu->r1.u64!=initial.stackPointer || cpu->r13.u64!=initial.pcrAddress)
                                throw std::runtime_error("Native graphics interrupt did not preserve owned stack/PCR");
                        },4096)) throw std::runtime_error("Native graphics non-yielding scope rejected");
                    })) throw std::runtime_error("Native graphics interrupt scope/lock balance rejected");
                    if (!registry.acknowledgeNativeScalerInterrupt(word))
                        throw std::runtime_error("Native graphics interrupt did not produce its required acknowledgements");
                    if (!registry.markGuestThreadReturned(token,0))
                        throw std::runtime_error("Native graphics interrupt worker return rejected");
                });
            });
            if (!worker) return false;
            (void)executor.runNativeWorkerNext(worker->layout.threadAddress,4096);
            // Resumed original issuer; no peer ran during the handoff. Only
            // full native configuration/event completion may retire this root.
            return registry.completeNativeScalerWrapper(word);
        };
        if (!registry.bindThreadResumeHandler([&](const recomp::bootstrap::GuestPreparedThread& thread, std::int32_t priority) {
            const auto initial = recomp::bootstrap::makeGuestThreadEntry(thread);
            if (!initial || !recomp::bootstrap::hasExactGuestFunction(initial->address)) { return false; }
            try {
                auto cpu = std::make_shared<PPCContext>();
                cpu->r1.u64 = initial->stackPointer;
                cpu->r13.u64 = initial->pcrAddress;
                cpu->r3.u64 = initial->argument0;
                cpu->r4.u64 = initial->argument1;
                const auto token = thread.layout.threadAddress;
                return executor.enqueue(token, [&, cpu, initial = *initial, token] {
                    recomp_host_guest_call_indirect(initial.address, *cpu, addressSpace.base());
                    const auto exitCode = initial.directEntry ? cpu->r3.u32 : 0U;
                    if (!registry.markGuestThreadReturned(token, exitCode)) {
                        throw std::runtime_error("Guest thread return state rejected");
                    }
                }, priority);
            } catch (const std::bad_alloc&) {
                return false;
            }
        })) {
            std::cerr << "trace setup failed: thread resume handler rejected\n";
            return 2;
        }
        if (experimentalNativeInitialization && (!hasTitleEnvironment
                || !registry.bindMountedDisc(static_cast<std::uint32_t>(titleValues[2])))) {
            std::cerr << "trace setup failed: mounted executable disc identity rejected\n"; return 2;
        }
        if (!registry.bindThreadExitHandler([&](auto token,auto code) {
                return executor.terminateCurrentThread(token,[&] {
                    if (!registry.markGuestThreadReturned(token,code)) return false;
#if defined(RECOMP_NATIVE_XMA)
                    __android_log_print(ANDROID_LOG_INFO,"RecompNativeThread","EXIT token=%08x code=%08x",token,code);
#endif
                    return true;
                });
            }) || !registry.bindVirtualCpuAffinity() || !registry.bindThreadDebugNaming() || !registry.bindNotificationService() || !registry.bindSignedOutUsers()
                || !registry.bindNoRawStorageDevices()
                || !registry.bindNoLaunchData()
                || !registry.bindUserLanguage(1) // Native test application's English preference.
                || !registry.bindUserCountry(103) // Test locale US; not host/account geography.
                || !registry.bindThreadPriorityHandler([&](auto token, auto priority) { return executor.setPriority(token, priority); })
                || !registry.bindNetworkLifecycle()
                || !registry.bindThreadWaitHandler([&](const auto& ready) { return executor.waitUntil(ready); })
                || !registry.bindThreadTimedWaitHandler([&](const auto& ready, auto deadline) { return executor.waitUntil(ready, deadline); })
                || !registry.bindThreadDelayHandler([&](auto deadline) { return executor.delayUntil(deadline); })) {
            std::cerr << "trace setup failed: virtual CPU, debug naming, or wait binding rejected\n";
            return 2;
        }
        if (nativeEventWakeBoost && !registry.bindNativeEventWakeBoostHandler(
            [&](const auto* tokens,auto count){return executor.boostEventWaiters(tokens,count);})) {
            std::cerr << "trace setup failed: native event wake boost binding rejected\n";return 2;
        }
        if (nativeCriticalSectionWaits && !registry.bindNativeCriticalSectionWaits()) {
            std::cerr << "trace setup failed: native critical-section wait binding rejected\n";return 2;
        }
        if (nativeHighPriorityTiers && !registry.bindNativeHighPriorityTiers()) {
            std::cerr << "trace setup failed: native +3/+4 priority tier rejected\n";return 2;
        }
#if defined(__ANDROID__)
        if (nativeAudioRenderer && !registry.bindExperimentalAudioRenderer(nativeAudioRenderer,
            [&](const recomp::bootstrap::GuestPreparedThread& thread) {
                const auto initial=recomp::bootstrap::makeGuestThreadEntry(thread);
                if (!initial || !initial->directEntry || !recomp::bootstrap::hasExactGuestFunction(initial->address)) return false;
                auto cpu=std::make_shared<PPCContext>();const auto token=thread.layout.threadAddress;
                return executor.enqueue(token,[&,cpu,initial=*initial,token] {
                    using Demand=recomp::audio::NativeAudioRenderClient::Demand;
                    for(;;) {
                        recomp::bootstrap::GuestExecutionGuard::checkpoint("native_audio_callback_demand");
                        const auto demand=nativeAudioRenderer->poll(token);
                        if (demand==Demand::Failed) throw std::runtime_error("Native audio demand/counter/backend failure");
                        if (demand==Demand::Wait) {
                            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1);
                            if (!executor.waitUntil([deadline]{return std::chrono::steady_clock::now()>=deadline;},deadline))
                                throw std::runtime_error("Native audio backpressure wait rejected");
                            continue;
                        }
                        if (!nativeAudioRenderer->beginCallback(token)) throw std::runtime_error("Native audio callback credit rejected");
                        cpu->r1.u64=initial.stackPointer;cpu->r13.u64=initial.pcrAddress;
                        cpu->r3.u64=initial.argument0;cpu->r4.u64=0;cpu->lr=0;
                        recomp_host_guest_call_indirect(initial.address,*cpu,addressSpace.base());
                        if (cpu->r1.u64!=initial.stackPointer || cpu->r13.u64!=initial.pcrAddress ||
                            !nativeAudioRenderer->finishCallback(token))
                            throw std::runtime_error("Native audio callback returned without one valid PCM packet or preserved stack/PCR");
                    }
                });
            })) {std::cerr << "trace setup failed: native PCM renderer binding rejected\n";return 2;}
        if (nativeCategoryOneUnityGain && !registry.bindNativeCategoryOneUnityGain()) {
            std::cerr << "trace setup failed: native category1 unity-gain policy rejected\n";return 2;
        }
#endif
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE) && defined(__ANDROID__)
        bindRendererInlineCapture(&registry,inspectColorProgram?&*inspectColorProgram:nullptr,
            [&](const auto& f){return executor.runNativeNonYielding(f,1);},nativeGraphics.get(),
            [&](const auto& rgba,unsigned width,unsigned height) {
                if(!liveCallbacks || !liveCallbacks->orderedRgba || !nativeGraphics ||
                    !nativeGraphics->nativeScalerConfigured())return false;
                std::vector<std::uint8_t> pixels;
                return nativeGraphics->mapNativeScanout(rgba.data(),rgba.size(),width,height,pixels) &&
                    liveCallbacks->orderedRgba(liveCallbacks->user,pixels.data(),
                        static_cast<std::uint32_t>(pixels.size()),width,height)==1;
            },[&](const auto& f){return executor.runNativeNonYielding(f,256);},
            [&](const auto& f){return executor.runNativeNonYielding(f,4096);},
            [&](unsigned device,const recomp::gpu::Gta5TileCommandInterrupt& request) {
                if(!nativeGraphics || request.predicated())return false;
                const auto result=recomp::bootstrap::executeNativeTileSceneHandoff(registry,executor,device,request,
                    [&](const auto& memory,const auto& guest){return nativeGraphics->executeTileCommandInterrupt(request,memory,guest);},
                    [&](const auto&,const auto& entry) {
                        PPCContext cpu{};cpu.r1.u64=entry.stackPointer;cpu.r13.u64=entry.pcrAddress;
                        cpu.r3.u64=entry.argument0;cpu.r4.u64=entry.argument1;cpu.lr=0;
                        recomp_host_guest_call_indirect(entry.address,cpu,addressSpace.base());
                        return cpu.r1.u64==entry.stackPointer && cpu.r13.u64==entry.pcrAddress && cpu.lr==0;
                    });
                __android_log_print(ANDROID_LOG_INFO,"RecompRenderer",
                    "RENDER_INITIAL_SCENE_IRQ admitted=%u returned=%u worker=%08x writes=%u waits=%u guest_returns=%u scene_completed=0 ring_retired=0",
                    unsigned(result.admitted),unsigned(result.returned),result.worker,result.scratchWrites,result.scratchWaits,result.guestReturns);
                return result.admitted && result.returned;
            },[&](unsigned device,const recomp::gpu::Gta5TileSceneContinuation& tail) {
                if(!nativeGraphics)return false;
                const auto& state=nativeGraphics->stateBatchSnapshot();
                const recomp::gpu::DrawBinState bins{{unsigned(state.binMask),unsigned(state.binMask>>32),
                    unsigned(state.binSelect),unsigned(state.binSelect>>32)},state.knownBinWords};
                const auto selected=tail.select(bins);
                if(!selected || !selected->interruptRequired)return false;
                const auto result=recomp::bootstrap::executeNativeTileSceneHandoff(registry,executor,device,tail.request(),
                    [&](const auto& memory,const auto& guest){return nativeGraphics->executeTileSceneContinuation(tail,memory,guest);},
                    [&](const auto&,const auto& entry) {
                        PPCContext cpu{};cpu.r1.u64=entry.stackPointer;cpu.r13.u64=entry.pcrAddress;
                        cpu.r3.u64=entry.argument0;cpu.r4.u64=entry.argument1;cpu.lr=0;
                        recomp_host_guest_call_indirect(entry.address,cpu,addressSpace.base());
                        return cpu.r1.u64==entry.stackPointer && cpu.r13.u64==entry.pcrAddress && cpu.lr==0;
                    });
                __android_log_print(ANDROID_LOG_INFO,"RecompRenderer",
                    "RENDER_LIVE_SCENE_IRQ admitted=%u returned=%u worker=%08x writes=%u waits=%u guest_returns=%u ring_retired=0",
                    unsigned(result.admitted),unsigned(result.returned),result.worker,result.scratchWrites,result.scratchWaits,result.guestReturns);
                return result.admitted && result.returned;
            },{[&]{return executor.currentTaskToken();},[&](unsigned token){return executor.parkedTask(token);},
               [&](const auto& ready){return executor.waitUntil(ready);}});
        struct ClearRendererCapture {~ClearRendererCapture(){unbindRendererInlineCapture();}} clearRendererCapture;
#endif
        NativeDiscBridgeScope nativeDiscScope(experimentalNativeInitialization ? &registry : nullptr,
                                               addressSpace.base());
        cooperativeResult = executor.run(guestThreadToken,
                [&] { _xstart(context, addressSpace.base()); },
                {functionEntryBudget, static_cast<std::uint32_t>(quantum), 65, nativeHighPriorityTiers},
                [&](std::uint32_t token) {
                    if (!registry.bindGuestThreadToken(token)) {
                        throw std::runtime_error("Cooperative thread binding rejected");
                    }
                }, traceSink);
        execution = cooperativeResult.execution;
        submitScalerInterrupt={}; // Captures executor; clear before its destruction.
        submitPostOutputSwap={};
    } else {
        execution = guard.run(functionEntryBudget,
                [&] { _xstart(context, addressSpace.base()); }, traceSink);
    }
    // All executor workers have joined. Capture mapping-dependent diagnostic
    // values now, before resolver teardown invalidates hasExactGuestFunction.
    // Reporting below uses this owned snapshot, not a re-enabled guest resolver.
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE)
    if(!saveStoppedRendererDraw((std::filesystem::path(argv[1]).parent_path()/"renderer-first-unsupported").string()))
        std::cerr<<"trace evidence failed: unsupported renderer draw save failed\n";
#if defined(__ANDROID__)
    dumpStoppedRendererTextureDescriptions(registry,nativeGraphics.get(),inspectColorProgram?&*inspectColorProgram:nullptr);
#else
    dumpStoppedRendererTextureDescriptions(registry);
#endif
#endif
    const auto submission = execution.scalarAccess
        ? recomp::bootstrap::inspectStoppedGpuSubmission(*execution.scalarAccess, registry)
        : recomp::bootstrap::StoppedGpuSubmission{};
    std::optional<recomp::bootstrap::GuestImportRegistry::StoppedPostOutputSwapInterrupt> stoppedPostOutputSwapInterrupt;
    std::uint32_t stoppedPostOutputSwapInterruptRejection=0;
    if(livePostOutputColor && submission.status==recomp::bootstrap::StoppedGpuSubmissionStatus::Decoded)
        stoppedPostOutputSwapInterrupt=registry.inspectStoppedPostOutputSwapInterrupt(submission.writeWord,&stoppedPostOutputSwapInterruptRejection);
    std::optional<recomp::bootstrap::GuestImportRegistry::StoppedDeferredSwapInterrupt> stoppedDeferredSwapInterrupt;
    std::uint32_t stoppedDeferredSwapInterruptRejection=0;
    if(liveDeferredColor && submission.status==recomp::bootstrap::StoppedGpuSubmissionStatus::Decoded)
        stoppedDeferredSwapInterrupt=registry.inspectStoppedDeferredSwapInterrupt(submission.writeWord,&stoppedDeferredSwapInterruptRejection);
    recomp_unbind_generated_guest_import_registry();
    const auto experimentalClockReads = recomp::bootstrap::experimentalGuestGpuClockReads();
    recomp::bootstrap::resetExperimentalGuestGpuClock();
    recomp::bootstrap::resetGuestGraphicsSubmission();
    recomp::bootstrap::resetGuestXmaRegisters();
#if defined(RECOMP_NATIVE_XMA)
    if (nativeXma) __android_log_print(ANDROID_LOG_INFO,"RecompNativeXma","RESULT frames=%llu pcm_bytes=%llu boundary=%s",
        static_cast<unsigned long long>(nativeXma->decodedFrames()),static_cast<unsigned long long>(nativeXma->writtenPcmBytes()),
        nativeXma->failure().c_str());
    if (nativeXma) std::cerr<<"native XMA result: frames="<<nativeXma->decodedFrames()
        <<" pcm_bytes="<<nativeXma->writtenPcmBytes()<<" boundary="<<nativeXma->failure()<<"\n";
#endif
    recomp::bootstrap::resetGuestScalarDataImportGuard();
    recomp::bootstrap::resetGuestScalarUptimeClock();
    const auto rejectedIndirect = recomp::bootstrap::stoppedGuestIndirectCall();
    recomp::bootstrap::resetGuestIndirectCallResolver();

    const auto fileIo = registry.fileIoStatistics();
    recomp::gpu::Pm4IndirectGraph indirectGraph;
    recomp::gpu::Pm4GraphError indirectError;
    const bool inspectIndirect = submission.status == recomp::bootstrap::StoppedGpuSubmissionStatus::Decoded;
    if (inspectIndirect) {
        (void)recomp::gpu::inspectPm4IndirectGraph(submission.snapshot,
            [&](auto address, auto bytes) { return registry.snapshotStoppedGraphicsPhysical(address, bytes); },
            indirectGraph, indirectError);
    }
    recomp::gpu::StoppedGpuWork stoppedWork;
    recomp::gpu::StoppedGpuWorkError stoppedWorkError;
    const bool inspectWork = inspectIndirect && indirectError.failure == recomp::gpu::Pm4GraphFailure::None;
    if(captureStoppedPm4 && inspectIndirect) {
        // One bounded post-join report; no per-draw logging or change to admission.
        std::cerr<<"STOPPED_SUBMISSION_DISPATCH ";submissionDiagnostic.json(std::cerr);std::cerr<<'\n';
        if(inspectWork) {
            const auto saved=recomp::trace::saveStoppedSubmission(std::filesystem::path(argv[1]).parent_path(),
                submission.snapshot,indirectGraph,submission.readWord,submission.writeWord,submissionDiagnostic);
            std::cerr<<"STOPPED_SUBMISSION_BINARY_SAVE status="<<static_cast<unsigned>(saved)
                <<" words="<<indirectGraph.totalWords<<" executed=0 ring_advanced=0\n";
        }
    }
    if(captureStoppedPm4 && inspectWork) {
        // Workers are joined; this extra input evidence cannot resume a guest,
        // issue a draw, evaluate a predicate or advance the command queue.
        recomp::gpu::StoppedDmaInputs inputs;
        // A uint16 triangle index count may be odd (18 observed draws are).
        // Use the allocation-checked byte reader, not the PM4 dword reader.
        const bool captured=recomp::gpu::captureStoppedDmaInputs(submission.snapshot,indirectGraph,
                [&](auto a,auto n){return registry.copySerializedGraphicsData(a,n);},inputs);
        if(!captured)std::cerr<<"trace evidence failed: stopped DMA input collection failed\n";
        if(captured && !inputs.inputs.empty()) {
            const auto directory=std::filesystem::path(argv[1]).parent_path()/"stopped-dma-inputs";
            std::error_code error;
            bool ok=std::filesystem::create_directory(directory,error) && !error;
            if(ok) {
                std::ofstream meta(directory/"inputs.json");
                meta<<"{\"version\":1,\"requested_bytes\":"<<inputs.requestedBytes<<",\"unavailable\":"<<inputs.unavailable
                    <<",\"execution_order\":false,\"predicates_evaluated\":false,\"guest_writes\":false,\"inputs\":[";
                for(std::size_t i=0;i<inputs.inputs.size();++i) {
                    const auto& input=inputs.inputs[i];
                    if(i)meta<<',';
                    meta<<"{\"node\":"<<input.node<<",\"offset_words\":"<<input.offsetWords
                        <<",\"failure\":"<<unsigned(input.failure)<<",\"physical_address\":"<<input.layout.physicalAddress
                        <<",\"index_count\":"<<input.layout.indexCount<<",\"predicated\":"<<(input.layout.predicate.predicated?"true":"false");
                    if(!input.bytes.empty()) {
                        const auto name="indices-"+std::to_string(i)+".bin";
                        std::ofstream file(directory/name,std::ios::binary);
                        file.write(reinterpret_cast<const char*>(input.bytes.data()),input.bytes.size());
                        file.close();ok=bool(file) && ok;
                        meta<<",\"path\":\""<<name<<"\",\"byte_count\":"<<input.bytes.size();
                    }
                    meta<<'}';
                }
                meta<<"]}";meta.close();ok=bool(meta) && ok;
            }
            if(!ok)std::cerr<<"trace evidence failed: stopped DMA input save failed\n";
        }
    }
    // Native-only envelope is deliberately invalid retail PM4. Read-only
    // diagnostics may inspect its owned wrapper without making the graph valid
    // or granting execution permission. All executor writers are joined here.
    std::optional<recomp::bootstrap::GuestImportRegistry::NativePendingSubmission> nativePendingSubmission;
    std::optional<recomp::bootstrap::GuestImportRegistry::NativePendingSubmission> deferredPendingSubmission;
    std::optional<recomp::gpu::NativeScalerWrapper> nativeScalerWrapper;
    if(nativeSwapCommands && inspectIndirect && indirectError.failure==recomp::gpu::Pm4GraphFailure::DecodeRejected &&
        indirectError.parent==0 && indirectError.offsetWords==0 &&
        indirectError.decode.failure==recomp::gpu::Pm4Failure::TruncatedPacket && indirectError.decode.opcode==0x53){
        auto pending=registry.inspectStoppedNativePendingSubmission(submission.writeWord);
        if(!pending && nativeIdentityScaler)pending=registry.inspectStoppedNativeScalerSubmission(submission.writeWord);
        if(!pending && livePostOutputColor){
            deferredPendingSubmission=registry.inspectStoppedDeferredSwapSubmission(submission.writeWord);
            pending=deferredPendingSubmission;
        }
        if(pending && pending->commandOffsetWords==indirectError.decode.offsetWords)nativePendingSubmission=pending;
        else deferredPendingSubmission.reset();
    }
    if (nativeIdentityScaler && nativePendingSubmission)
        nativeScalerWrapper=registry.inspectStoppedNativeScalerWrapper(submission.writeWord);
    if (inspectWork) (void)recomp::gpu::inspectStoppedGpuWork(submission.snapshot, indirectGraph, stoppedWork, stoppedWorkError);
    recomp::gpu::StoppedMemoryEffects stoppedMemory;
    recomp::gpu::StoppedMemoryError stoppedMemoryError;
    if (inspectWork) (void)recomp::gpu::inspectStoppedMemoryEffects(submission.snapshot, indirectGraph, stoppedMemory, stoppedMemoryError);
    std::cout << ProtocolHeader << '\n'
              << "stop_reason="
              << recomp::bootstrap::guestExecutionStopReasonName(execution.reason)
              << '\n'
              << "completed_function_entries=" << execution.completedFunctionEntries << '\n'
              << "debug_message_count=" << debugMessageCount << '\n'
              << "debug_message_bytes=" << debugMessageBytes << '\n'
              << "dropped_function_labels=" << droppedFunctionLabels << '\n'
              << "validated_mapping_count=" << validatedMappingCount << '\n'
              << "file_opens=" << fileIo.opens << '\n'
              << "file_reads=" << fileIo.reads << '\n'
              << "file_bytes=" << fileIo.bytes << '\n'
              << "file_seeks=" << fileIo.seeks << '\n';
    const auto discStats = registry.nativeDiscStatistics();
    if (hasKernelVersion) std::cout << "native_kernel_version=" << kernelVersionValues[0]
        << ',' << kernelVersionValues[1] << ',' << kernelVersionValues[2] << '\n';
    if (discStats[0]) std::cout << "native_mounted_disc=" << discStats[0] << ','
        << discStats[1] << ',' << discStats[2] << '\n';
    if (hasGraphics) std::cout << "graphics_initialized=" << registry.graphicsInitialized() << '\n';
    if (imagePageProtections) std::cout << "xex_image_protections=" << imageBase << ',' << titleValues[1] << '\n';
    if (disconnectedInput) std::cout << "disconnected_input=1\n";
    if(nativeInput)std::cout << "native_input=1\n";
    if (nativeEventWakeBoost) {
        const auto stats=registry.nativeEventWakeBoostStatistics();
        std::cout << "native_event_wake_boost=" << stats.signalRequests << ',' << stats.threadsBoosted << ',' << stats.threadsSuppressed << '\n';
    }
    if (nativeCategoryOneUnityGain)
        std::cout << "native_category1_gain=1065353216," << registry.nativeCategoryOneGainQueries() << '\n';
    if (nativeCriticalSectionWaits) {
        const auto s=registry.nativeCriticalSectionWaitStatistics();
        std::cout << "native_critical_section_waits=" << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << '\n';
    }
    if (nativeHighPriorityTiers)std::cout << "native_high_priority_tiers=" << registry.nativeHighPriorityTiersRequests() << '\n';
    if (experimentalIdleCachePair) std::cout << "experimental_idle_cache_pairs=" << registry.experimentalIdleCachePairs() << '\n';
    if(nativeScalerTiming){using Timing=recomp::gpu::NativeScalerTiming;
        std::cout<<"native_scaler_timing=1,"<<Timing::NominalGpuMHz<<','<<Timing::PixelClockWholeMHz<<','<<Timing::PixelClockHz<<','
            <<Timing::TotalWidth<<','<<Timing::TotalHeight<<','<<Timing::RefreshHz<<','<<registry.nativeScalerTimingQueries()<<'\n';
    }
    if(nativeIdentityScaler){const auto s=registry.nativeScalerCommandStatus();
        std::cout<<"native_scaler_command="<<s.issued<<','<<s.previousReadWord<<','<<s.words<<','<<s.issuerThread<<'\n';
        const auto storage=registry.completedNativeScalerStorageCoherency();
        std::cout<<"native_scaler_storage_coherency=";
        if(storage)std::cout<<1<<','<<storage->previousWord<<','<<storage->readWord<<','<<storage->commandPhysical<<','
            <<storage->request.physicalBase<<','<<storage->request.byteCount<<','<<storage->request.cacheFlags;
        else std::cout<<"0,0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if(experimentalOwnedColor){const auto completed=registry.completedNativeOwnedColorBatch();
        std::cout<<"native_owned_color_batch=";
        if(completed)std::cout<<1<<','<<completed->previousWord<<','<<completed->readWord<<','<<completed->draws<<','
            <<completed->eventWords<<','<<completed->copyBytes<<','<<completed->edramBytes<<','<<completed->nativeFences<<','<<completed->policy;
        else std::cout<<"0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if(nativeSwapCommands){const auto swap=registry.nativeSwapCommandStatus();
        std::cout<<"native_swap_command="<<swap.issued<<','<<swap.pending<<','<<swap.presentQueued<<','
            <<swap.commandPhysical<<','<<swap.previousWord<<','<<swap.readWord<<','<<swap.issuedReadWord<<'\n';
        const auto mode=registry.nativeDisplayModeStatus();
        std::cout<<"native_display_mode="<<mode.selected<<','<<mode.flags<<','<<mode.queries<<'\n';
        const auto ordered=registry.completedNativeCommandStorageCoherency();
        std::cout<<"native_command_storage_coherency=";
        if(ordered)std::cout<<1<<','<<ordered->previousWord<<','<<ordered->readWord<<','<<ordered->request.physicalBase<<','
            <<ordered->request.byteCount<<','<<ordered->request.cacheFlags;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
        const auto wrapped=registry.completedNativeWrappedSwap();
        std::cout<<"native_wrapped_swap=";
        if(wrapped){const auto& w=*wrapped;
            std::cout<<1<<','<<w.previousWord<<','<<w.readWord<<','<<w.childPhysical<<','<<w.gpu.nativeFences<<','
                <<w.gpu.beforeCompletionNs<<','<<w.gpu.afterCompletionNs;
            for(unsigned i=0;i<4;++i){std::uint32_t value=0;
                for(unsigned b=0;b<4;++b)value|=std::uint32_t{w.gpu.eventBytes[i*4+b]}<<(i<2?8*b:24-8*b);
                std::cout<<','<<w.eventPhysical[i]<<','<<value;
            }
        }else std::cout<<"0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        std::cout<<"native_pending_submission=";
        if(nativePendingSubmission){const auto& p=*nativePendingSubmission;
            std::cout<<1<<','<<p.rootPhysical<<','<<p.childPhysical<<','<<p.childWords<<','<<p.commandOffsetWords<<','<<p.readWord<<','<<p.writeWord;
        }else std::cout<<"0,0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if (nativeIdentityScaler) {
        const auto scope=registry.nativeCommandInterruptScopeStatus();
        const auto interrupt=registry.nativeScalerInterruptStatus();
        std::cout<<"native_scaler_interrupt="<<scope.attempted<<','<<scope.returned<<','<<scope.balanced<<','<<scope.worker<<','
            <<scope.heldLocks<<','<<scope.lockPairs[0]<<','<<scope.lockPairs[1]<<','<<interrupt.prefixAttempted<<','
            <<interrupt.prefixReady<<','<<interrupt.ackAttempted<<','<<interrupt.acknowledged<<','<<interrupt.rejection<<'\n';
        std::cout<<"native_scaler_pacing=";
        for (unsigned i=0;i<interrupt.pacing.size();++i) std::cout<<(i?",":"")<<interrupt.pacing[i];
        std::cout<<'\n';
        const auto completion=registry.completedNativeScalerWrapper();
        std::cout<<"native_scaler_completion=";
        if (completion) {
            std::cout<<1<<','<<completion->previousWord<<','<<completion->readWord<<','<<completion->gpu.nativeFences;
            for (unsigned i=0;i<2;++i) {unsigned value=0;
                for (unsigned b=0;b<4;++b) value=(value<<8)|completion->gpu.eventBytes[i*4+b];
                std::cout<<','<<completion->eventPhysical[i]<<','<<value;
            }
        } else std::cout<<"0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto postStorage=registry.completedNativePostScalerStorageCoherency();
        std::cout<<"native_post_scaler_storage=";
        if (postStorage) {
            const auto& s=*postStorage;
            std::cout<<s.batches<<','<<s.firstWord<<','<<s.previousWord<<','<<s.readWord<<','<<
                s.request.physicalBase<<','<<s.request.byteCount<<','<<s.request.cacheFlags<<','<<s.batches;
        } else std::cout<<"0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        std::cout<<"native_scaler_wrapper_plan=";
        if (nativeScalerWrapper) {
            const auto registration=registry.graphicsInterruptRegistration();
            std::optional<std::uint32_t> mask,base;
#if defined(__ANDROID__)
            if (nativeGraphics) { mask=nativeGraphics->retainedRegister(0x1DC);base=nativeGraphics->retainedRegister(0x1DD); }
#endif
            std::cout<<1<<','<<nativeScalerWrapper->scratchPhysical()<<','<<nativeScalerWrapper->callbackAddress()<<','
                <<nativeScalerWrapper->callbackContext()<<','<<(registration?registration->routine:0)<<','
                <<(registration?registration->userData:0)<<','<<mask.has_value()<<','<<mask.value_or(0)<<','
                <<base.has_value()<<','<<base.value_or(0);
        } else std::cout<<"0,0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto gamma=registry.completedNativeDisplayGamma();
        std::cout<<"native_display_gamma=";
        if(gamma) {
            std::cout<<1<<','<<gamma->previousWord<<','<<gamma->readWord<<','<<gamma->childPhysical<<','<<gamma->frontbufferPhysical<<','<<
                gamma->gpu.nativeFences<<','<<gamma->gpu.presentQueued;
            for(unsigned i=0;i<2;++i){unsigned value=0;for(unsigned b=0;b<4;++b)value=(value<<8)|gamma->gpu.eventBytes[i*4+b];
                std::cout<<','<<gamma->eventPhysical[i]<<','<<value;}
        }else std::cout<<"0,0,0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto displayStorage=registry.completedNativePostDisplayStorageCoherency();
        std::cout<<"native_post_display_storage=";
        if(displayStorage){const auto& s=*displayStorage;
            std::cout<<s.batches<<','<<s.firstWord<<','<<s.previousWord<<','<<s.readWord<<','<<
                s.request.physicalBase<<','<<s.request.byteCount<<','<<s.request.cacheFlags<<','<<s.batches;
        }else std::cout<<"0,0,0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if(nativeDisplayOutput){
        const auto output=registry.completedNativeDisplayOutput();
        std::cout<<"native_display_output=";
        if(output){std::cout<<1<<','<<output->previousWord<<','<<output->readWord<<','<<output->childPhysical<<','<<
                output->gpu.nativeFences<<','<<output->gpu.presentQueued;
            for(unsigned i=0;i<2;++i){unsigned value=0;for(unsigned b=0;b<4;++b)value=(value<<8)|output->gpu.eventBytes[i*4+b];
                std::cout<<','<<output->eventPhysical[i]<<','<<value;}
        }else std::cout<<"0,0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto outputStorage=registry.completedNativePostOutputStorageCoherency();
        std::cout<<"native_post_output_storage=";
        if(outputStorage){const auto& s=*outputStorage;
            std::cout<<s.batches<<','<<s.firstWord<<','<<s.previousWord<<','<<s.readWord<<','<<
                s.request.physicalBase<<','<<s.request.byteCount<<','<<s.request.cacheFlags<<','<<s.batches;
        }else std::cout<<"0,0,0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if(liveDeferredColor){
        const auto runtimeMaintenance=registry.completedNativeRuntimeMaintenancePoints();
        std::cout<<"native_runtime_maintenance=";
        if(runtimeMaintenance){const auto& s=*runtimeMaintenance;
            std::cout<<s.batches<<','<<s.previousWord<<','<<s.readWord<<','<<s.draws<<','<<s.shaderLoads<<','<<
                s.invalidations<<','<<s.fencePhysical<<','<<s.fenceValue<<','<<s.commandCursor;
        }else std::cout<<"0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        // Reporting only: attempts,rejection,reachedExecute,failedLatch then the
        // actual allocation owners, so a live rejection is explainable.
        const auto maintenanceReject=registry.nativeRuntimeMaintenanceDiagnostic();
        std::cout<<"native_runtime_maintenance_rejection="<<maintenanceReject.attempts<<','<<maintenanceReject.rejection<<','
            <<maintenanceReject.reachedExecute<<','<<maintenanceReject.failedLatch<<','
            <<maintenanceReject.ringAllocation<<','<<maintenanceReject.writeBackAllocation<<','
            <<maintenanceReject.fencePhysical<<','<<maintenanceReject.fenceAllocation<<','
            <<maintenanceReject.cursorPhysical<<','<<maintenanceReject.cursorAllocation<<','<<maintenanceReject.leaseCount;
        for(unsigned i=0;i<maintenanceReject.leaseCount && i<maintenanceReject.leasePhysical.size();++i)
            std::cout<<','<<maintenanceReject.leasePhysical[i]<<','<<maintenanceReject.leaseAllocation[i];
        std::cout<<'\n';
        const auto runtimeStorage=registry.completedNativeRuntimeCommandStorageCoherency();
        std::cout<<"native_runtime_command_storage=";
        if(runtimeStorage){const auto& s=*runtimeStorage;
            std::cout<<s.batches<<','<<s.firstWord<<','<<s.previousWord<<','<<s.readWord<<','<<
                s.request.physicalBase<<','<<s.request.byteCount<<','<<s.request.cacheFlags;
        }else std::cout<<"0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto status=registry.nativeDeferredColorPrefixStatus();
        const auto completed=registry.completedNativeDeferredColorPrefix();
        const auto ring=registry.graphicsRingConfiguration();bool repeatRejected=false;
#if defined(__ANDROID__)
        if(status[0] && inspectColorProgram)repeatRejected=
            !registry.executeExperimentalDeferredColorPrefix(*inspectColorProgram,{},submission.writeWord);
#endif
        std::cout<<"native_deferred_color_prefix="<<status[0]<<','<<status[1]<<','<<status[2]<<','<<bool(completed);
        if(completed){const auto& c=*completed;
            std::cout<<','<<c.previousWord<<','<<c.pendingWriteWord<<','<<c.prefixWords<<','<<c.inputCount<<','<<c.copyBytes<<','
                <<c.nativeFences<<','<<c.leadingSetupFences;
        }else std::cout<<",0,0,0,0,0,0,0";
        std::cout<<','<<repeatRejected<<','<<(ring?ring->readWord:0)<<'\n';
        std::cout<<"stopped_deferred_swap_interrupt=[";
        if(stoppedDeferredSwapInterrupt){const auto& s=*stoppedDeferredSwapInterrupt;
            std::cout<<1<<','<<s.submission.readWord<<','<<s.submission.writeWord<<','<<(s.submission.childWords-129)<<','
                <<s.wrapper.scratchPhysical()<<','<<s.wrapper.callbackAddress()<<','<<s.wrapper.callbackContext()<<','
                <<s.device<<','<<s.handler<<','<<s.frequency;
            for(auto word:s.deviceValues)std::cout<<','<<word;
            for(auto word:s.queuedSurfaces)std::cout<<','<<word;
            for(auto word:s.scratch)std::cout<<','<<word;
            for(auto word:s.pacingRecord)std::cout<<','<<word;
        }else std::cout<<0<<','<<stoppedDeferredSwapInterruptRejection;
        std::cout<<"]\n";
    }
    if(livePostOutputColor){
        const auto status=registry.nativePostOutputColorStatus();
        std::cout<<"native_post_output_color_status="<<status[0]<<','<<status[1]<<','<<status[2]<<'\n';
        const auto completed=registry.completedNativePostOutputColorBatch();
        std::cout<<"native_post_output_color=";
        if(completed){const auto& c=completed->color;
            std::cout<<1<<','<<c.previousWord<<','<<c.readWord<<','<<c.draws<<','<<c.eventWords<<','<<c.copyBytes<<','<<c.edramBytes<<','
                <<c.nativeFences<<','<<completed->leadingSetupFences<<','<<c.policy;
        }else std::cout<<"0,0,0,0,0,0,0,0,0,0";
        std::cout<<'\n';
        const auto context=registry.nativePostOutputSystemContext();
        std::cout<<"native_post_output_system_context=";
        if(context)std::cout<<1<<','<<context->stackPointer<<','<<context->buffer<<','<<context->identifierOutput<<','<<context->readWord<<','<<context->policy;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
    }
    if(livePostOutputColor){
        const auto storage=registry.completedNativePostOutputSwapStorageCoherency();
        const auto nextContext=registry.nativePostSwapSystemContext();
        std::cout<<"native_post_swap_system_context=";
        if(nextContext)std::cout<<1<<','<<nextContext->stackPointer<<','<<nextContext->buffer<<','<<nextContext->identifierOutput<<','<<nextContext->readWord<<','<<nextContext->policy;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
        std::cout<<"native_deferred_swap_request=[";
        if(const auto deferred=registry.nativeDeferredSwapRequest()){
            const auto& r=deferred->request;const auto& owner=deferred->imageOwner;
            std::cout<<1<<','<<3<<','<<r.commandVirtual<<','<<r.commandPhysical<<','<<r.allocationBase<<','<<r.allocationBytes<<','
                <<r.protection<<','<<r.frontbufferVirtual<<','<<r.format<<','<<r.colorSpace<<','<<r.width<<','<<r.height<<','<<r.identifierWord;
            for(auto word:r.fetch)std::cout<<','<<word;
            std::cout<<','<<deferred->imagePhysical<<','<<owner.virtualBase<<','<<owner.byteCount<<','<<owner.protection<<','
                <<owner.writable<<','<<deferred->issuedReadWord;
        }else std::cout<<0;
        std::cout<<"]\n";
        std::cout<<"native_deferred_swap_storage=";
        if(const auto storage=registry.completedNativeDeferredSwapStorageCoherency())
            std::cout<<1<<','<<storage->previousWord<<','<<storage->readWord<<','<<storage->request.physicalBase<<','
                <<storage->request.byteCount<<','<<storage->request.cacheFlags;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
        std::cout<<"native_post_output_swap_storage=";
        if(storage)std::cout<<1<<','<<storage->previousWord<<','<<storage->readWord<<','<<storage->request.physicalBase<<','
            <<storage->request.byteCount<<','<<storage->request.cacheFlags;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
        const auto swap=registry.nativePostOutputSwapStatus();
        std::cout<<"native_post_output_swap="<<swap.issued<<','<<swap.pending<<','<<swap.presentQueued<<','<<swap.commandPhysical<<','
            <<swap.previousWord<<','<<swap.readWord<<','<<swap.issuedReadWord<<'\n';
        const auto wrapped=registry.completedNativePostOutputWrappedSwap();
        std::cout<<"native_post_output_wrapped_swap=";
        if(wrapped){const auto& c=*wrapped;const auto irq=registry.nativePostOutputSwapInterruptScopeStatus();const auto& s=irq.scope;
            const auto imagePhysical=c.imageOwner.physicalBase+(c.imageRequest.frontbufferVirtual-c.imageOwner.virtualBase);
            std::cout<<1<<','<<c.previousWord<<','<<c.readWord<<','<<c.childPhysical<<','<<imagePhysical<<','
                <<c.imageOwner.physicalBase<<','<<c.imageOwner.virtualBase<<','<<c.imageOwner.byteCount<<','
                <<c.imageRequest.width<<','<<c.imageRequest.height<<','<<c.gpu.nativeFences<<','<<c.gpu.interruptIdleFences<<','
                <<c.gpu.interruptAcknowledged<<','<<c.gpu.beforeCompletionNs<<','<<c.gpu.afterCompletionNs<<','
                <<s.worker<<','<<s.attempted<<','<<s.returned<<','<<s.balanced<<','<<s.heldLocks<<','<<s.lockPairs[0]<<','<<s.lockPairs[1]<<','
                <<irq.queuedCallbackValidated<<','<<irq.rejection<<','
                <<(registry.nativePostOutputSwapInterruptPhase()==recomp::gpu::NativePostOutputSwapInterruptSequence::Phase::Acknowledged);
            for(unsigned i=0;i<4;++i){unsigned value=0;
                for(unsigned b=0;b<4;++b)value|=unsigned{c.gpu.eventBytes[i*4+b]}<<(i<2?8*b:24-8*b);
                std::cout<<','<<c.eventPhysical[i]<<','<<value;}
        }else {std::cout<<0;for(unsigned i=1;i<33;++i)std::cout<<",0";}
        std::cout<<'\n';
        if(inspectIndirect){
            const auto pending=registry.inspectStoppedPostOutputSwapSubmission(submission.writeWord);
            std::cout<<"stopped_post_output_swap_submission=[";
            if(pending){const auto& p=*pending;std::cout<<1<<','<<p.rootPhysical<<','<<p.childPhysical<<','<<p.childWords<<','
                <<p.commandOffsetWords<<','<<p.readWord<<','<<p.writeWord;
            }else std::cout<<0;
            std::cout<<"]\n";
            const auto& irq=stoppedPostOutputSwapInterrupt;
            std::cout<<"stopped_post_output_swap_interrupt=[";
            if(irq){std::cout<<1<<','<<irq->wrapper.scratchPhysical()<<','<<irq->wrapper.callbackAddress()<<','
                <<irq->wrapper.callbackContext()<<','<<irq->device<<','<<irq->handler<<','<<irq->frequency;
                for(auto value:irq->deviceValues)std::cout<<','<<value;
                for(auto value:irq->queuedSurfaces)std::cout<<','<<value;
                for(auto value:irq->scratch)std::cout<<','<<value;
                std::optional<unsigned> base,mask;
#if defined(__ANDROID__)
                if(nativeGraphics){base=nativeGraphics->retainedRegister(0x1DD);mask=nativeGraphics->retainedRegister(0x1DC);}
#endif
                std::cout<<','<<base.has_value()<<','<<base.value_or(0)<<','<<mask.has_value()<<','<<mask.value_or(0);
            }else std::cout<<0<<','<<stoppedPostOutputSwapInterruptRejection;
            std::cout<<"]\n";
            if(pending){
                // Stopped writers, still-pinned owned ticket. Raw bounded
                // wrapper words remain private; this does not execute them.
                const auto bytes=registry.snapshotStoppedGraphicsPhysical(pending->childPhysical,pending->childWords*4);
                std::cout<<"stopped_post_output_swap_words=[";
                if(bytes)for(unsigned i=0;i<pending->childWords;++i){std::uint32_t value=0;
                    for(unsigned b=0;b<4;++b)value=(value<<8)|(*bytes)[i*4+b];std::cout<<(i?",":"")<<value;
                }
                std::cout<<"]\n";
            }
        }
    }
    std::optional<recomp::gpu::StoppedSwapRequestScope> postSwapCaptureScope;
    if(livePostOutputColor && execution.reason==recomp::bootstrap::GuestExecutionStopReason::RejectedImport &&
        execution.detail=="__imp__VdSwap" && execution.importArguments){
        recomp::bootstrap::GuestImportCall call;
        call.stackPointer=static_cast<std::uint32_t>(execution.importArguments->stackPointer);
        call.arguments=execution.importArguments->registers;
        const auto nextRequest=registry.inspectStoppedPostSwapRequest(call);
        std::cout<<"stopped_post_swap_request=[";
        if(nextRequest){const auto& s=*nextRequest;
            const auto translation=std::uint64_t{s.commandVirtual}-s.commandPhysical;
            std::optional<std::uint32_t> imagePhysical;
            if(s.frontbufferVirtual>=translation && recomp::gpu::NativeSwapImagePlan::make(s.fetch,s.frontbufferVirtual,
                    static_cast<std::uint32_t>(s.frontbufferVirtual-translation),s.format,s.colorSpace,s.width,s.height))
                imagePhysical=static_cast<std::uint32_t>(s.frontbufferVirtual-translation);
            postSwapCaptureScope=recomp::gpu::StoppedSwapRequestScope::make(s.commandVirtual,s.commandPhysical,
                s.allocationBase,s.allocationBytes,imagePhysical);
            std::cout<<1<<','<<s.commandVirtual<<','<<s.commandPhysical<<','<<s.allocationBase<<','<<s.allocationBytes<<','
                <<s.protection<<','<<s.frontbufferVirtual<<','<<s.format<<','<<s.colorSpace<<','<<s.width<<','<<s.height<<','<<s.identifierWord;
            for(auto word:s.fetch)std::cout<<','<<word;
        }else std::cout<<0;
        std::cout<<"]\n";
        const auto swap=registry.inspectStoppedPostOutputSwapRequest(call);
        std::cout<<"stopped_post_output_swap_request=[";
        if(swap){std::cout<<1<<','<<swap->commandVirtual<<','<<swap->commandPhysical<<','<<swap->allocationBase<<','
            <<swap->allocationBytes<<','<<swap->protection<<','<<swap->frontbufferVirtual<<','<<swap->format<<','
            <<swap->colorSpace<<','<<swap->width<<','<<swap->height<<','<<swap->identifierWord;
            for(auto word:swap->fetch)std::cout<<','<<word;
        }else std::cout<<0;
        std::cout<<"]\n";
        // Writers have joined. Retain the lock through validation and decode;
        // DO NOT invoke any image/presentation callback for this diagnostic.
        std::array<std::uint32_t,10> metadata{};
        const auto context=registry.nativePostOutputSystemContext();
        const bool continuous=registry.visitStoppedPostOutputSwapImage(call,[&](const auto& image){
            std::uint32_t black=0,opaque=0;
            for(std::size_t i=0;i<image.rgba.size();i+=4){
                black+=!(image.rgba[i]|image.rgba[i+1]|image.rgba[i+2]);opaque+=image.rgba[i+3]==255;
            }
            metadata={image.physicalAddress,image.owner.physicalBase,image.owner.virtualBase,image.owner.byteCount,
                image.owner.protection,recomp::gpu::NativeSwapImagePlan::Capacity,static_cast<std::uint32_t>(image.rgba.size()),
                black,opaque,context->readWord};
            return true;
        });
        std::cout<<"stopped_post_output_swap_image=["<<continuous;
        if(continuous)for(auto value:metadata)std::cout<<','<<value;
        std::cout<<"]\n";
    }
    if(nativeNoSystemOverlay){const auto context=registry.nativeSystemContext();
        std::cout<<"native_no_system_overlay=";
        if(context)std::cout<<1<<','<<context->stackPointer<<','<<context->buffer<<','<<context->identifierOutput<<','<<context->readWord<<','<<context->policy;
        else std::cout<<"0,0,0,0,0,0";
        std::cout<<'\n';
        if(execution.reason==recomp::bootstrap::GuestExecutionStopReason::UnimplementedImport &&
            execution.detail=="__imp__VdSwap" && execution.importArguments){
            recomp::bootstrap::GuestImportCall call;
            call.stackPointer=static_cast<std::uint32_t>(execution.importArguments->stackPointer);
            call.arguments=execution.importArguments->registers;
            const auto swap=registry.inspectStoppedNativeSwapRequest(call);
            std::cout<<"stopped_native_swap_request=[";
            if(swap){std::cout<<1<<','<<swap->commandVirtual<<','<<swap->commandPhysical<<','<<swap->allocationBase<<','
                <<swap->allocationBytes<<','<<swap->protection<<','<<swap->frontbufferVirtual<<','<<swap->format<<','
                <<swap->colorSpace<<','<<swap->width<<','<<swap->height<<','<<swap->identifierWord;
                for(auto word:swap->fetch)std::cout<<','<<word;
            }else std::cout<<0;
            std::cout<<"]\n";
            if(captureNativeSwapImage || callbacks){
                // All guest writers have joined. Metadata and snapshot remain
                // read-only; this creates neither a future lease nor GPU work.
                using Plan=recomp::gpu::NativeSwapImagePlan;
                const auto ring=registry.graphicsRingConfiguration();
                std::optional<recomp::bootstrap::GuestImportRegistry::StoppedPhysicalOwnership> owner;
                std::uint32_t physical=0,black=0,opaque=0;
                std::vector<std::uint8_t> rgba;
                bool saved=false;
                bool decoded=false,continuity=false;int accepted=0;
                if(callbacks){
                    // The actual lifecycle lock spans copy-identity validation,
                    // decode AND the consumer. No separately acquired snapshots
                    // stand in for current ownership; no VdSwap return follows.
                    continuity=registry.visitNativeOwnedSwapImage(call,[&](const auto& image){
                        owner=image.owner;physical=image.physicalAddress;rgba=image.rgba;decoded=true;
                        for(std::size_t i=0;i<rgba.size();i+=4){
                            black+=!(rgba[i]|rgba[i+1]|rgba[i+2]);opaque+=rgba[i+3]==255;
                        }
                        accepted=callbacks->stoppedRgba(callbacks->user,rgba.data(),
                            static_cast<std::uint32_t>(rgba.size()),image.request.width,image.request.height);
                        return true; // Continuity validated even if the consumer rejects.
                    });
                }else if(swap && ring && ring->virtualAddress>=ring->physicalAddress &&
                    swap->frontbufferVirtual>=ring->virtualAddress-ring->physicalAddress){
                    physical=swap->frontbufferVirtual-(ring->virtualAddress-ring->physicalAddress);
                    const auto plan=Plan::make(swap->fetch,swap->frontbufferVirtual,physical,swap->format,
                        swap->colorSpace,swap->width,swap->height);
                    if(plan)owner=registry.stoppedGraphicsPhysicalOwnership(physical,Plan::Capacity);
                    if(owner && owner->writable && owner->physicalBase<=physical &&
                        std::uint64_t{physical}+Plan::Capacity<=std::uint64_t{owner->physicalBase}+owner->byteCount &&
                        std::uint64_t{owner->virtualBase}+physical-owner->physicalBase==swap->frontbufferVirtual){
                        const auto tiled=registry.snapshotStoppedGraphicsPhysical(physical,Plan::Capacity);
                        if(tiled && plan->decode(tiled->data(),tiled->size(),rgba)){
                            decoded=true;
                            for(std::size_t i=0;i<rgba.size();i+=4){
                                black+=!(rgba[i]|rgba[i+1]|rgba[i+2]);opaque+=rgba[i+3]==255;
                            }
                            if(captureNativeSwapImage)saved=recomp::trace::saveNativeSwapImage(argv[0],rgba);
                        }
                    }
                }
                std::cout<<(callbacks?"stopped_native_swap_consumer=[":"stopped_native_swap_image=[");
                if(callbacks?decoded:saved)std::cout<<1<<','<<physical<<','<<owner->physicalBase<<','<<owner->virtualBase<<','<<owner->byteCount<<','
                    <<owner->protection<<','<<Plan::Capacity<<','<<rgba.size()<<','<<black<<','<<opaque;
                else std::cout<<0;
                if(callbacks && decoded)std::cout<<','<<(accepted==1);
                std::cout<<"]\n";
                if(callbacks){
                    std::cout<<"stopped_native_swap_continuity=[";
                    if(continuity)std::cout<<1<<','<<ring->readWord<<','<<Plan::Capacity<<','<<Plan::PixelBytes;
                    else std::cout<<0;
                    std::cout<<"]\n";
                }
            }
        }
    }
    if (cooperative) {
        const auto s = registry.threadDelayStatistics();
        std::cout << "thread_delays=" << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3]
                  << ',' << s[4] << ',' << s[5] << ',' << s[6] << '\n';
    }
#if defined(__ANDROID__)
    if (experimentalNativeAudio) {
        const auto c=nativeAudio->configuration();
        if (!c || (!nativeAudioRenderer && (nativeAudio->started() || nativeAudio->framesWritten()!=0 || nativeAudio->framesRead()!=0))) {
            std::cerr << "trace evidence failed: configuration-only audio evidence no longer applies\n"; return 2;
        }
        std::cout << "experimental_native_audio=" << c->sampleRate << ',' << c->channels << ','
            << c->bufferCapacityFrames << ',' << c->framesPerBurst << ',' << registry.audioSpeakerQueries() << '\n';
        if (nativeAudioRenderer) {
            const auto stats=nativeAudioRenderer->statistics();const auto read=nativeAudio->framesRead();
            if (!read || nativeAudio->framesWritten()!=stats.writtenFrames) {
                std::cerr << "trace evidence failed: native renderer stream counter mismatch\n";return 2;
            }
            std::cout << "native_audio_renderer=" << stats.workerToken << ',' << stats.callbacksEntered << ','
                << stats.callbacksCompleted << ',' << stats.acceptedPackets << ',' << stats.writtenFrames << ','
                << stats.serviceReadFrames << ',' << stats.pendingPackets << ',' << stats.clippedSamples << ','
                << stats.startRequested << ',' << stats.callbackActive << ',' << stats.failed << ','
                << *read << ',' << nativeAudio->started() << '\n';
        }
    }
#endif
    if (const auto memory = registry.lastMemoryStatistics()) {
        std::cout << "native_memory_statistics=" << (*memory)[1] << ',' << (*memory)[3] << ','
                  << (*memory)[4] << ',' << (*memory)[5] << ',' << (*memory)[6] << ','
                  << (*memory)[11] << ',' << (*memory)[25] << '\n';
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::InvalidIndirectTarget && rejectedIndirect) {
        std::cout << "invalid_indirect_call=" << rejectedIndirect->target << ',' << rejectedIndirect->linkRegister
                  << ',' << rejectedIndirect->stackPointer << '\n';
        std::cout << "invalid_indirect_registers=";
        for (auto value : rejectedIndirect->registers) std::cout << value << ',';
        std::cout << '\n';
        // Writers are joined. Inspect only bounded live heap/graphics storage or
        // the verified loaded image, never an arbitrary flat-address-space read.
        for (auto index : {0U, 2U, 5U, 6U, 7U}) {
            const auto address = rejectedIndirect->registers[index];
            constexpr std::uint32_t bytes = 160;
            auto snapshot = registry.snapshotStoppedGuestHeap(address, bytes);
            if (!snapshot) snapshot = registry.snapshotStoppedGraphicsVirtual(address, bytes);
            if (!snapshot && address >= imageBase && std::uint64_t{address} + bytes <= std::uint64_t{imageBase} + titleValues[1]) {
                const auto* begin = addressSpace.translate(address);
                snapshot = std::vector<std::uint8_t>(begin, begin + bytes);
            }
            std::cout << "invalid_indirect_object=" << index << ',' << address << ',' << bool(snapshot);
            if (snapshot) for (std::size_t offset = 0; offset < snapshot->size(); offset += 4) {
                const auto* p = snapshot->data() + offset;
                std::cout << ',' << ((std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16)
                                    | (std::uint32_t{p[2]} << 8) | p[3]);
            }
            std::cout << '\n';
        }
    }
#if defined(__ANDROID__)
    if (nativeGraphics && nativeGraphics->displayConfiguration()) {
        const auto& dimensions = nativeGraphics->displayConfiguration()->dimensions;
        std::cout << "native_display_dimensions=" << dimensions[0] << ',' << dimensions[1] << ','
                  << dimensions[2] << ',' << dimensions[3] << '\n';
    }
#endif
    if (experimentalGpuClock) std::cout << "experimental_gpu_clock_500mhz_reads=" << experimentalClockReads << '\n';
    std::cout << "cpu_timebase_frequency=" << clockProbe.returnValue << '\n';
    std::cout << "cpu_timebase_reads=" << recomp::bootstrap::guestTimebaseReadCount() << '\n';
    if (nativeInitialCoherency) {
        if (const auto completed = registry.completedNativeInitialCoherency())
            std::cout << "native_initial_coherency=1," << completed->physicalBase << ',' << completed->byteCount << ',' << completed->cacheFlags << '\n';
        else std::cout << "native_initial_coherency=0,0,0,0\n";
    }
    if (experimentalNativeInitialization) {
        const auto completed = registry.completedExperimentalNativeInitialization();
        if (completed) std::cout << "experimental_native_initialization=1," << completed->readWord << ','
            << completed->draws << ',' << completed->words << ',' << completed->registers << '\n';
        else std::cout << "experimental_native_initialization=0,0,0,0,0\n";
    }
    if (experimentalNativeInitialization && hasHsioLock) {
        const auto state = registry.nativeEdramTrainingState();
        std::cout << "native_edram_training_policy=" << state.workerChecks << ',' << state.noCommandQueries << ',' << state.statusQueries << ',' << state.directNoCommandQueries << '\n';
    }
    if (nativeEmptyCacheCoherency) {
        if (const auto state=registry.completedWarmEmptyCacheCoherency())
            std::cout << "native_warm_empty_cache_coherency=" << state->batches << ',' << state->readWord << ','
                << state->request.physicalBase << ',' << state->request.byteCount << ',' << state->request.cacheFlags << '\n';
        if (const auto state=registry.completedEmptyGuestCacheCoherency())
            std::cout << "native_empty_cache_coherency=" << state->batches << ',' << state->readWord << ','
                << state->request.physicalBase << ',' << state->request.byteCount << ',' << state->request.cacheFlags << '\n';
        else std::cout << "native_empty_cache_coherency=0,0,0,0,0\n";
    }
    if (nativeStateBatches) {
        for(bool warm:{false,true}) {
            const auto state=warm?registry.completedWarmStateBatches():registry.completedNativeStateBatches();
            if(state && state->maintenanceBatches)
                std::cout << "native_maintenance_points=" << unsigned(warm) << ',' << state->maintenanceBatches << ','
                    << state->maintenanceDraws << ',' << state->readWord << '\n';
        }
        if (const auto state=registry.completedWarmStateBatches())
            std::cout << "native_warm_state_batches=" << state->batches << ',' << state->readWord << ',' << state->words << ','
                << state->stateWrites << ',' << state->invalidations << '\n';
        if (const auto warm=registry.completedWarmRingCoherency())
            std::cout << "native_ring_warm_coherency=1," << warm->physicalBase << ',' << warm->byteCount << ',' << warm->cacheFlags << '\n';
        if (const auto state=registry.completedNativeStateBatches())
            std::cout << "native_state_batches=" << state->batches << ',' << state->readWord << ',' << state->words << ','
                << state->stateWrites << ',' << state->invalidations << '\n';
        else std::cout << "native_state_batches=0,0,0,0,0\n";
#if defined(__ANDROID__)
        if (nativeGraphics) {
            const auto& state=nativeGraphics->stateBatchSnapshot();
            std::cout << "native_bin_state=" << static_cast<std::uint32_t>(state.binMask) << ','
                << static_cast<std::uint32_t>(state.binMask>>32) << ','
                << static_cast<std::uint32_t>(state.binSelect) << ','
                << static_cast<std::uint32_t>(state.binSelect>>32) << ',' << state.knownBinWords << '\n';
        }
#endif
    }
    if (submission.status != recomp::bootstrap::StoppedGpuSubmissionStatus::NotSubmission) {
        std::cout << "gpu_submission=" << static_cast<unsigned>(submission.status) << ','
                  << static_cast<unsigned>(submission.error.failure) << ',' << submission.error.offsetWords << ','
                  << submission.error.opcode << ',' << submission.readWord << ',' << submission.writeWord << ','
                  << submission.snapshot.words().size() << ',' << submission.snapshot.packets().size() << '\n';
        const auto& packets = submission.snapshot.packets();
        for (std::size_t i = 0; i < std::min<std::size_t>(packets.size(), 64); ++i) {
            const auto& p = packets[i];
            std::cout << "gpu_packet=" << p.offsetWords << ',' << static_cast<unsigned>(p.kind) << ','
                      << p.payloadWords << ',' << p.opcode << ',' << p.firstRegister << ',' << p.secondRegister << ','
                      << p.predicated << ',' << p.repeatRegister << '\n';
        }
    }
    if (inspectIndirect) {
        // JSON arrays of bounded metadata only: no raw command words or paths.
        std::cout << "gpu_indirect_graph=[" << static_cast<unsigned>(indirectError.failure) << ','
                  << indirectError.parent << ',' << indirectError.offsetWords << ','
                  << static_cast<unsigned>(indirectError.decode.failure) << ',' << indirectError.decode.offsetWords << ','
                  << indirectError.decode.opcode << ',' << indirectGraph.totalWords << ',' << indirectGraph.totalPackets << ",[";
        std::size_t remainingPackets = 64;
        for (std::size_t i = 0; i < indirectGraph.nodes.size(); ++i) {
            const auto& n = indirectGraph.nodes[i];
            if (i) std::cout << ',';
            std::cout << '[' << n.parent << ',' << n.parentOffsetWords << ',' << n.physicalAddress << ',' << n.depth << ','
                      << n.snapshot.words().size() << ',' << n.snapshot.packets().size() << ",[";
            const auto count = std::min(remainingPackets, n.snapshot.packets().size());
            for (std::size_t j = 0; j < count; ++j) {
                const auto& p = n.snapshot.packets()[j];
                if (j) std::cout << ',';
                std::cout << '[' << p.offsetWords << ',' << static_cast<unsigned>(p.kind) << ',' << p.payloadWords << ','
                          << p.opcode << ',' << p.firstRegister << ',' << p.secondRegister << ',' << p.predicated << ',' << p.repeatRegister << ']';
            }
            remainingPackets -= count;
            std::cout << "]]";
        }
        std::cout << "]]\n";
    }
    if (inspectWork) {
        std::cout << "gpu_stopped_work=[" << stoppedWorkError.kind << ',' << stoppedWorkError.node << ','
                  << static_cast<unsigned>(stoppedWorkError.error.failure) << ',' << stoppedWorkError.error.offsetWords << ','
                  << stoppedWorkError.error.opcode << ",[";
        for (std::size_t i = 0; i < stoppedWork.shaders.size(); ++i) {
            const auto& s = stoppedWork.shaders[i];
            if (i) std::cout << ',';
            std::cout << '[' << s.node << ',' << s.load.packetOffsetWords << ',' << s.load.stage << ','
                      << s.load.instructionWords << ',' << s.load.immediate << ',' << s.load.physicalAddress << ']';
        }
        std::cout << "],[";
        for (std::size_t i = 0; i < stoppedWork.draws.size(); ++i) {
            const auto& d = stoppedWork.draws[i];
            if (i) std::cout << ',';
            std::cout << '[' << d.node << ',' << d.draw.packetOffsetWords << ',' << d.draw.opcode << ','
                      << d.draw.indexCount << ',' << d.draw.primitive << ',' << d.draw.source << ',' << d.draw.majorMode << ','
                      << d.draw.index32 << ',' << d.draw.notEndOfPacket << ',' << d.draw.conditionalVizQuery << ',' << d.draw.predicated << ']';
        }
        std::cout << "]]\n";
    }
    if (inspectWork && !stoppedWorkError.kind) {
        std::cout << "gpu_stopped_shader_proofs=[";
        for (std::size_t i = 0; i < stoppedWork.shaders.size(); ++i) {
            const auto& s = stoppedWork.shaders[i];
            recomp::gpu::CopyShaderProof proof; recomp::gpu::CopyShaderError error;
            const auto& bytes = s.load.inlineBigEndianBytes;
            (void)recomp::gpu::analyzeCopyShader(s.load.immediate ? bytes.data() : nullptr,bytes.size(),
                static_cast<recomp::gpu::CopyShaderStage>(s.load.stage),proof,error);
            if (i) std::cout << ',';
            std::cout << '[' << s.node << ',' << s.load.packetOffsetWords << ',' << s.load.stage << ','
                << static_cast<unsigned>(error.failure) << ',' << error.index << ',' << proof.controlCount << ','
                << proof.executedCount << ',' << proof.exported.count() << ',' << proof.initialComponentsRead.count() << ']';
        }
        std::cout << "]\n";
    }
    if (inspectWork) {
        std::cout << "gpu_stopped_memory=[" << static_cast<unsigned>(stoppedMemoryError.failure) << ','
                  << stoppedMemoryError.node << ',' << stoppedMemoryError.offsetWords << ','
                  << stoppedMemory.unmodeledScratchMaskBits << ',' << stoppedMemory.otherRegisterWrites << ','
                  << stoppedMemory.drawRequests << ",[";
        const auto ring = registry.graphicsRingConfiguration();
        for (std::size_t i = 0; i < stoppedMemory.writes.size(); ++i) {
            const auto& w = stoppedMemory.writes[i];
            const auto overlap = [&](std::uint32_t address, std::size_t bytes) {
                return w.physicalAddress < std::uint64_t{address} + bytes && address < std::uint64_t{w.physicalAddress} + 4;
            };
            bool aliasesCommands = ring && (overlap(ring->physicalAddress,ring->bytes) ||
                (ring->writeBack && overlap(ring->writeBack->physicalAddress,4)));
            for (const auto& n : indirectGraph.nodes) aliasesCommands |= overlap(n.physicalAddress,n.snapshot.words().size()*4);
            aliasesCommands |= registry.stoppedGraphicsWordAliasesIdentifier(w.physicalAddress);
            if (i) std::cout << ',';
            std::cout << '[' << w.node << ',' << w.offsetWords << ',' << w.kind << ',' << w.physicalAddress << ','
                      << w.value << ',' << w.priorDrawRequests << ',' << registry.stoppedGraphicsWordIsWritable(w.physicalAddress)
                      << ',' << aliasesCommands << ',' << w.bigEndianProfile << ']';
        }
        std::cout << "]]\n";
        recomp::gpu::StoppedWordStaging staging;
        if (recomp::gpu::planStoppedWordStaging(stoppedMemory,staging)) {
            std::size_t before = 0;
            for (std::size_t i = 0; i < staging.updates.count; ++i)
                before += staging.updates.words[i].phase == recomp::gpu::NativeWordPhase::BeforeGraphics;
            std::cout << "gpu_stopped_word_staging=" << before << ',' << staging.updates.count - before
                      << ',' << staging.destinationCount << ',' << staging.updates.bufferBytes << '\n';
        }
    }
    if (inspectWork) {
        recomp::gpu::StoppedConstantInitialization plan; recomp::gpu::StoppedConstantError error;
        (void)recomp::gpu::inspectStoppedConstantInitialization(submission.snapshot,indirectGraph,plan,error);
        std::size_t incomplete = 0;
        for (const auto& item : plan.registers) incomplete += item.second.known != 0xFFFFFFFF;
        std::cout << "gpu_stopped_constant_initialization=[" << static_cast<unsigned>(error.failure) << ','
            << error.node << ',' << error.offsetWords << ',' << plan.draws.size() << ',' << plan.shaderLoads << ','
            << plan.invalidations << ',' << plan.registers.size() << ',' << plan.otherRegisterWrites << ',' << incomplete << ",[";
        std::size_t retained = 0;
        for (const auto& item : plan.registers) if (item.second.known != 0xFFFFFFFF && retained < 16) {
            if (retained++) std::cout << ',';
            std::cout << '[' << item.first << ',' << item.second.value << ',' << item.second.known << ']';
        }
        std::cout << "],[";
        for (std::size_t i = 0; i < plan.draws.size(); ++i) {
            const auto& d = plan.draws[i];
            unsigned position = 0, parameter = 0;
            for (unsigned c = 0; c < 4; ++c) { position |= d.pair.position[c] << c; parameter |= d.pair.parameter[c] << c; }
            if (i) std::cout << ',';
            std::cout << '[' << d.node << ',' << d.offsetWords << ',' << position << ',' << parameter << ']';
        }
        std::cout << "]]\n";
    }
    if (captureStoppedShaders) {
        std::vector<std::vector<std::uint8_t>> captured;
        const unsigned status = !inspectWork || stoppedWorkError.kind ? 0 :
            (recomp::gpu::captureStoppedShaderBytes(stoppedWork,
                [&](auto address, auto bytes) { return registry.snapshotStoppedGraphicsPhysical(address, bytes); },
                captured) ? 1 : 2);
        std::cout << "stopped_shader_capture=" << status << '\n';
        if (status == 1) for (std::size_t i = 0; i < stoppedWork.shaders.size(); ++i) {
            const auto& s = stoppedWork.shaders[i];
            std::cout << "stopped_shader_blob=" << s.node << ',' << s.load.packetOffsetWords << ',' << s.load.stage << ',';
            // Explicit physical provenance; legacy three-field records mean
            // inline only. Never pass external bytes off as inline proof.
            if (!s.load.immediate) std::cout << "0," << s.load.physicalAddress << ',';
            constexpr char digits[] = "0123456789abcdef";
            for (auto byte : captured[i]) std::cout << digits[byte >> 4] << digits[byte & 15];
            std::cout << '\n';
        }
    }
    if (captureStoppedPm4) {
        const unsigned status = !inspectWork ? 0 :
            (indirectGraph.nodes.size() > 16 || indirectGraph.totalWords > 4096 ? 2 : 1);
        std::cout << "stopped_pm4_capture=" << status << '\n';
        if (status == 1) for (std::size_t id = 0; id <= indirectGraph.nodes.size(); ++id) {
            const auto& snapshot = id ? indirectGraph.nodes[id - 1].snapshot : submission.snapshot;
            std::cout << "stopped_pm4_blob=" << id << ',';
            constexpr char digits[] = "0123456789abcdef";
            for (auto word : snapshot.words()) for (unsigned i = 0; i < 8; ++i)
                std::cout << digits[(word >> (28 - i * 4)) & 15];
            std::cout << '\n';
        }
        // The native envelope is NOT retail PM4. An explicit command capture
        // can snapshot its CURRENT owned root/child separately, without
        // guessing addresses across runs or making the failed graph valid.
        unsigned nativeStatus=0;
        recomp::gpu::StoppedPhysicalRanges nativeRanges;
        std::vector<std::vector<std::uint8_t>> nativeBytes;
        if(deferredPendingSubmission && capturePhysicalRanges.empty()){
            const auto& p=*deferredPendingSubmission;
            nativeStatus=2;
            if(p.childWords<=4093){
                nativeRanges={{{p.rootPhysical,12}},{{p.childPhysical,p.childWords*4}}};
                if(recomp::gpu::captureStoppedPhysicalRanges(nativeRanges,
                    [&](auto address,auto count){return registry.snapshotStoppedGraphicsPhysical(address,count);},nativeBytes,16384))nativeStatus=1;
            }
        }
        std::cout<<"stopped_native_pending_capture="<<nativeStatus<<"\nstopped_native_pending_ownership=[";
        if(nativeStatus==1)for(unsigned i=0;i<2;++i){
            if(i)std::cout<<',';
            const auto owner=registry.stoppedGraphicsPhysicalOwnership(nativeRanges[i][0],nativeRanges[i][1]);
            if(!owner)std::cout<<"null";
            else std::cout<<'['<<owner->physicalBase<<','<<owner->virtualBase<<','<<owner->byteCount<<','<<owner->protection<<','<<owner->writable<<']';
        }
        std::cout<<"]\n";
        if(nativeStatus==1)for(unsigned i=0;i<2;++i){
            std::cout<<"stopped_native_pending_blob="<<nativeRanges[i][0]<<','<<nativeRanges[i][1]<<',';
            constexpr char digits[]="0123456789abcdef";
            for(auto b:nativeBytes[i])std::cout<<digits[b>>4]<<digits[b&15];
            std::cout<<'\n';
        }
        if(captureStoppedShaders){
            // Inspect the SAME owned child just captured, never reinterpret
            // the failed retail graph as successful work. No image pixel read,
            // execution, event publication or ring retirement is authorized.
            unsigned shaderStatus=0;recomp::gpu::NativeDeferredShaderCapture shaders;
#if defined(__ANDROID__)
            std::array<std::uint32_t,10> colorInspection{};
            const auto ring=registry.graphicsRingConfiguration();colorInspection[9]=ring?ring->readWord:0;
            std::array<std::uint32_t,4> memoryInspection{{0,0,0,colorInspection[9]}};
            std::array<std::uint32_t,9> gpuInspection{{0,0,0,0,0,0,0,0,colorInspection[9]}};
            std::array<std::uint32_t,9> prefixTimeline{{0,0,0,0,0,0,0,0,colorInspection[9]}};
            std::array<std::uint32_t,9> pendingStateInspection{{0,0,0,0,0,0,0,0,colorInspection[9]}};
            std::array<std::uint32_t,4> writeGuardInspection{{0,0,0,colorInspection[9]}};
#endif
            if(nativeStatus==1){
                shaderStatus=2;
                try{
                    if(const auto request=registry.nativeDeferredSwapRequest()){
                        const auto command=recomp::gpu::NativeSwapCommand::make({3,request->request.commandPhysical,
                            request->imagePhysical,request->issuedReadWord});
                        if(command){
                            const auto frame=recomp::gpu::NativeDeferredSwapSubmission::parse(*command,
                                nativeRanges[1][0],nativeBytes[1].data(),nativeBytes[1].size());
                            if(frame && recomp::gpu::captureNativeDeferredPrefixShaders(*frame,
                                [&](auto address,auto count){return registry.snapshotStoppedGraphicsPhysical(address,count);},shaders)){
                                shaderStatus=1;
#if defined(__ANDROID__)
                                if(inspectColorProgram && experimentalOwnedColor && nativeDisplayOutput && nativeGraphics){
                                    const auto state=nativeGraphics->stateBatchSnapshot();
                                    const recomp::gpu::DrawBinState bins{{{static_cast<std::uint32_t>(state.binMask),static_cast<std::uint32_t>(state.binMask>>32),
                                        static_cast<std::uint32_t>(state.binSelect),static_cast<std::uint32_t>(state.binSelect>>32)}},state.knownBinWords};
                                    colorInspection[0]=2;
                                    const auto color=recomp::gpu::NativeDeferredColorCapture::create(*frame,bins,*inspectColorProgram,
                                        [&](auto address,auto count){return registry.snapshotStoppedGraphicsPhysical(address,count);},&colorInspection[1]);
                                    if(color){colorInspection={{1,0,frame->swapOffsetWords(),static_cast<unsigned>(color->commands().draws.size()),
                                        static_cast<unsigned>(color->commands().shaders.size()),static_cast<unsigned>(color->commands().constants.size()),
                                        color->sourceBytes(),color->copy().copy.destinationBase,color->color().sourceRanges().constantZeroTextures,
                                        colorInspection[9]}};
#if defined(RECOMP_RENDERER_BOUNDARY_TRACE)
                compareRendererInlineInputs(color->color(),nativeGraphics.get(),inspectColorProgram?&*inspectColorProgram:nullptr);
#endif
                                        memoryInspection[0]=2;
                                        // All guest workers are JOINED. Resolve current allocation
                                        // metadata only; no future pin or write permission escapes.
                                        const auto identifier=registry.graphicsSystemIdentifierAddress();
                                        const auto previous=registry.completedNativePostOutputWrappedSwap();
                                        if(ring && ring->writeBack && identifier && previous && ring->virtualAddress>=ring->physicalAddress){
                                            const auto translation=ring->virtualAddress-ring->physicalAddress;
                                            if(*identifier>=translation){
                                                const auto fullWords=frame->swapOffsetWords()+recomp::gpu::NativeDeferredSwapWrapper::Words;
                                                const recomp::gpu::NativeColorMemoryControls controls{{ring->physicalAddress,ring->bytes},
                                                    {frame->physicalAddress(),fullWords*4},{ring->writeBack->physicalAddress,4},{*identifier-translation,4},
                                                    ring->readWord,submission.writeWord,{{0xC0013F00,frame->physicalAddress(),fullWords}}};
                                                const auto old=previous->imageOwner.physicalBase+
                                                    (previous->imageRequest.frontbufferVirtual-previous->imageOwner.virtualBase);
                                                const auto memoryPlan=recomp::gpu::NativeDeferredColorMemoryPlan::create(*color,controls,
                                                    {old,recomp::gpu::NativeSwapImagePlan::Capacity});
                                                if(memoryPlan){
                                                    recomp::gpu::NativeDeferredColorResolvedMemory memory;bool owned=true;
                                                    const auto resolve=[&](recomp::gpu::NativePhysicalRange range,recomp::gpu::NativeColorMappedRange& mapped){
                                                        const auto owner=registry.stoppedGraphicsPhysicalOwnership(range.address,range.bytes);
                                                        if(!owner){owned=false;return;}
                                                        mapped={range,{owner->physicalBase,owner->byteCount},addressSpace.translate(owner->virtualBase),owner->writable};
                                                    };
                                                    for(unsigned i=0;i<memoryPlan->inputCount();++i)resolve(memoryPlan->inputs()[i],memory.inputs[i]);
                                                    for(unsigned i=0;i<5;++i)resolve(memoryPlan->protectedRanges()[i],memory.protectedRanges[i]);
                                                    for(unsigned i=0;i<7;++i)resolve(memoryPlan->writes()[i],memory.writes[i]);
                                                    if(owned && recomp::gpu::matchesNativeDeferredColorMemory(*color,*memoryPlan,memory)){
                                                        memoryInspection={{1,memoryPlan->inputCount(),7,ring->readWord}};
                                                        if(diagnoseDeferredColor){
                                                            // Snapshot all CURRENT before-images with guest
                                                            // workers joined. Inspect only: never call the
                                                            // publication method on this stopped diagnostic.
                                                            auto writeGuard=recomp::gpu::NativeDeferredColorWriteGuard::create(*color,*memoryPlan,memory);
                                                            writeGuardInspection[0]=2;
                                                            recomp::gpu::NativeDeferredColorDiagnosticResult rendered;
                                                            const bool complete=nativeGraphics->executeStoppedDeferredColorDiagnostic(*inspectColorProgram,*color,rendered);
                                                            const auto after=nativeGraphics->stateBatchSnapshot();
                                                            const bool counters=state.binMask==after.binMask && state.binSelect==after.binSelect &&
                                                                state.invalidationEpoch==after.invalidationEpoch && state.experimentalIdleCachePairEpoch==after.experimentalIdleCachePairEpoch &&
                                                                state.knownBinWords==after.knownBinWords && state.batches==after.batches &&
                                                                state.experimentalColorBatches==after.experimentalColorBatches;
                                                            const bool same=recomp::gpu::matchesNativeDeferredColorMemory(*color,*memoryPlan,memory);
                                                            recomp::gpu::NativeDeferredColorDiagnosticResult repeated;repeated.nativeFences=99;
                                                            unsigned forbiddenPublications=0;
                                                            const recomp::gpu::NativeDeferredColorPublisher forbidden{&forbiddenPublications,[](void* p,const auto&) noexcept{
                                                                ++*static_cast<unsigned*>(p);return false;}};
                                                            const bool once=complete && !nativeGraphics->executeStoppedDeferredColorDiagnostic(*inspectColorProgram,*color,repeated) &&
                                                                repeated.nativeFences==99 && !repeated.copy && !repeated.edram && !repeated.auditedPrefixTimeline &&
                                                                !repeated.pendingRegisters && !repeated.pendingState &&
                                                                !nativeGraphics->executeExperimentalDeferredColorPrefix(*inspectColorProgram,*color,forbidden) && !forbiddenPublications;
                                                            gpuInspection={{2,0,0,0,0,unsigned(same),unsigned(counters),unsigned(once),ring->readWord}};
                                                            if(complete && same && counters && once && rendered.copy && rendered.edram && rendered.auditedPrefixTimeline &&
                                                                rendered.pendingRegisters && rendered.pendingState &&
                                                                rendered.copy->matchesPlan(color->copy().copy))gpuInspection={{1,rendered.leadingSetupFences,rendered.nativeFences,
                                                                    static_cast<unsigned>(rendered.edram->knownBytes().size()),rendered.copy->pixelBytes(),1,1,1,ring->readWord}};
                                                            if(gpuInspection[0]==1){const auto& t=*rendered.auditedPrefixTimeline;
                                                                prefixTimeline={{1,t.leadingFences(),t.completedStages(),t.invalidations(),t.copyIdleCleans(),
                                                                    t.depthWaits(),t.cacheFlushes(),t.literalEvents(),ring->readWord}};
                                                                const auto& p=*rendered.pendingState;unsigned unknown=0;
                                                                for(unsigned reg:{0x5C8u,0xF01u,0xA2Fu,0xA30u,0xA31u,0x21F9u})unknown+=!rendered.pendingRegisters->count(reg);
                                                                pendingStateInspection={{1,static_cast<unsigned>(rendered.pendingRegisters->size()),unknown,
                                                                    static_cast<unsigned>(p.invalidationEpoch-state.invalidationEpoch),p.batches-state.batches,
                                                                    p.experimentalColorBatches-state.experimentalColorBatches,
                                                                    unsigned(p.experimentalIdleCachePairEpoch==state.experimentalIdleCachePairEpoch),
                                                                    unsigned(color->initialBins().words==bins.words && color->initialBins().knownWords==bins.knownWords),ring->readWord}};
                                                                if(writeGuard && !writeGuard->published() && writeGuard->matches(*color,memory))
                                                                    writeGuardInspection={{1,static_cast<unsigned>(writeGuard->snapshotBytes()),memoryPlan->inputCount()+12,ring->readWord}};
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
#endif
                            }
                        }
                    }
                }catch(...){shaderStatus=2;}
            }
            std::cout<<"stopped_deferred_shader_capture="<<shaderStatus<<'\n';
            if(shaderStatus==1)for(std::size_t i=0;i<shaders.loads.size();++i){
                const auto& s=shaders.loads[i];
                std::cout<<"stopped_deferred_shader_blob="<<s.node<<','<<s.load.packetOffsetWords<<','<<s.load.stage<<',';
                if(!s.load.immediate)std::cout<<"0,"<<s.load.physicalAddress<<',';
                constexpr char digits[]="0123456789abcdef";
                for(auto b:shaders.bytes[i])std::cout<<digits[b>>4]<<digits[b&15];
                std::cout<<'\n';
            }
#if defined(__ANDROID__)
            if(inspectColorProgram && experimentalOwnedColor && nativeDisplayOutput){
                std::cout<<"stopped_deferred_color_inspection=[";
                for(unsigned i=0;i<colorInspection.size();++i)std::cout<<(i?",":"")<<colorInspection[i];
                std::cout<<"]\n";
                std::cout<<"stopped_deferred_memory_inspection=[";
                for(unsigned i=0;i<memoryInspection.size();++i)std::cout<<(i?",":"")<<memoryInspection[i];
                std::cout<<"]\n";
                if(diagnoseDeferredColor){
                    std::cout<<"stopped_deferred_color_gpu=[";
                    for(unsigned i=0;i<gpuInspection.size();++i)std::cout<<(i?",":"")<<gpuInspection[i];
                    std::cout<<"]\n";
                    std::cout<<"stopped_deferred_color_timeline=[";
                    for(unsigned i=0;i<prefixTimeline.size();++i)std::cout<<(i?",":"")<<prefixTimeline[i];
                    std::cout<<"]\n";
                    std::cout<<"stopped_deferred_color_pending_state=[";
                    for(unsigned i=0;i<pendingStateInspection.size();++i)std::cout<<(i?",":"")<<pendingStateInspection[i];
                    std::cout<<"]\n";
                    std::cout<<"stopped_deferred_color_write_guard=[";
                    for(unsigned i=0;i<writeGuardInspection.size();++i)std::cout<<(i?",":"")<<writeGuardInspection[i];
                    std::cout<<"]\n";
                }
            }
#endif
        }
    }
#if defined(__ANDROID__)
    if(inspectColorProgram && experimentalOwnedColor && nativeDisplayOutput){
        // Stopped, read-only diagnostic of the next batch. This does NOT use
        // the one-shot live publisher, advance the queue, or export textures.
        unsigned status=0;std::array<std::uint32_t,6> counts{};std::array<std::uint32_t,3> nonzero{};unsigned literals=0;
        if(inspectWork && nativeGraphics){
            const auto state=nativeGraphics->stateBatchSnapshot();
            const recomp::gpu::DrawBinState bins{{{static_cast<std::uint32_t>(state.binMask),static_cast<std::uint32_t>(state.binMask>>32),
                static_cast<std::uint32_t>(state.binSelect),static_cast<std::uint32_t>(state.binSelect>>32)}},state.knownBinWords};
            status=registry.inspectStoppedPostOutputColorBatch(*inspectColorProgram,bins,submission.writeWord,[&](const auto& capture){
                recomp::gpu::NativeColorChainBindings bindings;if(!capture.color().bindings(bindings))return false;
                std::array<std::uint32_t,3> observed{};
                for(const auto& texture:bindings.pairs[0].textures){
                    if(texture.slot>=3 || !texture.source.bytes)return false;
                    observed[texture.slot]=static_cast<std::uint32_t>(std::count_if(texture.source.bytes,
                        texture.source.bytes+texture.source.size,[](auto byte){return byte!=0;}));
                }
                counts={{static_cast<std::uint32_t>(capture.commands().commands.words().size()),static_cast<std::uint32_t>(capture.commands().shaders.size()),
                    static_cast<std::uint32_t>(capture.commands().draws.size()),capture.memory().inputCount,
                    capture.controls().previousWord,capture.controls().nextWord}};
                nonzero=observed;literals=capture.color().sourceRanges().constantZeroTextures;return true;
            })?1:2;
        }
        const auto ring=registry.graphicsRingConfiguration();
        std::cout<<"stopped_post_output_color_inspection="<<status;
        for(auto count:counts)std::cout<<','<<count;
        std::cout<<','<<(ring?ring->readWord:0)<<','<<literals;
        for(auto count:nonzero)std::cout<<','<<count;
        std::cout<<'\n';
        if(diagnosePostOutputColor){
            recomp::gpu::NativeColorChainExecutionResult rendered;bool complete=false,unchanged=true;
            if(status==1 && nativeGraphics){
                const auto before=nativeGraphics->stateBatchSnapshot();
                const recomp::gpu::DrawBinState bins{{{static_cast<std::uint32_t>(before.binMask),static_cast<std::uint32_t>(before.binMask>>32),
                    static_cast<std::uint32_t>(before.binSelect),static_cast<std::uint32_t>(before.binSelect>>32)}},before.knownBinWords};
                complete=registry.runStoppedPostOutputColorDiagnostic(*inspectColorProgram,bins,submission.writeWord,
                    [&](const auto& capture){return nativeGraphics->executeStoppedPostOutputColorDiagnostic(*inspectColorProgram,capture,rendered);});
                const auto after=nativeGraphics->stateBatchSnapshot();const auto finalRing=registry.graphicsRingConfiguration();
                unchanged=after.binMask==before.binMask && after.binSelect==before.binSelect && after.knownBinWords==before.knownBinWords &&
                    after.batches==before.batches && after.invalidationEpoch==before.invalidationEpoch &&
                    after.experimentalColorBatches==before.experimentalColorBatches && after.experimentalIdleCachePairEpoch==before.experimentalIdleCachePairEpoch &&
                    finalRing && ring && finalRing->readWord==ring->readWord;
                if(!unchanged)std::_Exit(4);
            }
            std::cout<<"stopped_post_output_color_gpu="<<unsigned(complete)<<','<<rendered.leadingSetupFences<<','<<rendered.nativeFences<<','
                <<(rendered.edram?rendered.edram->knownBytes().size():0)<<','<<(rendered.copy?rendered.copy->pixelBytes():0)<<','
                <<unsigned(unchanged)<<','<<(ring?ring->readWord:0)<<'\n';
        }
    }
    if(inspectColorProgram && !experimentalOwnedColor){
        unsigned status=0;std::array<std::uint32_t,6> counts{};
        std::unique_ptr<recomp::gpu::NativeColorBatchCapture> owned;
        recomp::gpu::DrawBinState initialColorBins;
        if(inspectWork && nativeGraphics){
            const auto state=nativeGraphics->stateBatchSnapshot();
            const recomp::gpu::DrawBinState bins{{{static_cast<std::uint32_t>(state.binMask),static_cast<std::uint32_t>(state.binMask>>32),
                static_cast<std::uint32_t>(state.binSelect),static_cast<std::uint32_t>(state.binSelect>>32)}},state.knownBinWords};
            initialColorBins=bins;
            const auto inspect=[&](const auto& capture){
                counts={{static_cast<std::uint32_t>(capture.commands().commands.words().size()),static_cast<std::uint32_t>(capture.commands().shaders.size()),
                    static_cast<std::uint32_t>(capture.commands().draws.size()),capture.memory().inputCount,
                    capture.controls().previousWord,capture.controls().nextWord}};return true;
            };
            if(renderStoppedColor || commitStoppedColor){owned=registry.snapshotStoppedNativeColorBatch(*inspectColorProgram,bins,submission.writeWord);
                status=owned && inspect(*owned)?1:2;
            }else status=registry.inspectStoppedNativeColorBatch(*inspectColorProgram,bins,submission.writeWord,inspect)?1:2;
        }
        const auto ring=registry.graphicsRingConfiguration();
        // Private metadata only; the reproduction harness validates this extra
        // record separately. No game payload, host pointer or native work count.
        std::cout<<"stopped_native_color_inspection="<<status;
        for(auto count:counts)std::cout<<','<<count;
        std::cout<<','<<(ring?ring->readWord:0)<<'\n';
        if(commitStoppedColor){
            using namespace recomp::gpu;
            // ALL guest workers have joined. This separate experiment commits
            // only native backend state and reconstructed SCRATCH outputs.
            // It does not reuse the read-only/lifecycle diagnostic as a live
            // publisher, expose guest pointers, or permit guest code to resume.
            unsigned committed=0,fences=0,scratchRptr=0,retainedWords=0,unknownRegs=0;
            bool unchangedMemory=false,stateMatches=false,oneShot=false,cacheRejected=false;
            if(owned && nativeGraphics){
                auto scratch=NativeColorScratchMemory::create(*owned);
                std::vector<NativePhysicalRange> ranges;
                const auto& request=owned->memory();
                ranges.insert(ranges.end(),request.controls.begin(),request.controls.end());
                ranges.insert(ranges.end(),request.outputs.begin(),request.outputs.end());
                std::vector<std::vector<std::uint8_t>> before;
                for(const auto& range:ranges){auto bytes=registry.snapshotStoppedGraphicsPhysical(range.address,range.bytes);
                    if(!bytes)break;before.push_back(std::move(*bytes));}
                const auto prior=nativeGraphics->stateBatchSnapshot();
                std::vector<std::optional<std::uint32_t>> priorRegisters;
                for(unsigned index=0;index<0x8000;++index)priorRegisters.push_back(nativeGraphics->retainedRegister(index));
                if(scratch && before.size()==ranges.size()){
                    std::vector<std::uint8_t> expectedEdram(NativeColorEdramSnapshot::Bytes);
                    struct Scope {const NativeColorBatchCapture& capture;const NativeColorResolvedMemory& memory;
                        std::vector<std::uint8_t>& expected;unsigned mode,calls=0,fences=0;};
                    Scope scope{*owned,scratch->memory(),expectedEdram,commitStoppedColor};
                    const NativeColorPublisher publisher{&scope,[](void* context,const NativeColorChainExecutionResult& result) noexcept{
                        auto& s=*static_cast<Scope*>(context);++s.calls;s.fences=result.nativeFences;
                        if(s.calls!=1 || !result.edram || !result.copy || result.edram->knownBytes().size()!=s.expected.size())return false;
                        std::memcpy(s.expected.data(),result.edram->knownBytes().data(),s.expected.size());
                        if(s.mode==2)return false;
                        // Fault belongs to scratch only; original guest RAM is
                        // never modified. Fresh input matching must catch this.
                        if(s.mode==3)s.memory.inputs[0].bytes()[0]^=1;
                        const bool accepted=publishCapturedNativeColorOutputs(s.capture,s.memory,*result.copy,result.eventBytes.data(),result.eventBytes.size());
                        if(s.mode==3)s.memory.inputs[0].bytes()[0]^=1;
                        return accepted;
                    }};
                    committed=nativeGraphics->executeExperimentalOwnedColorBatch(*inspectColorProgram,*owned,publisher);
                    fences=scope.fences;
                    const auto* rp=scratch->memory().outputs[3].bytes();
                    scratchRptr=(unsigned{rp[0]}<<24)|(unsigned{rp[1]}<<16)|(unsigned{rp[2]}<<8)|rp[3];
                    const auto state=nativeGraphics->stateBatchSnapshot();
                    stateMatches=state.binMask==prior.binMask && state.binSelect==prior.binSelect && state.knownBinWords==prior.knownBinWords &&
                        state.batches==prior.batches+committed && state.invalidationEpoch==prior.invalidationEpoch+committed*3 &&
                        state.experimentalColorBatches==prior.experimentalColorBatches+committed &&
                        state.experimentalIdleCachePairEpoch==prior.experimentalIdleCachePairEpoch;
                    for(unsigned index=0;index<0x8000;++index){
                        const auto value=nativeGraphics->retainedRegister(index);
                        auto expected=priorRegisters[index];
                        if(committed){const auto found=owned->commands().finalRegisters.find(index);
                            if(found!=owned->commands().finalRegisters.end() && index>=0x2000)expected=found->second.bits.value;
                            if(index==0x5C8 || index==0xA2F || index==0xA30 || index==0xA31 || index==0x21F9)expected=std::nullopt;
                        }
                        if(value!=expected)stateMatches=false;
                    }
                    for(unsigned index:{0x5C8u,0xA2Fu,0xA30u,0xA31u,0x21F9u})unknownRegs+=!nativeGraphics->retainedRegister(index);
                    for(unsigned offset=0;offset<expectedEdram.size();offset+=4){unsigned value=73;
                        const bool known=nativeGraphics->retainedColorEdramWord(NativeColorEdramSnapshot::Begin+offset,value);
                        const auto* p=expectedEdram.data()+offset;
                        const auto expected=p[0]|(unsigned{p[1]}<<8)|(unsigned{p[2]}<<16)|(unsigned{p[3]}<<24);
                        if(known && value==expected)++retainedWords;
                        if(known!=bool(committed) || (known && value!=expected) || (!known && value!=73))stateMatches=false;
                    }
                    for(unsigned at:{0u,NativeColorEdramSnapshot::Begin-4,NativeColorEdramSnapshot::Begin+NativeColorEdramSnapshot::Bytes,AliasEdramBytes-4}){
                        unsigned value=73;if(nativeGraphics->retainedColorEdramWord(at,value) || value!=73)stateMatches=false;
                    }
                    const auto calls=scope.calls;
                    oneShot=!nativeGraphics->executeExperimentalOwnedColorBatch(*inspectColorProgram,*owned,publisher) && scope.calls==calls;
                    NativeInitializationReadback unused;
                    cacheRejected=!nativeGraphics->completeEmptyGuestCacheCoherency({0,0x1000,0x01000000}) &&
                        !nativeGraphics->completeNativeStateBatch({0xC0006000,7},unused) &&
                        !nativeGraphics->completeExperimentalIdleCachePair({0x5C8,0x20000,0xE00,1,0xE40,1},unused);
                    unchangedMemory=true;
                    for(unsigned i=0;i<ranges.size();++i){auto bytes=registry.snapshotStoppedGraphicsPhysical(ranges[i].address,ranges[i].bytes);
                        if(!bytes || *bytes!=before[i])unchangedMemory=false;}
                    const auto after=registry.graphicsRingConfiguration();
                    unchangedMemory=unchangedMemory && after && ring && after->readWord==ring->readWord;
                }
            }
            std::cout<<"stopped_native_color_scratch_commit="<<commitStoppedColor<<','<<committed<<','<<fences<<','<<scratchRptr<<','
                <<retainedWords<<','<<unsigned(stateMatches)<<','<<unsigned(unchangedMemory)<<','<<unsigned(oneShot)<<','<<unsigned(cacheRejected)<<','<<unknownRegs<<'\n';
        }
        if(renderStoppedColor){
            // All generated workers have stopped. Keep independent before/after
            // copies of every output and control range; never pass these guest
            // addresses to the diagnostic renderer or publish its returned bytes.
            std::vector<recomp::gpu::NativePhysicalRange> protectedRanges;
            std::vector<std::vector<std::uint8_t>> beforeBytes;
            std::vector<std::optional<std::uint32_t>> beforeRegisters;
            recomp::gpu::VulkanEngine::StateBatchSnapshot beforeState;
            bool unchangedMemory=bool(owned && nativeGraphics);
            if(unchangedMemory){
                const auto& memory=owned->memory();
                protectedRanges.insert(protectedRanges.end(),memory.controls.begin(),memory.controls.end());
                protectedRanges.insert(protectedRanges.end(),memory.outputs.begin(),memory.outputs.end());
                for(const auto& range:protectedRanges){
                    auto bytes=registry.snapshotStoppedGraphicsPhysical(range.address,range.bytes);
                    if(!bytes){unchangedMemory=false;break;}
                    beforeBytes.push_back(std::move(*bytes));
                }
                beforeState=nativeGraphics->stateBatchSnapshot();
                beforeRegisters.reserve(0x8000);
                for(unsigned index=0;index<0x8000;++index)beforeRegisters.push_back(nativeGraphics->retainedRegister(index));
            }
            recomp::gpu::NativeColorChainExecutionResult rendered;
            const auto execute=[&](const auto& capture){
                return nativeGraphics->executeStoppedColorBatchDiagnostic(*inspectColorProgram,capture,rendered);
            };
            const bool completed=unchangedMemory && (lockedStoppedColor?
                registry.runStoppedNativeColorDiagnostic(*inspectColorProgram,initialColorBins,submission.writeWord,execute):execute(*owned));
            if(lockedStoppedColor)std::cout<<"stopped_native_color_lifecycle="<<unsigned(completed)<<'\n';
            const auto after=registry.graphicsRingConfiguration();
            const bool unchanged=after && ring && after->readWord==ring->readWord;
            if(unchangedMemory)for(std::size_t index=0;index<protectedRanges.size();++index){
                const auto& range=protectedRanges[index];
                const auto bytes=registry.snapshotStoppedGraphicsPhysical(range.address,range.bytes);
                if(!bytes || *bytes!=beforeBytes[index])unchangedMemory=false;
            }
            bool unchangedState=completed;
            if(completed){
                const auto state=nativeGraphics->stateBatchSnapshot();
                unchangedState=state.binMask==beforeState.binMask && state.binSelect==beforeState.binSelect &&
                    state.knownBinWords==beforeState.knownBinWords && state.batches==beforeState.batches &&
                    state.invalidationEpoch==beforeState.invalidationEpoch &&
                    state.experimentalColorBatches==beforeState.experimentalColorBatches &&
                    state.experimentalIdleCachePairEpoch==beforeState.experimentalIdleCachePairEpoch;
                for(unsigned index=0;index<beforeRegisters.size();++index)
                    if(beforeRegisters[index]!=nativeGraphics->retainedRegister(index))unchangedState=false;
            }
            recomp::gpu::NativeColorChainExecutionResult repeated;repeated.nativeFences=99;repeated.eventBytes.fill(0xA5);
            const bool oneShot=completed && !nativeGraphics->executeStoppedColorBatchDiagnostic(*inspectColorProgram,*owned,repeated) &&
                repeated.nativeFences==99 && !repeated.copy && !repeated.edram && !repeated.pendingRegisters && !repeated.pendingState && !repeated.auditedTimeline &&
                std::all_of(repeated.eventBytes.begin(),repeated.eventBytes.end(),[](auto byte){return byte==0xA5;});
            std::cout<<"stopped_native_color_render="<<unsigned(completed)<<','<<rendered.nativeFences<<','
                <<(rendered.edram?rendered.edram->knownBytes().size()/4:0)<<','<<unsigned(unchanged)<<'\n';
            std::cout<<"stopped_native_color_isolation="<<unsigned(unchangedMemory)<<','<<unsigned(unchangedState)<<','<<unsigned(oneShot)<<'\n';
            unsigned pendingCount=0,unknownEffects=0,highWrites=0;bool pendingMatches=completed && rendered.pendingRegisters.has_value();
            if(pendingMatches){
                const auto& journal=*rendered.pendingRegisters;pendingCount=static_cast<unsigned>(journal.size());
                for(unsigned index:{0x5C8u,0xA2Fu,0xA30u,0xA31u,0x21F9u})unknownEffects+=!journal.count(index);
                for(const auto& item:owned->commands().finalRegisters)if(item.first>=0x2000 && item.first!=0x21F9){
                    const auto found=journal.find(item.first);++highWrites;
                    if(found==journal.end() || found->second!=std::make_pair(item.second.bits.value,item.second.bits.known))pendingMatches=false;
                }
            }
            std::cout<<"stopped_native_color_registers="<<pendingCount<<','<<unknownEffects<<','<<highWrites<<','<<unsigned(pendingMatches)<<'\n';
            bool stateMatches=completed && rendered.pendingState.has_value();
            if(stateMatches){const auto& state=*rendered.pendingState;const auto& bins=owned->commands().finalBins;
                stateMatches=state.binMask==(std::uint64_t{bins.words[0]}|(std::uint64_t{bins.words[1]}<<32)) &&
                    state.binSelect==(std::uint64_t{bins.words[2]}|(std::uint64_t{bins.words[3]}<<32)) &&
                    state.knownBinWords==bins.knownWords && state.batches==beforeState.batches+1 &&
                    state.experimentalColorBatches==beforeState.experimentalColorBatches+1 &&
                    state.invalidationEpoch==beforeState.invalidationEpoch+3 &&
                    state.experimentalIdleCachePairEpoch==beforeState.experimentalIdleCachePairEpoch;
            }
            std::cout<<"stopped_native_color_pending_state="<<unsigned(stateMatches)<<'\n';
            if(rendered.auditedTimeline){const auto& timeline=*rendered.auditedTimeline;
                std::cout<<"stopped_native_color_timeline="<<timeline.completedStages()<<','<<timeline.depthWaits()<<','
                    <<timeline.invalidations()<<','<<timeline.idleCleans()<<','<<timeline.cacheFlushes()<<','<<timeline.literalEvents()<<'\n';
            }
        }
    }
#endif
    if(!inspectOwnershipRanges.empty()){
        const bool inspectRequest=postSwapCaptureScope && std::all_of(inspectOwnershipRanges.begin(),inspectOwnershipRanges.end(),
            [&](const auto& r){return postSwapCaptureScope->permitsOwnershipQuery(r[0],r[1]);});
        const bool inspectOwners=inspectWork || inspectRequest;
        std::cout << "stopped_ownership_status=" << unsigned(inspectOwners) << "\nstopped_ownership_requests=[";
        for(std::size_t i=0;i<inspectOwnershipRanges.size();++i){
            if(i)std::cout << ',';
            std::cout << '[' << inspectOwnershipRanges[i][0] << ',' << inspectOwnershipRanges[i][1] << ']';
        }
        std::cout << "]\nstopped_ownership_results=[";
        if(inspectOwners)for(std::size_t i=0;i<inspectOwnershipRanges.size();++i){
            if(i)std::cout << ',';
            const auto owner=registry.stoppedGraphicsPhysicalOwnership(inspectOwnershipRanges[i][0],inspectOwnershipRanges[i][1]);
            if(!owner)std::cout << "null";
            else std::cout << '[' << owner->physicalBase << ',' << owner->virtualBase << ',' << owner->byteCount
                << ',' << owner->protection << ',' << unsigned(owner->writable) << ']';
        }
        std::cout << "]\n";
    }
    if (!capturePhysicalRanges.empty()) {
        std::vector<std::vector<std::uint8_t>> bytes;
        const auto nativeCaptureBudget=deferredPendingSubmission?16384u:4096u;
        const bool nativeCapture=nativePendingSubmission && captureByteBudget<=nativeCaptureBudget &&
            std::all_of(capturePhysicalRanges.begin(),capturePhysicalRanges.end(),[&](const auto& r){
                const auto& p=*nativePendingSubmission;
                return (r[0]>=p.rootPhysical && std::uint64_t{r[0]}+r[1]<=std::uint64_t{p.rootPhysical}+12) ||
                    (r[0]>=p.childPhysical && std::uint64_t{r[0]}+r[1]<=std::uint64_t{p.childPhysical}+p.childWords*4) ||
                    (nativeScalerWrapper && r[0]>=nativeScalerWrapper->scratchPhysical() &&
                        std::uint64_t{r[0]}+r[1]<=std::uint64_t{nativeScalerWrapper->scratchPhysical()}+32);
            });
        const bool requestCapture=postSwapCaptureScope && std::all_of(capturePhysicalRanges.begin(),capturePhysicalRanges.end(),
            [&](const auto& r){return postSwapCaptureScope->permitsCommandPayload(r[0],r[1]);});
        const unsigned status = !inspectWork && !nativeCapture && !requestCapture ? 0 :
            (recomp::gpu::captureStoppedPhysicalRanges(capturePhysicalRanges,
                [&](auto address, auto count) { return registry.snapshotStoppedGraphicsPhysical(address,count); }, bytes,captureByteBudget) ? 1 : 2);
        std::cout << "stopped_physical_byte_budget=" << captureByteBudget << "\nstopped_physical_capture=" << status << "\nstopped_physical_requests=[";
        for (std::size_t i = 0; i < capturePhysicalRanges.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << '[' << capturePhysicalRanges[i][0] << ',' << capturePhysicalRanges[i][1] << ']';
        }
        std::cout << "]\n";
        std::cout << "stopped_physical_ownership=[";
        if(status==1)for(std::size_t i=0;i<capturePhysicalRanges.size();++i){
            if(i)std::cout << ',';
            const auto owner=registry.stoppedGraphicsPhysicalOwnership(capturePhysicalRanges[i][0],capturePhysicalRanges[i][1]);
            if(!owner)std::cout << "null";
            else std::cout << '[' << owner->physicalBase << ',' << owner->virtualBase << ',' << owner->byteCount
                << ',' << owner->protection << ',' << unsigned(owner->writable) << ']';
        }
        std::cout << "]\n";
        if (status == 1) for (std::size_t i = 0; i < bytes.size(); ++i) {
            std::cout << "stopped_physical_blob=" << capturePhysicalRanges[i][0] << ',' << capturePhysicalRanges[i][1] << ',';
            constexpr char digits[] = "0123456789abcdef";
            for (auto b : bytes[i]) std::cout << digits[b >> 4] << digits[b & 15];
            std::cout << '\n';
        }
    }
    if (const auto ring = registry.graphicsRingConfiguration()) {
        if (const auto retired=registry.retiredGraphicsRingReadWord())
            std::cout << "native_ring_restart=1," << *retired << '\n';
        std::cout << "graphics_ring_physical=" << ring->physicalAddress << '\n'
                  << "graphics_ring_virtual=" << ring->virtualAddress << '\n'
                  << "graphics_ring_bytes=" << ring->bytes << '\n'
                  << "graphics_ring_read_word=" << ring->readWord << '\n';
        if (ring->writeBack) {
            std::cout << "graphics_wb_physical=" << ring->writeBack->physicalAddress << '\n'
                      << "graphics_wb_virtual=" << ring->writeBack->virtualAddress << '\n'
                      << "graphics_wb_exponent=" << ring->writeBack->blockSizeLog2 << '\n';
        }
        if (const auto identifier = registry.graphicsSystemIdentifierAddress()) {
            std::cout << "graphics_identifier_virtual=" << *identifier << '\n';
        }
    }
    std::cout << "title_termination_registrations=" << registry.titleTerminationRegistrations().size() << '\n';
    if (execution.scalarAccess) {
        const auto& access = *execution.scalarAccess;
        std::cout << "scalar_address=" << access.address << '\n'
                  << "scalar_width=" << access.width << '\n'
                  << "scalar_value=" << access.value << '\n'
                  << "scalar_write=" << access.write << '\n'
                  << "scalar_region=" << static_cast<std::uint32_t>(access.region) << '\n';
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::HostException) {
        // The guard already bounded and sanitised this to one printable line.
        std::cout << "stop_exception=" << execution.detail << '\n';
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::UnimplementedImport
            || execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport) {
        std::cout << "stop_import=" << execution.detail << '\n';
        if (execution.importArguments) {
            const auto& arguments = *execution.importArguments;
            std::cout << "import_stack_pointer=" << arguments.stackPointer << '\n';
            for (std::size_t i = 0; i < arguments.registers.size(); ++i) {
                std::cout << "import_r" << i + 3 << '=' << arguments.registers[i] << '\n';
            }
        }
    } else {
        std::cout << "stop_import=none\n";
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport
            && execution.detail == "__imp__KeSetBasePriorityThread") {
        if (const auto request = registry.lastThreadPriorityRequest()) {
            std::cout << "priority_increment=" << request->increment << '\n'
                      << "priority_previous=" << request->previousIncrement << '\n'
                      << "priority_initial=" << request->initial << '\n'
                      << "priority_current=" << request->current << '\n'
                      << "priority_suspended=" << request->suspended << '\n'
                      << "priority_returned=" << request->returned << '\n';
        }
    }
    for (std::size_t i = 0; i < functionLabels.size(); ++i) {
        std::cout << "function_label=" << functionLabels[(nextFunctionLabel + i) % functionLabels.size()] << '\n';
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport &&
        execution.detail == "__imp__RtlEnterCriticalSection") {
        if (const auto state=registry.lastCriticalSectionRejection()) {
            std::cout << "critical_section_rejection=";
            for(std::size_t i=0;i<state->size();++i) {if(i)std::cout<<',';std::cout<<(*state)[i];}
            std::cout << '\n';
        }
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport &&
        execution.detail == "__imp__KeWaitForMultipleObjects" && execution.importArguments) {
        const auto count=static_cast<std::uint32_t>(execution.importArguments->registers[0]);
        const auto array=static_cast<std::uint32_t>(execution.importArguments->registers[1]);
        if (count && count<=64 && array && !(array&3U) &&
            std::uint64_t{array}+count*4<=recomp::bootstrap::GuestAddressSpace::Size &&
            !(array<0x7FC90000 && std::uint64_t{array}+count*4>0x7FC80000)) {
            std::cout << "dispatcher_wait_objects=[";
            for (std::uint32_t i=0;i<count;++i) {
                const auto* p=addressSpace.translate(array+i*4);
                const auto address=(std::uint32_t{p[0]}<<24)|(std::uint32_t{p[1]}<<16)|(std::uint32_t{p[2]}<<8)|p[3];
                const auto snapshot=registry.inspectDispatcherEvent(address);
                if (i) std::cout << ',';
                std::cout << '[' << address << ',' << bool(snapshot) << ','
                          << (snapshot?(*snapshot)[0]:0) << ',' << (snapshot?(*snapshot)[1]:0) << ','
                          << (snapshot?(*snapshot)[2]:0) << ']';
            }
            std::cout << "]\n";
        }
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport
            && execution.detail == "__imp__NtCreateFile") {
        if (const auto request = registry.lastFileCreateRequest()) {
            std::cout << "file_create_access=" << request->access << '\n'
                      << "file_create_attributes=" << request->attributes << '\n'
                      << "file_create_share=" << request->share << '\n'
                      << "file_create_disposition=" << request->disposition << '\n'
                      << "file_create_options=" << request->options << '\n'
                      << "file_create_object_root=" << request->objectRoot << '\n'
                      << "file_create_object_flags=" << request->objectFlags << '\n'
                      << "file_create_lookup=" << request->lookup << '\n'
                      << "file_create_record=" << (request->lookup == 1 && !request->mountedContent ? lastFileMetadataIndex : 0) << '\n';
            std::cout << "file_create_path=";
            for(unsigned char c:request->path)std::cout << (c>=32 && c<127?char(c):'?');
            std::cout << '\n';
        }
    }
    if (execution.reason == recomp::bootstrap::GuestExecutionStopReason::RejectedImport
            && (execution.detail == "__imp__XamContentCreateEx" || execution.detail == "__imp__XamContentClose")) {
        if (const auto mask = registry.contentMountRejection()) { std::cout << "content_mount_rejection=" << mask << '\n'; }
    }
    if (cooperative) {
        std::cout << "workers_created=" << cooperativeResult.workersCreated << '\n'
                  << "workers_entered=" << cooperativeResult.workersEntered << '\n'
                  << "context_switches=" << cooperativeResult.contextSwitches << '\n'
                  << "wait_operations=" << cooperativeResult.waitOperations << '\n';
    }
    return 0;
}

#if defined(RECOMP_PRIVATE_TRACE_LIBRARY)
namespace {
std::atomic_flag privateTraceEntered=ATOMIC_FLAG_INIT;
int privateTraceEntry(int argc,char** argv,const RecompPrivateTraceCallbacksV2* callbacks,
        const RecompPrivateTraceCallbacksV3* liveCallbacks=nullptr,
        const RecompPrivateTraceCallbacksV5* inputCallbacks=nullptr) noexcept {
    if(argc<1 || argc>1024 || !argv)return 2;
    for(int i=0;i<argc;++i)if(!argv[i] || strnlen(argv[i],4097)>4096)return 2;
    if(privateTraceEntered.test_and_set(std::memory_order_acq_rel))return 3;
    try {return runTraceMain(argc,argv,callbacks,liveCallbacks,inputCallbacks);}
    catch(...) {return 2;}
}
}
extern "C" int recomp_run_private_trace_v1(int argc,char** argv) noexcept {
    return privateTraceEntry(argc,argv,nullptr);
}
extern "C" int recomp_run_private_trace_v2(int argc,char** argv,const RecompPrivateTraceCallbacksV2* callbacks) noexcept {
    if(!callbacks || callbacks->structureBytes!=sizeof(RecompPrivateTraceCallbacksV2) || callbacks->version!=2 ||
        callbacks->reserved0 || callbacks->reserved1 || !callbacks->stoppedRgba)return 2;
    const auto owned=*callbacks;
    return privateTraceEntry(argc,argv,&owned);
}
extern "C" int recomp_run_private_trace_v3(int argc,char** argv,const RecompPrivateTraceCallbacksV3* callbacks) noexcept {
    if(!callbacks || callbacks->structureBytes!=sizeof(RecompPrivateTraceCallbacksV3) || callbacks->version!=3 ||
        callbacks->reserved0 || callbacks->reserved1 || !callbacks->orderedRgba)return 2;
    const auto owned=*callbacks;
    return privateTraceEntry(argc,argv,nullptr,&owned);
}
extern "C" int recomp_run_private_trace_v4(int argc,char** argv,const RecompPrivateTraceCallbacksV4* callbacks) noexcept {
    if(!callbacks || callbacks->structureBytes!=sizeof(RecompPrivateTraceCallbacksV4) || callbacks->version!=4 ||
        callbacks->reserved0 || callbacks->reserved1 || !callbacks->orderedRgba)return 2;
    recomp::private_trace::DisplayCallbacksV4 owned(*callbacks);
    const RecompPrivateTraceCallbacksV3 bridge{sizeof(RecompPrivateTraceCallbacksV3),3,0,0,&owned,
        [](void* user,const std::uint8_t* rgba,std::uint32_t bytes,std::uint32_t width,std::uint32_t height)noexcept{
            return static_cast<recomp::private_trace::DisplayCallbacksV4*>(user)->consume(rgba,bytes,width,height);}};
    return privateTraceEntry(argc,argv,nullptr,&bridge);
}
extern "C" int recomp_run_private_trace_v5(int argc,char** argv,const RecompPrivateTraceCallbacksV5* callbacks) noexcept {
    if(!callbacks || callbacks->structureBytes!=sizeof(*callbacks) || callbacks->version!=5 ||
       callbacks->reserved0 || callbacks->reserved1 || !callbacks->gamepadState || !callbacks->gamepadCapabilities)return 2;
    const auto owned=*callbacks;
    const auto& d=owned.display;
    if(d.structureBytes!=sizeof(d) || d.version!=4 || d.reserved0 || d.reserved1 || !d.orderedRgba)return 2;
    recomp::private_trace::DisplayCallbacksV4 display(d);
    const RecompPrivateTraceCallbacksV3 bridge{sizeof(RecompPrivateTraceCallbacksV3),3,0,0,&display,
        [](void* user,const std::uint8_t* rgba,std::uint32_t bytes,std::uint32_t width,std::uint32_t height)noexcept{
            return static_cast<recomp::private_trace::DisplayCallbacksV4*>(user)->consume(rgba,bytes,width,height);}};
    return privateTraceEntry(argc,argv,nullptr,&bridge,&owned);
}
#else
int main(int argc,char** argv) {return runTraceMain(argc,argv);}
#endif
