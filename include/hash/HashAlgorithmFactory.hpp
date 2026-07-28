#pragma once
//
// HashAlgorithmFactory.hpp
// Single place that maps a config.ini algorithm name to a concrete
// IHashAlgorithm implementation. This is the extension point described
// in the README for adding new algorithms.
//

#include "hash/IHashAlgorithm.hpp"
#include <string>

namespace cppminer::hash {

/// Creates a new IHashAlgorithm instance for the given algorithm name.
/// One instance is created per worker thread (see MinerEngine), so this
/// factory may be called concurrently from multiple threads.
///
/// Throws std::invalid_argument if the algorithm name is unrecognized.
HashAlgorithmPtr createAlgorithm(const std::string& name);

} // namespace cppminer::hash
