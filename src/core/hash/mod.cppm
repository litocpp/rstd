export module rstd.core:hash;
import :num.types;
import :core;
export import :trait;

namespace rstd::hash
{

constexpr auto rotate_left(u64 value, u64 amount) noexcept -> u64 {
    return value.rotate_left(amount);
}

export class DefaultHasher {
    u64            v0;
    u64            v1;
    u64            v2;
    u64            v3;
    u64            tail;
    rstd::uint32_t tail_len;
    usize          length;

    void round() noexcept {
        v0 = v0.wrapping_add(v1);
        v1 = rotate_left(v1, u64(13));
        v1 ^= v0;
        v0 = rotate_left(v0, u64(32));
        v2 = v2.wrapping_add(v3);
        v3 = rotate_left(v3, u64(16));
        v3 ^= v2;
        v0 = v0.wrapping_add(v3);
        v3 = rotate_left(v3, u64(21));
        v3 ^= v0;
        v2 = v2.wrapping_add(v1);
        v1 = rotate_left(v1, u64(17));
        v1 ^= v2;
        v2 = rotate_left(v2, u64(32));
    }

    void compress(u64 block) noexcept {
        v3 ^= block;
        round();
        v0 ^= block;
    }

public:
    DefaultHasher(u64 k0 = u64(), u64 k1 = u64()) noexcept
        : v0(u64(0x736f6d6570736575ULL) ^ k0),
          v1(u64(0x646f72616e646f6dULL) ^ k1),
          v2(u64(0x6c7967656e657261ULL) ^ k0),
          v3(u64(0x7465646279746573ULL) ^ k1),
          tail(),
          tail_len(0),
          length() {}

    void write(slice<byte> bytes) noexcept {
        length += bytes.len();
        for (rstd::size_t i = 0; i < bytes.len().to_primitive(); ++i) {
            auto const value = u64(bytes[usize(i)]);
            tail |= value << u64(tail_len * 8);
            if (++tail_len == 8) {
                compress(tail);
                tail     = u64();
                tail_len = 0;
            }
        }
    }

    template<typename T>
    void write_value(const T& value) noexcept {
        auto const* source = reinterpret_cast<byte const*>(rstd::addressof(value));
        write(slice<byte>::from_raw_parts(source, usize(sizeof(T))));
    }

    auto finish() const noexcept -> u64 {
        auto       state      = *this;
        auto const length_low = static_cast<rstd::uint64_t>(state.length.to_primitive() & 0xffu);
        u64        final      = state.tail | (u64(length_low) << u64(56));
        state.v3 ^= final;
        state.round();
        state.v0 ^= final;
        state.v2 ^= u64(0xff);
        state.round();
        state.round();
        state.round();
        return state.v0 ^ state.v1 ^ state.v2 ^ state.v3;
    }
};

export struct Hasher {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Hasher;
        void write(slice<byte> bytes) noexcept { return trait_call<0>(this, bytes); }
        auto finish() const noexcept -> u64 { return trait_call<1>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write, &T::finish>;
};

export struct Hash {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Hash;
        void hash(DefaultHasher& state) const noexcept { return trait_call<0>(this, state); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::hash>;
};

} // namespace rstd::hash

namespace rstd
{

template<typename T>
    requires requires(T& state, slice<byte> bytes) {
        { state.write(bytes) } noexcept;
        { state.finish() } noexcept;
    }
struct Impl<hash::Hasher, T> : ImplBase<T> {
    void write(slice<byte> bytes) noexcept { this->self().write(bytes); }
    auto finish() const noexcept -> u64 { return this->self().finish(); }
};

template<typename T>
    requires(num::Numeric<T> || mtp::is_arithmetic<T> || mtp::is_ptr<T>)
struct Impl<hash::Hash, T> : ImplBase<T> {
    void hash(hash::DefaultHasher& state) const noexcept { state.write_value(this->self()); }
};

} // namespace rstd
