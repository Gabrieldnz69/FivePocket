#include "guest_execution_guard.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace recomp::bootstrap {
namespace {

constexpr std::size_t MaximumImportNameBytes = 128;
constexpr std::size_t MaximumHostExceptionBytes = 192;

// Reporting only. The private trace protocol is line oriented and key=value,
// so an unsanitised what() could inject newlines or forge a protocol key.
// Appends stay inside the caller's reserved capacity so a detail built while
// unwinding std::bad_alloc cannot itself allocate.
void appendBoundedDetail(std::string& out, const char* text) {
    if (!text) text = "?";
    for (; *text && out.size() < MaximumHostExceptionBytes; ++text) {
        const auto byte = static_cast<unsigned char>(*text);
        out.push_back(byte < 0x20 || byte > 0x7e ? '?' : *text);
    }
}

struct StopSignal final {};

struct ActiveExecution {
    std::uint64_t maximumFunctionEntries;
    std::uint64_t completedFunctionEntries{0};
    GuestExecutionStopReason reason{GuestExecutionStopReason::Completed};
    std::string detail;
    const GuestExecutionGuard::FunctionTraceSink* traceSink{nullptr};
    std::optional<GuestImportArguments> importArguments{};
    std::optional<GuestScalarAccess> scalarAccess{};
};

thread_local ActiveExecution* activeExecution = nullptr;
std::vector<std::uint32_t> unboundDataSlots;
bool dataSlotsBound = false;
std::uint32_t uptimeBundle = 0;
std::function<std::optional<std::uint64_t>()> uptimeSource;
std::optional<std::uint64_t> lastUptime;
std::function<std::optional<std::uint64_t>()> experimentalGpuClockSource;
std::optional<std::uint64_t> lastExperimentalGpuNanoseconds;
std::uint64_t experimentalGpuReads = 0;
std::function<bool(std::uint32_t)> graphicsSubmissionHandler;
bool graphicsSubmissionActive = false;
std::function<bool(std::uint32_t,bool,std::uint32_t&)> xmaRegisterHandler;
bool xmaRegisterActive = false;

bool touchesUptime(std::uint32_t address, std::uint32_t width) {
    return uptimeBundle && width && address < std::uint64_t{uptimeBundle} + 24 &&
        std::uint64_t{address} + width > uptimeBundle;
}

class ActiveExecutionScope final {
public:
    explicit ActiveExecutionScope(ActiveExecution& execution) noexcept
        : previous_(activeExecution) {
        activeExecution = &execution;
    }

    ~ActiveExecutionScope() {
        activeExecution = previous_;
    }

    ActiveExecutionScope(const ActiveExecutionScope&) = delete;
    ActiveExecutionScope& operator=(const ActiveExecutionScope&) = delete;

private:
    ActiveExecution* previous_;
};

std::string boundedDetail(std::string_view value) {
    const auto length = std::min(value.size(), MaximumImportNameBytes);
    return std::string(value.substr(0, length));
}

} // namespace

bool bindExperimentalGuestGpuClock(std::function<std::optional<std::uint64_t>()> source) {
    if (!source || experimentalGpuClockSource || activeExecution) return false;
    experimentalGpuClockSource = std::move(source);
    lastExperimentalGpuNanoseconds.reset(); experimentalGpuReads = 0;
    return true;
}

void resetExperimentalGuestGpuClock() noexcept {
    experimentalGpuClockSource = {}; lastExperimentalGpuNanoseconds.reset(); experimentalGpuReads = 0;
}

std::uint64_t experimentalGuestGpuClockReads() noexcept { return experimentalGpuReads; }

bool tryReadExperimentalGuestGpuClock(std::uint32_t address, std::uint32_t width, std::uint64_t& value) {
    if (!experimentalGpuClockSource || address != 0x7FC8112C || width != 4) return false;
    const auto ns = experimentalGpuClockSource();
    if (!ns || (lastExperimentalGpuNanoseconds && *ns < *lastExperimentalGpuNanoseconds) ||
        experimentalGpuReads == UINT64_MAX) return false;
    lastExperimentalGpuNanoseconds = ns;
    value = static_cast<std::uint32_t>(*ns / 2); // Hypothesized 500 MHz, modulo 2^32.
    ++experimentalGpuReads;
    return true;
}

bool bindGuestGraphicsSubmission(std::function<bool(std::uint32_t)> handler) {
    if (!handler || graphicsSubmissionHandler || activeExecution) return false;
    graphicsSubmissionHandler = std::move(handler); return true;
}

