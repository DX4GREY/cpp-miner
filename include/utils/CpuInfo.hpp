#pragma once
//
// CpuInfo.hpp
// Small helper for detecting CPU topology on Linux: logical core count,
// model name, and helpers for pinning a thread to a specific core.
//

#include <string>
#include <cstddef>

namespace cppminer::utils {

struct CpuInfo {
    std::string modelName;      ///< e.g. "AMD Ryzen 9 5950X"
    unsigned int logicalCores;  ///< std::thread::hardware_concurrency()
};

/// Reads /proc/cpuinfo (falls back to hardware_concurrency() only) to
/// build a CpuInfo snapshot. Never throws; missing data is left blank/0.
CpuInfo detectCpuInfo() noexcept;

/// Pins the calling thread to a single logical CPU core using
/// pthread_setaffinity_np. Returns true on success. Safe to call even if
/// affinity pinning is not desired (caller checks config first).
bool pinThreadToCore(std::size_t coreIndex) noexcept;

/// Attempts to raise the calling thread's scheduling priority.
/// `niceLevel` follows POSIX nice() semantics (-20 highest .. 19 lowest);
/// requires appropriate privileges for negative values, and silently does
/// its best-effort otherwise.
void applyThreadPriority(const std::string& priority) noexcept;

} // namespace cppminer::utils
