module;
#include <rstd/macro.hpp>
export module rstd.core:random.sequence;
export import :random.uniform;
import :slice.ops;

namespace rstd::random
{
/// A source error leaves a valid, partially shuffled permutation.
export template<typename T, RandomSource R>
constexpr auto shuffle(mut_ref<T[]> values, R& source) -> Result<empty, rng_error_t<R>> {
    for (auto remaining = values.len(); remaining > usize(1); --remaining) {
        auto last     = remaining - usize(1);
        auto range    = UniformInt<usize>::make(usize(), last).unwrap();
        auto selected = rstd_try(range.sample(source));
        slice_::swap(values, selected, last);
    }
    return Ok(empty {});
}

export template<typename T, RandomSource R>
constexpr auto shuffle_forward(mut_ref<T[]> values, R& source) -> Result<empty, rng_error_t<R>> {
    for (usize first {}; first + usize(1) < values.len(); ++first) {
        auto range = UniformInt<usize>::make(first, values.len() - usize(1)).unwrap();
        auto selected = rstd_try(range.sample(source));
        slice_::swap(values, first, selected);
    }
    return Ok(empty {});
}
} // namespace rstd::random