void resetGuestGraphicsSubmission() noexcept {
    // Do not destroy a callable while it is executing. Lifecycle reset belongs
    // after all workers join, never inside a callback/guarded guest run.
    if (!activeExecution && !graphicsSubmissionActive) graphicsSubmissionHandler = {};
}

bool tryWriteGuestGraphicsSubmission(std::uint32_t address, std::uint32_t width, std::uint64_t value) {
    if (!activeExecution || !graphicsSubmissionHandler || graphicsSubmissionActive ||
        address != 0x7FC80714 || width != 4 || value > UINT32_MAX) return false;
    graphicsSubmissionActive = true;
    bool accepted = false;
    try { accepted = graphicsSubmissionHandler(static_cast<std::uint32_t>(value)); } catch (...) {}
    graphicsSubmissionActive = false;
    return accepted;
}

// The inline hull used by loadGuestScalar/storeGuestScalar to skip both special checks.
static void updateGuestScalarSpecialHull() noexcept {
    std::uint64_t lo = UINT64_MAX, hi = 0;
    if (!unboundDataSlots.empty()) {
        lo = unboundDataSlots.front(); hi = std::uint64_t{unboundDataSlots.back()} + 4;
    }
    if (uptimeBundle) {
        lo = std::min<std::uint64_t>(lo, uptimeBundle); hi = std::max<std::uint64_t>(hi, std::uint64_t{uptimeBundle} + 24);
    }
    if (hi == 0) { guestScalarSpecialLo = guestScalarSpecialHi = 0; return; }
    guestScalarSpecialLo = static_cast<std::uint32_t>(lo);
    guestScalarSpecialHi = static_cast<std::uint32_t>(std::min<std::uint64_t>(hi, UINT32_MAX));
}

bool bindGuestScalarDataImportGuard(const std::vector<std::uint32_t>& slots) {
    if (dataSlotsBound || activeExecution || slots.size() > 256) return false;
    if (std::any_of(slots.begin(), slots.end(), [](auto slot) {
        return !slot || (slot & 3U) || classifyGuestScalarAccess(slot, 4) != GuestScalarRegion::Ordinary;
    })) return false;
    auto sorted = slots;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return false;
    unboundDataSlots = std::move(sorted); dataSlotsBound = true;
    updateGuestScalarSpecialHull();
    return true;
}

bool bindGuestXmaRegisters(std::function<bool(std::uint32_t,bool,std::uint32_t&)> handler) {
    if (!handler || xmaRegisterHandler || activeExecution) return false;
    xmaRegisterHandler=std::move(handler); return true;
}
void resetGuestXmaRegisters() noexcept {
    if (!activeExecution && !xmaRegisterActive) xmaRegisterHandler={};
}
bool tryAccessGuestXmaRegisters(std::uint32_t address,std::uint32_t width,bool write,std::uint64_t& value) {
    if (!activeExecution || !xmaRegisterHandler || xmaRegisterActive || width!=4 || (address&3) ||
        address<0x7FEA0000 || address>=0x7FEB0000 || (write && value>UINT32_MAX)) return false;
    std::uint32_t word=write?static_cast<std::uint32_t>(value):0;
    bool accepted=false; xmaRegisterActive=true;
    try { accepted=xmaRegisterHandler(address,write,word); } catch (...) {}
    xmaRegisterActive=false;
    if (accepted && !write) value=word;
    return accepted;
}

bool bindGuestScalarUptimeClock(std::uint32_t bundle, std::function<std::optional<std::uint64_t>()> source) {
    if (uptimeBundle || activeExecution || !bundle || (bundle & 3U) || !source ||
        classifyGuestScalarAccess(bundle, 24) != GuestScalarRegion::Ordinary) return false;
    uptimeSource = std::move(source); uptimeBundle = bundle; lastUptime.reset();
    updateGuestScalarSpecialHull();
    return true;
}

void resetGuestScalarUptimeClock() noexcept {
    uptimeBundle = 0; uptimeSource = {}; lastUptime.reset();
    updateGuestScalarSpecialHull();
}

bool tryReadGuestScalarUptime(std::uint32_t address, std::uint32_t width, std::uint64_t& value) {
    if (!touchesUptime(address, width)) return false;
    if (address == uptimeBundle + 16 && width == 4) {
        const auto elapsed = uptimeSource();
        if (elapsed && (!lastUptime || *elapsed >= *lastUptime)) {
            lastUptime = elapsed;
            value = static_cast<std::uint32_t>(*elapsed); // Intentional 49.7-day wrap.
            return true;
        }
    }
    recomp_guest_execution_scalar_access({address, width, 0, false, GuestScalarRegion::UnsupportedTimestampAccess});
}

