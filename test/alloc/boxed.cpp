#include <gtest/gtest.h>

#include <type_traits>

import rstd;

using rstd::boxed::Box;

static_assert(rstd::Impled<Box<int>, rstd::ops::Deref>);
static_assert(rstd::Impled<Box<int>, rstd::ops::DerefMut>);
static_assert(sizeof(Box<int>) == sizeof(int*));

namespace
{

struct Counter {
    static inline int alive = 0;

    int v = 0;

    Counter(): v(0) { ++alive; }
    explicit Counter(int x): v(x) { ++alive; }

    Counter(const Counter& o): v(o.v) { ++alive; }
    Counter(Counter&& o) noexcept: v(o.v) { ++alive; }

    Counter& operator=(const Counter&)     = default;
    Counter& operator=(Counter&&) noexcept = default;

    ~Counter() { --alive; }
};

struct CallbackDropProbe {
    int* drops;

    explicit CallbackDropProbe(int& drops): drops(&drops) {}
    CallbackDropProbe(const CallbackDropProbe&)            = delete;
    CallbackDropProbe& operator=(const CallbackDropProbe&) = delete;

    CallbackDropProbe(CallbackDropProbe&& other) noexcept: drops(other.drops) {
        other.drops = nullptr;
    }

    ~CallbackDropProbe() {
        if (drops != nullptr) ++*drops;
    }

