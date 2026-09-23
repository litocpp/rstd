module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

struct SortValue {
    u64   key;
    usize ordinal;
};

auto sort_key(const SortValue& value) -> u64 {
    auto key = value.key.to_primitive();
    for (int i = 0; i < 8; ++i) key = key * 6364136223846793005ULL + 1442695040888963407ULL;
    return u64(key);
}

auto copy_values(const Vec<SortValue>& source) -> Vec<SortValue> {
    return source.iter()
        .map([](ref<SortValue> value) {
            return *value;
        })
        .collect<Vec<SortValue>>();
}

template<int Distribution>
auto sort_input() -> Vec<SortValue> {
    auto           values = Vec<SortValue>::with_capacity(usize(1024));
    rstd::uint64_t seed   = 123;
    for (usize index; index < usize(1024); ++index) {
        seed     = seed * 6364136223846793005ULL + 1;
        auto key = Distribution == 2 ? seed % 8 : seed % 4096;
        values.push(SortValue { u64(key), index });
    }
    if constexpr (Distribution == 1)
        rstd::slice_::sort_by(values.as_mut_slice().as_mut_ref(),
                              [](const SortValue& a, const SortValue& b) {
                                  return sort_key(a) <=> sort_key(b);
                              });
    for (usize i; i < values.len(); ++i) values[i].ordinal = i;
    return values;
}

auto valid_sort(const Vec<SortValue>& values, const Vec<SortValue>& seed) -> bool {
    if (values.len() != seed.len()) return false;
    auto seen = Vec<bool>::make();
    for (usize i; i < seed.len(); ++i) seen.push(false);
    for (usize i; i < values.len(); ++i) {
        const auto& value = values[i];
        if (value.ordinal >= seed.len() || seen[value.ordinal] ||
            value.key != seed[value.ordinal].key)
            return false;
        seen[value.ordinal] = true;
        if (i == usize()) continue;
        const auto& previous = values[i - usize(1)];
        if (sort_key(previous) > sort_key(value) ||
            (sort_key(previous) == sort_key(value) && previous.ordinal > value.ordinal))
            return false;
    }
    return true;
}

template<bool Cached, int Distribution>
auto stable_sort_case(bench::BenchConfig config, const char* name) -> CaseRunResult {
    auto seed      = sort_input<Distribution>();
    auto operation = [](Vec<SortValue>& input) {
        if constexpr (Cached)
            rstd::slice_::sort_by_cached_key(input.as_mut_slice().as_mut_ref(), sort_key);
        else
            rstd::slice_::sort_by(input.as_mut_slice().as_mut_ref(),
                                  [](const SortValue& a, const SortValue& b) {
                                      return sort_key(a) <=> sort_key(b);
                                  });
    };
    auto checked = copy_values(seed);
    operation(checked);
    if (! valid_sort(checked, seed)) return failed("stable sort precheck failed"_Str);
    if constexpr (Cached) {
        auto counted = copy_values(seed);
        auto calls   = usize();
        rstd::slice_::sort_by_cached_key(counted.as_mut_slice().as_mut_ref(),
                                         [&](const SortValue& value) {
                                             ++calls;
                                             return sort_key(value);
                                         });
        if (calls != seed.len()) return failed("cached key count precheck failed"_Str);
    }
    auto runner = bench::Bench::new_(rstd::move(config));
    return complete_measurement(runner.run_batched_ref(text(name),
                                                       [&] {
                                                           return copy_values(seed);
                                                       },
                                                       operation,
                                                       {},
                                                       { .items_per_iteration = u64(1024) }),
                                true);
}

const BenchCase CASES[] = {
    { "slice",
      "stable_sort_random",
      5,
      &stable_sort_case<false, 0>,
      { .n = 1024, .distribution = "lcg-seed-123" } },
    { "slice",
      "cached_key_random",
      5,
      &stable_sort_case<true, 0>,
      { .n = 1024, .distribution = "lcg-seed-123" } },
    { "slice",
      "stable_sort_sorted",
      5,
      &stable_sort_case<false, 1>,
      { .n = 1024, .distribution = "sorted" } },
    { "slice",
      "cached_key_sorted",
      5,
      &stable_sort_case<true, 1>,
      { .n = 1024, .distribution = "sorted" } },
    { "slice",
      "stable_sort_duplicates",
      5,
      &stable_sort_case<false, 2>,
      { .n = 1024, .keys = 8, .distribution = "lcg-seed-123" } },
    { "slice",
      "cached_key_duplicates",
      5,
      &stable_sort_case<true, 2>,
      { .n = 1024, .keys = 8, .distribution = "lcg-seed-123" } },
};

auto rstd_bench::slice_benchmarks() -> BenchList {
    return { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}