void checkGuestScalarUptimeWrite(std::uint32_t address, std::uint32_t width, std::uint64_t value) {
    if (touchesUptime(address, width))
        recomp_guest_execution_scalar_access({address, width, value, true, GuestScalarRegion::UnsupportedTimestampAccess});
}

void resetGuestScalarDataImportGuard() noexcept {
    unboundDataSlots.clear(); dataSlotsBound = false;
    updateGuestScalarSpecialHull();
}

bool isUnboundGuestDataImportAccess(std::uint32_t address, std::uint32_t width) noexcept {
    const auto end = std::uint64_t{address} + width;
    if (!width || unboundDataSlots.empty() || end <= unboundDataSlots.front() ||
        address >= std::uint64_t{unboundDataSlots.back()} + 4) return false;
    const auto first = std::lower_bound(unboundDataSlots.begin(), unboundDataSlots.end(),
        address > 3 ? address - 3 : 0);
    return first != unboundDataSlots.end() && *first < end;
}

const char* guestExecutionStopReasonName(GuestExecutionStopReason reason) noexcept {
    switch (reason) {
    case GuestExecutionStopReason::Completed:
        return "completed";
    case GuestExecutionStopReason::InvalidEntry:
        return "invalid-entry";
    case GuestExecutionStopReason::InvalidBudget:
        return "invalid-budget";
    case GuestExecutionStopReason::NestedRun:
        return "nested-run";
    case GuestExecutionStopReason::FunctionEntryBudgetExceeded:
        return "function-entry-budget-exceeded";
    case GuestExecutionStopReason::UnimplementedImport:
        return "unimplemented-import";
    case GuestExecutionStopReason::RejectedImport:
        return "rejected-import";
    case GuestExecutionStopReason::InvalidIndirectTarget:
        return "invalid-indirect-target";
    case GuestExecutionStopReason::HostException:
        return "host-exception";
    case GuestExecutionStopReason::NoRunnableTask:
        return "no-runnable-task";
    case GuestExecutionStopReason::GuestTrap:
        return "guest-trap";
    case GuestExecutionStopReason::UnsupportedScalarAccess:
        return "unsupported-scalar-access";
    }
    return "unknown";
}

GuestExecutionResult GuestExecutionGuard::run(
        std::uint64_t maximumFunctionEntries,
        const Entry& entry,
        const FunctionTraceSink& traceSink) noexcept {
    if (!entry) {
        return {GuestExecutionStopReason::InvalidEntry, 0, "Guest entry callback is empty"};
    }
    if (maximumFunctionEntries == 0) {
        return {GuestExecutionStopReason::InvalidBudget, 0, "Function-entry budget must be positive"};
    }
    if (activeExecution != nullptr) {
        return {GuestExecutionStopReason::NestedRun, 0, "Nested guest execution is not allowed"};
    }

    ActiveExecution execution{
            maximumFunctionEntries,
            0,
            GuestExecutionStopReason::Completed,
            {},
            traceSink ? &traceSink : nullptr,
    };
    ActiveExecutionScope scope(execution);
    // Reserve before running so a host-exception detail can be built
    // without allocating while that exception is being handled.
    execution.detail.reserve(MaximumHostExceptionBytes);
    try {
        entry();
    } catch (const StopSignal&) {
        // The checkpoint/import stop populated the structured result.
    } catch (const std::exception& error) {
        execution.reason = GuestExecutionStopReason::HostException;
        // "host-exception" alone names no boundary; the type and message say
        // which host component failed. Neither is a guest pointer dereference.
        execution.detail.clear();
        appendBoundedDetail(execution.detail, "Guest entry raised a host C++ exception: ");
#if defined(__cpp_rtti) || defined(__GXX_RTTI)
        appendBoundedDetail(execution.detail, typeid(error).name());
        appendBoundedDetail(execution.detail, ": ");
#endif
        appendBoundedDetail(execution.detail, error.what());
    } catch (...) {
        execution.reason = GuestExecutionStopReason::HostException;
        execution.detail = "Guest entry raised an unknown host exception";
    }

    return {
            execution.reason,
            execution.completedFunctionEntries,
            std::move(execution.detail),
            (execution.reason == GuestExecutionStopReason::UnimplementedImport ||
             execution.reason == GuestExecutionStopReason::RejectedImport)
                    ? execution.importArguments : std::nullopt,
            execution.reason == GuestExecutionStopReason::UnsupportedScalarAccess
                    ? execution.scalarAccess : std::nullopt,
    };
}

