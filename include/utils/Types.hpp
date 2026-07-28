#pragma once
//
// Types.hpp
// Small shared value types used across module boundaries.
//

#include <cstdint>
#include <string>
#include <atomic>

namespace cppminer::utils {

/// Aggregate, lock-free share/hash counters updated from worker threads.
/// All fields are atomics so multiple mining workers can update them
/// concurrently without an external mutex.
struct Counters {
    std::atomic<std::uint64_t> hashesTotal{0};     ///< Lifetime hash count.
    std::atomic<std::uint64_t> sharesAccepted{0};
    std::atomic<std::uint64_t> sharesRejected{0};
};

} // namespace cppminer::utils
