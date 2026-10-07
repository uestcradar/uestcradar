#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace radar_example {

class Sha256 {
public:
    void update(std::span<const std::byte> bytes) {
        if (finished_) {
            throw std::logic_error("SHA-256 digest is already finalized");
        }
        total_bytes_ += bytes.size();
        for (const std::byte value : bytes) {
            block_[block_size_++] = std::to_integer<std::uint8_t>(value);
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    template <class T>
    void update_object(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        update(std::as_bytes(std::span{&value, std::size_t{1}}));
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() {
        if (finished_) {
            return digest_;
        }
        const std::uint64_t bit_count = total_bytes_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56) {
            while (block_size_ < block_.size()) {
                block_[block_size_++] = 0;
            }
            transform();
            block_size_ = 0;
        }
        while (block_size_ < 56) {
            block_[block_size_++] = 0;
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[block_size_++] = static_cast<std::uint8_t>(
                bit_count >> static_cast<unsigned>(shift));
        }
        transform();
        for (std::size_t index = 0; index < state_.size(); ++index) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                digest_[index * 4 + byte] =
                    static_cast<std::uint8_t>(
                        state_[index] >> (24U - byte * 8U));
            }
        }
        finished_ = true;
        return digest_;
    }

    [[nodiscard]] std::string hex() {
        static constexpr char kHex[] = "0123456789abcdef";
        const auto bytes = finish();
        std::string result(bytes.size() * 2, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            result[index * 2] = kHex[bytes[index] >> 4U];
            result[index * 2 + 1] = kHex[bytes[index] & 0x0fU];
        }
        return result;
    }

private:
    static constexpr std::array<std::uint32_t, 64> kRoundConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    static constexpr std::uint32_t rotate_right(
        std::uint32_t value, unsigned count) noexcept {
        return (value >> count) | (value << (32U - count));
    }

    void transform() {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            words[index] =
                (static_cast<std::uint32_t>(block_[index * 4]) << 24U) |
                (static_cast<std::uint32_t>(block_[index * 4 + 1]) << 16U) |
                (static_cast<std::uint32_t>(block_[index * 4 + 2]) << 8U) |
                static_cast<std::uint32_t>(block_[index * 4 + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15], 7) ^
                rotate_right(words[index - 15], 18) ^
                (words[index - 15] >> 3U);
            const auto s1 = rotate_right(words[index - 2], 17) ^
                rotate_right(words[index - 2], 19) ^
                (words[index - 2] >> 10U);
            words[index] = words[index - 16] + s0 +
                words[index - 7] + s1;
        }

        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                rotate_right(e, 25);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choose +
                kRoundConstants[index] + words[index];
            const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> block_{};
    std::array<std::uint8_t, 32> digest_{};
    std::uint64_t total_bytes_{};
    std::size_t block_size_{};
    bool finished_{};
};

inline std::string sha256(std::span<const std::byte> bytes) {
    Sha256 digest;
    digest.update(bytes);
    return digest.hex();
}

}  // namespace radar_example
