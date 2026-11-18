#include "guest_cooperative_executor.h"
#include "guest_thread_priority.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace recomp::bootstrap {
namespace {
struct Cancelled final {};
struct ThreadExited final {};
}

thread_local GuestCooperativeExecutor::Current* GuestCooperativeExecutor::current_ = nullptr;

GuestCooperativeExecutor::GuestCooperativeExecutor(ThreadFactory factory)
    : factory_(factory ? std::move(factory) : ThreadFactory([](Entry entry) {
          return std::thread(std::move(entry));
      })) {}

bool GuestCooperativeExecutor::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool GuestCooperativeExecutor::terminateCurrentThread(std::uint32_t token,const Ready& commitExit) {
    if (!current_ || current_->executor!=this || !current_->lock->owns_lock() ||
        !GuestExecutionGuard::isActive() || terminal_ || nativeNonYielding_ ||
        nativeHandoff_ || !commitExit || jobs_[current_->index]->token!=token ||
        jobs_[current_->index]->finished) return false;
    if (!commitExit()) return false;
    throw ThreadExited{};
}

GuestCooperativeResult GuestCooperativeExecutor::run(
        std::uint32_t mainToken,
        const Entry& entry,
        const GuestCooperativeLimits& limits,
        const BindThread& bindThread,
        const GuestExecutionGuard::FunctionTraceSink& traceSink) {
    if (mainToken == 0 || !entry || !bindThread) {
        return {{GuestExecutionStopReason::InvalidEntry, 0, "Executor entry or binding is empty"}};
    }
    if (limits.functionEntries == 0 || limits.quantum == 0 || limits.quantum > 1000000
            || limits.tasks == 0 || limits.tasks > MaximumTasks) {
        return {{GuestExecutionStopReason::InvalidBudget, 0, "Executor limits are outside their bounds"}};
    }
    if (current_ != nullptr || GuestExecutionGuard::isActive()) {
        return {{GuestExecutionStopReason::NestedRun, 0, "Nested cooperative execution is not allowed"}};
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return {{GuestExecutionStopReason::NestedRun, 0, "Executor already has an active run"}};
    }
    std::unique_lock lock(gate_);
    entries_ = switches_ = waits_ = 0;
    workersEntered_ = 0;
    boundToken_ = 0;
    jobCount_ = selected_ = 0;
    reschedule_ = false;
    nativeNonYielding_.reset();
    nativeHandoff_.reset();
    terminal_.reset();
    limits_ = limits;
    try {
        bindThread_ = bindThread;
        traceSink_ = traceSink;
        auto main = std::make_unique<Job>();
        main->token = mainToken;
        main->entry = entry;
        jobs_[0] = std::move(main);
        jobCount_ = 1;
        execute(0, lock);
        changed_.wait(lock, [&] { return terminal_.has_value(); });
    } catch (...) {
        // Setup/coordination failures are distinct from a deliberate guest stop.
        if (!terminal_) {
            terminal_.emplace(GuestExecutionResult{
                    GuestExecutionStopReason::HostException, entries_, "Executor setup failed"});
        }
        notifySchedulingChange();
    }
    // Waiting tasks need this gate to unwind. Never join while holding it.
    lock.unlock();
    for (std::size_t i = 1; i < jobCount_; ++i) {
        if (jobs_[i]->host.joinable()) { jobs_[i]->host.join(); }
    }
    lock.lock();
    auto result = GuestCooperativeResult{std::move(*terminal_),
            jobCount_ == 0 ? 0 : jobCount_ - 1, workersEntered_, switches_, waits_};
    result.execution.completedFunctionEntries = entries_;
    for (auto& job : jobs_) { job.reset(); }
    jobCount_ = 0;
    bindThread_ = {};
    traceSink_ = {};
    terminal_.reset();
    running_.store(false, std::memory_order_release);
    return result;
}

std::uint32_t GuestCooperativeExecutor::currentTaskToken() const noexcept {
    if(!current_ || current_->executor!=this || !current_->lock->owns_lock() ||
       !GuestExecutionGuard::isActive() || terminal_ || jobs_[current_->index]->finished)return 0;
    return jobs_[current_->index]->token;
}
bool GuestCooperativeExecutor::parkedTask(std::uint32_t token) const noexcept {
    if(!currentTaskToken() || !token)return false;
    for(std::size_t i=0;i<jobCount_;++i)
        if(jobs_[i]->token==token)return jobs_[i]->entered &&
            !jobs_[i]->finished && bool(jobs_[i]->ready);
    return false;
}

