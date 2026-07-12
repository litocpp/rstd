#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct WakeState {
    struct Fields {
        Option<task::Waker> waker;
        bool                ready { false };
        int                 polls { 0 };
    };

    sync::Mutex<Fields> fields;

    WakeState(): fields(Fields {}) {}
};

struct DropSignal {
    std::atomic<int>* drops;

    explicit DropSignal(std::atomic<int>& drops): drops(rstd::addressof(drops)) {}
    DropSignal(const DropSignal&)            = delete;
    DropSignal& operator=(const DropSignal&) = delete;

    DropSignal(DropSignal&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}

    ~DropSignal() {
        if (drops != nullptr) {
            drops->fetch_add(1, std::memory_order_release);
        }
    }
};

struct SharedWakeFuture {
    using Output = int;

    sync::Arc<WakeState> state;

    auto poll(mut_ref<SharedWakeFuture> self, task::Context& cx) -> task::Poll<int> {
        auto fields = self->state->fields.lock().unwrap();
        ++fields->polls;
        if (fields->ready) {
            return task::Poll<int>::Ready(fields->polls);
        }
        fields->waker = Some(cx.waker().clone());
        return task::Poll<int>::Pending();
    }
};

auto wait_for_waker(sync::Arc<WakeState> const& state) -> task::Waker {
    for (;;) {
        {
            auto fields = state->fields.lock().unwrap();
            if (fields->waker.is_some()) {
                return fields->waker->clone();
            }
        }
        hint::spin_loop();
    }
}

void mark_ready(sync::Arc<WakeState> const& state) {
    auto fields   = state->fields.lock().unwrap();
    fields->ready = true;
}

auto wake_repeatedly(task::Waker waker) -> bool {
    for (int i = 0; i < 100; ++i) {
        waker.wake_by_ref();
    }
    rstd::move(waker).wake();
    return true;
}

auto increment_after_yields(std::atomic<int>& completed) -> async::coro<void> {
    co_await async::yield_now();
    co_await async::yield_now();
    completed.fetch_add(1, std::memory_order_release);
}

auto wait_for_count(std::atomic<int>& value, int expected) -> bool {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (value.load(std::memory_order_acquire) == expected) {
            return true;
        }
        thread::sleep(time::Duration::from_millis(1));
    }
    return false;
}

auto hold_external_segment(async::AnyExecutor executor,
                           std::atomic<bool>& entered,
                           std::atomic<bool>& release,
                           std::atomic<bool>& resumed,
                           DropSignal         drop_signal) -> async::coro<void> {
    (void)drop_signal;
    if (! co_await executor) {
        co_return;
    }
    entered.store(true, std::memory_order_release);
    while (! release.load(std::memory_order_acquire)) {
        hint::spin_loop();
    }
    co_await async::yield_now();
    resumed.store(true, std::memory_order_release);
}

auto wait_for_flag(std::atomic<bool>& value) -> bool {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (value.load(std::memory_order_acquire)) {
            return true;
        }
        thread::sleep(time::Duration::from_millis(1));
    }
    return false;
}

TEST(RstdAsyncConcurrency, EscapedWakerIsSafeAfterScopedRootDetaches) {
    auto state    = sync::Arc<WakeState>::make();
    auto producer = thread::spawn([state = state.clone()] {
                        auto waker = wait_for_waker(state);
                        mark_ready(state);
                        waker.wake_by_ref();
                        return true;
                    }).unwrap();

    EXPECT_EQ(async::block_on(SharedWakeFuture { state.clone() }), 2);
    EXPECT_TRUE(rstd::move(producer).join().unwrap());

    auto escaped = Option<task::Waker> {};
    {
        auto fields = state->fields.lock().unwrap();
        escaped     = fields->waker.take();
    }
    ASSERT_TRUE(escaped.is_some());
    escaped->wake_by_ref();
    rstd::move(*escaped).wake();

    auto fields = state->fields.lock().unwrap();
    EXPECT_EQ(fields->polls, 2);
}

