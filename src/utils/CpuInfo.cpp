#include "utils/CpuInfo.hpp"

#include <thread>
#include <fstream>
#include <sstream>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

namespace cppminer::utils {

CpuInfo detectCpuInfo() noexcept {
    CpuInfo info;
    info.logicalCores = std::thread::hardware_concurrency();
    if (info.logicalCores == 0) {
        info.logicalCores = 1; // never report zero usable cores
    }

    try {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("model name", 0) == 0) {
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    info.modelName = line.substr(colon + 2);
                }
                break;
            }
        }
    } catch (...) {
        // Leave modelName empty; this is a best-effort utility.
    }

    if (info.modelName.empty()) {
        info.modelName = "Unknown CPU";
    }
    return info;
}

bool pinThreadToCore(std::size_t coreIndex) noexcept {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(static_cast<int>(coreIndex), &cpuSet);

    const pthread_t self = pthread_self();
    return pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuSet) == 0;
}

void applyThreadPriority(const std::string& priority) noexcept {
    // Map the user-facing priority setting onto a POSIX 'nice' delta.
    // Negative deltas require CAP_SYS_NICE / root; setpriority() will
    // simply fail silently otherwise, which is an acceptable fallback
    // for a mining workload running as an unprivileged user.
    int niceLevel = 0;
    if (priority == "low") {
        niceLevel = 10;
    } else if (priority == "high") {
        niceLevel = -5;
    } else {
        niceLevel = 0; // "normal"
    }
    setpriority(PRIO_PROCESS, 0, niceLevel);
}

} // namespace cppminer::utils
