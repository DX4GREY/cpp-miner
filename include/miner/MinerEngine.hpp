#pragma once
//
// MinerEngine.hpp
// Owns the pool of mining worker threads (one per configured CPU
// thread), each running an independent instance of the selected hash
// algorithm against the current job from JobQueue. Tracks hash and
// share counters and exposes hashrate statistics.
//

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include <functional>

#include "config/ConfigManager.hpp"
#include "logger/Logger.hpp"
#include "threading/JobQueue.hpp"
#include "network/StratumClient.hpp"
#include "utils/Types.hpp"

namespace cppminer::miner {

/// Coordinates N worker threads that each hash against the current job
/// and submit shares back through a StratumClient.
class MinerEngine {
public:
    MinerEngine(const config::MinerConfig& config,
                logger::Logger& logger,
                network::StratumClient& stratumClient);

    ~MinerEngine();

    MinerEngine(const MinerEngine&) = delete;
    MinerEngine& operator=(const MinerEngine&) = delete;

    /// Publishes a newly-received job to all workers.
    void updateJob(const MiningJob& job);

    /// Updates the pool-assigned difficulty target used by all workers.
    void updateDifficulty(double difficulty) noexcept;

    /// Records the outcome of a share submission (called from the
    /// StratumClient's submit callback) into the accepted/rejected
    /// counters used for statistics reporting.
    void recordShareResult(bool accepted) noexcept;

    /// Spawns worker threads and begins hashing. No-op if already started.
    void start();

    /// Signals all worker threads to stop and joins them.
    void stop();

    /// Runs a fixed-duration, single-job-independent benchmark: spins up
    /// the configured thread count against synthetic input and reports
    /// aggregate hashrate. Blocks until the benchmark completes.
    void runBenchmark(std::chrono::seconds duration);

    /// Instantaneous hashrate in hashes/sec, computed over the last
    /// `print_hashrate_interval` window.
    [[nodiscard]] double currentHashrate() const noexcept;

    /// Average hashrate in hashes/sec since start().
    [[nodiscard]] double averageHashrate() const noexcept;

    [[nodiscard]] std::uint64_t sharesAccepted() const noexcept { return counters_.sharesAccepted.load(); }
    [[nodiscard]] std::uint64_t sharesRejected() const noexcept { return counters_.sharesRejected.load(); }
    [[nodiscard]] std::uint64_t totalHashes() const noexcept { return counters_.hashesTotal.load(); }

private:
    void workerLoop(std::size_t workerIndex);

    const config::MinerConfig& config_;
    logger::Logger& logger_;
    network::StratumClient& stratumClient_;

    threading::JobQueue jobQueue_;
    std::atomic<double> difficulty_{1.0};

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    utils::Counters counters_;

    // Hashrate bookkeeping.
    std::chrono::steady_clock::time_point startTime_;
    mutable std::atomic<std::uint64_t> lastSampleHashes_{0};
    mutable std::chrono::steady_clock::time_point lastSampleTime_;
};

} // namespace cppminer::miner