TEST(RstdAsyncConcurrency, WakeStormCreatesOneQueuedExecution) {
    auto state = sync::Arc<WakeState>::make();
    auto producer =
        thread::spawn([state = state.clone()] {
            auto base = wait_for_waker(state);
            mark_ready(state);
            auto first  = thread::spawn([waker = base.clone()]() mutable {
                             return wake_repeatedly(rstd::move(waker));
                          }).unwrap();
            auto second = thread::spawn([waker = base.clone()]() mutable {
                              return wake_repeatedly(rstd::move(waker));
                          }).unwrap();
            auto third  = thread::spawn([waker = base.clone()]() mutable {
                             return wake_repeatedly(rstd::move(waker));
                          }).unwrap();
            auto fourth = thread::spawn([waker = rstd::move(base)]() mutable {
                              return wake_repeatedly(rstd::move(waker));
                          }).unwrap();
            return rstd::move(first).join().unwrap() && rstd::move(second).join().unwrap() &&
                   rstd::move(third).join().unwrap() && rstd::move(fourth).join().unwrap();
        }).unwrap();

    EXPECT_EQ(async::block_on(SharedWakeFuture { state.clone() }), 2);
    EXPECT_TRUE(rstd::move(producer).join().unwrap());

    auto fields = state->fields.lock().unwrap();
    EXPECT_EQ(fields->polls, 2);
}

TEST(RstdAsyncConcurrency, MultipleCallersSubmitThroughWorkerInboxes) {
    constexpr int callers        = 4;
    constexpr int tasks_per_call = 64;
    constexpr int expected_tasks = callers * tasks_per_call;
    auto          completed      = std::atomic<int> { 0 };
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(4).build().unwrap();
    auto handle  = runtime.handle();

    auto first_handle  = handle.clone();
    auto second_handle = handle.clone();
    auto third_handle  = handle.clone();
    auto fourth_handle = handle.clone();
    auto first         = thread::spawn([handle = rstd::move(first_handle), &completed] {
                     for (int i = 0; i < tasks_per_call; ++i) {
                         (void)handle.spawn(increment_after_yields(completed));
                     }
                         }).unwrap();
    auto second        = thread::spawn([handle = rstd::move(second_handle), &completed] {
                      for (int i = 0; i < tasks_per_call; ++i) {
                          (void)handle.spawn(increment_after_yields(completed));
                      }
                         }).unwrap();
    auto third         = thread::spawn([handle = rstd::move(third_handle), &completed] {
                     for (int i = 0; i < tasks_per_call; ++i) {
                         (void)handle.spawn(increment_after_yields(completed));
                     }
                         }).unwrap();
    auto fourth        = thread::spawn([handle = rstd::move(fourth_handle), &completed] {
                      for (int i = 0; i < tasks_per_call; ++i) {
                          (void)handle.spawn(increment_after_yields(completed));
                      }
                         }).unwrap();

    rstd::move(first).join().unwrap();
    rstd::move(second).join().unwrap();
    rstd::move(third).join().unwrap();
    rstd::move(fourth).join().unwrap();
    EXPECT_TRUE(wait_for_count(completed, expected_tasks));
    EXPECT_EQ(completed.load(std::memory_order_acquire), expected_tasks);
}

TEST(RstdAsyncConcurrency, ShutdownWaitsForRunningExternalSegment) {
    auto context  = async::LocalExecutorContext::make();
    auto executor = async::AnyExecutor::from_executor(context.executor());
    auto runtime  = Option<async::Runtime> { Some(
        async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap()) };
    auto entered  = std::atomic<bool> { false };
    auto release  = std::atomic<bool> { false };
    auto resumed  = std::atomic<bool> { false };
    auto drops    = std::atomic<int> { 0 };
    auto joined   = (*runtime).spawn(
        hold_external_segment(executor.clone(), entered, release, resumed, DropSignal { drops }));
    auto executor_thread = thread::spawn([&context] {
                               for (;;) {
                                   if (context.run_ready() != 0) {
                                       return true;
                                   }
                                   thread::sleep(time::Duration::from_millis(1));
                               }
                           }).unwrap();

    ASSERT_TRUE(wait_for_flag(entered));
    EXPECT_EQ(drops.load(std::memory_order_acquire), 0);
    auto releaser = thread::spawn([&release] {
                        thread::sleep(time::Duration::from_millis(2));
                        release.store(true, std::memory_order_release);
                    }).unwrap();
    runtime       = None();

    (void)rstd::move(releaser).join().unwrap();
    EXPECT_TRUE(rstd::move(executor_thread).join().unwrap());
    EXPECT_TRUE(joined.is_finished());
    EXPECT_FALSE(resumed.load(std::memory_order_acquire));
    EXPECT_EQ(drops.load(std::memory_order_acquire), 1);
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
}

} // namespace
