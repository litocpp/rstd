module;
#include <cstddef>
#include <cstdint>
#include <rstd/enum.hpp>

export module rstd:crypto;

export import rstd.core;
export import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::string::String;

export namespace rstd::crypto
{

class Sha256DigestParseError {
    RSTD_ENUM(Sha256DigestParseError, (Length, (usize actual;)), (Character, (usize index;)))
};

class Sha256Digest : public DefaultInClass<Sha256Digest, Clone> {
    array<u8, 32> bytes_;

public:
    static auto from_bytes(array<u8, 32> bytes) noexcept -> Sha256Digest {
        auto result   = Sha256Digest {};
        result.bytes_ = rstd::move(bytes);
        return result;
    }

    static auto parse_hex(ref<str> value) -> Result<Sha256Digest, Sha256DigestParseError> {
        if (value.len() != usize(64)) {
            return Err(Sha256DigestParseError::Length(value.len()));
        }
        auto bytes  = array<u8, 32> {};
        auto nibble = [](u8 value) -> Option<u8> {
            const auto byte = value.to_primitive();
            if (byte >= '0' && byte <= '9') return Some(u8(byte - '0'));
            if (byte >= 'a' && byte <= 'f') return Some(u8(byte - 'a' + 10));
            if (byte >= 'A' && byte <= 'F') return Some(u8(byte - 'A' + 10));
            return None();
        };
        for (usize index {}; index < usize(32); ++index) {
            auto high = nibble(value[index * usize(2)]);
            if (high.is_none()) {
                return Err(Sha256DigestParseError::Character(index * usize(2)));
            }
            auto low = nibble(value[index * usize(2) + usize(1)]);
            if (low.is_none()) {
                return Err(Sha256DigestParseError::Character(index * usize(2) + usize(1)));
            }
            bytes[index] = u8((high->to_primitive() << 4u) | low->to_primitive());
        }
        return Ok(from_bytes(rstd::move(bytes)));
    }

    auto as_bytes() const noexcept -> slice<u8> { return bytes_.as_slice(); }

    auto to_hex() const -> String {
        static constexpr char digits[] = "0123456789abcdef";
        auto                  result   = String::make();
        result.reserve(usize(64));
        for (const auto value : bytes_) {
            const auto byte = value.to_primitive();
            result.push_ascii(digits[byte >> 4u]);
            result.push_ascii(digits[byte & 0x0fu]);
        }
        return result;
    }

    auto clone() const -> Sha256Digest { return from_bytes(bytes_); }