bool GuestCooperativeExecutor::enqueue(std::uint32_t token, const Entry& entry, std::int32_t priority) {
    if (current_ == nullptr || current_->executor != this || !current_->lock->owns_lock()
            || !GuestExecutionGuard::isActive() || terminal_ || token == 0 || !entry
            || jobCount_ >= limits_.tasks || !validPortableSchedulerPriority(priority,limits_.nativeHighPriorityTiers)) {
        return false;
    }
    for (std::size_t i = 0; i < jobCount_; ++i) {
        if (jobs_[i]->token == token) { return false; }
    }
    const auto index = jobCount_;
    try {
        auto job = std::make_unique<Job>();
        job->token = token;
        job->entry = entry;
        job->priority = priority;
        jobs_[index] = std::move(job);
        auto host = factory_([this, index] { worker(index); });
        if (!host.joinable()) {
            jobs_[index].reset();
            return false;
        }
        jobs_[index]->host = std::move(host);
        ++jobCount_;
        reschedule_ = true;
        return true;
    } catch (const std::bad_alloc&) {
        jobs_[index].reset();
        return false;
    } catch (const std::system_error&) {
        jobs_[index].reset();
        return false;
    }
}

bool GuestCooperativeExecutor::boostEventWaiters(const std::uint32_t* tokens,std::size_t count) {
    if (!current_ || current_->executor!=this || !current_->lock->owns_lock() ||
        !GuestExecutionGuard::isActive() || terminal_ || !tokens || !count || count>MaximumTasks) return false;
    std::array<Job*,MaximumTasks> selected{};
    for(std::size_t i=0;i<count;++i) {
        for(std::size_t j=0;j<i;++j) if(tokens[i]==tokens[j]) return false;
        for(std::size_t j=0;j<jobCount_;++j) if(jobs_[j]->token==tokens[i]) {
            auto& job=*jobs_[j];
            if(j==current_->index || job.finished || !job.ready) return false;
            selected[i]=&job;break;
        }
        if(!selected[i]) return false;
    }
    for(std::size_t i=0;i<count;++i) selected[i]->eventWakeBoost=true;
    return true;
}

bool GuestCooperativeExecutor::setPriority(std::uint32_t token, std::int32_t priority) {
    if (current_ == nullptr || current_->executor != this || !current_->lock->owns_lock()
            || !GuestExecutionGuard::isActive() || terminal_ || !validPortableSchedulerPriority(priority,limits_.nativeHighPriorityTiers)) {
        return false;
    }
    for (std::size_t i = 0; i < jobCount_; ++i) {
        if (jobs_[i]->token == token && !jobs_[i]->finished) {
            jobs_[i]->priority = priority;
            // Do not reset the consumed slice: repeated priority writes must
            // not let the current task evade rotation indefinitely.
            return true;
        }
    }
    return false;
}

void GuestCooperativeExecutor::worker(std::size_t index) {
    std::unique_lock lock(gate_);
    jobs_[index]->selected.wait(lock, [&] { return terminal_ || selected_ == index; });
    if (!terminal_) { execute(index, lock); }
    else { jobs_[index]->finished = true; }
}

void GuestCooperativeExecutor::notifySchedulingChange() {
    if (terminal_) {
        changed_.notify_all(); // Coordinator/deadline waiter.
        for (std::size_t i = 0; i < jobCount_; ++i) jobs_[i]->selected.notify_one();
    } else if (selected_ < jobCount_) {
        jobs_[selected_]->selected.notify_one();
    }
}

void GuestCooperativeExecutor::execute(std::size_t index, std::unique_lock<std::mutex>& lock) {
    Current context{this, index, &lock};
    auto* previous = current_;
    current_ = &context;
    GuestExecutionGuard guard;
    auto result = guard.run(limits_.functionEntries, [&] {
        bind(index);
        jobs_[index]->entered=true;
        if (index != 0) { ++workersEntered_; }
        try { jobs_[index]->entry(); }
        catch (const ThreadExited&) {
            // Only terminateCurrentThread can construct this private signal.
            // The registry has already published completion under this gate;
            // finish() now retires the task and selects its real waiters/peers.
        }
    }, [&](std::string_view label) { checkpoint(label); });
    current_ = previous;
    finish(index, std::move(result), lock);
}

void GuestCooperativeExecutor::bind(std::size_t index) {
    const auto token = jobs_[index]->token;
    bindThread_(token);
    if (boundToken_ != 0 && token != boundToken_) { ++switches_; }
    boundToken_ = token;
}

