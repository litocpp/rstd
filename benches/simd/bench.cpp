module;
#include <rstd/macro.hpp>

module rstd_benches;
import :simd.kernels;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
using namespace rstd_bench::simd_kernels;
namespace bench = rstd::bench;

enum class SearchKind
{
    Scalar,
    Simd,
    Libc
};

template<SearchKind Kind>
struct SimdSearchSession {
    Vec<u8>    input;
    Vec<usize> expected;
    usize      length, offset, slot;
    auto       operation() -> usize {
        auto start = slot * (length + offset) + offset;
        auto bytes =
            slice<u8>::from_raw_parts(input.as_slice().as_raw_ptr() + start.to_primitive(), length);
        slot = (slot + usize(1)) % expected.len();
        if constexpr (Kind == SearchKind::Simd)
            return search_simd(bytes, u8(0));
        else if constexpr (Kind == SearchKind::Libc)
            return search_libc(bytes, u8(0));
        else
            return search_scalar(bytes, u8(0));
    }
    auto check() -> Result<empty, String> {
        slot = usize();
        for (usize i; i < expected.len(); ++i)
            if (operation() != expected[i]) return Err("SIMD search result mismatch"_Str);
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

template<SearchKind Kind>
struct SimdSearchFactory {
    usize length, offset, slots;
    int   mode;
    auto  prepare() -> Result<SimdSearchSession<Kind>, String> {
        auto input    = Vec<u8>::with_capacity((length + offset) * slots);
        auto expected = Vec<usize>::with_capacity(slots);
        for (usize i; i < (length + offset) * slots; ++i) input.push(u8(7));
        u64 seed(12345);
        for (usize i; i < slots; ++i) {
            seed     = seed.wrapping_mul(u64(6364136223846793005ULL)).wrapping_add(u64(1));
            auto hit = mode == 0   ? length
                       : mode == 1 ? usize()
                       : mode == 2 ? length - usize(1)
                                   : usize(seed.to_primitive() % length.to_primitive());
            expected.push(usize(hit));
            if (hit < length) input[i * (length + offset) + offset + hit] = u8(0);
        }
        return Ok(SimdSearchSession<Kind> {
            rstd::move(input), rstd::move(expected), length, offset, usize() });
    }
};

enum class SimdNumericWork
{
    ScalarTransform,
    SliceTransform,
    NativeTransform,
    SimdTransform,
    ScalarSum,
    SimdSum
};

template<SimdNumericWork Work>
struct SimdNumericSession {
    Vec<f32> input;
    Vec<f32> output;

    auto operation() {
        auto* in = input.as_slice().as_raw_ptr();
        auto  n  = input.len().to_primitive();
        if constexpr (Work == SimdNumericWork::ScalarSum)
            return sum_scalar(in, n);
        else if constexpr (Work == SimdNumericWork::SimdSum)
            return sum_simd(in, n);
        else {
            auto* out = output.as_mut_slice().as_raw_ptr();
            if constexpr (Work == SimdNumericWork::SliceTransform)
                transform_slice(output.as_mut_slice().as_mut_ref(), input.as_slice());
            else if constexpr (Work == SimdNumericWork::ScalarTransform)
                transform_scalar(out, in, n);
            else if constexpr (Work == SimdNumericWork::NativeTransform)
                transform_native(out, in, n);
            else
                transform_simd(out, in, n);
            return out;
        }
    }
    auto check() -> Result<empty, String> {
        if constexpr (Work == SimdNumericWork::ScalarSum || Work == SimdNumericWork::SimdSum) {
            if (operation() !=
                sum_scalar(input.as_slice().as_raw_ptr(), input.len().to_primitive()))
                return Err("SIMD ordered sum mismatch"_Str);
        } else {
            operation();
            for (usize i; i < input.len(); ++i)
                if (output[i] != input[i] * f32(2) + f32(1))
                    return Err("SIMD transform result mismatch"_Str);
        }
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

template<SimdNumericWork Work>
struct SimdNumericFactory {
    usize length;
    auto  prepare() -> Result<SimdNumericSession<Work>, String> {
        auto input  = Vec<f32>::with_capacity(length);
        auto output = Vec<f32>::make();
        for (usize i; i < length; ++i) input.push(f32(float(i.to_primitive() % 16)));
        if constexpr (Work != SimdNumericWork::ScalarSum && Work != SimdNumericWork::SimdSum) {
            output.reserve(length);
            for (usize i; i < length; ++i) output.push(f32());
        }
        return Ok(SimdNumericSession<Work> { rstd::move(input), rstd::move(output) });
    }
};

template<SearchKind Kind>
auto add_simd_search(bench::Suite& suite,
                     usize         n,
                     int           mode,
                     usize         offset = usize(),
                     usize         slots  = usize(1)) -> Result<empty, String> {
    auto position       = mode == 0   ? "none"_str
                          : mode == 1 ? "first"_str
                          : mode == 2 ? "last"_str
                                      : "random"_str;
    auto implementation = Kind == SearchKind::Scalar ? "scalar"_str
                          : Kind == SearchKind::Simd ? "simd"_str
                                                     : "libc"_str;
    auto name           = rstd::format(
        "search/{}/{}/{}/offset{}/slots{}", n, position, implementation, offset, slots);
    auto descriptor           = make_descriptor("simd"_str,
                                                name.as_str(),
                                                u64(1),
                                                parameter("n"_str, u64(n.to_primitive())),
                                                parameter("hit"_str, position),
                                                parameter("offset"_str, u64(offset.to_primitive())),
                                                parameter("slots"_str, u64(slots.to_primitive())));
    descriptor.implementation = String::make(implementation);
    return suite.add(rstd::move(descriptor), SimdSearchFactory<Kind> { n, offset, slots, mode });
}

template<SimdNumericWork Work>
auto add_simd_numeric(bench::Suite& suite, usize n, ref<str> operation, ref<str> implementation)
    -> Result<empty, String> {
    auto name       = rstd::format("{}/{}/{}", operation, n, implementation);
    auto descriptor = make_descriptor(
        "simd"_str, name.as_str(), u64(1), parameter("n"_str, u64(n.to_primitive())));
    descriptor.implementation = String::make(implementation);
    return suite.add(rstd::move(descriptor), SimdNumericFactory<Work> { n });
}

auto rstd_bench::register_simd(bench::Suite& suite) -> Result<empty, String> {
    constexpr rstd::size_t search_sizes[] { 1, 8, 15, 16, 17, 31, 32, 33, 64, 4096, 1048576 };
    for (auto size : search_sizes) {
        for (int mode = 0; mode < 3; ++mode) {
            rstd_try(add_simd_search<SearchKind::Scalar>(suite, usize(size), mode));
            rstd_try(add_simd_search<SearchKind::Simd>(suite, usize(size), mode));
            rstd_try(add_simd_search<SearchKind::Libc>(suite, usize(size), mode));
        }
    }
    const usize offsets[] { usize(1), usize(7), usize(15) };
    for (auto offset : offsets) {
        rstd_try(add_simd_search<SearchKind::Scalar>(suite, usize(4097), 3, offset, usize(64)));
        rstd_try(add_simd_search<SearchKind::Simd>(suite, usize(4097), 3, offset, usize(64)));
        rstd_try(add_simd_search<SearchKind::Libc>(suite, usize(4097), 3, offset, usize(64)));
    }
    rstd_try(add_simd_search<SearchKind::Scalar>(suite, usize(4096), 0, usize(), usize(32768)));
    rstd_try(add_simd_search<SearchKind::Simd>(suite, usize(4096), 0, usize(), usize(32768)));
    rstd_try(add_simd_search<SearchKind::Libc>(suite, usize(4096), 0, usize(), usize(32768)));
    constexpr rstd::size_t numeric_sizes[] { 8, 1024, 16777216 };
    for (auto size : numeric_sizes) {
        auto n = usize(size);
        rstd_try(add_simd_numeric<SimdNumericWork::ScalarTransform>(
            suite, n, "transform"_str, "scalar"_str));
        rstd_try(add_simd_numeric<SimdNumericWork::SliceTransform>(
            suite, n, "transform"_str, "slice"_str));
        rstd_try(add_simd_numeric<SimdNumericWork::NativeTransform>(
            suite, n, "transform"_str, "native"_str));
        rstd_try(add_simd_numeric<SimdNumericWork::SimdTransform>(
            suite, n, "transform"_str, "simd"_str));
        rstd_try(add_simd_numeric<SimdNumericWork::ScalarSum>(suite, n, "sum"_str, "scalar"_str));
        rstd_try(add_simd_numeric<SimdNumericWork::SimdSum>(suite, n, "sum"_str, "simd"_str));
    }
    return Ok(empty {});
}
