module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

struct ParallelAllocationFields {
    rstd::uint64_t epoch {};
    unsigned       pending {};
    bool           freeing {}, stop {}, failed {};
};

template<typename Allocator>
struct ParallelAllocationState {
    static_assert(
        rstd::Impled<Allocator, rstd::alloc::Allocator, rstd::alloc::ThreadSafeAllocator>);
    static constexpr unsigned                   batch = 128;
    Allocator                                   allocator;
    unsigned                                    worker_count;
    bool                                        remote;
    rstd::sync::Mutex<ParallelAllocationFields> fields { ParallelAllocationFields {} };
    rstd::sync::Condvar                         changed;
    void*                                       pointers[4][batch] {};
    Option<rstd::thread::ThreadId>              identities[4];

    ParallelAllocationState(Allocator allocator, unsigned workers, bool remote)
        : allocator(rstd::move(allocator)), worker_count(workers), remote(remote) {}

    static auto layout() -> rstd::alloc::Layout {
        return rstd::alloc::Layout::from_size_align(usize(64), usize(16)).unwrap();
    }
    auto worker(unsigned index) -> bool {
        identities[index] = Some(rstd::thread::current().id());
        rstd::uint64_t completed {};
        while (true) {
            {
                auto guard = fields.lock().unwrap();
                changed.wait_while(guard, [&](const ParallelAllocationFields& value) {
                    return value.epoch == completed && ! value.stop;
                });
                if (guard->stop) return true;
                completed = guard->epoch;
            }
            bool failed = false;
            for (unsigned i = 0; i < batch; ++i) {
                auto memory = rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout());
                if (memory.is_err()) {
                    failed = true;
                    break;
                }
                pointers[index][i] = memory->pointer;
                rstd::hint::black_box(memory->pointer);
            }
            {
                auto guard = fields.lock().unwrap();
                guard->failed |= failed;
                --guard->pending;
                changed.notify_all();
                changed.wait_while(guard, [](const ParallelAllocationFields& value) {
                    return ! value.freeing;
                });
            }
            auto source = remote ? (index + 1) % worker_count : index;
            for (unsigned i = 0; i < batch; ++i) {
                if (! pointers[source][i]) continue;
                rstd::as<rstd::alloc::Allocator>(allocator).deallocate(pointers[source][i],
                                                                       layout());
                pointers[source][i] = nullptr;
            }
            {
                auto guard = fields.lock().unwrap();
                --guard->pending;
                changed.notify_all();
            }
        }
    }
    void round() {
        auto guard = fields.lock().unwrap();
        if (guard->failed) return;
        guard->freeing = false;
        guard->pending = worker_count;
        ++guard->epoch;
        changed.notify_all();
        changed.wait_while(guard, [](const ParallelAllocationFields& value) {
            return value.pending != 0;
        });
        guard->freeing = true;
        guard->pending = worker_count;
        changed.notify_all();
        changed.wait_while(guard, [](const ParallelAllocationFields& value) {
            return value.pending != 0;
        });
    }
    auto validate() -> Result<empty, String> {
        auto guard = fields.lock().unwrap();
        if (guard->failed) return Err("worker allocation failed"_Str);
        if (guard->pending != 0) return Err("workers did not complete the round"_Str);
        for (unsigned i = 0; i < worker_count; ++i) {
            if (identities[i].is_none()) return Err("worker did not start"_Str);
            for (unsigned j = 0; j < i; ++j)
                if (*identities[i] == *identities[j])
                    return Err("workers share a thread identity"_Str);
            for (auto pointer : pointers[i])
                if (pointer) return Err("worker retained a live allocation"_Str);
        }
        if constexpr (requires { allocator.statistics().live_bytes; }) {
            if (allocator.statistics().live_bytes != usize())
                return Err("shared pool has live requests"_Str);
        }
        return Ok(empty {});
    }
};

template<typename Allocator>
struct ParallelAllocationSession {
    using State  = ParallelAllocationState<Allocator>;
    using Shared = rstd::sync::Arc<State>;
    Option<Shared>                                 state;
    Vec<rstd::thread::JoinHandle<bool>>            workers;
    Option<rstd::alloc::SharedTrackingAllocator<>> tracker;
    unsigned                                       worker_count;
    bool                                           remote;