std::size_t GuestCooperativeExecutor::nextTask(std::size_t after) {
    try {
        if (nativeHandoff_) {
            const auto& handoff=*nativeHandoff_;
            return jobs_[handoff.worker]->finished?handoff.parent:handoff.worker;
        }
        for (std::size_t offset = 1; offset <= jobCount_; ++offset) {
            const auto candidate = (after + offset) % jobCount_;
            auto& job = *jobs_[candidate];
            if (job.finished) { continue; }
            if (job.ready) {
                const auto ready = job.ready; // Keep the invoked callback alive while clearing its slot.
                if (!ready()) {
                    if (job.deadline && std::chrono::steady_clock::now() >= *job.deadline) {
                        // Poll once more at/after expiry if time advanced during the first check.
                        if (!ready()) { throw std::logic_error("Expired wait readiness did not complete"); }
                    } else { continue; }
                }
                job.ready = {};
                job.deadline.reset();
            }
            return candidate;
        }
    } catch (...) {
        if (!terminal_) {
            terminal_.emplace(GuestExecutionResult{GuestExecutionStopReason::HostException,
                    entries_, "Cooperative readiness check failed"});
        }
        notifySchedulingChange();
    }
    return MaximumTasks;
}

std::size_t GuestCooperativeExecutor::selectTask(std::size_t after, std::unique_lock<std::mutex>& lock) {
    try {
        for (;;) {
            const auto next = nextTask(after);
            if (terminal_ || next != MaximumTasks) { return next; }
            std::optional<std::chrono::steady_clock::time_point> earliest;
            for (std::size_t i = 0; i < jobCount_; ++i) {
                const auto& job = *jobs_[i];
                if (!job.finished && job.ready && job.deadline && (!earliest || *job.deadline < *earliest)) {
                    earliest = job.deadline;
                }
            }
            if (!earliest) { return MaximumTasks; }
            // No guest can run. Sleep until the nearest deadline without spinning
            // or holding the gate; the outer process watchdog still bounds this.
            changed_.wait_until(lock, *earliest);
        }
    } catch (...) {
        if (!terminal_) { terminal_.emplace(GuestExecutionResult{GuestExecutionStopReason::HostException,
                entries_, "Cooperative deadline coordination failed"}); }
        notifySchedulingChange();
        return MaximumTasks;
    }
}

bool GuestCooperativeExecutor::waitUntil(const Ready& ready,
        std::optional<std::chrono::steady_clock::time_point> deadline) {
    return waitUntilImpl(ready, deadline, false);
}

bool GuestCooperativeExecutor::delayUntil(std::chrono::steady_clock::time_point deadline) {
    return waitUntilImpl([deadline] { return std::chrono::steady_clock::now() >= deadline; }, deadline, true);
}

bool GuestCooperativeExecutor::runNativeNonYielding(const Entry& entry,std::uint32_t maximumEntries) {
    if (!current_ || current_->executor!=this || !current_->lock->owns_lock() ||
        !GuestExecutionGuard::isActive() || terminal_ || nativeNonYielding_ ||
        !entry || !maximumEntries || maximumEntries>4096) return false;
    nativeNonYielding_=NonYielding{current_->index,entries_,maximumEntries};
    struct Clear { std::optional<NonYielding>& scope;~Clear() { scope.reset(); } } clear{nativeNonYielding_};
    entry();
    return true;
}
bool GuestCooperativeExecutor::runNativeWorkerNext(std::uint32_t token,std::uint32_t maximumEntries) {
    if (!current_ || current_->executor!=this || !current_->lock->owns_lock() ||
        !GuestExecutionGuard::isActive() || terminal_ || nativeNonYielding_ || nativeHandoff_ ||
        !token || !maximumEntries || maximumEntries>4096) return false;
    const auto parent=current_->index;
    std::size_t worker=MaximumTasks;
    for (std::size_t i=1;i<jobCount_;++i) if (jobs_[i]->token==token) {worker=i;break;}
    if (worker==MaximumTasks || worker==parent || jobs_[worker]->entered || jobs_[worker]->finished || jobs_[worker]->ready)
        return false;
    nativeHandoff_=NativeHandoff{parent,worker,entries_,maximumEntries};
    struct Clear {std::optional<NativeHandoff>& scope;~Clear(){scope.reset();}} clear{nativeHandoff_};
    selected_=worker;notifySchedulingChange();
    jobs_[parent]->selected.wait(*current_->lock,[&] {return terminal_ || selected_==parent;});
    if (terminal_) throw Cancelled{};
    bind(parent);
    return jobs_[worker]->finished;
}

