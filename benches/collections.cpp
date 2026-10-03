module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.cppstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

auto map_key(u64 index) -> u64 {
    return index.wrapping_mul(u64(0x9e3779b97f4a7c15ULL));
}

template<bool Set>
struct RstdCollection {
    static constexpr bool tracked = ! Set;
    static constexpr bool is_set  = Set;
    static constexpr bool cppstd  = false;

    template<typename A>
    static auto make(A allocator) {
        auto hasher = rstd::hash::RandomState(u64(123), u64(456));
        if constexpr (Set)
            return ::alloc::collections::HashSet<u64>::with_hasher(rstd::move(hasher));
        else
            return ::alloc::collections::HashMap<u64,
                                                 u64,
                                                 rstd::hash::RandomState,
                                                 ::alloc::collections::DefaultHashEqual<u64>,
                                                 A>(
                usize(), rstd::move(hasher), {}, rstd::move(allocator));
    }
    static auto value(u64 index) -> u64 { return Set ? u64(1) : index + u64(1); }
    template<typename M>
    static void insert(M& map, u64 key, u64 index) {
        if constexpr (Set)
            map.insert(key);
        else
            map.insert(key, index);
    }
    template<typename M>
    static auto get(const M& map, u64 key) -> Option<u64> {
        if constexpr (Set)
            return map.contains(key) ? Some(u64(1)) : None();
        else {
            auto found = map.get(key);
            return found.is_some() ? Some(**found + u64(1)) : None();
        }
    }
    template<typename M>
    static auto size(const M& map) -> usize {
        return map.len();
    }
};

struct CollectionHasher {
    rstd::hash::RandomState state { u64(123), u64(456) };
    auto                    operator()(rstd::uint64_t key) const noexcept -> rstd::size_t {
        return static_cast<rstd::size_t>(rstd::hash::hash_one(state, u64(key)).to_primitive());
    }
};

template<bool Set>
struct CppstdCollection {
    static constexpr bool tracked = false;
    static constexpr bool is_set  = Set;
    static constexpr bool cppstd  = true;

    static auto make(::alloc::Global) {
        if constexpr (Set)
            return std::unordered_set<rstd::uint64_t, CollectionHasher> {};
        else
            return std::unordered_map<rstd::uint64_t, rstd::uint64_t, CollectionHasher> {};
    }
    static auto value(u64 index) -> u64 { return Set ? u64(1) : index + u64(1); }
    template<typename M>
    static void insert(M& map, u64 key, u64 index) {
        if constexpr (Set)
            map.emplace(key.to_primitive());
        else
            map.emplace(key.to_primitive(), index.to_primitive());
    }
    template<typename M>
    static auto get(const M& map, u64 key) -> Option<u64> {
        auto found = map.find(key.to_primitive());
        if (found == map.end()) return None();
        if constexpr (Set)
            return Some(u64(1));
        else
            return Some(u64(found->second) + u64(1));
    }
    template<typename M>
    static auto size(const M& map) -> usize {
        return usize(map.size());
    }
};

template<typename Backend, typename A>
auto collection_input(usize count, A allocator) {
    auto map = Backend::make(rstd::move(allocator));
    for (usize i; i < count; ++i)
        Backend::insert(map, map_key(u64(i.to_primitive())), u64(i.to_primitive()));
    return map;
}

template<typename Backend, bool Build, typename A>
struct CollectionSession {
    usize                                                                 count;
    rstd::uint64_t                                                        hit_percent;
    A                                                                     allocator;
    decltype(collection_input<Backend>(usize(), rstd::mtp::declval<A>())) map;
    Vec<u64, A>                                                           queries;
    u64                                                                   expected;