    ParallelAllocationSession(Shared                                         state,
                              unsigned                                       count,
                              bool                                           remote,
                              Option<rstd::alloc::SharedTrackingAllocator<>> tracker)
        : state(Some(rstd::move(state))),
          tracker(rstd::move(tracker)),
          worker_count(count),
          remote(remote) {}
    ParallelAllocationSession(ParallelAllocationSession&& other) noexcept
        : state(other.state.take()),
          workers(rstd::move(other.workers)),
          tracker(other.tracker.take()),
          worker_count(other.worker_count),
          remote(other.remote) {}
    ~ParallelAllocationSession() { (void)finish(); }

    auto check() -> Result<empty, String> {
        (*state)->round();
        return (*state)->validate();
    }
    auto work() const -> bench::RunConfig {
        return { .items_per_iteration = u64(worker_count * State::batch) };
    }
    auto run(bench::Bench& engine, ref<str> name)
        -> Result<bench::BenchmarkResult, bench::BenchError> {
        auto workload = bench::repeated(
            [this] {
                (*state)->round();
            },
            work());
        return workload.run(engine, name);
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        auto before = tracker->snapshot();
        for (u64 i; i < iterations; ++i) (*state)->round();
        auto validated = (*state)->validate();
        if (validated.is_err()) return Err(rstd::move(validated).unwrap_err());
        auto                         after = tracker->snapshot();
        bench::DiagnosticMeasurement result;
        result.work = work();
        result.allocation_unavailable =
            "thread-safe upstream metrics below; thread, dispatch and control storage excluded"_Str;
        auto metric = [&](ref<str>              name,
                          u64                   value,
                          bench::DiagnosticUnit unit,
                          ref<str>              scope,
                          ref<str>              source) {
            result.metrics.push(bench::DiagnosticMetric { String::make(name),
                                                          unit,
                                                          bench::MetricDirection::Neutral,
                                                          String::make(scope),
                                                          String::make(source),
                                                          bench::DiagnosticValue::Count(value) });
        };
        metric("workers"_str,
               u64(worker_count),
               bench::DiagnosticUnit::Count,
               "session"_str,
               "persistent threads started before timing or diagnostics"_str);
        metric(
            "remote_releases"_str,
            remote ? iterations * u64(worker_count * State::batch) : u64(),
            bench::DiagnosticUnit::Count,
            "operation"_str,
            "a different persistent worker frees each allocation after the allocation barrier"_str);
        metric("upstream_allocations"_str,
               after.allocations - before.allocations,
               bench::DiagnosticUnit::Count,
               "operation"_str,
               "SharedTracking snapshot delta; adds a diagnostic-only mutex"_str);
        metric("upstream_deallocations"_str,
               after.deallocations - before.deallocations,
               bench::DiagnosticUnit::Count,
               "operation"_str,
               "SharedTracking snapshot delta before owner cleanup"_str);
        metric("upstream_requested"_str,
               after.requested_bytes - before.requested_bytes,
               bench::DiagnosticUnit::Bytes,
               "operation"_str,
               "upstream layout requests during the measured rounds"_str);
        metric("upstream_live_before_cleanup"_str,
               after.live_bytes,
               bench::DiagnosticUnit::Bytes,
               "session"_str,
               "logical upstream bytes; includes any retained slabs"_str);
        metric("upstream_peak_lifetime"_str,
               after.peak_live_bytes,
               bench::DiagnosticUnit::Bytes,
               "session"_str,
               "upstream requested-byte peak including precheck; not RSS"_str);
        if constexpr (requires { (*state)->allocator.statistics(); }) {
            auto stats = (*state)->allocator.statistics();
            metric("slab_reserved"_str,
                   u64(stats.reserved_bytes.to_primitive()),
                   bench::DiagnosticUnit::Bytes,
                   "session"_str,
                   "shared pool slabs including embedded metadata"_str);
            metric("empty_slab_retained"_str,
                   u64(stats.retained_bytes.to_primitive()),
                   bench::DiagnosticUnit::Bytes,
                   "session"_str,
                   "fully empty slabs before the shared owner is destroyed"_str);
            metric("live_requested"_str,
                   u64(stats.live_bytes.to_primitive()),
                   bench::DiagnosticUnit::Bytes,
                   "session"_str,
                   "shared pool requests not yet freed"_str);
        }
        auto cleaned = finish();
        if (cleaned.is_err()) return Err(rstd::move(cleaned).unwrap_err());
        auto final = tracker->snapshot();
        metric("cleanup_upstream_live"_str,
               final.live_bytes,
               bench::DiagnosticUnit::Bytes,
               "session"_str,
               "after workers join and the last allocator owner is destroyed"_str);
        if (final.live_bytes != u64() || final.overflow)
            return Err("parallel allocator cleanup accounting failed"_Str);
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> {
        if (state.is_none()) return Ok(empty {});
        auto owner = rstd::move(state.take()).unwrap();
        {
            auto guard  = owner->fields.lock().unwrap();
            guard->stop = true;
            owner->changed.notify_all();
        }
        bool valid = true;
        while (! workers.is_empty()) {
            auto handle = workers.pop().unwrap();
            auto joined = rstd::move(handle).join();
            if (joined.is_err() || ! *joined) valid = false;
        }
        auto checked = owner->fields.lock().unwrap();
        if (checked->failed || checked->pending != 0) valid = false;
        if (! valid) return Err("parallel allocator workers failed"_Str);
        return Ok(empty {});
    }
};

template<bool Pooled>
struct ParallelAllocationFactory {
    unsigned workers;
    bool     remote;

