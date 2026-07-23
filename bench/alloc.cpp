#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace
{

auto string_clone(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto source     = String::make("benchmark string payload used by rstd clone measurements"_str);
    auto total      = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = true;
    auto run_config = bench::RunConfig {
        .items_per_iteration = u64(1),
        .bytes_per_iteration = u64(source.len().to_primitive()),
    };
    return rstd_bench::measure_case(
        "string_clone",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto copy = rstd::as<rstd::clone::Clone>(source).clone();
            if (copy.len() != source.len() || copy.as_str()[usize()] != u8('b')) {
                valid = false;
            }
            ++calls;
            total += copy.len().to_primitive();
            rstd::hint::black_box(total);
        },
        [&] {
            return valid && total == calls * source.len().to_primitive();
        });
}

auto vec_push_reserved(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto total      = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = true;
    auto run_config = bench::RunConfig {
        .items_per_iteration = u64(64),
        .bytes_per_iteration = u64(64 * sizeof(int)),
    };
    return rstd_bench::measure_case(
        "vec_push_reserved_64",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto vec = Vec<int>::with_capacity(usize(64));
            for (int value = 0; value < 64; ++value) {
                vec.push(static_cast<int>(value));
            }
            if (vec.len() != usize(64) || vec[usize()] != 0 || vec[usize(63)] != 63) {
                valid = false;
            }
            ++calls;
            total += vec.len().to_primitive();
            rstd::hint::black_box(total);
        },
        [&] {
            return valid && total == calls * 64;
        });
}

auto bytes_extend_freeze(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    byte payload[64] {};
    for (rstd::size_t index = 0; index < 64; ++index) {
        payload[index] = byte { static_cast<std::uint8_t>(index) };
    }
    auto total      = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = true;
    auto run_config = bench::RunConfig {
        .items_per_iteration = u64(1),
        .bytes_per_iteration = u64(64),
    };
    return rstd_bench::measure_case(
        "bytes_extend_freeze_64",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto buf = bytes::BytesMut::with_capacity(usize(64));
            buf.extend_from_slice(slice<u8>::from_raw_parts(payload, usize(64)));
            auto frozen = buf.freeze();
            if (frozen.len() != usize(64) || frozen[usize()] != u8() ||
                frozen[usize(63)] != u8(63)) {
                valid = false;
            }
            ++calls;
            total += frozen.len().to_primitive();
            rstd::hint::black_box(total);
        },
        [&] {
            return valid && total == calls * 64;
        });
}

const rstd_bench::BenchCase CASES[] = {
    { "alloc", "string_clone", 1'000, &string_clone },
    { "alloc", "vec_push_reserved_64", 1'000, &vec_push_reserved },
    { "alloc", "bytes_extend_freeze_64", 1'000, &bytes_extend_freeze },
};

} // namespace

namespace rstd_bench
{

auto alloc_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
