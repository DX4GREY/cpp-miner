#include "miner/MinerEngine.hpp"
#include "hash/HashAlgorithmFactory.hpp"
#include "utils/CpuInfo.hpp"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <random>
#include <cstdlib>

namespace cppminer::miner {

namespace {

/// Encodes an unsigned integer as a fixed-width, zero-padded hex string.
std::string toHex(std::uint32_t value, int width) {
    std::ostringstream oss;
    oss << std::hex << std::setw(width) << std::setfill('0') << value;
    return oss.str();
}

/// Very small helper: interprets the first 8 bytes of a digest as a
/// big-endian integer for coarse difficulty comparison. This mirrors the
/// spirit of Stratum's "compare hash to target" rule without pulling in
/// full 256-bit bignum arithmetic; production algorithms (RandomX, etc.)
/// should compare the full 256-bit value against the pool's exact target
/// using proper bignum comparison.
std::uint64_t leadingBits(const hash::Digest256& digest) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | digest[i];
    }
    return value;
}

/// Converts a pool difficulty value into an approximate threshold that a
/// share's leading 64 bits must fall below to be considered valid. This
/// is a simplified stand-in for exact target-vs-hash bignum comparison.
std::uint64_t difficultyToThreshold(double difficulty) noexcept {
    if (difficulty <= 0.0) difficulty = 1.0;
    constexpr double kDiff1 = static_cast<double>(0xFFFFFFFFFFFFFFFFull); // simplified diff-1 ceiling
    const double scaled = kDiff1 / difficulty;
    // Clamp to the representable uint64_t range: difficulty < 1 would
    // otherwise request a threshold larger than 2^64-1, which is
    // undefined behavior to cast directly.
    if (scaled >= kDiff1) return 0xFFFFFFFFFFFFFFFFull;
    return static_cast<std::uint64_t>(scaled);
}

} // namespace

MinerEngine::MinerEngine(const config::MinerConfig& config,
                          logger::Logger& logger,
                          network::StratumClient& stratumClient)
    : config_(config), logger_(logger), stratumClient_(stratumClient) {}

MinerEngine::~MinerEngine() {
    stop();
}

void MinerEngine::updateJob(const MiningJob& job) {
    jobQueue_.publish(job);
}

void MinerEngine::updateDifficulty(double difficulty) noexcept {
    if (const char* env = std::getenv("CPP_MINER_DBG_DIFF")) {
        const double overrideDiff = std::atof(env);
        if (overrideDiff > 0.0) {
            logger_.warn("DBG: overriding difficulty " + std::to_string(difficulty) +
                          " -> " + std::to_string(overrideDiff));
            difficulty_.store(overrideDiff, std::memory_order_relaxed);
            return;
        }
    }
    difficulty_.store(difficulty, std::memory_order_relaxed);
}

void MinerEngine::recordShareResult(bool accepted) noexcept {
    if (accepted) {
        counters_.sharesAccepted.fetch_add(1, std::memory_order_relaxed);
    } else {
        counters_.sharesRejected.fetch_add(1, std::memory_order_relaxed);
    }
}

void MinerEngine::start() {
    if (running_.exchange(true)) return;

    startTime_ = std::chrono::steady_clock::now();
    lastSampleTime_ = startTime_;
    lastSampleHashes_.store(0);

    unsigned int threadCount = config_.threads;
    if (threadCount == 0) {
        threadCount = utils::detectCpuInfo().logicalCores;
    }
    logger_.info("MinerEngine: starting " + std::to_string(threadCount) + " worker thread(s).");

    workers_.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i) {
        workers_.emplace_back(&MinerEngine::workerLoop, this, static_cast<std::size_t>(i));
    }
}

