#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

import rstd;

using namespace rstd;

namespace
{
static constinit sync::Once STATIC_ONCE = sync::Once::make();

struct RvalueOnly {
    int* calls;

    void operator()() && { ++*calls; }
    void operator()() & = delete;
};

struct NotCallable {};

struct NoThrowCallable {
    void operator()() noexcept {}
};

using FunctionPointer = void (*)();

using RegularCallable = decltype([] {
});
using MutableCallable = decltype([]() mutable {
});

static_assert(Impled<RegularCallable, FnOnce<void()>>);
static_assert(Impled<MutableCallable, FnOnce<void()>>);
static_assert(Impled<RvalueOnly, FnOnce<void()>>);
static_assert(Impled<FunctionPointer, FnOnce<void()>>);
static_assert(Impled<NoThrowCallable, FnOnce<void() noexcept>>);
static_assert(! Impled<RegularCallable, FnOnce<void() noexcept>>);
static_assert(! Impled<NotCallable, FnOnce<void()>>);
static_assert(Impled<RegularCallable, FnMut<void()>>);
static_assert(Impled<RegularCallable, Fn<void()>>);
static_assert(! std::is_copy_constructible_v<sync::Once>);
static_assert(! std::is_move_constructible_v<sync::Once>);
} // namespace

TEST(FnOnce, InvokesRvalueQualifiedCallable) {
    int calls = 0;
    invoke_once<void()>(RvalueOnly { &calls });
    EXPECT_EQ(calls, 1);
}

TEST(Once, StaticConstructionAndSequentialCalls) {
    auto was_completed = STATIC_ONCE.is_completed();
    int  calls         = 0;

    STATIC_ONCE.call_once([&] {
        ++calls;
    });
    STATIC_ONCE.call_once([&] {
        ++calls;
    });

    EXPECT_EQ(calls, was_completed ? 0 : 1);
    EXPECT_TRUE(STATIC_ONCE.is_completed());
}

TEST(Once, ConcurrentCallsPublishOnePayload) {
    auto once     = sync::Once::make();
    int  payload  = 0;
    auto calls    = std::atomic<int> {};
    auto observed = std::vector<int>(16);
    auto threads  = std::vector<std::thread> {};

    for (int index = 0; index < 16; ++index) {
        threads.emplace_back([&, index] {
            once.call_once([&] {
                payload = 42;
                calls.fetch_add(1, std::memory_order_relaxed);
            });
            observed[static_cast<std::size_t>(index)] = payload;
        });
    }
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
    for (auto value : observed) EXPECT_EQ(value, 42);
}

TEST(Once, WaitObservesPublishedPayload) {
    auto once     = sync::Once::make();
    auto started  = std::atomic<bool> {};
    int  payload  = 0;
    int  observed = 0;

    auto waiter = std::thread([&] {
        started.store(true, std::memory_order_release);
        once.wait();
        observed = payload;
    });

    while (! started.load(std::memory_order_acquire)) std::this_thread::yield();
    once.call_once([&] {
        payload = 73;
    });
    waiter.join();

    EXPECT_EQ(observed, 73);
}

TEST(Once, ForceReportsCleanInitialState) {
    auto once       = sync::Once::make();
    bool was_poison = true;

    once.call_once_force([&](sync::OnceState const& state) {
        was_poison = state.is_poisoned();
    });
    once.wait_force();

    EXPECT_FALSE(was_poison);
    EXPECT_TRUE(once.is_completed());
}

TEST(Once, RunningInitializerBlocksConcurrentCallers) {
    auto once     = sync::Once::make();
    auto started  = std::atomic<bool> {};
    auto release  = std::atomic<bool> {};
    auto returned = std::atomic<int> {};

    auto winner = std::thread([&] {
        once.call_once([&] {
            started.store(true, std::memory_order_release);
            while (! release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    });
    while (! started.load(std::memory_order_acquire)) std::this_thread::yield();

    auto followers = std::vector<std::thread> {};
    for (int index = 0; index < 8; ++index) {
        followers.emplace_back([&] {
            once.call_once([] {
            });
            returned.fetch_add(1, std::memory_order_relaxed);
        });
    }

    EXPECT_EQ(returned.load(std::memory_order_relaxed), 0);
    release.store(true, std::memory_order_release);
    winner.join();
    for (auto& follower : followers) follower.join();
    EXPECT_EQ(returned.load(std::memory_order_relaxed), 8);
}
