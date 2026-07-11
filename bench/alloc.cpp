#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace
{

auto string_clone(rstd_bench::BenchContext& context) -> bool {
    auto source = String::make("benchmark string payload used by rstd clone measurements");
    auto total  = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        auto copy = rstd::as<rstd::clone::Clone>(source).clone();
        if (copy.len() != source.len() || copy.data()[0] != 'b') {
            return false;
        }
        total += copy.len();
        rstd::hint::black_box(total);
    }

    context.set_items_processed(context.iterations());
    context.set_bytes_processed(context.iterations() * source.len());
    return total == context.iterations() * source.len();
}

auto vec_push_reserved(rstd_bench::BenchContext& context) -> bool {
    auto total = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        auto vec = Vec<int>::with_capacity(64);
        for (int value = 0; value < 64; ++value) {
            vec.push(static_cast<int>(value));
        }
        if (vec.len() != 64 || vec[0] != 0 || vec[63] != 63) {
            return false;
        }
        total += vec.len();
        rstd::hint::black_box(total);
    }

    context.set_items_processed(context.iterations() * 64);
    context.set_bytes_processed(context.iterations() * 64 * sizeof(int));
    return total == context.iterations() * 64;
}

auto bytes_extend_freeze(rstd_bench::BenchContext& context) -> bool {
    const u8 payload[] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    };
    auto total = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        auto buf = bytes::BytesMut::with_capacity(64);
        buf.extend_from_slice(payload, 64);
        auto frozen = buf.freeze();
        if (frozen.len() != 64 || frozen[0] != 0 || frozen[63] != 63) {
            return false;
        }
        total += frozen.len();
        rstd::hint::black_box(total);
    }

    context.set_items_processed(context.iterations());
    context.set_bytes_processed(context.iterations() * 64);
    return total == context.iterations() * 64;
}

const rstd_bench::BenchCase CASES[] = {
    { "alloc", "string_clone", 200'000, 1'000, &string_clone },
    { "alloc", "vec_push_reserved_64", 200'000, 1'000, &vec_push_reserved },
    { "alloc", "bytes_extend_freeze_64", 200'000, 1'000, &bytes_extend_freeze },
};

} // namespace

namespace rstd_bench
{

auto alloc_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
