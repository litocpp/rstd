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

auto mutex_lock_unlock(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto mutex      = sync::Mutex<std::uint64_t>(0);
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        "mutex_lock_unlock",
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

auto condvar_ping_pong(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
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
        "condvar_ping_pong",
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

const rstd_bench::BenchCase CASES[] = {
    { "sync", "mutex_lock_unlock", 5'000, &mutex_lock_unlock },
    { "sync", "condvar_ping_pong", 100, &condvar_ping_pong },
};

} // namespace

namespace rstd_bench
{

auto sync_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
