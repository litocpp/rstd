module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.cppstd;
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

auto valid_sort(const Vec<SortValue>& values, const Vec<SortValue>& seed, bool stable) -> bool {
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
            (stable && sort_key(previous) == sort_key(value) && previous.ordinal > value.ordinal))
            return false;
    }
    return true;
}

enum class SortKind
{
    Stable,
    Cached,
    Unstable
};

template<SortKind Kind, bool Cppstd, int Distribution>
struct SortSession {
    Vec<SortValue> seed;
    Vec<SortValue> checked;
    auto           operation() const {
        return [](Vec<SortValue>& input) {
            if constexpr (Cppstd) {
                auto  values = input.as_mut_slice().as_mut_ref();
                auto* first  = values.as_raw_ptr();
                auto* last   = first + values.len().to_primitive();
                auto  less   = [](const SortValue& a, const SortValue& b) {
                    return sort_key(a) < sort_key(b);
                };
                if constexpr (Kind == SortKind::Stable)
                    std::stable_sort(first, last, less);
                else
                    std::sort(first, last, less);
            } else if constexpr (Kind == SortKind::Cached)
                rstd::slice_::sort_by_cached_key(input.as_mut_slice().as_mut_ref(), sort_key);
            else if constexpr (Kind == SortKind::Unstable)
                rstd::slice_::sort_unstable_by(input.as_mut_slice().as_mut_ref(),
                                               [](const SortValue& a, const SortValue& b) {
                                                   return sort_key(a) < sort_key(b);
                                               });
            else
                rstd::slice_::sort_by(input.as_mut_slice().as_mut_ref(),
                                      [](const SortValue& a, const SortValue& b) {
                                          return sort_key(a) <=> sort_key(b);
                                      });
        };
    }

    auto check() -> Result<empty, String> {
        auto operation = this->operation();
        checked        = copy_values(seed);
        operation(checked);
        if (! valid_sort(checked, seed, Kind != SortKind::Unstable))
            return Err("sort order, permutation or stability precheck failed"_Str);
        if constexpr (Kind == SortKind::Cached) {
            auto counted = copy_values(seed);
            auto calls   = usize();
            rstd::slice_::sort_by_cached_key(counted.as_mut_slice().as_mut_ref(),
                                             [&](const SortValue& value) {
                                                 ++calls;
                                                 return sort_key(value);
                                             });
            if (calls != seed.len()) return Err("cached key count precheck failed"_Str);
        }

        return Ok(empty {});
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::batched_ref(
            [this] {
                return copy_values(seed);
            },
            operation(),
            {},
            { .items_per_iteration = u64(1024) });
        return workload.run(engine, name);
    }
    auto finish() -> Result<empty, String> { return Ok(empty {}); }
};

template<SortKind Kind, bool Cppstd, int Distribution>
auto add_sort(bench::Suite& suite, ref<str> name) -> Result<empty, String> {
    auto label      = Cppstd ? rstd::format("{}/cppstd", name) : String::make(name);
    auto descriptor = make_descriptor(
        "slice"_str,
        label.as_str(),
        u64(5),
        parameter("n"_str, u64(1024)),
        parameter("distribution"_str, Distribution == 1 ? "sorted"_str : "lcg-seed-123"_str));
    if constexpr (Distribution == 2) descriptor.parameters.push(parameter("keys"_str, u64(8)));
    descriptor.implementation = Cppstd ? "cppstd"_Str : "rstd"_Str;
    descriptor.parameters.push(parameter("storage"_str, "Vec<SortValue>"_str));
    descriptor.parameters.push(parameter("key"_str, "lcg-8/u64"_str));
    descriptor.parameters.push(
        parameter("ordering"_str, Kind == SortKind::Unstable ? "unstable"_str : "stable"_str));
    descriptor.parameters.push(parameter(
        "key_evaluation"_str, Kind == SortKind::Cached ? "cached"_str : "per-comparison"_str));
    return suite.add(rstd::move(descriptor), bench::factory([] {
                         using Session = SortSession<Kind, Cppstd, Distribution>;
                         return Result<Session, String>(Ok(Session { sort_input<Distribution>() }));
                     }));
}

template<int Distribution>
auto add_sort_distribution(bench::Suite& suite, ref<str> distribution) -> Result<empty, String> {
    auto stable   = rstd::format("stable_sort_{}", distribution);
    auto unstable = rstd::format("unstable_sort_{}", distribution);
    auto cached   = rstd::format("cached_key_{}", distribution);
    rstd_try((add_sort<SortKind::Stable, false, Distribution>(suite, stable.as_str())));
    rstd_try((add_sort<SortKind::Stable, true, Distribution>(suite, stable.as_str())));
    rstd_try((add_sort<SortKind::Unstable, false, Distribution>(suite, unstable.as_str())));
    rstd_try((add_sort<SortKind::Unstable, true, Distribution>(suite, unstable.as_str())));
    return add_sort<SortKind::Cached, false, Distribution>(suite, cached.as_str());
}

auto rstd_bench::register_slice(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_sort_distribution<0>(suite, "random"_str));
    rstd_try(add_sort_distribution<1>(suite, "sorted"_str));
    return add_sort_distribution<2>(suite, "duplicates"_str);
}
