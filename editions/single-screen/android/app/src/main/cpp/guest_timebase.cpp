#include "guest_timebase.h"
#include <atomic>
#include <chrono>

namespace recomp::bootstrap {
namespace {
std::atomic<std::uint64_t> reads{0};
}
std::uint64_t readGuestTimebase() noexcept {
    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady);
    static const auto epoch = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - epoch).count();
    reads.fetch_add(1, std::memory_order_relaxed);
    // Signed nanoseconds cover over 292 years, far beyond a bounded trace.
    return guestTimebaseFromNanoseconds(elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0);
}
std::uint64_t guestTimebaseReadCount() noexcept {
    return reads.load(std::memory_order_relaxed);
}
}
