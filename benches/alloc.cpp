module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

auto string_clone(bench::BenchConfig config, const char* name) -> CaseRunResult {
    auto source = "benchmark string payload used by rstd clone measurements"_Str;
    auto copied = source.clone();
    if (copied != source) return failed("clone precheck failed"_Str);
    return measure_case(
        name,
        rstd::move(config),
        { .items_per_iteration = u64(1), .bytes_per_iteration = u64(source.len().to_primitive()) },
        [&] {
            rstd::hint::black_box(source.as_str());
            return source.clone();
        },
        [] {
            return true;
        });
}

auto push_values(Vec<i32>& values) -> void {
    for (i32 value {}; value < i32(64); value += i32(1)) values.push(i32(value));
}

template<bool Reserved>
auto vec_push(bench::BenchConfig config, const char* name) -> CaseRunResult {
    auto check = Vec<i32>::with_capacity(usize(64));
    push_values(check);
    if (check.len() != usize(64) || check[usize()] != i32() || *check.last().unwrap() != i32(63))
        return failed("push precheck failed"_Str);
    auto runner = bench::Bench::new_(rstd::move(config));
    auto work   = bench::RunConfig { .items_per_iteration = u64(64),
                                     .bytes_per_iteration = u64(64 * sizeof(i32)) };
    if constexpr (Reserved) {
        return complete_measurement(runner.run_batched_ref(
                                        text(name),
                                        [] {
                                            return Vec<i32>::with_capacity(usize(64));
                                        },
                                        [](Vec<i32>& values) {
                                            push_values(values);
                                        },
                                        {},
                                        rstd::move(work)),
                                    true);
    } else {
        return complete_measurement(runner.run(
                                        text(name),
                                        [] {
                                            auto values = Vec<i32>::with_capacity(usize(64));
                                            push_values(values);
                                            return values;
                                        },
                                        rstd::move(work)),
                                    true);
    }
}

auto bytes_extend_freeze(bench::BenchConfig config, const char* name) -> CaseRunResult {
    rstd::byte payload[64] {};
    for (rstd::size_t i = 0; i < 64; ++i) payload[i] = rstd::byte { static_cast<rstd::uint8_t>(i) };
    auto input     = slice<u8>::from_raw_parts(payload, usize(64));
    auto operation = [&] {
        auto buffer = rstd::bytes::BytesMut::with_capacity(usize(64));
        buffer.extend_from_slice(rstd::hint::black_box(input));
        return buffer.freeze();
    };
    auto check = operation();
    if (check.len() != usize(64) || check[usize()] != u8() || check[usize(63)] != u8(63))
        return failed("bytes precheck failed"_Str);
    return measure_case(name,
                        rstd::move(config),
                        { .items_per_iteration = u64(1), .bytes_per_iteration = u64(64) },
                        operation,
                        [] {
                            return true;
                        });
}

const BenchCase CASES[] = {
    { "alloc", "string_clone_end_to_end", 1000, &string_clone },
    { "alloc", "vec_push_end_to_end", 1000, &vec_push<false>, { .n = 64 } },
    { "alloc", "vec_push_reserved", 1000, &vec_push<true>, { .n = 64 } },
    { "alloc", "bytes_extend_freeze_end_to_end", 1000, &bytes_extend_freeze, { .n = 64 } },
};

auto rstd_bench::alloc_benchmarks() -> BenchList {
    return { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}
