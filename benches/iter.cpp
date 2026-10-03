module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace iter  = rstd::iter;
namespace bench = rstd::bench;

auto input_values(usize size) -> Vec<i32> {
    return iter::range(usize(), size)
        .map([](usize i) {
            return i32(i.to_primitive());
        })
        .collect<Vec<i32>>();
}

template<bool Pipeline>
auto pipeline_sum(slice<i32> input) -> i64 {
    if constexpr (Pipeline) {
        return iter::from_slice(input)
            .map([](ref<i32> value) {
                return i64((*value).to_primitive()) * i64(2);
            })
            .filter([](const i64& value) {
                return value > i64(4);
            })
            .fold(i64(), [](i64 sum, i64 value) {
                return sum + value;
            });
    } else {
        i64 sum;
        for (usize i; i < input.len(); ++i) {
            auto value = i64(input[i].to_primitive()) * i64(2);
            if (value > i64(4)) sum += value;
        }
        return sum;
    }
}

template<bool Pipeline, rstd::size_t Size>
auto map_filter_sum(bench::Bench& runner, ref<str> name) -> CaseRunResult {
    auto input = input_values(usize(Size));
    if (pipeline_sum<true>(input.as_slice()) != pipeline_sum<false>(input.as_slice()))
        return failed("sum precheck failed"_Str);
    return measure_case(
        name,
        runner,
        { .items_per_iteration = u64(Size) },
        [&] {
            return pipeline_sum<Pipeline>(rstd::hint::black_box(input.as_slice()));
        },
        [] {
            return true;
        });
}

template<bool Pipeline>
auto find_value(slice<i32> input, i32 wanted) -> Option<i32> {
    if constexpr (Pipeline) {
        return iter::from_slice(input)
            .map([](ref<i32> x) {
                return *x;
            })
            .find([&](const i32& x) {
                return x == wanted;
            });
    } else {
        for (usize i; i < input.len(); ++i)
            if (input[i] == wanted) return Some(i32(input[i]));
        return None();
    }
}

template<bool Pipeline, int Wanted>
auto find_case(bench::Bench& runner, ref<str> name) -> CaseRunResult {
    auto input  = input_values(usize(1024));
    auto wanted = i32(Wanted);
    if (find_value<true>(input.as_slice(), wanted) != find_value<false>(input.as_slice(), wanted))
        return failed("find precheck failed"_Str);
    return measure_case(
        name,
        runner,
        { .items_per_iteration = u64(Wanted < 0 ? 1024 : Wanted + 1) },
        [&] {
            return find_value<Pipeline>(rstd::hint::black_box(input.as_slice()),
                                        rstd::hint::black_box(wanted));
        },
        [] {
            return true;
        });
}

template<bool Owned>
auto chunk_sum(slice<i32> input) -> i64 {
    i64 sum;
    if constexpr (Owned) {
        auto chunks = iter::chunks(iter::from_slice(input).map([](ref<i32> x) {
            return *x;
        }),
                                   usize(16));
        for (auto chunk = chunks.next(); chunk.is_some(); chunk = chunks.next())
            for (const auto& x : *chunk) sum += i64(x.to_primitive());
    } else {
        auto chunks = rstd::slice_::chunks(input, usize(16));
        for (auto chunk = chunks.next(); chunk.is_some(); chunk = chunks.next())
            for (const auto& x : *chunk) sum += i64(x.to_primitive());
    }
    return sum;
}

template<bool Owned>
auto chunks_case(bench::Bench& runner, ref<str> name) -> CaseRunResult {
    auto input = input_values(usize(1024));
    if (chunk_sum<true>(input.as_slice()) != chunk_sum<false>(input.as_slice()))
        return failed("chunk precheck failed"_Str);
    return measure_case(
        name,
        runner,
        { .items_per_iteration = u64(1024) },
        [&] {
            return chunk_sum<Owned>(rstd::hint::black_box(input.as_slice()));
        },
        [] {
            return true;
        });
}

auto group_input() -> Vec<i32> {
    auto           input = Vec<i32>::with_capacity(usize(1024));
    rstd::uint64_t seed  = 123;
    for (usize i; i < usize(1024); ++i) {
        seed = seed * 6364136223846793005ULL + 1;
        input.push(i32((seed >> 32) % 4096));
    }
    return input;
}

template<bool Pipeline, int Keys>
auto group_values(slice<i32> input) {
    auto key = [](const i32& value) {
        return value % i32(Keys);
    };
    if constexpr (Pipeline) {
        return iter::group_by(iter::from_slice(input).map([](ref<i32> x) {
            return *x;
        }),
                              key);
    } else {
        auto index = iter::GroupIndex<i32, i32>(rstd::hash::RandomState {}, {});
        for (const auto& value : input) index.insert(key(value), i32(value));
        return rstd::move(index).into_groups();
    }
}

template<bool Pipeline, int Keys>
auto group_case(bench::Bench& runner, ref<str> name) -> CaseRunResult {
    auto input = group_input();
    auto a     = group_values<true, Keys>(input.as_slice());
    auto b     = group_values<false, Keys>(input.as_slice());
    if (a.len() != b.len()) return failed("group size precheck failed"_Str);
    for (usize i; i < a.len(); ++i)
        if (a[i].key != b[i].key || a[i].items.as_slice() != b[i].items.as_slice())
            return failed("group membership precheck failed"_Str);
    return measure_case(
        name,
        runner,
        { .items_per_iteration = u64(1024) },
        [&] {
            return group_values<Pipeline, Keys>(rstd::hint::black_box(input.as_slice()));
        },
        [] {
            return true;
        });
}

