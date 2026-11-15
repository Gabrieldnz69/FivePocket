#include "indirect_call_resolver.h"

#include "guest_execution_guard.h"
#include "ppc_config.h"
#include "ppc_context.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>

namespace recomp::bootstrap {
namespace {

std::atomic_size_t validatedMappingCount{0};
std::mutex rejectedCallMutex;
std::optional<InvalidGuestIndirectCall> rejectedCall;

bool configuredCodeRange(std::uint64_t& begin, std::uint64_t& end) noexcept {
    begin = static_cast<std::uint64_t>(PPC_CODE_BASE);
    const auto size = static_cast<std::uint64_t>(PPC_CODE_SIZE);
    constexpr std::uint64_t GuestAddressSpaceSize = 1ULL << 32U;
    if (size == 0 || begin >= GuestAddressSpaceSize
            || size > GuestAddressSpaceSize - begin) {
        return false;
    }
    end = begin + size;
    return true;
}

} // namespace

bool initializeGuestIndirectCallResolver(
        std::size_t expectedMappingCount,
        std::size_t& outputMappingCount) noexcept {
    validatedMappingCount.store(0, std::memory_order_release);
    { std::lock_guard lock(rejectedCallMutex); rejectedCall.reset(); }
    outputMappingCount = 0;
    if (expectedMappingCount == 0
            || expectedMappingCount > MaximumGuestFunctionMappings) {
        return false;
    }

    std::uint64_t codeBegin = 0;
    std::uint64_t codeEnd = 0;
    if (!configuredCodeRange(codeBegin, codeEnd)) {
        return false;
    }
    std::uint64_t previousAddress = 0;
    for (std::size_t index = 0; index < expectedMappingCount; ++index) {
        const auto& mapping = PPCFuncMappings[index];
        const auto address = static_cast<std::uint64_t>(mapping.guest);
        if (mapping.host == nullptr
                || address < codeBegin
                || address >= codeEnd
                || address > std::numeric_limits<std::uint32_t>::max()
                || (address & 3U) != 0
                || (index != 0 && address <= previousAddress)) {
            return false;
        }
        previousAddress = address;
    }
    const auto& sentinel = PPCFuncMappings[expectedMappingCount];
    if (sentinel.guest != 0 || sentinel.host != nullptr) {
        return false;
    }

    outputMappingCount = expectedMappingCount;
    validatedMappingCount.store(expectedMappingCount, std::memory_order_release);
    return true;
}

void resetGuestIndirectCallResolver() noexcept {
    validatedMappingCount.store(0, std::memory_order_release);
    std::lock_guard lock(rejectedCallMutex); rejectedCall.reset();
}

std::optional<InvalidGuestIndirectCall> stoppedGuestIndirectCall() {
    std::lock_guard lock(rejectedCallMutex); return rejectedCall;
}

bool hasExactGuestFunction(std::uint32_t guestTarget) noexcept {
    const auto count = validatedMappingCount.load(std::memory_order_acquire);
    std::size_t first = 0, last = count;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (PPCFuncMappings[middle].guest < guestTarget) { first = middle + 1; }
        else { last = middle; }
    }
    return first < count && PPCFuncMappings[first].guest == guestTarget
            && PPCFuncMappings[first].host != nullptr;
}

} // namespace recomp::bootstrap

extern "C" void recomp_host_guest_call_indirect(
        std::uint32_t guestTarget,
        PPCContext& context,
        std::uint8_t* base) {
    const auto count = recomp::bootstrap::validatedMappingCount.load(
            std::memory_order_acquire);
    std::size_t first = 0;
    std::size_t last = count;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (PPCFuncMappings[middle].guest < guestTarget) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    if (first >= count
            || PPCFuncMappings[first].guest != guestTarget
            || PPCFuncMappings[first].host == nullptr) {
        {
            std::lock_guard lock(recomp::bootstrap::rejectedCallMutex);
            if (!recomp::bootstrap::rejectedCall)
                recomp::bootstrap::rejectedCall = recomp::bootstrap::InvalidGuestIndirectCall{
                    guestTarget,static_cast<std::uint32_t>(context.lr),context.r1.u32,
                    {context.r3.u32,context.r4.u32,context.r5.u32,context.r6.u32,
                     context.r7.u32,context.r11.u32,context.r26.u32,context.r31.u32}};
        }
        recomp::bootstrap::GuestExecutionGuard::stopForInvalidIndirectTarget();
    }
    PPCFuncMappings[first].host(context, base);
}
