module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
using rstd::simd::Simd;
using rstd::simd::Mask;
namespace bench = rstd::bench;

enum class SimdOperation
{
    Shift,
    SaturatingAdd,
    Min,
    Cast,
    Swizzle,
    Reduce,
    Fma,
    Classify,
    MaskQuery,
    MaskLogic,
    Select
};

template<SimdOperation Work, rstd::size_t N>
struct SimdOperationSession {
    using T = rstd::mtp::
        cond<Work == SimdOperation::Shift || Work == SimdOperation::SaturatingAdd, u32, f32>;
    using V = Simd<T, N>;
    V       input;
    Mask<N> mask;

    auto evaluate() {
        if constexpr (Work == SimdOperation::Shift)
            return input.wrapping_shr(u64(33));
        else if constexpr (Work == SimdOperation::SaturatingAdd)
            return input.saturating_add(V::splat(u32::MAX - u32(2)));
        else if constexpr (Work == SimdOperation::Min)
            return input.min(V::splat(T(2)));
        else if constexpr (Work == SimdOperation::Cast)
            return input.template cast<i32>();
        else if constexpr (Work == SimdOperation::Swizzle)
            return [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
                return input.template swizzle<(N - 1 - I)...>();
            }(rstd::mtp::make_index_sequence<N> {});
        else if constexpr (Work == SimdOperation::Reduce)
            return input.reduce_sum_unordered();
        else if constexpr (Work == SimdOperation::Fma)
            return input.mul_add(V::splat(T(2)), V::splat(T(1)));
        else if constexpr (Work == SimdOperation::Classify)
            return input.is_finite();
        else if constexpr (Work == SimdOperation::MaskQuery)
            return rstd::tuple { mask.any(), mask.count(), mask.first_set() };
        else if constexpr (Work == SimdOperation::MaskLogic)
            return (mask ^ input.simd_lt(V::splat(T(3)))) & Mask<N>::first_n(usize(N - 1));
        else
            return mask.select(input, V::splat(T(99)));
    }
    auto check() -> Result<empty, String> {
        auto result = evaluate();
        bool valid  = true;
        if constexpr (Work == SimdOperation::Reduce) {
            T sum {};
            for (usize i; i < usize(N); ++i) sum += input[i];
            valid = result == sum;
        } else if constexpr (Work == SimdOperation::MaskQuery) {
            usize         count;
            Option<usize> first;
            for (usize i; i < usize(N); ++i)
                if (mask[i]) {
                    ++count;
                    if (first.is_none()) first = Some(i);
                }
            valid = rstd::get<0>(result) == (count != usize()) && rstd::get<1>(result) == count;
            auto found = rstd::get<2>(result);
            valid      = valid && found.is_some() == first.is_some();
            if (found.is_some() && first.is_some()) valid = valid && *found == *first;
        } else {
            for (usize i; i < usize(N); ++i) {
                if constexpr (Work == SimdOperation::Shift)
                    valid = valid && result[i] == input[i].wrapping_shr(u64(33));
                else if constexpr (Work == SimdOperation::SaturatingAdd)
                    valid = valid && result[i] == input[i].saturating_add(u32::MAX - u32(2));
                else if constexpr (Work == SimdOperation::Min)
                    valid = valid && result[i] == input[i].min(T(2));
                else if constexpr (Work == SimdOperation::Cast)
                    valid = valid && result[i] == rstd::as_cast<i32>(input[i]);
                else if constexpr (Work == SimdOperation::Swizzle)
                    valid = valid && result[i] == input[usize(N - 1) - i];
                else if constexpr (Work == SimdOperation::Fma)
                    valid = valid && result[i] == input[i] * T(2) + T(1);
                else if constexpr (Work == SimdOperation::Classify)
                    valid = valid && result[i] == input[i].is_finite();
                else if constexpr (Work == SimdOperation::MaskLogic)
                    valid =
                        valid && result[i] == ((mask[i] != (input[i] < T(3))) && i < usize(N - 1));
                else
                    valid = valid && result[i] == (mask[i] ? input[i] : T(99));
            }
        }
        if (! valid) return Err("SIMD operation result mismatch"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& runner, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        return runner.run(
            name,
            [this] {
                for (int i = 0; i < 64; ++i) {
                    rstd::hint::black_box(input);
                    rstd::hint::black_box(mask);
                    rstd::hint::black_box(evaluate());
                }
            },
            bench::RunConfig { .batch = f64(64) });
    }
    auto finish() -> Result<empty, String> { return check(); }
};

template<SimdOperation Work, rstd::size_t N>
struct SimdOperationFactory {
    int  pattern;
    auto prepare() -> Result<SimdOperationSession<Work, N>, String> {
        using Session = SimdOperationSession<Work, N>;
        using T       = typename Session::T;
        array<T, N> input {};
        for (usize i; i < usize(N); ++i) input[i] = T(i.to_primitive() + 1);
        if constexpr (Work == SimdOperation::Cast || Work == SimdOperation::Classify) {
            if (pattern == 1) {
                input[usize(0)] = T(__builtin_nan(""));
                input[usize(1)] = T(__builtin_inf());
                input[usize(2)] = T(-1.75);
                input[usize(3)] = T::from_bits(u32(1));
            }
        }
        array<u32, N> mask_lanes {};
        for (usize i; i < usize(N); ++i) {
            auto lane     = i.to_primitive();
            mask_lanes[i] = u32(pattern == 1 || (pattern == 2 && lane == 0) ||
                                (pattern == 3 && lane == N - 1) ||
                                (pattern == 4 && lane % 4 == 0) || (pattern == 5 && lane % 4 != 0));
        }
        auto mask = Simd<u32, N>::from_array(mask_lanes).simd_ne(Simd<u32, N>::splat(u32()));
        return Ok(Session { Session::V::from_array(input), mask });
    }
};

template<rstd::size_t N, bool Scalar, int Chains>
struct SimdAddSession {
    array<u32, N> seed {};
    auto          operation() {
        if constexpr (Scalar) {
            array<u32, N> states[Chains];
            for (auto& x : states) x = seed;
            for (int iteration = 0; iteration < 64; ++iteration)
                for (auto& x : states) {
                    rstd::hint::black_box(x);
                    for (usize i; i < usize(N); ++i) x[i] = x[i].wrapping_add(seed[i]);
                }
            for (auto& x : states) rstd::hint::black_box(x);
            return states[Chains - 1];
        } else {
            using V = Simd<u32, N>;
            V    states[Chains];
            auto increment = V::from_array(seed);
            for (auto& x : states) x = increment;
            for (int iteration = 0; iteration < 64; ++iteration)
                for (auto& x : states) {
                    rstd::hint::black_box(x);
                    x = x + increment;
                }
            for (auto& x : states) rstd::hint::black_box(x);
            return states[Chains - 1].to_array();
        }
    }
    auto check() -> Result<empty, String> {
        auto result = operation();
        for (usize i; i < usize(N); ++i)
            if (result[i] != seed[i].wrapping_mul(u32(65)))
                return Err("SIMD add chain mismatch"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& runner, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        return runner.run(
            name,
            [this] {
                return operation();
            },
            bench::RunConfig { .batch = f64(64 * Chains) });
    }
    auto finish() -> Result<empty, String> { return check(); }
};

template<rstd::size_t N, bool Scalar, int Chains>
struct SimdAddFactory {
    auto prepare() -> Result<SimdAddSession<N, Scalar, Chains>, String> {
        SimdAddSession<N, Scalar, Chains> s;
        for (usize i; i < usize(N); ++i) s.seed[i] = u32(i.to_primitive() + 1);
        return Ok(rstd::move(s));
    }
};

template<rstd::size_t N>
struct SimdMemorySession {
    array<f32, N + 2> source {}, output {};
    usize             count, offset;
    auto              operation() {
        auto input = rstd::slice<f32>::from_raw_parts(
            source.as_slice().as_raw_ptr() + offset.to_primitive(), count);
        auto out = rstd::mut_ref<f32[]>::from_raw_parts(
            output.as_mut_slice().as_raw_ptr() + offset.to_primitive(), count);
        Simd<f32, N>::load_or(input, Simd<f32, N>::splat(f32(99))).store_partial(out);
        return output.as_slice().as_raw_ptr();
    }
    auto check() -> Result<empty, String> {
        for (usize i; i < usize(N + 2); ++i) output[i] = f32(-1);
        operation();
        for (usize i; i < usize(N + 2); ++i)
            if (output[i] != (i >= offset && i < offset + count ? source[i] : f32(-1)))
                return Err("SIMD partial memory mismatch"_Str);
        return Ok(empty {});
    }
    auto run(bench::Bench& runner, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        return runner.run(name, [this] {
            return operation();
        });
    }
    auto finish() -> Result<empty, String> { return check(); }
};

template<rstd::size_t N>
struct SimdMemoryFactory {
    usize count, offset;
    auto  prepare() -> Result<SimdMemorySession<N>, String> {
        SimdMemorySession<N> s { .count = count, .offset = offset };
        for (usize i; i < usize(N + 2); ++i) s.source[i] = f32(i.to_primitive());
        return Ok(rstd::move(s));
    }
};

template<SimdOperation Work, rstd::size_t N>
auto add_simd_operation(bench::Suite& suite, ref<str> operation, int pattern = 0)
    -> Result<empty, String> {
    auto name = rstd::format("api/{}/lanes{}/pattern{}", operation, N, pattern);
    return suite.add(make_descriptor("simd"_str, name.as_str(), u64(1)),
                     SimdOperationFactory<Work, N> { pattern });
}

template<rstd::size_t N, bool Scalar, int Chains>
auto add_simd_chain(bench::Suite& suite) -> Result<empty, String> {
    auto name =
        rstd::format("api/add/lanes{}/chains{}/{}", N, Chains, Scalar ? "scalar"_str : "simd"_str);
    auto descriptor           = make_descriptor("simd"_str, name.as_str(), u64(1));
    descriptor.implementation = String::make(Scalar ? "scalar"_str : "simd"_str);
    return suite.add(rstd::move(descriptor), SimdAddFactory<N, Scalar, Chains> {});
}

template<rstd::size_t N>
auto add_simd_operations(bench::Suite& suite) -> Result<empty, String> {
    rstd_try((add_simd_operation<SimdOperation::Shift, N>(suite, "wrapping_shr"_str)));
    rstd_try((add_simd_operation<SimdOperation::SaturatingAdd, N>(suite, "saturating_add"_str)));
    rstd_try((add_simd_operation<SimdOperation::Min, N>(suite, "min"_str)));
    rstd_try((add_simd_operation<SimdOperation::Swizzle, N>(suite, "swizzle"_str)));
    rstd_try((add_simd_operation<SimdOperation::Reduce, N>(suite, "reduce_sum_unordered"_str)));
    rstd_try((add_simd_operation<SimdOperation::Fma, N>(suite, "mul_add"_str)));
    for (int pattern = 0; pattern < 2; ++pattern) {
        rstd_try((add_simd_operation<SimdOperation::Cast, N>(suite, "cast_f32_i32"_str, pattern)));
        rstd_try((add_simd_operation<SimdOperation::Classify, N>(suite, "is_finite"_str, pattern)));
    }
    for (int pattern = 0; pattern < 6; ++pattern) {
        rstd_try((add_simd_operation<SimdOperation::MaskQuery, N>(
            suite, "mask_any_count_first"_str, pattern)));
        rstd_try(
            (add_simd_operation<SimdOperation::MaskLogic, N>(suite, "mask_logic"_str, pattern)));
        rstd_try((add_simd_operation<SimdOperation::Select, N>(suite, "select"_str, pattern)));
    }
    rstd_try((add_simd_chain<N, false, 1>(suite)));
    rstd_try((add_simd_chain<N, true, 1>(suite)));
    rstd_try((add_simd_chain<N, false, 4>(suite)));
    rstd_try((add_simd_chain<N, true, 4>(suite)));
    for (usize offset; offset < usize(2); ++offset)
        for (usize count; count <= usize(N); ++count) {
            auto name =
                rstd::format("api/load_store_partial/lanes{}/len{}/offset{}", N, count, offset);
            rstd_try(suite.add(make_descriptor("simd"_str, name.as_str(), u64(1)),
                               SimdMemoryFactory<N> { count, offset }));
        }
    return Ok(empty {});
}

auto rstd_bench::register_simd_operations(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_simd_operations<4>(suite));
    return add_simd_operations<16>(suite);
}
