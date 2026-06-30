#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct PingPongFields {
    bool          m_main_turn { true };
    std::uint64_t m_count { 0 };
};

struct PingPongState {
    sync::Mutex<PingPongFields> m_fields;
    sync::Condvar               m_cvar;

    PingPongState(): m_fields(PingPongFields {}), m_cvar() {}
};

auto mutex_lock_unlock(rstd_bench::BenchContext& context) -> bool {
    auto mutex = sync::Mutex<std::uint64_t>(0);

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        auto guard = mutex.lock().unwrap_unchecked();
        ++*guard;
        rstd::hint::black_box(*guard);
    }

    auto guard = mutex.lock().unwrap_unchecked();
    context.set_items_processed(context.iterations());
    return *guard == context.iterations();
}

auto condvar_ping_pong(rstd_bench::BenchContext& context) -> bool {
    auto state      = sync::Arc<PingPongState>::make();
    auto iterations = context.iterations();
    auto worker     = state.clone();
    auto spawned    = thread::spawn([worker = rstd::move(worker), iterations] {
        for (std::uint64_t i = 0; i < iterations; ++i) {
            auto guard = worker->m_fields.lock().unwrap_unchecked();
            worker->m_cvar.wait_while(guard, [](const PingPongFields& fields) {
                return fields.m_main_turn;
            });
            ++guard->m_count;
            guard->m_main_turn = true;
            worker->m_cvar.notify_one();
        }
        return true;
    });
    if (spawned.is_err()) {
        return false;
    }

    auto handle = rstd::move(spawned).unwrap_unchecked();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        auto guard = state->m_fields.lock().unwrap_unchecked();
        state->m_cvar.wait_while(guard, [](const PingPongFields& fields) {
            return !fields.m_main_turn;
        });
        guard->m_main_turn = false;
        state->m_cvar.notify_one();
        state->m_cvar.wait_while(guard, [](const PingPongFields& fields) {
            return !fields.m_main_turn;
        });
        rstd::hint::black_box(guard->m_count);
    }

    auto joined = rstd::move(handle).join();
    if (joined.is_err() || !rstd::move(joined).unwrap_unchecked()) {
        return false;
    }

    auto guard = state->m_fields.lock().unwrap_unchecked();
    context.set_items_processed(iterations * 2);
    return guard->m_count == iterations && guard->m_main_turn;
}

const rstd_bench::BenchCase CASES[] = {
    { "sync", "mutex_lock_unlock", 500'000, 5'000, &mutex_lock_unlock },
    { "sync", "condvar_ping_pong", 10'000, 100, &condvar_ping_pong },
};

} // namespace

namespace rstd_bench
{

auto sync_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
