#pragma once
//
// IHashAlgorithm.hpp
// Abstract interface every mining algorithm implements. Adding a new
// algorithm (e.g. a real RandomX binding, KawPow, SHA-256d, etc.) means
// writing one class against this interface and registering it in
// HashAlgorithmFactory -- nothing else in the codebase needs to change.
//

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <memory>

namespace cppminer::hash {

/// Fixed-size 256-bit digest used as the common output type for all
/// algorithms. Algorithms producing a different native width should
/// truncate/pad into this container at their own boundary.
using Digest256 = std::array<std::uint8_t, 32>;

/// Interface implemented by every supported mining algorithm.
///
/// Implementations must be safe to use one-instance-per-thread; the
/// MinerEngine constructs one instance per worker thread rather than
/// sharing a single instance, so implementations do not need to be
/// internally thread-safe.
class IHashAlgorithm {
public:
    virtual ~IHashAlgorithm() = default;

    /// Human-readable algorithm name (matches the config.ini value).
    [[nodiscard]] virtual std::string name() const = 0;

    /// Performs any per-thread setup required before hashing begins
    /// (e.g. allocating a scratchpad, requesting huge pages). Called
    /// once by the worker thread that owns this instance.
    virtual void initialize(bool requestHugePages) = 0;

    /// Computes the algorithm's digest for `input` with the given
    /// `nonce` mixed in, writing the result into `outDigest`.
    /// Implementations should avoid heap allocation on this hot path.
    virtual void hash(const std::uint8_t* input, std::size_t inputLen,
                       std::uint32_t nonce, Digest256& outDigest) noexcept = 0;
};

using HashAlgorithmPtr = std::unique_ptr<IHashAlgorithm>;

} // namespace cppminer::hash