    void operator()() {}
};

TEST(BoxTest, MakeAndAccess) {
    {
        auto b = Box<int>::make(42);
        ASSERT_NE(b.get(), nullptr);
        EXPECT_EQ(*b.get(), 42);
    }

    // Counter 版本：确保构造/析构生命周期正确
    EXPECT_EQ(Counter::alive, 0);
    {
        auto b = Box<Counter>::make(Counter { 7 });
        EXPECT_EQ(b->v, 7);
        EXPECT_EQ(Counter::alive, 1);
    }
    EXPECT_EQ(Counter::alive, 0);
}

TEST(BoxTest, DerefUsesBorrowProjection) {
    auto box = Box<Counter>::make(4);

    EXPECT_EQ(box.deref().as_raw_ptr(), box.get());
    EXPECT_EQ(rstd::as<rstd::ops::Deref>(box).deref().as_raw_ptr(), box.get());
    EXPECT_EQ(&*box, box.get());

    static_assert(std::is_same_v<decltype(*box), Counter&>);
    const auto& const_box = box;
    static_assert(std::is_same_v<decltype(*const_box), const Counter&>);

    box->v = 8;
    EXPECT_EQ(const_box->v, 8);
}

TEST(BoxTest, DynArrowKeepsDelegateAliveForFullExpression) {
    int  calls    = 0;
    auto callback = Box<rstd::dyn<rstd::FnMut<void()>>>::make([&calls] {
        ++calls;
    });

    callback->operator()();
    EXPECT_EQ(calls, 1);
}

TEST(BoxTest, DynFnMutWithByteSliceDestructs) {
    rstd::usize seen = 0;
    {
        auto callback = Box<rstd::dyn<rstd::FnMut<void(rstd::slice<rstd::byte>)>>>::make(
            [&seen](rstd::slice<rstd::byte> bytes) {
                seen = bytes.len();
            });
        auto value = rstd::byte {};
        callback->operator()(rstd::slice<rstd::byte>::from_raw_parts(&value, 1));
    }
    EXPECT_EQ(seen, 1u);
}

TEST(BoxTest, DynDropUsesConcreteDropAndLayout) {
    int drops = 0;
    {
        auto callback = Box<rstd::dyn<rstd::FnMut<void()>>>::make(CallbackDropProbe { drops });

        auto layout = rstd::alloc::Layout::for_value(callback.as_ptr());
        EXPECT_EQ(layout.size, sizeof(CallbackDropProbe));
        EXPECT_EQ(layout.align, alignof(CallbackDropProbe));

        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}

TEST(BoxTest, MoveCtorTransfersOwnership) {
    EXPECT_EQ(Counter::alive, 0);
    {
        auto  b1 = Box<Counter>::make(Counter { 1 });
        auto* p1 = b1.get();
        ASSERT_NE(p1, nullptr);
        EXPECT_EQ(Counter::alive, 1);

        auto b2 = rstd::move(b1);
        EXPECT_EQ(b2.get(), p1);
        EXPECT_EQ(Counter::alive, 1);
    }
    EXPECT_EQ(Counter::alive, 0);
}

TEST(BoxTest, MoveAssignDropsOldAndTakesNew) {
    EXPECT_EQ(Counter::alive, 0);
    {
        auto b1 = Box<Counter>::make(Counter { 11 });
        auto b2 = Box<Counter>::make(Counter { 22 });
        EXPECT_EQ(Counter::alive, 2);

        auto* p1 = b1.get();
        ASSERT_NE(p1, nullptr);

        b2 = rstd::move(b1); // should delete old b2 target first
        EXPECT_EQ(Counter::alive, 1);

        EXPECT_EQ(b2.get(), p1);
        EXPECT_EQ(b2->v, 11);
    }
    EXPECT_EQ(Counter::alive, 0);
}

TEST(BoxTest, MoveAssignRestoresMovedFromBox) {
    EXPECT_EQ(Counter::alive, 0);
    {
        auto b = Box<Counter>::make(Counter { 5 });
        auto c = rstd::move(b);
        EXPECT_EQ(Counter::alive, 1);

        b = Box<Counter>::make(Counter { 7 });
        EXPECT_EQ(Counter::alive, 2);
        EXPECT_EQ(b->v, 7);
        EXPECT_EQ(c->v, 5);
    }
    EXPECT_EQ(Counter::alive, 0);
}

TEST(BoxDeathTest, UseAfterMovePanics) {
    auto b = Box<Counter>::make(Counter { 5 });
    auto c = rstd::move(b);

    EXPECT_DEATH(static_cast<void>(b.get()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.as_ptr()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.as_mut_ptr()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.as_ref()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.deref_mut()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(*b), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.operator->()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(b.clone()), "Box used after move");
    EXPECT_DEATH(static_cast<void>(Box<Counter>(rstd::move(b))), "Box used after move");
    EXPECT_DEATH(static_cast<void>(rstd::move(b).into_raw()), "Box used after move");

    const auto& const_b = b;
    EXPECT_DEATH(static_cast<void>(const_b.as_ref()), "Box used after move");
    EXPECT_EQ(c->v, 5);
}

TEST(BoxDeathTest, CloneFromMovedBoxPanics) {
    auto source       = Box<int>::make(1);
    auto moved_source = rstd::move(source);
    auto target       = Box<int>::make(2);

    EXPECT_DEATH(target.clone_from(source), "Box used after move");

    auto moved_target = rstd::move(target);
    EXPECT_DEATH(target.clone_from(moved_source), "Box used after move");
    EXPECT_EQ(*moved_source, 1);
    EXPECT_EQ(*moved_target, 2);
}

TEST(BoxDeathTest, DynAndArrayUseAfterMovePanics) {
    using Callback      = rstd::dyn<rstd::FnMut<void()>>;
    auto callback       = Box<Callback>::make([] {
    });
    auto moved_callback = rstd::move(callback);
    EXPECT_DEATH(callback->operator()(), "Box used after move");

    auto values = rstd::vec::Vec<int>::make();
    values.push(1);
    values.push(2);
    auto array       = values.into_boxed_slice();
    auto moved_array = rstd::move(array);
    EXPECT_DEATH(static_cast<void>(array.clone()), "Box used after move");

    moved_callback->operator()();
    EXPECT_EQ(moved_array.as_ref().len(), 2u);
}

TEST(BoxDeathTest, FromNullPanics) {
    EXPECT_DEATH(static_cast<void>(Box<int>::from_raw(rstd::mut_ptr<int>::from_raw_parts(nullptr))),
                 "Box cannot be constructed from null");
}

TEST(BoxTest, FromRawTakesOwnership) {
    EXPECT_EQ(Counter::alive, 0);
    {
        auto* raw = new Counter(99);
        EXPECT_EQ(Counter::alive, 1);

        {
            auto b = Box<Counter>::from_raw(rstd::mut_ptr<Counter>::from_raw_parts(raw));
            EXPECT_EQ(b->v, 99);
        } // should delete raw here

        EXPECT_EQ(Counter::alive, 0);
    }
}

TEST(BoxTest, IntoRawReleasesOwnership) {
    EXPECT_EQ(Counter::alive, 0);
    Counter* raw = nullptr;

    {
        auto b = Box<Counter>::make(Counter { 123 });
        EXPECT_EQ(Counter::alive, 1);

        raw = rstd::move(b).into_raw(); // ownership released to caller
        ASSERT_NE(raw, nullptr);
        EXPECT_EQ(raw->v, 123);
        // b is moved-from; its destructor should NOT delete raw
    }

    EXPECT_EQ(Counter::alive, 1);
    delete raw; // caller responsibility after into_raw
    EXPECT_EQ(Counter::alive, 0);
}

} // namespace
