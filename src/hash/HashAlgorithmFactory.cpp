#include "hash/HashAlgorithmFactory.hpp"
#include "hash/Sha256dAlgorithm.hpp"

#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace cppminer::hash {

HashAlgorithmPtr createAlgorithm(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // ---------------------------------------------------------------
    // Extension point: add new "else if (lower == "...")" branches here
    // to register additional algorithms. For example, a real RandomX
    // binding would look like:
    //
    //   if (lower == "randomx") {
    //       return std::make_unique<RandomXAlgorithm>();
    //   }
    //
    // where RandomXAlgorithm wraps librandomx's randomx_create_vm /
    // randomx_calculate_hash calls behind the IHashAlgorithm interface.
    // ---------------------------------------------------------------

    if (lower == "sha256d" || lower == "sha256" || lower == "randomx") {
        // "randomx" currently maps to the bundled reference algorithm
        // (see Sha256dAlgorithm.hpp) until a real RandomX library is
        // linked in -- this keeps the full pipeline runnable out of the
        // box. Swap this branch to instantiate a real RandomX wrapper
        // once that dependency is added to CMakeLists.txt.
        return std::make_unique<Sha256dAlgorithm>();
    }

    throw std::invalid_argument("Unknown mining algorithm: " + name);
}

} // namespace cppminer::hash
