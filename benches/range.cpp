module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

struct TraceSpan {
    ::alloc::RangeSize offset {}, size {};
};

// Deliberately simple reference model, not a production allocation strategy.
struct TraceFirstFit {
    TraceSpan          free[1025] {};
    unsigned           count { 1 };
    ::alloc::RangeSize searches {};
    explicit TraceFirstFit(::alloc::RangeSize capacity) { free[0] = { 0, capacity }; }
    auto allocate(::alloc::RangeSize size, ::alloc::RangeSize alignment) -> Option<TraceSpan> {
        for (unsigned i = 0; i < count; ++i) {
            ++searches;
            auto old = free[i];
            auto offset =
                ::alloc::AlignedRangeOffset(old.offset, size, alignment, old.offset + old.size);
            if (offset.is_none()) continue;
            for (unsigned j = i; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
            if (*offset > old.offset) deallocate({ old.offset, *offset - old.offset });
            if (*offset + size < old.offset + old.size)
                deallocate({ *offset + size, old.offset + old.size - *offset - size });
            return Some(TraceSpan { *offset, size });
        }
        return None();
    }
    void deallocate(TraceSpan span) {
        unsigned i = 0;
        while (i < count && free[i].offset < span.offset) ++i;
        for (unsigned j = count; j > i; --j) free[j] = free[j - 1];
        free[i] = span;
        ++count;
        if (i && free[i - 1].offset + free[i - 1].size == free[i].offset) {
            free[i - 1].size += free[i].size;
            for (unsigned j = i; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
            --i;
        }
        if (i + 1 < count && free[i].offset + free[i].size == free[i + 1].offset) {
            free[i].size += free[i + 1].size;
            for (unsigned j = i + 1; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
        }
    }
};

struct RangeTraceEvent {
    unsigned slot, size, alignment;
};
struct RangeTraceMetrics {
    ::alloc::RangeSize failures {}, searches {}, metadata {}, free {}, largest {};
    bool               invalid {};
};

template<bool Segregated, typename Metadata = ::alloc::Global>
struct RangeTraceSession {
    ::alloc::RangeSize   capacity;
    Metadata             metadata;
    Vec<RangeTraceEvent> events;
    RangeTraceMetrics    latest;

    RangeTraceSession(::alloc::RangeSize capacity, Metadata metadata)
        : capacity(capacity), metadata(rstd::move(metadata)) {
        rstd::uint32_t random = 0x7a9135;
        for (unsigned i = 0; i < 20000; ++i) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            events.push(RangeTraceEvent { unsigned(random % 128),
                                          unsigned(16 + (random >> 7) % 4081),
                                          1U << ((random >> 23) % 9) });
        }
    }
    auto operation() -> RangeTraceMetrics {
        RangeTraceMetrics result;
        bool              occupied[128] {};
        if constexpr (Segregated) {
            ::alloc::RangeAllocator<Metadata> ranges(capacity,
                                                     rstd::alloc::clone_allocator(metadata));
            ::alloc::RangeAllocation          live[128] {};
            for (auto event : events) {
                if (occupied[event.slot] && ranges.deallocate(live[event.slot].id).is_err())
                    result.invalid = true;
                auto allocated       = ranges.allocate(event.size, event.alignment);
                occupied[event.slot] = allocated.is_ok();
                if (allocated.is_ok())
                    live[event.slot] = *allocated;
                else {
                    ++result.failures;
                    if (allocated.unwrap_err() != ::alloc::RangeError::Exhausted)
                        result.invalid = true;
                }
            }
            auto stats      = ranges.statistics();
            result.searches = stats.search_steps;
            result.metadata = stats.metadata_bytes;
            result.free     = stats.free_bytes;
            result.largest  = stats.largest_free_range;
            for (unsigned i = 0; i < 128; ++i)
                if (occupied[i] && ranges.deallocate(live[i].id).is_err()) result.invalid = true;
            if (ranges.statistics().largest_free_range != capacity) result.invalid = true;
        } else {
            TraceFirstFit ranges(capacity);
            TraceSpan     live[128] {};
            for (auto event : events) {
                if (occupied[event.slot]) ranges.deallocate(live[event.slot]);
                auto allocated       = ranges.allocate(event.size, event.alignment);
                occupied[event.slot] = allocated.is_some();
                if (allocated.is_some())
                    live[event.slot] = *allocated;
                else
                    ++result.failures;
            }
            result.searches = ranges.searches;
            result.metadata = sizeof(ranges);
            for (unsigned i = 0; i < ranges.count; ++i) {
                result.free += ranges.free[i].size;
                if (ranges.free[i].size > result.largest) result.largest = ranges.free[i].size;
            }
            for (unsigned i = 0; i < 128; ++i)
                if (occupied[i]) ranges.deallocate(live[i]);
            if (ranges.count != 1 || ranges.free[0].size != capacity) result.invalid = true;
        }
        return result;
    }
    auto check() -> Result<empty, String> {
        latest = operation();
        if (capacity == 1048576 && latest.failures != 0)
            return Err("unexpected range exhaustion"_Str);
        return finish();
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                return operation();
            },
            { .items_per_iteration = u64(20000) });
        return workload.run(engine, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        for (u64 i; i < iterations; ++i) latest = operation();
        bench::DiagnosticMeasurement result;
        result.work = { .items_per_iteration = u64(20000) };
        auto metric = [&](ref<str> name, ::alloc::RangeSize value, bench::DiagnosticUnit unit) {
            result.metrics.push(bench::DiagnosticMetric {
                String::make(name),
                unit,
                bench::MetricDirection::Neutral,
                "trace"_Str,
                "one deterministic 20000-event trace before final cleanup"_Str,
                bench::DiagnosticValue::Count(u64(value)) });
        };
        metric("failures"_str, latest.failures, bench::DiagnosticUnit::Count);
        metric("search_steps"_str, latest.searches, bench::DiagnosticUnit::Count);
        metric("metadata_reserved"_str, latest.metadata, bench::DiagnosticUnit::Bytes);
        metric("final_free"_str, latest.free, bench::DiagnosticUnit::Bytes);
        metric("largest_gap"_str, latest.largest, bench::DiagnosticUnit::Bytes);
        result.metrics.push(bench::DiagnosticMetric {
            "failure_rate"_Str,
            bench::DiagnosticUnit::Ratio,
            bench::MetricDirection::Lower,
            "trace"_Str,
            "failed allocations divided by the 20000 deterministic requests"_Str,
            bench::DiagnosticValue::Ratio(u64(latest.failures), u64(20000)) });
        result.metrics.push(bench::DiagnosticMetric {
            "external_fragmentation"_Str,
            bench::DiagnosticUnit::Ratio,
            bench::MetricDirection::Lower,
            "trace"_Str,
            "free bytes outside the largest gap divided by total free bytes before cleanup"_Str,
            latest.free
                ? bench::DiagnosticValue::Ratio(u64(latest.free - latest.largest), u64(latest.free))
                : bench::DiagnosticValue::Unavailable("no free bytes remain"_Str) });
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> {
        if (latest.invalid) return Err("range trace failed to restore full capacity"_Str);
        return Ok(empty {});
    }
};

template<bool Segregated>
struct RangeTraceFactory {
    ::alloc::RangeSize capacity;
    auto               prepare() {
        using Session = RangeTraceSession<Segregated>;
        return Result<Session, String>(Ok(Session(capacity, {})));
    }
    auto prepare(bench::DiagnosticContext& context) {
        using Session = RangeTraceSession<Segregated, rstd::alloc::TrackingAllocator<>>;
        return Result<Session, String>(Ok(Session(capacity, context.allocator())));
    }
};

auto rstd_bench::register_range(bench::Suite& suite) -> Result<empty, String> {
    const ::alloc::RangeSize capacities[] { 65536, 1048576 };
    const bool               modes[] { false, true };
    for (auto capacity : capacities) {
        for (bool segregated : modes) {
            auto name = rstd::format(
                "{}_{}", segregated ? "segregated"_str : "first_fit_model"_str, capacity);
            auto descriptor = make_descriptor(
                "range"_str,
                name.as_str(),
                u64(1),
                parameter("capacity"_str, u64(capacity)),
                parameter("events"_str, u64(20000)),
                parameter("slots"_str, u64(128)),
                parameter("seed"_str, u64(0x7a9135)),
                parameter("scope"_str,
                          "complete trace including metadata creation and cleanup"_str));
            auto id = descriptor.id.clone();
            if (segregated) {
                rstd_try(suite.add(rstd::move(descriptor), RangeTraceFactory<true> { capacity }));
                rstd_try(suite.add_diagnostic(id, RangeTraceFactory<true> { capacity }));
            } else {
                rstd_try(suite.add(rstd::move(descriptor), RangeTraceFactory<false> { capacity }));
                rstd_try(suite.add_diagnostic(id, RangeTraceFactory<false> { capacity }));
            }
        }
    }
    return Ok(empty {});
}
