module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

enum class AllocationPolicy
{
    Global,
    Recycling,
    Pool
};
enum class AllocationPattern
{
    Single,
    Batch,
    Mixed,
    Burst,
    VecResize
};

template<AllocationPolicy Policy, typename Upstream>
struct PolicyOwner;

template<typename Upstream>
struct PolicyOwner<AllocationPolicy::Global, Upstream> {
    Upstream allocator;
    explicit PolicyOwner(Upstream upstream): allocator(rstd::move(upstream)) {}
    auto view() { return rstd::alloc::clone_allocator(allocator); }
    auto allocate(rstd::alloc::Layout layout) {
        return rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout);
    }
    void deallocate(void* pointer, rstd::alloc::Layout layout) {
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(pointer, layout);
    }
};

template<typename Upstream>
struct PolicyOwner<AllocationPolicy::Recycling, Upstream> {
    ::alloc::RecyclingArena<Upstream> allocator;
    explicit PolicyOwner(Upstream upstream): allocator(usize(65536), rstd::move(upstream)) {}
    auto view() { return allocator.allocator(); }
    auto allocate(rstd::alloc::Layout layout) { return allocator.allocate(layout); }
    void deallocate(void* pointer, rstd::alloc::Layout layout) {
        allocator.deallocate(pointer, layout);
    }
};

template<typename Upstream>
struct PolicyOwner<AllocationPolicy::Pool, Upstream> {
    rstd::alloc::PoolAllocator<Upstream> allocator;
    explicit PolicyOwner(Upstream upstream): allocator({}, rstd::move(upstream)) {}
    auto view() { return ::alloc::allocator_ref(allocator); }
    auto allocate(rstd::alloc::Layout layout) { return allocator.allocate(layout); }
    void deallocate(void* pointer, rstd::alloc::Layout layout) {
        allocator.deallocate(pointer, layout);
    }
};

template<AllocationPolicy Policy, typename Upstream = ::alloc::Global>
struct AllocationPolicySession {
    Box<PolicyOwner<Policy, Upstream>> owner;
    AllocationPattern                  pattern;
    rstd::size_t                       size;
    rstd::size_t                       alignment;
    bool                               failed {};
    u64                                relocated_bytes {}, grows {}, shrinks {};

