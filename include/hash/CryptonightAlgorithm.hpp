#pragma once
//
// CryptonightAlgorithm.hpp
// Self-contained CryptoNight (v1) implementation.
// CryptoNight is a memory-hard Proof-of-Work algorithm used by
// Monero (pre-RandomX), Bytecoin, and other Cryptonote coins.
//
// Reference: CryptoNight whitepaper and RandomX repository.
//

#include "hash/IHashAlgorithm.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <memory>

namespace cppminer::hash {

class CryptonightAlgorithm final : public IHashAlgorithm {
public:
    CryptonightAlgorithm();

    [[nodiscard]] std::string name() const override { return "cryptonight"; }

    void initialize(bool requestHugePages) override;

    void hash(const std::uint8_t* input, std::size_t inputLen,
              std::uint32_t nonce, Digest256& outDigest) noexcept override;

    ~CryptonightAlgorithm() override = default;

private:
    static constexpr std::size_t kScratchpadSize = 2 * 1024 * 1024; // 2 MiB
    static constexpr std::size_t kAesBlockSize = 16;
    static constexpr std::uint32_t kIterations = 524288;

    using KeccakState = std::array<std::uint64_t, 25>;
    using Scratchpad = std::array<std::uint8_t, kScratchpadSize>;

    // -- Keccak-f[1600] helpers --
    static constexpr std::uint64_t keccakRhoOffsets[24] = {
        1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 60, 18, 39, 61, 20, 44
    };
    static constexpr std::uint64_t keccakPi[24] = {
        0, 1, 35, 2, 49, 3, 52, 4, 55, 5, 56, 6, 58, 7, 61, 8, 67, 9, 68, 10, 83, 11, 84, 12
    };

    static void keccak1600(KeccakState& state);
    static void xorIntoState(KeccakState& state, const std::uint8_t* data, std::size_t len);

    // -- CryptoNight scratchpad init and main loop --
    void initScratchpad(const std::uint8_t* input, std::size_t inputLen, std::uint32_t nonce);
    void mainLoop(std::uint8_t* scratchpad) const;

    Scratchpad scratchpad_{};
};

} // namespace cppminer::hash