#include <rstd/test/gtest.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

import rstd;

using namespace rstd;

namespace
{
static constinit auto STATIC_LAZY = sync::LazyLock<int>::make([] {
    return 144;
});
[[maybe_unused]]
static constinit auto STATIC_MOVE_LAZY = sync::LazyLock<std::unique_ptr<int>>::make([] {
    return std::make_unique<int>(1);
});

struct MoveOnlyInitializer {
    std::unique_ptr<int> value;

    explicit MoveOnlyInitializer(int value): value(std::make_unique<int>(value)) {}
    MoveOnlyInitializer(MoveOnlyInitializer const&) = delete;
    MoveOnlyInitializer(MoveOnlyInitializer&&)      = default;

    auto operator()() && -> std::unique_ptr<int> { return rstd::move(value); }
    auto operator()() & -> std::unique_ptr<int> = delete;
};

struct Payload {
    int value;
};

struct InitializerDropProbe {
    int* drops;
    int  value;

    InitializerDropProbe(int* drops, int value): drops(drops), value(value) {}
    InitializerDropProbe(InitializerDropProbe const&) = delete;
    InitializerDropProbe(InitializerDropProbe&& other) noexcept
        : drops(other.drops), value(other.value) {
        other.drops = nullptr;
    }
    ~InitializerDropProbe() {
        if (drops != nullptr) ++*drops;
    }

    auto operator()() && -> int { return value; }
};

struct ValueDropProbe {
    int* drops;

    explicit ValueDropProbe(int* drops): drops(drops) {}
    ValueDropProbe(ValueDropProbe const&) = delete;
    ValueDropProbe(ValueDropProbe&& other) noexcept: drops(other.drops) { other.drops = nullptr; }
    ~ValueDropProbe() {
        if (drops != nullptr) ++*drops;
    }
};

struct ValueInitializerDropProbe {
    int* initializer_drops;
    int* value_drops;

    ValueInitializerDropProbe(int* initializer_drops, int* value_drops)
        : initializer_drops(initializer_drops), value_drops(value_drops) {}
    ValueInitializerDropProbe(ValueInitializerDropProbe const&) = delete;
    ValueInitializerDropProbe(ValueInitializerDropProbe&& other) noexcept
        : initializer_drops(other.initializer_drops), value_drops(other.value_drops) {
        other.initializer_drops = nullptr;
    }
    ~ValueInitializerDropProbe() {
        if (initializer_drops != nullptr) ++*initializer_drops;
    }

    auto operator()() && -> ValueDropProbe { return ValueDropProbe(value_drops); }
};

static_assert(! std::is_copy_constructible_v<decltype(STATIC_LAZY)>);
static_assert(! std::is_move_constructible_v<decltype(STATIC_LAZY)>);
static_assert(! std::is_move_constructible_v<decltype(STATIC_MOVE_LAZY)>);
} // namespace

TEST(LazyLock, GetDoesNotInitializeAndForceRunsOnce) {
    int  calls = 0;
    auto lazy  = sync::LazyLock<int>::make([&] {
        ++calls;
        return 21;
    });

    EXPECT_TRUE(lazy.get().is_none());
    EXPECT_EQ(lazy.force().get(), 21);
    EXPECT_EQ(lazy.force().get(), 21);
    EXPECT_EQ(lazy.get().unwrap().get(), 21);
    EXPECT_EQ(calls, 1);
}

TEST(LazyLock, StaticConstructionAndDeref) {
    EXPECT_EQ(*STATIC_LAZY, 144);
    EXPECT_EQ(STATIC_LAZY.force().get(), 144);
}

TEST(LazyLock, ArrowUsesDerefTarget) {
    auto lazy = sync::LazyLock<Payload>::make([] {
        return Payload { 377 };
    });

    EXPECT_EQ(lazy->value, 377);
}

TEST(LazyLock, MutableForceAndGetMut) {
    auto lazy = sync::LazyLock<int>::make([] {
        return 8;
    });

    lazy.force_mut().get_mut() = 13;
    ASSERT_TRUE(lazy.get_mut().is_some());
    lazy.get_mut().unwrap().get_mut() = 34;

    EXPECT_EQ(*lazy, 34);
}

TEST(LazyLock, ConcurrentForcePublishesOneValue) {
    auto calls    = std::atomic<int> {};
    auto lazy     = sync::LazyLock<int>::make([&] {
        calls.fetch_add(1, std::memory_order_relaxed);
        return 233;
    });
    auto observed = std::vector<int>(16);
    auto threads  = std::vector<std::thread> {};

    for (int index = 0; index < 16; ++index) {
        threads.emplace_back([&, index] {
            observed[static_cast<std::size_t>(index)] = lazy.force().get();
        });
    }
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
    for (auto value : observed) EXPECT_EQ(value, 233);
}

TEST(LazyLock, SupportsMoveOnlyInitializerAndValue) {
    auto lazy = sync::LazyLock<std::unique_ptr<int>>::make(MoveOnlyInitializer(89));

    EXPECT_TRUE(lazy.get().is_none());
    EXPECT_EQ(*lazy.force().get(), 89);
    EXPECT_EQ(*lazy.get().unwrap().get(), 89);
}

TEST(LazyLock, DropsUnforcedInitializerExactlyOnce) {
    int initializer_drops = 0;
    {
        auto lazy = sync::LazyLock<int>::make(InitializerDropProbe(&initializer_drops, 3));
        EXPECT_TRUE(lazy.get().is_none());
        EXPECT_EQ(initializer_drops, 0);
    }
    EXPECT_EQ(initializer_drops, 1);
}

TEST(LazyLock, DropsInitializerAndValueExactlyOnce) {
    int initializer_drops = 0;
    int value_drops       = 0;
    {
        auto lazy = sync::LazyLock<ValueDropProbe>::make(
            ValueInitializerDropProbe(&initializer_drops, &value_drops));
        auto value = lazy.force();
        static_cast<void>(value);
        EXPECT_EQ(initializer_drops, 1);
        EXPECT_EQ(value_drops, 0);
    }
    EXPECT_EQ(initializer_drops, 1);
    EXPECT_EQ(value_drops, 1);
}