    CollectionSession(usize n, rstd::uint64_t hit, A alloc)
        : count(n),
          hit_percent(hit),
          allocator(alloc),
          map(collection_input<Backend>(n, alloc)),
          queries(Vec<u64, A>::new_in(alloc)) {
        if constexpr (! Build) {
            for (usize i; i < count; ++i) {
                auto present = (i.to_primitive() % 100) < hit_percent;
                queries.push(map_key(u64((present ? i : i + count).to_primitive())));
                if (present) expected += Backend::value(u64(i.to_primitive()));
            }
        }
    }
    auto lookup() const -> u64 {
        u64 sum;
        for (auto key : rstd::hint::black_box(queries.as_slice())) {
            auto value = Backend::get(map, key);
            if (value.is_some()) sum += *value;
        }
        return sum;
    }
    auto check() -> Result<empty, String> {
        if (Backend::size(map) != count) return Err("collection size differs"_Str);
        for (usize i; i < count; ++i) {
            auto value = Backend::get(map, map_key(u64(i.to_primitive())));
            if (value.is_none() || *value != Backend::value(u64(i.to_primitive())))
                return Err("collection contents differ"_Str);
        }
        if (Backend::get(map, map_key(u64(count.to_primitive()))).is_some())
            return Err("absent key found"_Str);
        if constexpr (! Build)
            if (lookup() != expected) return Err("collection lookup precheck failed"_Str);
        return Ok(empty {});
    }
    auto work() const -> bench::RunConfig {
        return { Build ? "entry"_Str : "query"_Str,
                 f64(count.to_primitive()),
                 u64(count.to_primitive()) };
    }
    auto operation() {
        if constexpr (Build)
            return collection_input<Backend>(rstd::hint::black_box(count), allocator);
        else
            return lookup();
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            work());
        return workload.run(engine, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String>
        requires Backend::tracked
    {
        for (u64 i; i < iterations; ++i) {
            auto value = operation();
            rstd::hint::black_box(value);
        }
        bench::DiagnosticMeasurement result;
        result.work = work();
        result.metrics.push({ "capacity"_Str,
                              bench::DiagnosticUnit::Count,
                              bench::MetricDirection::Neutral,
                              "prepared-map"_Str,
                              "HashMap::capacity"_Str,
                              bench::DiagnosticValue::Count(u64(map.capacity().to_primitive())) });
        result.metrics.push({ "capacity_utilization"_Str,
                              bench::DiagnosticUnit::Ratio,
                              bench::MetricDirection::Neutral,
                              "prepared-map"_Str,
                              "HashMap::len/capacity"_Str,
                              bench::DiagnosticValue::Ratio(u64(count.to_primitive()),
                                                            u64(map.capacity().to_primitive())) });
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> { return check(); }
};

template<typename Backend, bool Build>
struct CollectionFactory {
    usize          count;
    rstd::uint64_t hit_percent;
    template<typename A>
    auto make(A allocator) {
        using Session = CollectionSession<Backend, Build, A>;
        return Result<Session, String>(Ok(Session(count, hit_percent, rstd::move(allocator))));
    }
    auto prepare() { return make(::alloc::Global {}); }
    auto prepare(bench::DiagnosticContext& context)
        requires Backend::tracked
    {
        return make(context.allocator());
    }
};

template<typename Backend, bool Build>
auto add_collection_case(bench::Suite& suite, usize count, rstd::uint64_t hit)
    -> Result<empty, String> {
    auto name =
        Build ? rstd::format("build-drop/{}", count) : rstd::format("find/{}/{}", count, hit);
    if constexpr (Backend::cppstd) name.push_str("/cppstd"_str);
    auto descriptor =
        rstd_bench::make_descriptor(Backend::is_set ? "set"_str : "map"_str, name.as_str(), u64(3));
    descriptor.implementation = Backend::cppstd ? "cppstd"_Str : "rstd"_Str;
    descriptor.parameters.push(rstd_bench::parameter("n"_str, u64(count.to_primitive())));
    if constexpr (! Build)
        descriptor.parameters.push(rstd_bench::parameter("hit_percent"_str, u64(hit)));
    descriptor.parameters.push(
        rstd_bench::parameter("hasher"_str, "rstd::RandomState/seed-123-456"_str));
    descriptor.parameters.push(
        rstd_bench::parameter("keys"_str, "u64/multiply-9e3779b97f4a7c15"_str));
    descriptor.parameters.push(rstd_bench::parameter("reserve"_str, "none"_str));
    descriptor.parameters.push(rstd_bench::parameter(
        "allocator"_str, Backend::cppstd ? "std::allocator"_str : "alloc::Global"_str));
    auto id = descriptor.id.clone();
    rstd_try(suite.add(rstd::move(descriptor), CollectionFactory<Backend, Build> { count, hit }));
    if constexpr (Backend::tracked)
        rstd_try(
            suite.add_diagnostic(id.as_str(), CollectionFactory<Backend, Build> { count, hit }));
    return Ok(empty {});
}

template<bool Set, bool Build>
auto add_collection_pair(bench::Suite& suite, usize count, rstd::uint64_t hit)
    -> Result<empty, String> {
    rstd_try((add_collection_case<RstdCollection<Set>, Build>(suite, count, hit)));
    return add_collection_case<CppstdCollection<Set>, Build>(suite, count, hit);
}

auto rstd_bench::add_collection_cases(bench::Suite& suite) -> Result<empty, String> {
    const rstd::size_t   sizes[] { 100, 10000 };
    const rstd::uint64_t percentages[] { 0, 50, 100 };
    for (auto size : sizes) {
        for (auto hit : percentages) {
            rstd_try((add_collection_pair<false, false>(suite, usize(size), hit)));
            rstd_try((add_collection_pair<true, false>(suite, usize(size), hit)));
        }
        rstd_try((add_collection_pair<false, true>(suite, usize(size), 0)));
        rstd_try((add_collection_pair<true, true>(suite, usize(size), 0)));
    }
    return Ok(empty {});
}
