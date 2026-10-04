module;
#include <rstd/macro.hpp>
export module rstd.core:random.normal;
export import :random.uniform;

namespace rstd::random
{
/// Finite parameters may still overflow the floating-point result. No tail truncation is applied.
export template<num::Float T>
class Normal {
    T         average;
    T         deviation;
    Option<T> spare;
    Normal(T mean, T stddev): average(mean), deviation(stddev) {}

public:
    using Output = T;
    static auto make(T mean, T stddev) -> Result<Normal, DistributionError> {
        if (! mean.is_finite() || ! stddev.is_finite() || stddev <= T(0))
            return Err(DistributionError::InvalidDeviation);
        return Ok(Normal(mean, stddev));
    }
    auto mean() const -> T { return average; }
    auto stddev() const -> T { return deviation; }
    void reset() { spare = None(); }
    auto clone() const -> Normal { return *this; }
    template<RandomSource R>
    auto sample(R& source) -> Result<T, rng_error_t<R>> {
        if (spare.is_some()) return Ok(spare.take().unwrap() * deviation + average);
        for (;;) {
            auto x      = rstd_try(Standard<T> {}.sample(source)) * T(2) - T(1);
            auto y      = rstd_try(Standard<T> {}.sample(source)) * T(2) - T(1);
            auto radius = x * x + y * y;
            if (radius == T(0) || radius >= T(1)) continue;
            auto scale = (T(-2) * radius.ln() / radius).sqrt();
            spare      = Some(y * scale);
            return Ok(x * scale * deviation + average);
        }
    }
};
} // namespace rstd::random
