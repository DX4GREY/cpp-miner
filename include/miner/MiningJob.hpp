#pragma once
//
// MiningJob.hpp
// Plain-data representation of a Stratum "mining.notify" job, plus the
// per-connection extranonce info received from "mining.subscribe".
// Deliberately a simple struct: it is copied into worker threads via
// JobQueue rather than shared with locking on every hash attempt.
//

#include <string>
#include <vector>
#include <cstdint>

namespace cppminer::miner {

/// A single unit of mining work handed out by the pool.
struct MiningJob {
    std::string jobId;
    std::string prevHash;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkleBranches;
    std::string version;
    std::string bits;        // encoded difficulty target ("nBits")
    std::string time;        // block time
    bool cleanJobs = false;  // true => discard all in-flight shares for old jobs

    // Subscription-scoped extranonce values (constant across jobs for a
    // given connection; copied in here for convenience).
    std::string extranonce1;
    unsigned int extranonce2Size = 4;

    // Pool-assigned difficulty (from "mining.set_difficulty"); the miner
    // must only submit shares whose hash meets this target or better.
    double difficulty = 1.0;
};

} // namespace cppminer::miner