template<bool Pipeline, int Keys>
auto count_values(slice<i32> input) -> Vec<rstd::tuple<i32, usize>> {
    using Pair = rstd::tuple<i32, usize>;
    if constexpr (Pipeline) {
        return iter::count_by(iter::from_slice(input).map([](ref<i32> x) {
                   return *x;
               }),
                              [](const i32& x) {
                                  return x % i32(Keys);
                              })
            .unwrap();
    } else {
        auto indices = rstd::collections::HashMap<i32, usize>::make();
        auto counts  = Vec<usize>::make();
        for (const auto& value : input) {
            auto key   = value % i32(Keys);
            auto found = indices.get(key);
            if (found.is_some()) {
                ++counts[**found];
            } else {
                auto index = counts.len();
                counts.push(usize(1));
                indices.insert(key, index);
            }
        }
        auto ordered = Vec<Option<Pair>>::make();
        for (usize i; i < counts.len(); ++i) ordered.push(None<Pair>());
        auto entries = rstd::move(indices).into_iter();
        for (auto entry = entries.next(); entry.is_some(); entry = entries.next()) {
            auto position = entry->template get<1>();
            ordered[position].insert(Pair(entry->template get<0>(), counts[position]));
        }
        return rstd::move(ordered)
            .into_iter()
            .map([](Option<Pair> pair) {
                return pair.unwrap();
            })
            .template collect<Vec<Pair>>();
    }
}

template<bool Pipeline, int Keys>
auto count_case(bench::Bench& runner, ref<str> name) -> CaseRunResult {
    auto input = group_input();
    auto a     = count_values<true, Keys>(input.as_slice());
    auto b     = count_values<false, Keys>(input.as_slice());
    if (a.as_slice() != b.as_slice()) return failed("count precheck failed"_Str);
    return measure_case(
        name,
        runner,
        { .items_per_iteration = u64(1024) },
        [&] {
            return count_values<Pipeline, Keys>(rstd::hint::black_box(input.as_slice()));
        },
        [] {
            return true;
        });
}

auto rstd_bench::register_iter(rstd::bench::Suite& suite) -> Result<empty, String> {
    using namespace rstd_bench;
    using namespace rstd::literals;

    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "count_by_8"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(8)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &count_case<true, 8>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "count_by_8"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(8)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &count_case<false, 8>));
    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "count_by_256"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(256)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &count_case<true, 256>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "count_by_256"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(256)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &count_case<false, 256>));
    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "map_filter_sum_64"_str,
                                                u64(1000),
                                                parameter("n"_str, u64(64)),
                                                parameter("distribution"_str, "ascending"_str)),
                                &map_filter_sum<true, 64>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "map_filter_sum_64"_str,
                                                u64(1000),
                                                parameter("n"_str, u64(64)),
                                                parameter("distribution"_str, "ascending"_str)),
                                &map_filter_sum<false, 64>));
    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "map_filter_sum_1024"_str,
                                                u64(1000),
                                                parameter("n"_str, u64(1024)),
                                                parameter("distribution"_str, "ascending"_str)),
                                &map_filter_sum<true, 1024>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "map_filter_sum_1024"_str,
                                                u64(1000),
                                                parameter("n"_str, u64(1024)),
                                                parameter("distribution"_str, "ascending"_str)),
                                &map_filter_sum<false, 1024>));
    rstd_try(suite.add_function(
        make_descriptor("iter"_str, "find_first"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<true, 0>));
    rstd_try(suite.add_function(
        make_descriptor("loop"_str, "find_first"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<false, 0>));
    rstd_try(suite.add_function(
        make_descriptor("iter"_str, "find_middle"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<true, 512>));
    rstd_try(suite.add_function(
        make_descriptor("loop"_str, "find_middle"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<false, 512>));
    rstd_try(suite.add_function(
        make_descriptor("iter"_str, "find_missing"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<true, -1>));
    rstd_try(suite.add_function(
        make_descriptor("loop"_str, "find_missing"_str, u64(1000), parameter("n"_str, u64(1024))),
        &find_case<false, -1>));
    rstd_try(suite.add_function(
        make_descriptor("iter"_str, "chunks_owned"_str, u64(50), parameter("n"_str, u64(1024))),
        &chunks_case<true>));
    rstd_try(suite.add_function(
        make_descriptor("slice"_str, "chunks_view"_str, u64(1000), parameter("n"_str, u64(1024))),
        &chunks_case<false>));
    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "group_by_8"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(8)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &group_case<true, 8>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "group_by_8"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(8)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &group_case<false, 8>));
    rstd_try(suite.add_function(make_descriptor("iter"_str,
                                                "group_by_256"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(256)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &group_case<true, 256>));
    rstd_try(suite.add_function(make_descriptor("loop"_str,
                                                "group_by_256"_str,
                                                u64(10),
                                                parameter("n"_str, u64(1024)),
                                                parameter("keys"_str, u64(256)),
                                                parameter("distribution"_str, "lcg-seed-123"_str)),
                                &group_case<false, 256>));
    return Ok(empty {});
}