void MinerEngine::stop() {
    if (!running_.exchange(false)) return;
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void MinerEngine::workerLoop(std::size_t workerIndex) {
    if (config_.cpuAffinity) {
        utils::pinThreadToCore(workerIndex % std::thread::hardware_concurrency());
    }

    auto algorithm = hash::createAlgorithm(config_.algorithm);
    algorithm->initialize(config_.hugePages);

    std::uint64_t lastJobVersion = 0;
    std::uint32_t nonce = static_cast<std::uint32_t>(workerIndex) * 0x1000000u;
    const std::uint32_t nonceStep = static_cast<std::uint32_t>(std::max<std::size_t>(1, workers_.capacity()));

    while (running_.load(std::memory_order_relaxed)) {
        const auto jobOpt = jobQueue_.current();
        if (!jobOpt) {
            // No job yet: idle briefly rather than spin-waiting on nothing.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        const MiningJob job = *jobOpt; // cheap copy: small struct of strings

        const std::string extranonce2 = toHex(static_cast<std::uint32_t>(workerIndex), job.extranonce2Size * 2);
        const auto threshold = difficultyToThreshold(difficulty_.load(std::memory_order_relaxed));

        if (!job.blob.empty()) {
            // MoneroOcean-style: mine directly against the provided blob.
            // The pool expects us to replace the 4-byte nonce field starting at
            // blob offset 0 (LE) and submit the modified blob back.
            if (job.blob.size() < 4) {
                continue; // malformed blob, wait for next job
            }

            std::string workBlob = job.blob;
            // The blob is hex; each byte is represented by two hex chars.
            const std::size_t blobBytes = workBlob.size() / 2;
            logger_.info("DBG: blob size hex=" + std::to_string(workBlob.size()) +
                          " bytes=" + std::to_string(blobBytes) + " first64=" + workBlob.substr(0, 64));
            // Try writing the nonce near the end of the blob to avoid out-of-bounds
            // writes if the pool uses a short blob. We will scan backwards from the
            // end to find the first occurrence of the original 4-byte nonce pattern.
            std::size_t nonceOffsetBytes = (blobBytes >= 8 ? blobBytes - 4 : 0);
            std::size_t nonceOffsetChars = nonceOffsetBytes * 2;
            // If the write didn't stick, scan backwards from the end to find a
            // location that accepts new nonce values.
            {
                const std::string testNonce = "01234567";
                std::string probe = workBlob;
                bool wroteAny = false;
                for (int b = 0; b < 4; ++b) {
                    probe[nonceOffsetChars + b * 2] = testNonce[b * 2];
                    probe[nonceOffsetChars + b * 2 + 1] = testNonce[b * 2 + 1];
                }
                bool changed = false;
                for (int b = 0; b < 4; ++b) {
                    if (probe.substr(nonceOffsetChars + b * 2, 2) != workBlob.substr(nonceOffsetChars + b * 2, 2)) {
                        changed = true;
                        break;
                    }
                }
                if (!changed && blobBytes >= 12) {
                    // scan backwards for first writable 4-byte slot
                    bool found = false;
                    for (std::size_t off = blobBytes - 4; off > 0; --off) {
                        std::string p2 = workBlob;
                        const std::size_t c = off * 2;
                        for (int b = 0; b < 4; ++b) {
                            p2[c + b * 2] = testNonce[b * 2];
                            p2[c + b * 2 + 1] = testNonce[b * 2 + 1];
                        }
                        bool ch = false;
                        for (int b = 0; b < 4; ++b) {
                            if (p2.substr(c + b * 2, 2) != workBlob.substr(c + b * 2, 2)) {
                                ch = true;
                                break;
                            }
                        }
                        if (ch) {
                            nonceOffsetBytes = off;
                            nonceOffsetChars = c;
                            found = true;
                            logger_.warn("DBG: using alternate nonce offset=" + std::to_string(off));
                            break;
                        }
                    }
                    if (!found) {
                        // fallback: try writing at the very end again
                        nonceOffsetBytes = blobBytes - 4;
                        nonceOffsetChars = nonceOffsetBytes * 2;
                        logger_.warn("DBG: falling back to end nonce offset=" + std::to_string(nonceOffsetBytes));
                    }
                }
            }
            const std::uint32_t baseNonce = nonce;

            // Hash a batch of nonces before checking for job changes / stop
            // requests, to amortize the atomic loads below.
            constexpr int kBatchSize = 2048;
            for (int i = 0; i < kBatchSize; ++i) {
                // Encode nonce as 8-char hex LE into the blob.
                const std::string nonceHex = toHex(baseNonce + static_cast<std::uint32_t>(i) * nonceStep, 8);
                for (int b = 0; b < 4; ++b) {
                    workBlob[nonceOffsetChars + b * 2]     = nonceHex[b * 2];
                    workBlob[nonceOffsetChars + b * 2 + 1] = nonceHex[b * 2 + 1];
                }

                // Convert hex blob to raw bytes for hashing.
                std::vector<std::uint8_t> rawBlob(workBlob.size() / 2);
                for (std::size_t j = 0; j < rawBlob.size(); ++j) {
                    const unsigned int byteVal = std::stoul(workBlob.substr(j * 2, 2), nullptr, 16);
                    rawBlob[j] = static_cast<std::uint8_t>(byteVal);
                }

                hash::Digest256 digest{};
                algorithm->hash(reinterpret_cast<const std::uint8_t*>(rawBlob.data()),
                                 rawBlob.size(), 0, digest);
                counters_.hashesTotal.fetch_add(1, std::memory_order_relaxed);

                if (leadingBits(digest) < threshold && job.jobId != "synthetic-standby") {
                    // Found a share: submit using the modified blob.
                    stratumClient_.submitBlobShare(job.jobId, workBlob);
                    logger_.info("DBG: share candidate nonce=" + nonceHex + " leading=" +
                                 std::to_string(leadingBits(digest)) + " threshold=" +
                                 std::to_string(threshold) + " nonceOffset=" +
                                 std::to_string(nonceOffsetBytes));
                }
            }
        } else {
            // Classic assembly: build header from individual fields.
            std::string headerStr = job.prevHash + job.coinbase1 + job.extranonce1 +
                                     extranonce2 + job.coinbase2 + job.version + job.bits + job.time;
            if (headerStr.size() > 200) headerStr.resize(200);

            // Hash a batch of nonces before checking for job changes / stop
            // requests, to amortize the atomic loads below.
            constexpr int kBatchSize = 2048;
            for (int i = 0; i < kBatchSize; ++i) {
                hash::Digest256 digest{};
                algorithm->hash(reinterpret_cast<const std::uint8_t*>(headerStr.data()),
                                 headerStr.size(), nonce, digest);
                counters_.hashesTotal.fetch_add(1, std::memory_order_relaxed);

                if (leadingBits(digest) < threshold && job.jobId != "synthetic-standby") {
                    // Found a share meeting the current target: submit it.
                    stratumClient_.submitShare(job.jobId, extranonce2, job.time, toHex(nonce, 8));
                }
                nonce += nonceStep;
            }
        }

        if (!running_.load(std::memory_order_relaxed)) break;
        if (jobQueue_.version() != lastJobVersion) {
            lastJobVersion = jobQueue_.version();
        }
    }
}

void MinerEngine::runBenchmark(std::chrono::seconds duration) {
    logger_.info("MinerEngine: running benchmark for " + std::to_string(duration.count()) + "s...");

    // Feed a synthetic job so the normal worker loop can run unmodified.
    MiningJob syntheticJob;
    syntheticJob.jobId = "benchmark";
    syntheticJob.prevHash = std::string(64, '0');
    syntheticJob.coinbase1 = "benchmark-coinbase1";
    syntheticJob.coinbase2 = "benchmark-coinbase2";
    syntheticJob.version = "20000000";
    syntheticJob.bits = "1d00ffff";
    syntheticJob.time = "00000000";
    syntheticJob.extranonce1 = "aabbccdd";
    syntheticJob.extranonce2Size = 4;
    updateJob(syntheticJob);
    // A very high synthetic difficulty means the "share found" branch is
    // essentially never taken during a benchmark, since there is no real
    // pool connection to submit to.
    updateDifficulty(1.0e12);

    start();
    std::this_thread::sleep_for(duration);
    const double hashrate = averageHashrate();
    stop();

    logger_.info("Benchmark complete: " + std::to_string(hashrate) + " H/s average across " +
                  std::to_string(workers_.capacity()) + " thread(s).");
}

double MinerEngine::currentHashrate() const noexcept {
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t nowHashes = counters_.hashesTotal.load(std::memory_order_relaxed);

    const double elapsedSec = std::chrono::duration<double>(now - lastSampleTime_).count();
    const std::uint64_t previousHashes = lastSampleHashes_.load(std::memory_order_relaxed);

    double rate = 0.0;
    if (elapsedSec > 0.001 && nowHashes >= previousHashes) {
        rate = static_cast<double>(nowHashes - previousHashes) / elapsedSec;
    }

    lastSampleHashes_.store(nowHashes, std::memory_order_relaxed);
    lastSampleTime_ = now;
    return rate;
}

double MinerEngine::averageHashrate() const noexcept {
    const double elapsedSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime_).count();
    if (elapsedSec <= 0.001) return 0.0;
    return static_cast<double>(counters_.hashesTotal.load(std::memory_order_relaxed)) / elapsedSec;
}

} // namespace cppminer::miner
