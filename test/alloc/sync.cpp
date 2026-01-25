#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

import rstd;

using rstd::sync::Arc;
using rstd::sync::Weak;

namespace
{

struct DropCounter {
    static inline std::atomic<int> live { 0 };
    static inline std::atomic<int> drops { 0 };

    int value = 0;

    DropCounter(): value(0) { live.fetch_add(1, std::memory_order_relaxed); }
    explicit DropCounter(int v): value(v) { live.fetch_add(1, std::memory_order_relaxed); }

    DropCounter(const DropCounter&)            = delete;
    DropCounter& operator=(const DropCounter&) = delete;

    DropCounter(DropCounter&& other) noexcept: value(other.value) {
        other.value = 0xdeadbeef;
        live.fetch_add(1, std::memory_order_relaxed);
    }
    DropCounter& operator=(DropCounter&& other) noexcept {
        value       = other.value;
        other.value = 0xdeadbeef;
        return *this;
    }

    ~DropCounter() {
        drops.fetch_add(1, std::memory_order_relaxed);
        live.fetch_sub(1, std::memory_order_relaxed);
    }

    static void reset() {
        live.store(0, std::memory_order_relaxed);
        drops.store(0, std::memory_order_relaxed);
    }
};

struct Payload {
    int x;
    explicit Payload(int v): x(v) {}
};

} // namespace

TEST(ArcBasic, MakeAndDeref) {
    auto a = Arc<int>::make(42);
    ASSERT_TRUE(a);
    EXPECT_EQ(*a, 42);
    EXPECT_EQ(a.strong_count(), 1u);
    EXPECT_EQ(a.weak_count(), 0u);
}

TEST(ArcBasic, CopyIncrementsStrong) {
    auto a = Arc<int>::make(7);
    EXPECT_EQ(a.strong_count(), 1u);

    auto b = a.clone();
    EXPECT_EQ(a.strong_count(), 2u);
    EXPECT_EQ(b.strong_count(), 2u);

    // Drop b
    b.reset();
    EXPECT_EQ(a.strong_count(), 1u);
}

TEST(ArcBasic, MoveDoesNotChangeStrong) {
    auto a = Arc<int>::make(9);
    EXPECT_EQ(a.strong_count(), 1u);

    auto b = std::move(a);
    EXPECT_FALSE(a);
    EXPECT_TRUE(b);
    EXPECT_EQ(b.strong_count(), 1u);
}

TEST(ArcWeak, DowngradeAndUpgrade) {
    auto a = Arc<int>::make(100);

    auto w = a.downgrade();
    EXPECT_EQ(a.strong_count(), 1u);
    EXPECT_EQ(a.weak_count(), 1u); // excludes implicit weak

    auto a2 = w.upgrade();
    ASSERT_TRUE(a2);
    EXPECT_EQ(*a2, 100);
    EXPECT_EQ(a.strong_count(), 2u);
    EXPECT_EQ(a.weak_count(), 1u);

    a2.reset();
    EXPECT_EQ(a.strong_count(), 1u);

    // Drop last strong -> value destroyed; weak upgrade fails
    a.reset();
    EXPECT_TRUE(w.expired());
    auto a3 = w.upgrade();
    EXPECT_FALSE(a3);
}

TEST(ArcWeak, WeakCountSemantics) {
    auto a = Arc<int>::make(1);
    EXPECT_EQ(a.weak_count(), 0u);

    auto w1 = a.downgrade();
    EXPECT_EQ(a.weak_count(), 1u);

    auto w2 = w1.clone();
    EXPECT_EQ(a.weak_count(), 2u);

    w1.reset();
    EXPECT_EQ(a.weak_count(), 1u);

    // if strong becomes 0, weak_count() returns raw weak count (including what remains)
    a.reset();
    EXPECT_EQ(w2.weak_count(), 1u);
}

TEST(ArcDrop, DropDestroysPayloadOnce) {
    DropCounter::reset();
    {
        auto a = Arc<DropCounter>::make(DropCounter { 123 });
        EXPECT_EQ(DropCounter::live.load(), 1);
        EXPECT_EQ(DropCounter::drops.load(), 1); // NOTE: temporary moved then destroyed once
        // Explanation: DropCounter{123} creates temporary => live+1, then moved into ArcInner =>
        // live+1, then temporary destroyed => drops+1, live-1. So live==1, drops==1 inside scope.

        auto b = a.clone();
        EXPECT_EQ(a.strong_count(), 2u);
        EXPECT_EQ(DropCounter::live.load(), 1);

        b.reset();
        EXPECT_EQ(a.strong_count(), 1u);
        EXPECT_EQ(DropCounter::live.load(), 1);
    }
    // Arc dropped => inner payload destroyed exactly once more
    EXPECT_EQ(DropCounter::live.load(), 0);
    // Total drops should be 2: temporary + payload in Arc
    EXPECT_EQ(DropCounter::drops.load(), 2);
}