    auto count() const -> unsigned { return pattern == AllocationPattern::Single ? 1 : 128; }
    auto layout(unsigned index) const -> rstd::alloc::Layout {
        constexpr rstd::size_t sizes[] { 24, 31, 32, 33, 63, 64, 65, 127, 128, 129, 511, 1025 };
        return rstd::alloc::Layout::from_size_align(
                   usize(pattern == AllocationPattern::Mixed ? sizes[index % 12] : size),
                   usize(alignment))
            .unwrap();
    }
    auto work() const -> bench::RunConfig {
        if (pattern == AllocationPattern::VecResize)
            return { .items_per_iteration = u64(1), .bytes_per_iteration = u64(6144) };
        u64 bytes;
        for (unsigned i = 0; i < count(); ++i) bytes += u64(layout(i).size.to_primitive());
        if (pattern == AllocationPattern::Burst) bytes += u64(64);
        return { .items_per_iteration =
                     u64(count() + (pattern == AllocationPattern::Burst ? 1 : 0)),
                 .bytes_per_iteration = bytes };
    }
    template<bool Diagnose>
    void resize_vector() {
        auto allocator = owner->view();
        auto values    = Vec<u8, decltype(allocator)>::make_in(rstd::move(allocator));
        constexpr rstd::size_t capacities[] { 24, 31, 32, 33, 64, 65, 128, 129, 512, 1025, 6144 };
        for (auto capacity : capacities) {
            auto* pointer = values.as_ptr().as_raw_ptr();
            auto  length  = values.len();
            if (values.try_reserve_exact(usize(capacity) - length).is_err()) {
                failed = true;
                return;
            }
            if constexpr (Diagnose) {
                ++grows;
                if (values.as_ptr().as_raw_ptr() != pointer)
                    relocated_bytes += u64(length.to_primitive());
            }
            while (values.len() < usize(capacity))
                values.push(u8(values.len().to_primitive() % 251));
        }
        values.truncate(usize(24));
        auto* pointer = values.as_ptr().as_raw_ptr();
        values.shrink_to_fit();
        if constexpr (Diagnose) {
            ++shrinks;
            if (values.as_ptr().as_raw_ptr() != pointer) relocated_bytes += u64(24);
        }
        rstd::hint::black_box(values.as_slice());
        for (usize i; i < values.len(); ++i)
            if (u8(values[i]) != u8(i.to_primitive())) failed = true;
    }
    void allocate_batch() {
        void*    pointers[128] {};
        unsigned allocated = 0;
        for (; allocated < count(); ++allocated) {
            auto memory = owner->allocate(layout(allocated));
            if (memory.is_err()) {
                failed = true;
                break;
            }
            pointers[allocated] = memory->pointer;
            rstd::hint::black_box(memory->pointer);
        }
        if (allocated != count()) {
            for (unsigned i = 0; i < allocated; ++i) owner->deallocate(pointers[i], layout(i));
            return;
        }
        for (unsigned i = 0; i < count(); ++i) {
            auto index = (i * 73) % count();
            owner->deallocate(pointers[index], layout(index));
        }
    }
    void burst() {
        allocate_batch();
        auto small  = rstd::alloc::Layout::from_size_align(usize(64), usize(16)).unwrap();
        auto memory = owner->allocate(small);
        if (memory.is_err())
            failed = true;
        else {
            rstd::hint::black_box(memory->pointer);
            owner->deallocate(memory->pointer, small);
        }
    }
    template<bool Diagnose = false>
    void operation() {
        if (pattern == AllocationPattern::VecResize)
            resize_vector<Diagnose>();
        else if (pattern == AllocationPattern::Burst)
            burst();
        else
            allocate_batch();
    }
    auto check() -> Result<empty, String> {
        operation();
        return finish();
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        if (pattern == AllocationPattern::VecResize) {
            auto workload = bench::repeated(
                [this] {
                    resize_vector<false>();
                },
                work());
            return workload.run(engine, name);
        }
        if (pattern == AllocationPattern::Burst) {
            auto workload = bench::repeated(
                [this] {
                    burst();
                },
                work());
            return workload.run(engine, name);
        }
        auto workload = bench::repeated(
            [this] {
                allocate_batch();
            },
            work());
        return workload.run(engine, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        auto                        before_relocated = relocated_bytes;
        auto                        before_grows     = grows;
        auto                        before_shrinks   = shrinks;
        rstd::alloc::PoolStatistics before_pool;
        if constexpr (Policy == AllocationPolicy::Pool) before_pool = owner->allocator.statistics();
        for (u64 i; i < iterations; ++i) operation<true>();
        bench::DiagnosticMeasurement result;
        result.work = work();
        auto metric =
            [&](ref<str> name, usize value, ref<str> source, ref<str> scope = "session"_str) {
                result.metrics.push(bench::DiagnosticMetric {
                    String::make(name),
                    bench::DiagnosticUnit::Bytes,
                    bench::MetricDirection::Lower,
                    String::make(scope),
                    String::make(source),
                    bench::DiagnosticValue::Count(u64(value.to_primitive())) });
            };
        auto count_metric =
            [&](ref<str> name, usize value, ref<str> source, ref<str> scope = "session"_str) {
                result.metrics.push(bench::DiagnosticMetric {
                    String::make(name),
                    bench::DiagnosticUnit::Count,
                    bench::MetricDirection::Neutral,
                    String::make(scope),
                    String::make(source),
                    bench::DiagnosticValue::Count(u64(value.to_primitive())) });
            };
        if constexpr (Policy == AllocationPolicy::Recycling) {
            auto stats = owner->allocator.stats();
            metric("data_reserved"_str,
                   stats.reserved_bytes,
                   "complete arena data slabs; includes embedded slab headers"_str);
            metric("slab_metadata"_str,
                   stats.slab_metadata_bytes,
                   "subset of data_reserved; embedded slab headers and alignment prefixes"_str);
            metric("metadata_reserved"_str,
                   stats.metadata_reserved_bytes,
                   "separate metadata arena upstream requests; can be added to data_reserved"_str);
            metric("metadata_used"_str,
                   stats.metadata_used_bytes,
                   "used bytes in arena metadata slabs"_str);
            metric("recycled_bytes"_str,
                   stats.free_bytes,
                   "released requests retained for exact Layout reuse"_str);
            metric("live_requested"_str, stats.live_bytes, "requests not yet deallocated"_str);
            count_metric("layout_classes"_str,
                         stats.layout_classes,
                         "exact size and alignment classes retained by the arena"_str);
        } else if constexpr (Policy == AllocationPolicy::Pool) {
            auto stats = owner->allocator.statistics();
            metric("slab_reserved"_str,
                   stats.reserved_bytes,
                   "all pool slabs; includes embedded metadata"_str);
            metric(
                "empty_slab_retained"_str, stats.retained_bytes, "fully empty pool slabs only"_str);
            metric("embedded_metadata"_str,
                   stats.metadata_bytes,
                   "subset of slab_reserved; slab and slot headers"_str);
            metric("live_requested"_str, stats.live_bytes, "requests not yet deallocated"_str);
            count_metric("slabs"_str, stats.slabs, "pool slabs still owned after the workload"_str);
            metric("peak_live_requested"_str,
                   stats.peak_live_bytes,
                   "peak requested bytes over this session including precheck"_str);
            count_metric("pool_grows"_str,
                         stats.grows - before_pool.grows,
                         "Pool grow and grow_zeroed requests during diagnostic iterations"_str,
                         "operation"_str);
            count_metric("pool_shrinks"_str,
                         stats.shrinks - before_pool.shrinks,
                         "Pool shrink requests during diagnostic iterations"_str,
                         "operation"_str);
            count_metric("pool_in_place_resizes"_str,
                         stats.in_place_resizes - before_pool.in_place_resizes,
                         "successful Pool resizes retaining their original address"_str,
                         "operation"_str);
            metric(
                "pool_copied_bytes"_str,
                stats.copied_bytes - before_pool.copied_bytes,
                "payload bytes copied by successful moving Pool resizes; includes allocator layout capacity"_str,
                "operation"_str);
            if (pattern == AllocationPattern::Burst) {
                auto trimmed = owner->allocator.trim();
                metric("trim_released"_str,
                       trimmed.bytes,
                       "explicit trim after the burst and idle sample"_str);
                metric(
                    "after_trim_reserved"_str,
                    owner->allocator.statistics().reserved_bytes,
                    "pool slabs after explicit trim; existing live allocations are not invalidated"_str);
            }
        }
        if (pattern == AllocationPattern::VecResize) {
            metric(
                "relocated_live_bytes"_str,
                usize((relocated_bytes - before_relocated).to_primitive()),
                "live Vec bytes requiring preservation when its address changes; not backend memcpy volume"_str);
            result.metrics.push(
                bench::DiagnosticMetric { "grow_requests"_Str,
                                          bench::DiagnosticUnit::Count,
                                          bench::MetricDirection::Lower,
                                          "operation"_Str,
                                          "Vec reserve_exact calls"_Str,
                                          bench::DiagnosticValue::Count(grows - before_grows) });
            result.metrics.push(bench::DiagnosticMetric {
                "shrink_requests"_Str,
                bench::DiagnosticUnit::Count,
                bench::MetricDirection::Lower,
                "operation"_Str,
                "Vec shrink_to_fit calls"_Str,
                bench::DiagnosticValue::Count(shrinks - before_shrinks) });
        }
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> {
        if (failed) return Err("allocation failed"_Str);
        if constexpr (Policy == AllocationPolicy::Recycling) {
            if (owner->allocator.stats().live_bytes != usize())
                return Err("arena has live requests"_Str);
        } else if constexpr (Policy == AllocationPolicy::Pool) {
            if (owner->allocator.statistics().live_bytes != usize())
                return Err("pool has live requests"_Str);
        }
        return Ok(empty {});
    }
};

template<AllocationPolicy Policy>
struct AllocationPolicyFactory {
    AllocationPattern pattern;
    rstd::size_t      size, alignment;
    auto              prepare() {
        using Owner   = PolicyOwner<Policy, ::alloc::Global>;
        using Session = AllocationPolicySession<Policy>;
        return Result<Session, String>(
            Ok(Session { Box<Owner>::make(::alloc::Global {}), pattern, size, alignment }));
    }
    auto prepare(bench::DiagnosticContext& context) {
        using Upstream = rstd::alloc::TrackingAllocator<>;
        using Owner    = PolicyOwner<Policy, Upstream>;
        using Session  = AllocationPolicySession<Policy, Upstream>;
        return Result<Session, String>(
            Ok(Session { Box<Owner>::make(context.allocator()), pattern, size, alignment }));
    }
};

template<AllocationPolicy Policy>
auto add_policy_cases(bench::Suite& suite, ref<str> policy) -> Result<empty, String> {
    struct Case {
        ref<str>          name;
        AllocationPattern pattern;
        rstd::size_t      size, alignment;
    };
    const Case cases[] {
        { "single_64"_str, AllocationPattern::Single, 64, 16 },
        { "batch_64"_str, AllocationPattern::Batch, 64, 16 },
        { "mixed"_str, AllocationPattern::Mixed, 0, 16 },
        { "aligned_256"_str, AllocationPattern::Batch, 256, 256 },
        { "burst_then_idle"_str, AllocationPattern::Burst, 65536, 16 },
        { "vec_grow_shrink"_str, AllocationPattern::VecResize, 0, 1 },
    };
    for (auto item : cases) {
        auto name = rstd::format("{}_{}", policy, item.name);
        auto descriptor =
            make_descriptor("allocator_policy"_str,
                            name.as_str(),
                            u64(8),
                            parameter("policy"_str, policy),
                            parameter("allocation_bytes"_str, u64(item.size)),
                            parameter("alignment"_str, u64(item.alignment)),
                            parameter("release_order"_str, "permutation_73"_str),
                            parameter("timing_upstream"_str, "Global; no tracking wrapper"_str));
        auto                            id = descriptor.id.clone();
        AllocationPolicyFactory<Policy> factory { item.pattern, item.size, item.alignment };
        rstd_try(suite.add(rstd::move(descriptor), factory));
        rstd_try(suite.add_diagnostic(id, factory));
    }
    return Ok(empty {});
}

auto rstd_bench::register_allocator_policy(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_policy_cases<AllocationPolicy::Global>(suite, "global"_str));
    rstd_try(add_policy_cases<AllocationPolicy::Recycling>(suite, "recycling"_str));
    return add_policy_cases<AllocationPolicy::Pool>(suite, "pool"_str);
}
