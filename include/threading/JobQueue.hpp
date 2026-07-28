#pragma once
//
// JobQueue.hpp
// A small single-producer/multi-consumer queue used to broadcast the
// *current* mining job to all worker threads. Mining jobs are not
// "consumed" one at a time like a work queue -- every worker restarts
// hashing against the newest job -- so this is really a thread-safe
// "latest value" slot with a version counter, guarded by a lightweight
// mutex (the update rate is low: only on new jobs from the pool, so a
// full lock-free structure would add complexity for no measurable gain).
//

#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <optional>

#include "miner/MiningJob.hpp"

namespace cppminer::threading {

/// Thread-safe holder for "the current job". Readers (worker threads)
/// take a shared lock and copy the job; the network thread takes a
/// unique lock only when a new job arrives.
class JobQueue {
public:
    /// Publishes a new job, bumping the version counter so workers can
    /// detect the change cheaply via `version()` before copying.
    void publish(cppminer::miner::MiningJob job) {
        std::unique_lock lock(mutex_);
        current_ = std::move(job);
        version_.fetch_add(1, std::memory_order_release);
    }

    /// Returns a copy of the current job, or std::nullopt if none has
    /// been published yet.
    [[nodiscard]] std::optional<cppminer::miner::MiningJob> current() const {
        std::shared_lock lock(mutex_);
        return current_;
    }

    /// Monotonically increasing version, bumped on every publish(). Worker
    /// threads poll this cheaply (relaxed atomic load) to decide whether
    /// they need to take the shared lock and fetch a fresh copy.
    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }

private:
    mutable std::shared_mutex mutex_;
    std::optional<cppminer::miner::MiningJob> current_;
    std::atomic<std::uint64_t> version_{0};
};

} // namespace cppminer::threading