TEST(ArcUnwrap, TryUnwrapSuccess) {
    auto a = Arc<Payload>::make(Payload { 5 });

    EXPECT_EQ(a.strong_count(), 1);
    auto res = a.try_unwrap();
    EXPECT_EQ(res.is_ok(), true);
    EXPECT_FALSE(a); // consumed
}

TEST(ArcUnwrap, TryUnwrapFailsWhenNotUnique) {
    auto a = Arc<Payload>::make(Payload { 11 });
    auto b = a.clone();

    EXPECT_EQ(a.strong_count(), 2);
    auto res = a.try_unwrap();
    EXPECT_FALSE(res);
    EXPECT_TRUE(b);
    EXPECT_EQ(b.strong_count(), 2u);

    // cleanup
    b.reset();
}

TEST(ArcRaw, IntoRawFromRawRoundtrip) {
    auto a = Arc<int>::make(77);
    auto p = a.into_raw();
    EXPECT_FALSE(a); // ownership leaked
    ASSERT_NE((p.as_ptr()), nullptr);
    EXPECT_EQ(*(p.as_ptr()), 77);

    auto b = Arc<int>::from_raw(p);
    EXPECT_TRUE(b);
    EXPECT_EQ(*b, 77);
    EXPECT_EQ(b.strong_count(), 1u);
}

TEST(ArcThread, CloneDropAcrossThreads) {
    // Basic stress: many threads clone and drop.
    auto          a        = Arc<int>::make(123);
    constexpr int kThreads = 8;
    constexpr int kIters   = 5000;

    std::vector<std::thread> ts;
    ts.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([a = a.clone()]() mutable {
            // Each thread holds one Arc by value (copied into lambda)
            for (int i = 0; i < kIters; ++i) {
                auto b = a.clone();
                // Touch data (read-only)
                if (*b != 123) std::abort();
            }
            // b dropped each loop
        });
    }
    for (auto& th : ts) th.join();

    // Only the original `a` remains (plus the copies captured in threads are gone)
    EXPECT_EQ(a.strong_count(), 1u);
    EXPECT_EQ(*a, 123);
}

TEST(ArcGetMut, UniqueAccessAndMutation) {
    auto a = Arc<int>::make(10);
    auto m = Arc<int>::get_mut(a);
    ASSERT_TRUE(m.is_some());
    *m.unwrap() = 42;
    EXPECT_EQ(*a, 42);

    auto w = a.downgrade();
    EXPECT_FALSE(Arc<int>::get_mut(a).is_some());

    w.reset();
    auto m2 = Arc<int>::get_mut(a);
    ASSERT_TRUE(m2.is_some());
    *m2.unwrap() = 99;
    EXPECT_EQ(*a, 99);
}

TEST(ArcUnique, IsUniqueTracksStrongAndWeak) {
    auto a = Arc<int>::make(1);
    EXPECT_TRUE(Arc<int>::is_unique(a));

    auto w = a.downgrade();
    EXPECT_FALSE(Arc<int>::is_unique(a));

    w.reset();
    EXPECT_TRUE(Arc<int>::is_unique(a));

    auto b = a.clone();
    EXPECT_FALSE(Arc<int>::is_unique(a));
    EXPECT_FALSE(Arc<int>::is_unique(b));
    b.reset();
    EXPECT_TRUE(Arc<int>::is_unique(a));
}

TEST(ArcPtrEq, ClonesShareAllocation) {
    auto a = Arc<int>::make(5);
    auto b = a.clone();
    auto c = Arc<int>::make(5);

    EXPECT_TRUE(Arc<int>::ptr_eq(a, b));
    EXPECT_FALSE(Arc<int>::ptr_eq(a, c));

    // EXPECT_EQ(a.as_ptr().as_ptr(), b.as_ptr().as_ptr());
    // EXPECT_NE(a.as_ptr().as_ptr(), c.as_ptr().as_ptr());
}

TEST(WeakBasic, EmptyWeakIsExpired) {
    auto w = Weak<int>::make();
    EXPECT_TRUE(w.expired());
    EXPECT_EQ(w.strong_count(), 0u);
    EXPECT_EQ(w.weak_count(), 0u);

    auto a = w.upgrade();
    EXPECT_FALSE(a);
}