void GuestExecutionGuard::checkpoint(std::string_view functionLabel) {
    if (activeExecution == nullptr) {
        return;
    }
    if (activeExecution->completedFunctionEntries
            >= activeExecution->maximumFunctionEntries) {
        activeExecution->reason = GuestExecutionStopReason::FunctionEntryBudgetExceeded;
        activeExecution->detail = "Generated guest function-entry budget exhausted";
        throw StopSignal{};
    }
    ++activeExecution->completedFunctionEntries;
    if (activeExecution->traceSink != nullptr) {
        // The sink consumes the view synchronously; avoid a temporary owned
        // string for every translated function entry. Keep the same byte bound.
        (*activeExecution->traceSink)(functionLabel.substr(0, MaximumImportNameBytes));
    }
}

void GuestExecutionGuard::captureImportArguments(const GuestImportArguments& arguments) noexcept {
    if (activeExecution) { activeExecution->importArguments = arguments; }
}

extern "C" [[noreturn]] void recomp_guest_execution_scalar_access(const GuestScalarAccess& access) {
    if (!activeExecution) throw std::runtime_error("Unsupported scalar access outside guarded execution");
    activeExecution->reason = GuestExecutionStopReason::UnsupportedScalarAccess;
    activeExecution->detail = "Scalar access requires hardware, address-wrap or data-import binding semantics";
    activeExecution->scalarAccess = access;
    throw StopSignal{};
}

void GuestExecutionGuard::stopForUnimplementedImport(std::string_view importName) {
    if (activeExecution == nullptr) {
        throw std::runtime_error("Unimplemented guest import called outside guarded execution");
    }
    activeExecution->reason = GuestExecutionStopReason::UnimplementedImport;
    activeExecution->detail = importName.empty()
            ? "unknown-import"
            : boundedDetail(importName);
    throw StopSignal{};
}

void GuestExecutionGuard::stopForInvalidIndirectTarget() {
    if (activeExecution == nullptr) {
        throw std::runtime_error("Invalid guest indirect target outside guarded execution");
    }
    activeExecution->reason = GuestExecutionStopReason::InvalidIndirectTarget;
    activeExecution->detail = "Generated guest indirect target is not mapped";
    throw StopSignal{};
}

void GuestExecutionGuard::stopForRejectedImport(std::string_view importName) {
    if (activeExecution == nullptr) {
        throw std::runtime_error("Rejected guest import called outside guarded execution");
    }
    activeExecution->reason = GuestExecutionStopReason::RejectedImport;
    activeExecution->detail = importName.empty()
            ? "unknown-import" : boundedDetail(importName);
    throw StopSignal{};
}

bool GuestExecutionGuard::isActive() noexcept {
    return activeExecution != nullptr;
}

void GuestExecutionGuard::stopForGuestTrap() {
    if (activeExecution == nullptr) { throw std::runtime_error("Guest trap outside guarded execution"); }
    activeExecution->reason = GuestExecutionStopReason::GuestTrap;
    activeExecution->detail = "Architected guest trap condition was true";
    throw StopSignal{};
}

extern "C" [[noreturn]] void recomp_guest_execution_trap() {
    GuestExecutionGuard::stopForGuestTrap();
}

extern "C" void recomp_guest_execution_checkpoint() {
    GuestExecutionGuard::checkpoint();
}

extern "C" void recomp_guest_execution_checkpoint_named(const char* functionLabel) {
    if (functionLabel == nullptr) {
        GuestExecutionGuard::checkpoint();
        return;
    }
    std::size_t length = 0;
    while (length < MaximumImportNameBytes && functionLabel[length] != '\0') {
        ++length;
    }
    GuestExecutionGuard::checkpoint(std::string_view(functionLabel, length));
}

extern "C" void recomp_guest_execution_unimplemented_import(const char* importName) {
    if (importName == nullptr) {
        GuestExecutionGuard::stopForUnimplementedImport({});
    }
    std::size_t length = 0;
    while (length < MaximumImportNameBytes && importName[length] != '\0') {
        ++length;
    }
    GuestExecutionGuard::stopForUnimplementedImport(
            std::string_view(importName, length));
}

} // namespace recomp::bootstrap
