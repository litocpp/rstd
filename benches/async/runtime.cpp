module;

#include <rstd/macro.hpp>

#if RSTD_OS_LINUX
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

module rstd_benches;
import rstd.bench;
import rstd;
import :async.support;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

struct ReadyInt {
    using Output = int;

    auto poll(mut_ref<ReadyInt>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(1);
    }
};

async::coro<int> child_value() {
    co_await async::yield_now();
    co_return 1;
}

async::coro<int> indexed_child_value(int value) {
    co_await async::yield_now();
    co_return value;
}

async::coro<Result<int, async::JoinError>> join_local_child() {
    auto handle = async::spawn_local(child_value());
    auto result = co_await rstd::move(handle);
    co_return rstd::move(result);
}

async::coro<Result<int, async::JoinError>> join_spawned_child() {
    auto handle = async::spawn(child_value());
    auto result = co_await rstd::move(handle);
    co_return rstd::move(result);
}

async::coro<Result<int, async::JoinError>> join_many_spawned_children() {
    auto handles = Vec<async::JoinHandle<int>>::make();
    for (int i = 0; i < 32; ++i) {
        handles.push(async::spawn(indexed_child_value(i)));
    }

    auto results = co_await async::join_all(rstd::move(handles));
    int  sum     = 0;
    for (usize i; i < results.len(); ++i) {
        if (results[i].is_err()) co_return Err(rstd::move(results[i]).unwrap_err());
        sum += *results[i];
    }
    co_return Ok(sum);
}

async::coro<int> sleep_zero() {
    co_await async::sleep(time::Duration::from_millis(u64()));
    co_return 1;
}

auto current_thread_ready(bench::Bench& runner, ref<str> name) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = rstd::uint64_t {};
    auto calls      = rstd::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        runner,
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(ReadyInt {});
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

auto current_thread_spawn_local_join(bench::Bench& runner, ref<str> name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = rstd::uint64_t {};
    auto calls      = rstd::uint64_t {};
    auto valid      = true;
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        runner,
        rstd::move(run_config),
        [&] {
            if (! valid) return;
            auto result = runtime.block_on(join_local_child());
            if (result.is_err()) {
                valid = false;
                return;
            }
            sum += *result;
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&]() -> Result<empty, String> {
            if (! valid) return Err(String::make(rstd_bench::text("task join aborted")));
            if (! (sum == calls)) return Err(String::make(rstd_bench::text("task sum differs")));
            return Ok(empty {});
        });
}

auto thread_pool_spawn_join(bench::Bench& runner, ref<str> name) -> rstd_bench::CaseRunResult {
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(usize(2)).build();
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto sum        = rstd::uint64_t {};
    auto calls      = rstd::uint64_t {};
    auto valid      = true;
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        runner,
        rstd::move(run_config),
        [&] {
            if (! valid) return;
            auto result = runtime->block_on(join_spawned_child());
            if (result.is_err()) {
                valid = false;
                return;
            }
            sum += *result;
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&]() -> Result<empty, String> {
            if (! valid) return Err(String::make(rstd_bench::text("task join aborted")));
            if (! (sum == calls)) return Err(String::make(rstd_bench::text("task sum differs")));
            return Ok(empty {});
        });
}

auto thread_pool_join_many(bench::Bench& runner, ref<str> name) -> rstd_bench::CaseRunResult {
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(usize(4)).build();
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto sum        = rstd::uint64_t {};
    auto calls      = rstd::uint64_t {};
    auto valid      = true;
    auto run_config = bench::RunConfig { .items_per_iteration = u64(32) };
    return rstd_bench::measure_case(
        name,
        runner,
        rstd::move(run_config),
        [&] {
            if (! valid) return;
            auto result = runtime->block_on(join_many_spawned_children());
            if (result.is_err()) {
                valid = false;
                return;
            }
            sum += *result;
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&]() -> Result<empty, String> {
            if (! valid) return Err(String::make(rstd_bench::text("task join aborted")));
            if (! (sum == calls * 496))
                return Err(String::make(rstd_bench::text("task sum differs")));
            return Ok(empty {});
        });
}

auto timer_sleep_zero(bench::Bench& runner, ref<str> name) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = rstd::uint64_t {};
    auto calls      = rstd::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        runner,
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(sleep_zero());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

auto rstd_bench::register_async_runtime(rstd::bench::Suite& suite) -> Result<empty, String> {
    using namespace rstd_bench;
    using namespace rstd::literals;

    rstd_try(
        suite.add_function(make_descriptor("async"_str, "current_thread_ready"_str, u64(1'000)),
                           &current_thread_ready));
    rstd_try(suite.add_function(
        make_descriptor("async"_str, "current_thread_spawn_local_join"_str, u64(500)),
        &current_thread_spawn_local_join));
    rstd_try(
        suite.add_function(make_descriptor("async"_str, "thread_pool_spawn_join_2"_str, u64(200)),
                           &thread_pool_spawn_join));
    rstd_try(
        suite.add_function(make_descriptor("async"_str, "thread_pool_join_many_4x32"_str, u64(20)),
                           &thread_pool_join_many));
    rstd_try(suite.add_function(make_descriptor("async"_str, "timer_sleep_zero"_str, u64(500)),
                                &timer_sleep_zero));
    return Ok(empty {});
}