    template<typename Allocator>
    auto prepare_with(Allocator allocator, Option<rstd::alloc::SharedTrackingAllocator<>> tracker) {
        using Session = ParallelAllocationSession<Allocator>;
        auto created  = Session::Shared::try_make(rstd::move(allocator), workers, remote);
        if (created.is_err())
            return Result<Session, String>(Err("parallel allocator state allocation failed"_Str));
        Session session(rstd::move(created).unwrap(), workers, remote, rstd::move(tracker));
        if (session.workers.try_reserve(usize(workers)).is_err())
            return Result<Session, String>(Err("worker handle allocation failed"_Str));
        for (unsigned index = 0; index < workers; ++index) {
            auto state   = (*session.state).clone();
            auto spawned = rstd::thread::spawn([state = rstd::move(state), index] {
                return state->worker(index);
            });
            if (spawned.is_err())
                return Result<Session, String>(
                    Err(rstd::format("worker spawn failed: {}", spawned.unwrap_err())));
            session.workers.push(rstd::move(spawned).unwrap());
        }
        return Result<Session, String>(Ok(rstd::move(session)));
    }
    auto prepare() {
        if constexpr (Pooled) {
            auto allocator = rstd::alloc::SharedPoolAllocator<>::try_make().unwrap();
            return prepare_with(rstd::move(allocator), None());
        } else
            return prepare_with(::alloc::Global {}, None());
    }
    auto prepare(bench::DiagnosticContext&) {
        auto tracker = rstd::alloc::SharedTrackingAllocator<>::make();
        if constexpr (Pooled) {
            auto allocator =
                rstd::alloc::SharedPoolAllocator<rstd::alloc::SharedTrackingAllocator<>>::try_make(
                    {}, tracker.clone())
                    .unwrap();
            return prepare_with(rstd::move(allocator), Some(rstd::move(tracker)));
        } else {
            auto allocator = tracker.clone();
            return prepare_with(rstd::move(allocator), Some(rstd::move(tracker)));
        }
    }
};

template<bool Pooled>
auto add_parallel_allocation_cases(bench::Suite& suite, ref<str> policy) -> Result<empty, String> {
    const unsigned workers[] { 2, 4 };
    const bool     modes[] { false, true };
    for (auto count : workers) {
        for (auto remote : modes) {
            auto name = rstd::format(
                "{}_{}_{}", policy, count, remote ? "remote_free"_str : "local_free"_str);
            auto descriptor = make_descriptor(
                "allocator_parallel"_str,
                name.as_str(),
                u64(4),
                parameter("policy"_str, policy),
                parameter("workers"_str, u64(count)),
                parameter("allocations_per_worker"_str, u64(128)),
                parameter("allocation_bytes"_str, u64(64)),
                parameter("release_thread"_str, remote ? "next_worker"_str : "same_worker"_str),
                parameter(
                    "scope"_str,
                    "dispatch, allocate, barrier, free and completion; excludes spawn/join"_str),
                parameter(
                    "synchronization"_str,
                    "two phases using the same Mutex and Condvar; SharedPool adds one allocator mutex"_str),
                parameter(
                    "accounting"_str,
                    "diagnostic-only SharedTracking upstream; thread, dispatch and control storage excluded"_str));
            auto                              id = descriptor.id.clone();
            ParallelAllocationFactory<Pooled> factory { count, remote };
            rstd_try(suite.add(rstd::move(descriptor), factory));
            rstd_try(suite.add_diagnostic(id, factory));
        }
    }
    return Ok(empty {});
}

auto rstd_bench::register_allocator_parallel(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_parallel_allocation_cases<false>(suite, "global"_str));
    return add_parallel_allocation_cases<true>(suite, "shared_pool"_str);
}
