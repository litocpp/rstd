#include <rstd/test/gtest.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

import rstd;

using namespace rstd;

namespace
{
static constinit sync::OnceLock<int> STATIC_CELL = sync::OnceLock<int>::make();
[[maybe_unused]]
static constinit sync::OnceLock<std::unique_ptr<int>> STATIC_MOVE_CELL =
    sync::OnceLock<std::unique_ptr<int>>::make();

struct alignas(64) AlignedValue {
    int value;
};

struct DropProbe {
    int* drops;

    explicit DropProbe(int* drops): drops(drops) {}
    DropProbe(DropProbe const&) = delete;
    DropProbe(DropProbe&& other) noexcept: drops(other.drops) { other.drops = nullptr; }
    ~DropProbe() {
        if (drops != nullptr) ++*drops;
    }
};

static_assert(! std::is_copy_constructible_v<sync::OnceLock<int>>);
static_assert(! std::is_move_constructible_v<sync::OnceLock<int>>);
} // namespace

TEST(OnceLock, SetGetAndFailedSetReturnsValue) {
    auto cell = sync::OnceLock<int>::make();

    EXPECT_TRUE(cell.get().is_none());
    EXPECT_TRUE(cell.set(11).is_ok());
    EXPECT_EQ(cell.get().unwrap().get(), 11);

    auto second = cell.set(29);
    ASSERT_TRUE(second.is_err());
    EXPECT_EQ(rstd::move(second).unwrap_err(), 29);
    EXPECT_EQ(cell.get().unwrap().get(), 11);
}

TEST(OnceLock, StaticCellInitializesOnce) {
    auto was_initialized = STATIC_CELL.get().is_some();
    auto calls           = 0;

    auto first  = STATIC_CELL.get_or_init([&] {
        ++calls;
        return 81;
    });
    auto second = STATIC_CELL.get_or_init([&] {
        ++calls;
        return 99;
    });

    EXPECT_EQ(first.get(), 81);
    EXPECT_EQ(second.get(), 81);
    EXPECT_EQ(calls, was_initialized ? 0 : 1);
}

TEST(OnceLock, GetDoesNotObserveRunningInitialization) {
    auto cell    = sync::OnceLock<int>::make();
    auto started = std::atomic<bool> {};
    auto release = std::atomic<bool> {};

    auto initializer = std::thread([&] {
        cell.get_or_init([&] {
            started.store(true, std::memory_order_release);
            while (! release.load(std::memory_order_acquire)) std::this_thread::yield();
            return 17;
        });
    });

    while (! started.load(std::memory_order_acquire)) std::this_thread::yield();
    EXPECT_TRUE(cell.get().is_none());
    release.store(true, std::memory_order_release);
    initializer.join();
    EXPECT_EQ(cell.get().unwrap().get(), 17);
}

TEST(OnceLock, ConcurrentInitializersPublishOneValue) {
    auto cell     = sync::OnceLock<int>::make();
    auto calls    = std::atomic<int> {};
    auto observed = std::vector<int>(16);
    auto threads  = std::vector<std::thread> {};

    for (int index = 0; index < 16; ++index) {
        threads.emplace_back([&, index] {
            auto value                                = cell.get_or_init([&, index] {
                calls.fetch_add(1, std::memory_order_relaxed);
                return 100 + index;
            });
            observed[static_cast<std::size_t>(index)] = value.get();
        });
    }
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
    for (auto value : observed) EXPECT_EQ(value, observed.front());
}

TEST(OnceLock, WaitObservesSetValue) {
    auto cell     = sync::OnceLock<int>::make();
    auto started  = std::atomic<bool> {};
    int  observed = 0;

    auto waiter = std::thread([&] {
        started.store(true, std::memory_order_release);
        observed = cell.wait().get();
    });
    while (! started.load(std::memory_order_acquire)) std::this_thread::yield();
    EXPECT_TRUE(cell.set(55).is_ok());
    waiter.join();

    EXPECT_EQ(observed, 55);
}

TEST(OnceLock, SupportsMoveOnlyValuesTakeAndReinitialize) {
    auto cell = sync::OnceLock<std::unique_ptr<int>>::make();

    EXPECT_TRUE(cell.set(std::make_unique<int>(7)).is_ok());
    EXPECT_EQ(*cell.get().unwrap().get(), 7);

    auto taken = cell.take();
    ASSERT_TRUE(taken.is_some());
    EXPECT_EQ(*taken.unwrap(), 7);
    EXPECT_TRUE(cell.get().is_none());

    auto value = cell.get_or_init([] {
        return std::make_unique<int>(13);
    });
    EXPECT_EQ(*value.get(), 13);

    auto inner = rstd::move(cell).into_inner();
    ASSERT_TRUE(inner.is_some());
    EXPECT_EQ(*inner.unwrap(), 13);
}

TEST(OnceLock, PreservesAlignment) {
    auto cell = sync::OnceLock<AlignedValue>::make();
    cell.set(AlignedValue { 5 });

    auto address = reinterpret_cast<std::uintptr_t>(cell.get().unwrap().as_raw_ptr());
    EXPECT_EQ(address % alignof(AlignedValue), 0U);
}

TEST(OnceLock, DropsStoredValueExactlyOnce) {
    int drops = 0;
    {
        auto cell = sync::OnceLock<DropProbe>::make();
        EXPECT_TRUE(cell.set(DropProbe(&drops)).is_ok());
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}

TEST(OnceLock, ConcurrentSetReturnsEveryLosingValue) {
    auto cell        = sync::OnceLock<std::unique_ptr<int>>::make();
    auto winners     = std::atomic<int> {};
    auto loser_count = std::atomic<int> {};
    auto loser_sum   = std::atomic<int> {};
    auto threads     = std::vector<std::thread> {};

    for (int value = 1; value <= 16; ++value) {
        threads.emplace_back([&, value] {
            auto result = cell.set(std::make_unique<int>(value));
            if (result.is_ok()) {
                winners.fetch_add(1, std::memory_order_relaxed);
            } else {
                auto returned = rstd::move(result).unwrap_err();
                loser_count.fetch_add(1, std::memory_order_relaxed);
                loser_sum.fetch_add(*returned, std::memory_order_relaxed);
            }
        });
    }
    for (auto& thread : threads) thread.join();

    ASSERT_TRUE(cell.get().is_some());
    auto winner = *cell.get().unwrap().get();
    EXPECT_EQ(winners.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(loser_count.load(std::memory_order_relaxed), 15);
    EXPECT_EQ(loser_sum.load(std::memory_order_relaxed) + winner, 136);
}
