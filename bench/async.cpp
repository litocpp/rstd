#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace
{

struct ReadyInt {
    using Output = int;

    auto poll(pin::Pin<mut_ref<ReadyInt>>, task::Context&) -> task::Poll<int> {
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
    for (usize i = 0; i < results.len(); ++i) {
        sum += results[i].unwrap_unchecked();
    }
    co_return sum;
}

async::coro<int> sleep_zero() {
    co_await async::sleep(time::Duration::from_millis(0));
    co_return 1;
}

auto current_thread_ready(rstd_bench::BenchContext& context) -> bool {
    auto runtime = async::Runtime {};
    auto sum     = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        sum += runtime.block_on(ReadyInt {});
        rstd::hint::black_box(sum);
    }

    context.set_items_processed(context.iterations());
    return sum == context.iterations();
}

auto current_thread_spawn_local_join(rstd_bench::BenchContext& context) -> bool {
    auto runtime = async::Runtime {};
    auto sum     = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        sum += runtime.block_on(join_local_child());
        rstd::hint::black_box(sum);
    }

    context.set_items_processed(context.iterations());
    return sum == context.iterations();
}

auto thread_pool_spawn_join(rstd_bench::BenchContext& context) -> bool {
    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(2).build();
    if (runtime_result.is_err()) {
        return false;
    }

    auto runtime = rstd::move(runtime_result).unwrap_unchecked();
    auto sum     = std::uint64_t {};
    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        sum += runtime.block_on(join_spawned_child());
        rstd::hint::black_box(sum);
    }

    context.set_items_processed(context.iterations());
    return sum == context.iterations();
}

auto thread_pool_join_many(rstd_bench::BenchContext& context) -> bool {
    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(4).build();
    if (runtime_result.is_err()) {
        return false;
    }

    auto runtime = rstd::move(runtime_result).unwrap_unchecked();
    auto sum     = std::uint64_t {};
    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        sum += runtime.block_on(join_many_spawned_children());
        rstd::hint::black_box(sum);
    }

    context.set_items_processed(context.iterations() * 32);
    return sum == context.iterations() * 496;
}

auto timer_sleep_zero(rstd_bench::BenchContext& context) -> bool {
    auto runtime = async::Runtime {};
    auto sum     = std::uint64_t {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        sum += runtime.block_on(sleep_zero());
        rstd::hint::black_box(sum);
    }

    context.set_items_processed(context.iterations());
    return sum == context.iterations();
}

const rstd_bench::BenchCase CASES[] = {
    { "async", "current_thread_ready", 200'000, 1'000, &current_thread_ready },
    { "async", "current_thread_spawn_local_join", 50'000, 500, &current_thread_spawn_local_join },
    { "async", "thread_pool_spawn_join_2", 20'000, 200, &thread_pool_spawn_join },
    { "async", "thread_pool_join_many_4x32", 2'000, 20, &thread_pool_join_many },
    { "async", "timer_sleep_zero", 100'000, 500, &timer_sleep_zero },
};

} // namespace

namespace rstd_bench
{

auto async_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
