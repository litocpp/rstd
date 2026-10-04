module;
#include <rstd/macro.hpp>
export module rstd.core:random.uniform;
export import :random.source;
import :num.integer_methods;
import :error;

namespace rstd::random
{
export enum class DistributionError { InvalidRange, InvalidProbability, InvalidDeviation };

export template<typename T>
struct Standard;

export template<num::Integer T>
struct Standard<T> {
    using Output = T;
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<T, rng_error_t<R>> {
        array<u8, sizeof(typename T::primitive_type)> bytes {};
        rstd_try(random::fill_bytes(source, bytes.as_mut_slice()));
        return Ok(T::from_le_bytes(bytes));
    }
};

export template<>
struct Standard<bool> {
    using Output = bool;
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<bool, rng_error_t<R>> {
        auto byte = rstd_try(Standard<u8> {}.sample(source));
        return Ok((byte & u8(1)) != u8());
    }
};

export template<num::Float T>
struct Standard<T> {
    using Output = T;
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<T, rng_error_t<R>> {
        if constexpr (mtp::same_as<T, f32>) {
            auto bits = rstd_try(Standard<u32> {}.sample(source)) >> u64(8);
            return Ok(T(static_cast<float>(bits.to_primitive()) * 0x1p-24f));
        } else {
            auto bits = rstd_try(Standard<u64> {}.sample(source)) >> u64(11);
            return Ok(T(static_cast<double>(bits.to_primitive()) * 0x1p-53));
        }
    }
};

export template<num::Integer T>
class UniformInt {
    using U = typename T::Unsigned;
    using P = typename U::primitive_type;
    T low;
    T high;
    U span;
    U mask;
    constexpr UniformInt(T first, T last)
        : low(first),
          high(last),
          span(U(static_cast<P>(last.to_primitive()))
                   .wrapping_sub(U(static_cast<P>(first.to_primitive())))),
          mask(span) {
        for (unsigned shift = 1; shift < sizeof(P) * 8; shift *= 2) mask |= mask >> u64(shift);
    }

public:
    using Output = T;
    static constexpr auto make(T low, T high) -> Result<UniformInt, DistributionError> {
        if (low > high) return Err(DistributionError::InvalidRange);
        return Ok(UniformInt(low, high));
    }
    constexpr auto min() const -> T { return low; }
    constexpr auto max() const -> T { return high; }
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<T, rng_error_t<R>> {
        if (span == U()) return Ok(low);
        for (;;) {
            auto offset = rstd_try(Standard<U> {}.sample(source)) & mask;
            if (offset > span) continue;
            auto bits = U(static_cast<P>(low.to_primitive())).wrapping_add(offset);
            return Ok(T(static_cast<typename T::primitive_type>(bits.to_primitive())));
        }
    }
};

export template<num::Float T>
class UniformReal {
    T low;
    T high;
    T width;
    constexpr UniformReal(T first, T last): low(first), high(last), width(last - first) {}

public:
    using Output = T;
    static constexpr auto make(T low, T high) -> Result<UniformReal, DistributionError> {
        if (! low.is_finite() || ! high.is_finite() || ! (low < high) || ! (high - low).is_finite())
            return Err(DistributionError::InvalidRange);
        return Ok(UniformReal(low, high));
    }
    constexpr auto min() const -> T { return low; }
    constexpr auto max() const -> T { return high; }
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<T, rng_error_t<R>> {
        auto unit   = rstd_try(Standard<T> {}.sample(source));
        auto result = low + width * unit;
        // Rounding may reach the excluded endpoint, including adjacent float bounds.
        if (result >= high) result = high.next_down();
        return Ok(result);
    }
};

/// Uses a 53-bit uniform grid; probabilities are resolved to that finite precision.
export class Bernoulli {
    f64 probability;
    explicit constexpr Bernoulli(f64 p): probability(p) {}

public:
    using Output = bool;
    static constexpr auto make(f64 p) -> Result<Bernoulli, DistributionError> {
        if (! p.is_finite() || p < f64(0) || p > f64(1))
            return Err(DistributionError::InvalidProbability);
        return Ok(Bernoulli(p));
    }
    constexpr auto p() const -> f64 { return probability; }
    template<RandomSource R>
    constexpr auto sample(R& source) const -> Result<bool, rng_error_t<R>> {
        if (probability == f64(0)) return Ok(false);
        if (probability == f64(1)) return Ok(true);
        auto unit = rstd_try(Standard<f64> {}.sample(source));
        return Ok(unit < probability);
    }
};
} // namespace rstd::random

namespace rstd
{
template<>
struct Impl<fmt::Display, random::DistributionError> : ImplBase<random::DistributionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        switch (this->self()) {
        case random::DistributionError::InvalidRange:
            return formatter.write_raw("invalid random range", 20);
        case random::DistributionError::InvalidProbability:
            return formatter.write_raw("invalid random probability", 26);
        case random::DistributionError::InvalidDeviation:
            return formatter.write_raw("invalid normal parameters", 25);
        }
        rstd::unreachable();
    }
};
template<>
struct Impl<fmt::Debug, random::DistributionError> : Impl<fmt::Display, random::DistributionError> {
};
template<>
struct Impl<error::Error, random::DistributionError>
    : DefaultInImpl<error::Error, random::DistributionError> {};
} // namespace rstd
