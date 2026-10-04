export module rstd.core:random.engine;
export import :random.source;
export import :random.seed;
import :num.integer_methods;

using namespace rstd::prelude;

template<typename W>
class RandomMt {
    static constexpr bool         wide  = sizeof(W) == 8;
    static constexpr rstd::size_t count = wide ? 312 : 624;
    static constexpr rstd::size_t shift = wide ? 156 : 397;
    static constexpr W            lower = W(0x7fffffff);
    array<W, count>               words {};
    usize                         index {};
    struct Unseeded {};
    explicit constexpr RandomMt(Unseeded) {}
    explicit constexpr RandomMt(W value) { seed(value); }

public:
    using ResultType                   = W;
    using Error                        = rstd::convert::Infallible;
    static constexpr W    default_seed = W(5489);
    static constexpr auto min() -> W { return W(); }
    static constexpr auto max() -> W { return W::MAX; }

    constexpr RandomMt(): RandomMt(default_seed) {}
    static constexpr auto from_seed(W value) -> RandomMt { return RandomMt(value); }
    constexpr void        seed(W value = default_seed) {
        words[usize()] = value;
        auto factor    = W(wide ? 6364136223846793005ULL : 1812433253ULL);
        for (usize i(1); i < usize(count); ++i) {
            auto previous = words[i - usize(1)];
            words[i]      = (previous ^ (previous >> u64(wide ? 62 : 30)))
                                .wrapping_mul(factor)
                                .wrapping_add(W(i.to_primitive()));
        }
        index = usize();
    }
    template<rstd::random::SeedSequence S>
    static auto from_seed_sequence(S& sequence) -> RandomMt {
        RandomMt result(Unseeded {});
        result.seed_sequence(sequence);
        return result;
    }
    template<rstd::random::SeedSequence S>
    void seed_sequence(S& sequence) {
        array<u32, 624> material {};
        sequence.generate(material.as_mut_slice());
        array<W, count> next_words {};
        bool            nonzero = false;
        for (usize i {}; i < usize(count); ++i) {
            if constexpr (wide)
                next_words[i] = W(material[i * usize(2)].to_primitive()) |
                                (W(material[i * usize(2) + usize(1)].to_primitive()) << u64(32));
            else
                next_words[i] = material[i];
            nonzero =
                nonzero || (i == usize() ? (next_words[i] & ~lower) != W() : next_words[i] != W());
        }
        if (! nonzero) next_words[usize()] = W(1) << u64(wide ? 63 : 31);
        words = next_words;
        index = usize();
    }
    constexpr auto next() -> W {
        auto following = (index + usize(1)) % usize(count);
        auto joined    = (words[index] & ~lower) | (words[following] & lower);
        auto twist     = W(wide ? 0xb5026f5aa96619e9ULL : 0x9908b0dfULL);
        auto value     = words[(index + usize(shift)) % usize(count)] ^ (joined >> u64(1));
        if ((joined & W(1)) != W()) value ^= twist;
        words[index] = value;
        index        = following;
        if constexpr (wide) {
            value ^= (value >> u64(29)) & W(0x5555555555555555ULL);
            value ^= (value << u64(17)) & W(0x71d67fffeda60000ULL);
            value ^= (value << u64(37)) & W(0xfff7eee000000000ULL);
            return value ^ (value >> u64(43));
        } else {
            value ^= value >> u64(11);
            value ^= (value << u64(7)) & W(0x9d2c5680);
            value ^= (value << u64(15)) & W(0xefc60000);
            return value ^ (value >> u64(18));
        }
    }
    constexpr void discard(u64 amount) {
        for (u64 i {}; i < amount; ++i) (void)next();
    }
    constexpr auto clone() const -> RandomMt { return *this; }
    /// Emits little-endian words, discarding unused bytes of the last word.
    constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        usize at {};
        while (at < output.len()) {
            auto value = next().to_le_bytes();
            for (usize i {}; i < value.len() && at < output.len(); ++i) output[at++] = u8(value[i]);
        }
        return Ok(empty {});
    }
    friend constexpr auto operator==(const RandomMt& a, const RandomMt& b) -> bool {
        // N future words determine the effective state, excluding discarded low bits.
        auto left  = a;
        auto right = b;
        for (usize i {}; i < usize(count); ++i)
            if (left.next() != right.next()) return false;
        return true;
    }
};

export namespace rstd::random
{
/// Fixed C++ MT algorithms, not cryptographic generators. Clones repeat the same stream.
using Mt19937    = RandomMt<u32>;
using Mt19937_64 = RandomMt<u64>;
} // namespace rstd::random
