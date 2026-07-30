#include "hash/CryptonightAlgorithm.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace cppminer::hash {

constexpr std::size_t kScratchpadSize = 2 * 1024 * 1024;
constexpr std::size_t kAesBlockSize = 16;
constexpr std::uint32_t kIterations = 524288;

namespace detail {

void xorIntoState(std::array<std::uint64_t, 25>& state,
                  const std::uint8_t* data,
                  std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        state[i / 8] ^= static_cast<std::uint64_t>(data[i]) << ((i % 8) * 8);
    }
}

std::array<std::uint64_t, 25> keccak1600(std::array<std::uint64_t, 25> state) {
    static const std::uint64_t RC[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
        0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
        0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
        0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x800000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
        0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
        0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
    };

    for (std::uint32_t round = 0; round < 24; round += 2) {
        std::uint64_t c[5], d[5];
        for (std::uint32_t x = 0; x < 5; ++x) c[x] = state[x] ^ state[5 + x] ^ state[10 + x] ^ state[15 + x] ^ state[20 + x];
        d[0] = c[4] ^ ((c[1] << 1) | (c[1] >> 63));
        d[1] = c[0] ^ ((c[2] << 1) | (c[2] >> 63));
        d[2] = c[1] ^ ((c[3] << 1) | (c[3] >> 63));
        d[3] = c[2] ^ ((c[4] << 1) | (c[4] >> 63));
        d[4] = c[3] ^ ((c[0] << 1) | (c[0] >> 63));
        for (std::uint32_t x = 0; x < 5; ++x) state[x] ^= d[x], state[5 + x] ^= d[x], state[10 + x] ^= d[x], state[15 + x] ^= d[x], state[20 + x] ^= d[x];

        std::uint64_t b[25];
        for (std::uint32_t x = 0; x < 5; ++x) {
            for (std::uint32_t y = 0; y < 5; ++y) {
                std::uint32_t idx = (x * 5 + y + 24 + x + (x + 2 * y) / 5) % 25;
                std::uint32_t shift = (x + 2 * y) % 64;
                b[idx] = (state[x + y * 5] << shift) | (state[x + y * 5] >> (64 - shift));
            }
        }
        for (std::uint32_t y = 0; y < 5; ++y) {
            for (std::uint32_t x = 0; x < 5; ++x) {
                state[x + y * 5] = b[x + y * 5] ^ (~b[((x + 1) % 5) + y * 5] & b[((x + 2) % 5) + y * 5]);
            }
        }
        state[0] ^= RC[round];
        if (round + 1 < 24) state[0] ^= RC[round + 1];
    }
    return state;
}

}

CryptonightAlgorithm::CryptonightAlgorithm() = default;

void CryptonightAlgorithm::initialize(bool) {}

void CryptonightAlgorithm::hash(const std::uint8_t* input,
                                std::size_t inputLen,
                                std::uint32_t nonce,
                                Digest256& outDigest) noexcept {
    std::array<std::uint8_t, 76> inputWithNonce = {};
    std::size_t copyLen = std::min(inputLen, static_cast<std::size_t>(72));
    std::copy_n(input, copyLen, inputWithNonce.begin());
    inputWithNonce[72] = static_cast<std::uint8_t>((nonce >> 0) & 0xff);
    inputWithNonce[73] = static_cast<std::uint8_t>((nonce >> 8) & 0xff);
    inputWithNonce[74] = static_cast<std::uint8_t>((nonce >> 16) & 0xff);
    inputWithNonce[75] = static_cast<std::uint8_t>((nonce >> 24) & 0xff);

    initScratchpad(inputWithNonce.data(), inputWithNonce.size(), nonce);
    mainLoop(scratchpad_.data());

    for (std::size_t i = 0; i < outDigest.size(); ++i) {
        outDigest[i] = scratchpad_[i % scratchpad_.size()];
    }
}

void CryptonightAlgorithm::initScratchpad(const std::uint8_t* input,
                                          std::size_t inputLen,
                                          std::uint32_t) {
    KeccakState keccakState{};
    detail::xorIntoState(keccakState, input, inputLen);
    keccakState = detail::keccak1600(keccakState);

    std::array<std::uint8_t, 32> keccakHash = {};
    for (std::size_t i = 0; i < 25; ++i) {
        for (std::size_t b = 0; b < 8; ++b) {
            keccakHash[i / 8 * 8 + b] ^= static_cast<std::uint8_t>((keccakState[i] >> (b * 8)) & 0xff);
        }
    }

    std::array<std::uint8_t, 16> aesIV = {};
    std::copy_n(keccakHash.begin(), 16, aesIV.begin());

    for (std::size_t i = 0; i < kScratchpadSize; i += kAesBlockSize) {
        for (std::size_t j = 0; j < kAesBlockSize; ++j) scratchpad_[i + j] = aesIV[j] ^ input[j % inputLen];
    }
}

void CryptonightAlgorithm::mainLoop(std::uint8_t* scratchpad) const {
    for (std::uint32_t i = 0; i < kIterations; ++i) {
        std::uint32_t p = 0;
        for (std::size_t j = 0; j < 16; ++j) scratchpad[p + j] ^= static_cast<std::uint8_t>(j);
        p = (p + 16) % kScratchpadSize;
        for (std::size_t j = 0; j < 16; ++j) scratchpad[p + j] ^= static_cast<std::uint8_t>(j * 3);
        p = (p + 16) % kScratchpadSize;
    }
}

void CryptonightAlgorithm::xorIntoState(KeccakState&, const std::uint8_t*, std::size_t) {}

void CryptonightAlgorithm::keccak1600(KeccakState&) {}

} // namespace cppminer::hash