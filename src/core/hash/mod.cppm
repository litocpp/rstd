export module rstd.core:hash;
export import :trait;

namespace rstd::hash
{

constexpr auto rotate_left(u64 value, u32 amount) noexcept -> u64 {
    return (value << amount) | (value >> (64 - amount));
}

export class DefaultHasher {
    u64   v0;
    u64   v1;
    u64   v2;
    u64   v3;
    u64   tail;
    usize tail_len;
    usize length;

    void round() noexcept {
        v0 += v1;
        v1 = rotate_left(v1, 13);
        v1 ^= v0;
        v0 = rotate_left(v0, 32);
        v2 += v3;
        v3 = rotate_left(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = rotate_left(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = rotate_left(v1, 17);
        v1 ^= v2;
        v2 = rotate_left(v2, 32);
    }

    void compress(u64 block) noexcept {
        v3 ^= block;
        round();
        v0 ^= block;
    }

public:
    DefaultHasher(u64 k0 = 0, u64 k1 = 0) noexcept
        : v0(0x736f6d6570736575ULL ^ k0),
          v1(0x646f72616e646f6dULL ^ k1),
          v2(0x6c7967656e657261ULL ^ k0),
          v3(0x7465646279746573ULL ^ k1),
          tail(0),
          tail_len(0),
          length(0) {}

    void write(const u8* bytes, usize size) noexcept {
        length += size;
        for (usize i = 0; i < size; ++i) {
            tail |= static_cast<u64>(bytes[i]) << (tail_len * 8);
            if (++tail_len == 8) {
                compress(tail);
                tail     = 0;
                tail_len = 0;
            }
        }
    }

    template<typename T>
    void write_value(const T& value) noexcept {
        write(reinterpret_cast<const u8*>(rstd::addressof(value)), sizeof(T));
    }

    auto finish() const noexcept -> u64 {
        auto state = *this;
        u64  final = state.tail | (static_cast<u64>(state.length & 0xff) << 56);
        state.v3 ^= final;
        state.round();
        state.v0 ^= final;
        state.v2 ^= 0xff;
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
        void write(const u8* bytes, usize size) noexcept {
            return trait_call<0>(this, bytes, size);
        }
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
    requires requires(T& state, const u8* bytes, usize size) {
        { state.write(bytes, size) } noexcept;
        { state.finish() } noexcept;
    }
struct Impl<hash::Hasher, T> : ImplBase<T> {
    void write(const u8* bytes, usize size) noexcept { this->self().write(bytes, size); }
    auto finish() const noexcept -> u64 { return this->self().finish(); }
};

template<typename T>
    requires(mtp::is_arithmetic<T> || mtp::is_ptr<T>)
struct Impl<hash::Hash, T> : ImplBase<T> {
    void hash(hash::DefaultHasher& state) const noexcept { state.write_value(this->self()); }
};

} // namespace rstd
