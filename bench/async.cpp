#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace
{

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

async::coro<int> join_local_child() {
    auto handle = async::spawn_local(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap_unchecked();
}

async::coro<int> join_spawned_child() {
    auto handle = async::spawn(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap_unchecked();
}

async::coro<int> join_many_spawned_children() {
    auto handles = Vec<async::JoinHandle<int>>::make();
    for (int i = 0; i < 32; ++i) {
        handles.push(async::spawn(indexed_child_value(i)));
    }

    auto results = co_await async::join_all(rstd::move(handles));
    int  sum     = 0;
    for (usize i; i < results.len(); ++i) {
        sum += results[i].unwrap_unchecked();
    }
    co_return sum;
}

async::coro<int> sleep_zero() {
    co_await async::sleep(time::Duration::from_millis(u64()));
    co_return 1;
}

auto current_thread_ready(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        "current_thread_ready",
        rstd::move(config),
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

auto current_thread_spawn_local_join(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        "current_thread_spawn_local_join",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(join_local_child());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

auto thread_pool_spawn_join(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto runtime    = async::RuntimeBuilder::multi_thread().worker_threads(usize(2)).build().ok();
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = runtime.is_some();
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        "thread_pool_spawn_join_2",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (runtime.is_none()) return;
            sum += runtime->block_on(join_spawned_child());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return valid && sum == calls;
        });
}

auto thread_pool_join_many(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto runtime    = async::RuntimeBuilder::multi_thread().worker_threads(usize(4)).build().ok();
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = runtime.is_some();
    auto run_config = bench::RunConfig { .items_per_iteration = u64(32) };
    return rstd_bench::measure_case(
        "thread_pool_join_many_4x32",
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (runtime.is_none()) return;
            sum += runtime->block_on(join_many_spawned_children());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return valid && sum == calls * 496;
        });
}

auto timer_sleep_zero(bench::BenchConfig config) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        "timer_sleep_zero",
        rstd::move(config),
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

const rstd_bench::BenchCase CASES[] = {
    { "async", "current_thread_ready", 1'000, &current_thread_ready },
    { "async", "current_thread_spawn_local_join", 500, &current_thread_spawn_local_join },
    { "async", "thread_pool_spawn_join_2", 200, &thread_pool_spawn_join },
    { "async", "thread_pool_join_many_4x32", 20, &thread_pool_join_many },
    { "async", "timer_sleep_zero", 500, &timer_sleep_zero },
};

} // namespace

namespace rstd_bench
{

auto async_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
