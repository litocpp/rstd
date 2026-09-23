module rstd_benches;
import rstd.bench;
import rstd;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct PingPongFields {
    bool           m_main_turn { true };
    bool           m_stop {};
    rstd::uint64_t m_count { 0 };
};

struct PingPongState {
    sync::Mutex<PingPongFields> m_fields;
    sync::Condvar               m_cvar;

    PingPongState(): m_fields(PingPongFields {}), m_cvar() {}
};

struct PingPongWorker {
    sync::Arc<PingPongState>         state;
    Option<thread::JoinHandle<bool>> handle;

    auto finish() -> Result<empty, String> {
        if (handle.is_none()) return Ok(empty {});
        {
            auto guard         = state->m_fields.lock().unwrap();
            guard->m_stop      = true;
            guard->m_main_turn = false;
            state->m_cvar.notify_one();
        }
        auto joined = rstd::move(handle.take()).unwrap().join();
        if (joined.is_err()) return Err("ping-pong worker join failed"_Str);
        if (! *joined) return Err("ping-pong worker failed"_Str);
        return Ok(empty {});
    }

    ~PingPongWorker() { (void)finish(); }
};

auto mutex_lock_unlock(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto mutex      = sync::Mutex<rstd::uint64_t>(0);
    auto calls      = rstd::uint64_t {};
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
    auto state   = sync::Arc<PingPongState>::make();
    auto worker  = state.clone();
    auto spawned = thread::spawn([worker = rstd::move(worker)] {
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
    if (spawned.is_err())
        return rstd_bench::failed(rstd::format("spawn failed: {}", spawned.unwrap_err()));
    auto worker_owner = PingPongWorker { state.clone(), Some(rstd::move(spawned).unwrap()) };
    auto calls        = rstd::uint64_t {};
    auto run_config   = bench::RunConfig { .items_per_iteration = u64(2) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
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
            auto guard = state->m_fields.lock().unwrap_unchecked();
            return guard->m_count == calls;
        },
        [&] {
            return worker_owner.finish();
        });
}

auto task_validation(Option<String>& error, bool valid) -> Result<empty, String> {
    if (error.is_some()) return Err(rstd::move(*error));
    if (! valid) return Err("task completion validation failed"_Str);
    return Ok(empty {});
}

auto blocking_task_group_recreate(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto completed = usize();
    auto calls     = usize();
    auto error     = Option<String> {};
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        { .items_per_iteration = u64(4) },
        [&] {
            if (error.is_some()) return;
            auto created = thread::BlockingTaskGroup<int>::make(usize(4), usize(4));
            if (created.is_err()) {
                error = Some(rstd::format("task group: {}", created.unwrap_err()));
                return;
            }
            auto group = rstd::move(created).unwrap();
            for (int index = 0; index < 4; ++index) {
                if (group
                        .submit([index] {
                            return index;
                        })
                        .is_err()) {
                    error = Some("task submission failed"_Str);
                    return;
                }
            }
            auto     outcomes = rstd::move(group).join();
            unsigned values   = 0;
            for (const auto& outcome : outcomes) {
                if (! outcome.is_completed() || *outcome.value() < 0 || *outcome.value() >= 4) {
                    error = Some("task group incomplete"_Str);
                    return;
                }
                values |= 1U << *outcome.value();
            }
            if (values != 15 || outcomes.len() != usize(4)) {
                error = Some("task group values differ"_Str);
                return;
            }
            completed += outcomes.len();
            ++calls;
        },
        [&] {
            return task_validation(error, calls != usize() && completed == calls * usize(4));
        });
}

auto blocking_task_set_shared_pool(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto built = thread::ThreadPoolBuilder::make().worker_count(usize(4)).build();
    if (built.is_err())
        return rstd_bench::failed(rstd::format("thread pool: {}", built.unwrap_err()));
    auto pool      = rstd::move(built).unwrap();
    auto completed = usize();
    auto calls     = usize();
    auto error     = Option<String> {};
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        { .items_per_iteration = u64(4) },
        [&] {
            if (error.is_some()) return;
            auto created = thread::BlockingTaskSet<int>::make(pool.handle(), usize(4));
            if (created.is_err()) {
                error = Some(rstd::format("task set: {}", created.unwrap_err()));
                return;
            }
            auto tasks = rstd::move(created).unwrap();
            for (int index = 0; index < 4; ++index) {
                if (tasks
                        .try_submit([index] {
                            return index;
                        })
                        .is_err()) {
                    error = Some("task set submission failed"_Str);
                    return;
                }
            }
            tasks.close();
            unsigned values = 0;
            auto     count  = usize();
            for (auto result = tasks.recv(); result.is_some(); result = tasks.recv()) {
                if (! result->is_completed() || *result->value() < 0 || *result->value() >= 4) {
                    error = Some("task set incomplete"_Str);
                    return;
                }
                values |= 1U << *result->value();
                ++count;
            }
            if (values != 15 || count != usize(4)) {
                error = Some("task set values differ"_Str);
                return;
            }
            completed += count;
            ++calls;
        },
        [&] {
            return task_validation(error, calls != usize() && completed == calls * usize(4));
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
