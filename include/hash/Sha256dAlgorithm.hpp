#pragma once
//
// Sha256dAlgorithm.hpp
// Reference hash algorithm: double SHA-256 (SHA-256 applied twice), the
// same primitive used by Bitcoin-style coins. It is self-contained (no
// external dependency) which makes the whole mining pipeline -- job
// intake, nonce iteration, share submission, hashrate accounting --
// runnable and testable end to end.
//
// NOTE ON RandomX: config.ini defaults to algorithm=randomx because that
// is the most common CPU-mined coin (Monero) today. RandomX itself is a
// memory-hard, JIT-compiling virtual machine (~10k+ lines, depends on
// LLVM-lite JIT and 2 MiB scratchpads per thread) that must come from a
// dedicated library such as https://github.com/tevador/RandomX. This
// project's HashAlgorithmFactory is where you plug that library in --
// see hash/HashAlgorithmFactory.cpp for the extension point and
// docs/EXTENDING.md-style notes in the README for the exact steps.
//

#include "hash/IHashAlgorithm.hpp"

namespace cppminer::hash {

/// Double SHA-256 reference algorithm implementing IHashAlgorithm.
class Sha256dAlgorithm final : public IHashAlgorithm {
public:
    Sha256dAlgorithm() = default;

    [[nodiscard]] std::string name() const override { return "sha256d"; }

    void initialize(bool /*requestHugePages*/) override {
        // No scratchpad required for SHA-256d; nothing to do.
    }

    void hash(const std::uint8_t* input, std::size_t inputLen,
              std::uint32_t nonce, Digest256& outDigest) noexcept override;

private:
    // Scratch buffer reused across calls to avoid per-hash heap allocation.
    std::array<std::uint8_t, 256> buffer_{};
};

} // namespace cppminer::hash
