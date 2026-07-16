#include <gtest/gtest.h>

import rstd;
import rstd.test.any_module_check;

using namespace rstd::prelude;

namespace
{

struct MoveOnlyValue {
    int* drops;
    int  value;

    MoveOnlyValue(int& drops, int value): drops(&drops), value(value) {}
    MoveOnlyValue(const MoveOnlyValue&)            = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&& other) noexcept: drops(other.drops), value(other.value) {
        other.drops = nullptr;
    }
    ~MoveOnlyValue() {
        if (drops != nullptr) ++*drops;
    }
};

} // namespace

TEST(Any, TypeIdIsStableAcrossModuleBoundary) {
    EXPECT_EQ(rstd::any::TypeId::of<int>(), any_int_type_id_from_module());
    EXPECT_NE(rstd::any::TypeId::of<int>(), rstd::any::TypeId::of<unsigned>());
}

TEST(Any, BorrowedDowncastChecksType) {
    int  drops = 0;
    auto value = Box<dyn<rstd::any::Any>>::make(MoveOnlyValue { drops, 7 });

    auto borrowed = rstd::any::downcast_ref<MoveOnlyValue>(value.as_ref());
    ASSERT_TRUE(borrowed.is_some());
    EXPECT_EQ((**borrowed).value, 7);
    EXPECT_TRUE(rstd::any::downcast_ref<int>(value.as_ref()).is_none());

    auto mutable_value = rstd::any::downcast_mut<MoveOnlyValue>(value.deref_mut());
    ASSERT_TRUE(mutable_value.is_some());
    (**mutable_value).value = 11;
    EXPECT_EQ((**rstd::any::downcast_ref<MoveOnlyValue>(value.as_ref())).value, 11);
    EXPECT_EQ(drops, 0);
}

TEST(Any, OwningDowncastPreservesAllocationAndDrop) {
    int drops = 0;
    {
        auto erased   = Box<dyn<rstd::any::Any>>::make(MoveOnlyValue { drops, 23 });
        auto concrete = rstd::move(erased).downcast<MoveOnlyValue>();
        ASSERT_TRUE(concrete.is_ok());
        EXPECT_EQ((*concrete)->value, 23);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Any, FailedOwningDowncastReturnsOriginalBox) {
    int drops = 0;
    {
        auto erased = Box<dyn<rstd::any::Any>>::make(MoveOnlyValue { drops, 31 });
        auto result = rstd::move(erased).downcast<int>();
        ASSERT_TRUE(result.is_err());
        auto restored = rstd::move(result).unwrap_err();
        EXPECT_EQ((**rstd::any::downcast_ref<MoveOnlyValue>(restored.as_ref())).value, 31);
    }
    EXPECT_EQ(drops, 1);
}