    friend auto operator==(const Sha256Digest& left, const Sha256Digest& right) noexcept -> bool {
        for (usize index {}; index < usize(32); ++index) {
            if (left.bytes_[index] != right.bytes_[index]) return false;
        }
        return true;
    }
};

constexpr auto rotate_right(std::uint32_t value, std::uint32_t count) noexcept -> std::uint32_t {
    return (value >> count) | (value << (32u - count));
}

constexpr std::uint32_t SHA256_CONSTANTS[] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

class Sha256 {
    std::uint32_t state_[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::uint8_t  block_[64] {};
    std::uint64_t length_ {};
    std::uint32_t block_length_ {};

    void transform() noexcept {
        std::uint32_t words[64] {};
        for (std::uint32_t index {}; index < 16u; ++index) {
            const auto position = index * 4u;
            words[index]        = (static_cast<std::uint32_t>(block_[position]) << 24u) |
                                  (static_cast<std::uint32_t>(block_[position + 1u]) << 16u) |
                                  (static_cast<std::uint32_t>(block_[position + 2u]) << 8u) |
                                  static_cast<std::uint32_t>(block_[position + 3u]);
        }
        for (std::uint32_t index = 16u; index < 64u; ++index) {
            const auto previous = words[index - 15u];
            const auto near     = words[index - 2u];
            const auto first =
                rotate_right(previous, 7u) ^ rotate_right(previous, 18u) ^ (previous >> 3u);
            const auto second = rotate_right(near, 17u) ^ rotate_right(near, 19u) ^ (near >> 10u);
            words[index]      = words[index - 16u] + first + words[index - 7u] + second;
        }

        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::uint32_t index {}; index < 64u; ++index) {
            const auto sigma_one =
                rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto first  = h + sigma_one + choice + SHA256_CONSTANTS[index] + words[index];
            const auto sigma_zero =
                rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto second   = sigma_zero + majority;
            h                   = g;
            g                   = f;
            f                   = e;
            e                   = d + first;
            d                   = c;
            c                   = b;
            b                   = a;
            a                   = first + second;
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

public:
    static auto make() noexcept -> Sha256 { return {}; }

    void update(slice<u8> input) noexcept {
        const auto length = static_cast<std::uint64_t>(input.len().to_primitive());
        length_ += length;
        for (std::uint64_t index {}; index < length; ++index) {
            block_[block_length_++] = input[usize(index)].to_primitive();
            if (block_length_ != 64u) continue;
            transform();
            block_length_ = 0u;
        }
    }

    auto finalize() && noexcept -> array<u8, 32> {
        const auto bit_length   = length_ * 8u;
        block_[block_length_++] = 0x80u;
        if (block_length_ > 56u) {
            while (block_length_ < 64u) block_[block_length_++] = 0u;
            transform();
            block_length_ = 0u;
        }
        while (block_length_ < 56u) block_[block_length_++] = 0u;
        for (std::uint32_t index {}; index < 8u; ++index) {
            const auto shift    = 56u - index * 8u;
            block_[56u + index] = static_cast<std::uint8_t>((bit_length >> shift) & 0xffu);
        }
        transform();

        auto result = array<u8, 32> {};
        for (std::uint32_t index {}; index < 8u; ++index) {
            for (std::uint32_t byte_index {}; byte_index < 4u; ++byte_index) {
                const auto shift                       = 24u - byte_index * 8u;
                result[usize(index * 4u + byte_index)] = u8((state_[index] >> shift) & 0xffu);
            }
        }
        return result;
    }

    auto finalize_digest() && noexcept -> Sha256Digest {
        return Sha256Digest::from_bytes(rstd::move(*this).finalize());
    }
};

auto sha256(slice<u8> input) noexcept -> array<u8, 32> {
    auto state = Sha256::make();
    state.update(input);
    return rstd::move(state).finalize();
}

auto sha256_hex(array<u8, 32> digest) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    auto                  result   = String::make();
    result.reserve(usize(64));
    for (const auto value : digest) {
        const auto byte = value.get().to_primitive();
        result.push_ascii(digits[byte >> 4u]);
        result.push_ascii(digits[byte & 0x0fu]);
    }
    return result;
}

auto sha256_hex(slice<u8> input) -> String {
    return sha256_hex(sha256(input));
}

auto sha256_hex(ref<str> input) -> String {
    return sha256_hex(input.as_bytes());
}

auto sha256_digest(slice<u8> input) noexcept -> Sha256Digest {
    return Sha256Digest::from_bytes(sha256(input));
}

auto sha256_digest(ref<str> input) noexcept -> Sha256Digest {
    return sha256_digest(input.as_bytes());
}

} // namespace rstd::crypto

export namespace rstd
{

template<>
struct Impl<str_::FromStr, crypto::Sha256Digest> {
    using Err = crypto::Sha256DigestParseError;

    static auto from_str(ref<str> value) -> Result<crypto::Sha256Digest, Err> {
        return crypto::Sha256Digest::parse_hex(value);
    }
};

template<>
struct Impl<convert::TryFrom<ref<str>>, crypto::Sha256Digest> {
    using Error = crypto::Sha256DigestParseError;

    static auto try_from(ref<str> value) -> Result<crypto::Sha256Digest, Error> {
        return rstd::from_str<crypto::Sha256Digest>(value);
    }
};

template<>
struct Impl<fmt::Display, crypto::Sha256Digest> : ImplBase<crypto::Sha256Digest> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        static constexpr char digits[] = "0123456789abcdef";
        for (const auto value : this->self().as_bytes()) {
            const auto byte = value.to_primitive();
            if (! formatter.write_raw(&digits[byte >> 4u], 1) ||
                ! formatter.write_raw(&digits[byte & 0x0fu], 1)) {
                return false;
            }
        }
        return true;
    }
};

template<>
struct Impl<fmt::Debug, crypto::Sha256Digest> : ImplBase<crypto::Sha256Digest> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Display, crypto::Sha256DigestParseError>
    : ImplBase<crypto::Sha256DigestParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Length()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "SHA-256 digest must contain 64 hexadecimal characters; found {}",
                error.as_Length().actual));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("SHA-256 digest contains a non-hexadecimal character at byte {}",
                                 error.as_Character().index));
    }
};

template<>
struct Impl<fmt::Debug, crypto::Sha256DigestParseError> : ImplBase<crypto::Sha256DigestParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, crypto::Sha256DigestParseError>
    : DefaultInImpl<error::Error, crypto::Sha256DigestParseError> {};

template<>
struct Impl<hash::Hash, crypto::Sha256Digest> : ImplBase<crypto::Sha256Digest> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        rstd::as<hash::Hasher>(state).write(this->self().as_bytes());
    }
};

} // namespace rstd
