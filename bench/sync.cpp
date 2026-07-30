#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct PingPongFields {
    bool          m_main_turn { true };
    bool          m_stop {};
    std::uint64_t m_count { 0 };
};

struct PingPongState {
    sync::Mutex<PingPongFields> m_fields;
    sync::Condvar               m_cvar;

    PingPongState(): m_fields(PingPongFields {}), m_cvar() {}
};

auto mutex_lock_unlock(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto mutex      = sync::Mutex<std::uint64_t>(0);
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto guard = mutex.lock().unwrap_unchecked();
            ++*guard;
            ++calls;
            rstd::hint::black_box(*guard);
        },
        [&] {
            auto guard = mutex.lock().unwrap_unchecked();
            return *guard == calls;
        });
}

auto condvar_ping_pong(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto state      = sync::Arc<PingPongState>::make();
    auto worker     = state.clone();
    auto spawned    = thread::spawn([worker = rstd::move(worker)] {
        while (true) {
            auto guard = worker->m_fields.lock().unwrap_unchecked();
            worker->m_cvar.wait_while(guard, [](const PingPongFields& fields) {
                return fields.m_main_turn && ! fields.m_stop;
            });
            if (guard->m_stop) break;
            ++guard->m_count;
            guard->m_main_turn = true;
            worker->m_cvar.notify_one();
        }
        return true;
    });
    auto handle     = rstd::move(spawned).ok();
    bool valid      = handle.is_some();
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(2) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) return;
            auto guard         = state->m_fields.lock().unwrap_unchecked();
            guard->m_main_turn = false;
            state->m_cvar.notify_one();
            state->m_cvar.wait_while(guard, [](const PingPongFields& fields) {
                return ! fields.m_main_turn;
            });
            ++calls;
            rstd::hint::black_box(guard->m_count);
        },
        [&] {
            if (handle.is_none()) return false;
            {
                auto guard         = state->m_fields.lock().unwrap_unchecked();
                guard->m_stop      = true;
                guard->m_main_turn = false;
                state->m_cvar.notify_one();
            }
            auto joined = rstd::move(handle).unwrap_unchecked().join();
            if (joined.is_err() || ! rstd::move(joined).unwrap_unchecked()) return false;
            auto guard = state->m_fields.lock().unwrap_unchecked();
            return valid && guard->m_count == calls;
        });
}

auto blocking_task_group_recreate(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto completed  = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(4) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto group = thread::BlockingTaskGroup<int>::make(usize(4), usize(4)).unwrap();
            for (int index = 0; index < 4; ++index) {
                group
                    .submit([index] {
                        return index;
                    })
                    .unwrap();
            }
            auto outcomes = rstd::move(group).join();
            completed += outcomes.len().to_primitive();
        },
        [&] {
            return completed != 0;
        });
}

auto blocking_task_set_shared_pool(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto pool       = thread::ThreadPoolBuilder::make().worker_count(usize(4)).build().unwrap();
    auto completed  = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(4) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(4)).unwrap();
            for (int index = 0; index < 4; ++index) {
                tasks
                    .try_submit([index] {
                        return index;
                    })
                    .unwrap();
            }
            tasks.close();
            while (tasks.recv().is_some()) ++completed;
        },
        [&] {
            return completed != 0;
        });
}

const rstd_bench::BenchCase CASES[] = {
    { "sync", "mutex_lock_unlock", 5'000, &mutex_lock_unlock },
    { "sync", "condvar_ping_pong", 100, &condvar_ping_pong },
    { "sync", "blocking_task_group_recreate", 10, &blocking_task_group_recreate },
    { "sync", "blocking_task_set_shared_pool", 100, &blocking_task_set_shared_pool },
};

} // namespace

namespace rstd_bench
{

auto sync_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