bool GuestCooperativeExecutor::waitUntilImpl(const Ready& ready,
        std::optional<std::chrono::steady_clock::time_point> deadline, bool yieldCurrentSlice) {
    if (current_ == nullptr || current_->executor != this || !current_->lock->owns_lock()
            || !GuestExecutionGuard::isActive() || terminal_ || nativeNonYielding_ || nativeHandoff_ || !ready
            || waits_ >= limits_.functionEntries) { return false; }
    ++waits_;
    if (!yieldCurrentSlice && ready()) { return true; }
    const auto index = current_->index;
    jobs_[index]->sliceEntries = 0;
    jobs_[index]->eventWakeBoost = false;
    jobs_[index]->ready = ready;
    jobs_[index]->deadline = deadline;
    try {
        const auto next = selectTask(index, *current_->lock);
        if (terminal_) { throw Cancelled{}; }
        if (next == index) { return true; } // A readiness check completed immediately.
        if (next == MaximumTasks) {
            jobs_[index]->ready = {};
            jobs_[index]->deadline.reset();
            return false;
        }
        selected_ = next;
        notifySchedulingChange();
        jobs_[index]->selected.wait(*current_->lock, [&] { return terminal_ || selected_ == index; });
        if (terminal_) { throw Cancelled{}; }
        bind(index);
        return true;
    } catch (...) {
        jobs_[index]->ready = {};
        jobs_[index]->deadline.reset();
        throw;
    }
}

void GuestCooperativeExecutor::checkpoint(std::string_view label) {
    if (terminal_) { throw Cancelled{}; }
    if (nativeHandoff_ && (current_->index!=nativeHandoff_->worker || entries_-nativeHandoff_->start>=nativeHandoff_->budget)) {
        terminal_.emplace(GuestExecutionResult{GuestExecutionStopReason::FunctionEntryBudgetExceeded,
                entries_,"Exclusive native worker handoff budget or ownership boundary"});
        notifySchedulingChange();throw Cancelled{};
    }
    if (nativeNonYielding_ && current_->index!=nativeNonYielding_->index)
        throw std::logic_error("Native non-yielding scope lost its owning task");
    if (nativeNonYielding_ && entries_-nativeNonYielding_->start>=nativeNonYielding_->budget) {
        terminal_.emplace(GuestExecutionResult{GuestExecutionStopReason::FunctionEntryBudgetExceeded,
                entries_,"Native non-yielding function-entry budget exhausted"});
        notifySchedulingChange();throw Cancelled{};
    }
    if (entries_ >= limits_.functionEntries) {
        terminal_.emplace(GuestExecutionResult{GuestExecutionStopReason::FunctionEntryBudgetExceeded,
                entries_, "Global cooperative function-entry budget exhausted"});
        notifySchedulingChange();
        throw Cancelled{};
    }
    ++entries_;
    if (traceSink_) { traceSink_(label); }
    const auto index = current_->index;
    auto& job = *jobs_[index];
    ++job.sliceEntries;
    if (nativeNonYielding_) return; // Accounting/stop checks above still apply.
    const auto shift = std::min(3,portablePrioritySliceShift(job.priority)+(job.eventWakeBoost?1:0));
    const auto quantum = shift >= 0
            ? std::uint64_t{limits_.quantum} << shift
            : std::max(std::uint64_t{1}, std::uint64_t{limits_.quantum} >> -shift);
    if (!reschedule_ && job.sliceEntries < quantum) { return; }
    reschedule_ = false;
    job.sliceEntries = 0;
    job.eventWakeBoost = false;
    const auto next = nextTask(index);
    if (terminal_) { throw Cancelled{}; }
    if (next == index || next == MaximumTasks) { return; }
    selected_ = next;
    notifySchedulingChange();
    jobs_[index]->selected.wait(*current_->lock, [&] { return terminal_ || selected_ == index; });
    if (terminal_) { throw Cancelled{}; }
    bind(index);
}

void GuestCooperativeExecutor::finish(std::size_t index, GuestExecutionResult result, std::unique_lock<std::mutex>& lock) {
    jobs_[index]->finished = true;
    jobs_[index]->ready = {};
    jobs_[index]->deadline.reset();
    if (!terminal_ && !result.completed()) {
        result.completedFunctionEntries = entries_;
        terminal_.emplace(std::move(result));
    }
    if (!terminal_) {
        selected_ = selectTask(index, lock);
        if (!terminal_ && selected_ == MaximumTasks) {
            bool allFinished = true;
            for (std::size_t i = 0; i < jobCount_; ++i) {
                allFinished = allFinished && jobs_[i]->finished;
            }
            terminal_.emplace(GuestExecutionResult{
                    allFinished ? GuestExecutionStopReason::Completed : GuestExecutionStopReason::NoRunnableTask,
                    entries_, allFinished ? "" : "All remaining tasks wait without a runnable signal producer"});
        }
    }
    notifySchedulingChange();
}

} // namespace recomp::bootstrap
