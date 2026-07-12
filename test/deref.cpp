#include <gtest/gtest.h>
#include <rstd/macro.hpp>

#include <type_traits>

import rstd;

namespace
{

struct Payload {
    int value;
};

struct RefLike {
    USE_TRAIT(RefLike)

    using Target = Payload;

    Payload* value;

    auto deref() const noexcept -> rstd::ref<Target> {
        return rstd::ref<Target>::from_raw_parts(value);
    }

    auto deref_mut() noexcept -> rstd::mut_ref<Target> {
        return rstd::mut_ref<Target>::from_raw_parts(value);
    }
};

struct SameNamedOnly {
    using Target = Payload;

    Payload* value;

    auto deref() const noexcept -> rstd::ref<Target> {
        return rstd::ref<Target>::from_raw_parts(value);
    }
};

} // namespace

template<>
struct rstd::Impl<rstd::ops::Deref, RefLike>
    : rstd::LinkClassMethod<rstd::ops::Deref, RefLike> {};

template<>
struct rstd::Impl<rstd::ops::DerefMut, RefLike>
    : rstd::LinkClassMethod<rstd::ops::DerefMut, RefLike> {};

static_assert(rstd::Impled<RefLike, rstd::ops::Deref>);
static_assert(rstd::Impled<RefLike, rstd::ops::DerefMut>);
static_assert(! rstd::Impled<SameNamedOnly, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<Payload>, rstd::ops::Deref>);
static_assert(! rstd::Impled<rstd::ref<Payload>, rstd::ops::DerefMut>);
static_assert(rstd::Impled<rstd::mut_ref<Payload>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::mut_ref<Payload>, rstd::ops::DerefMut>);
static_assert(! rstd::Impled<rstd::Option<rstd::ref<Payload>>, rstd::ops::Deref>);
static_assert(! rstd::Impled<rstd::Result<Payload, int>, rstd::ops::Deref>);
static_assert(! rstd::Impled<rstd::ptr<Payload>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<rstd::str>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<rstd::ffi::CStr>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<rstd::ffi::OsStr>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<rstd::path::Path>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::ref<rstd::dyn<rstd::FnMut<void()>>>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::mut_ref<rstd::dyn<rstd::FnMut<void()>>>,
                          rstd::ops::DerefMut>);
static_assert(rstd::Impled<rstd::sync::MutexGuard<int>, rstd::ops::DerefMut>);

TEST(Deref, ConcreteTraitAndOperatorShareTarget) {
    Payload value { 7 };
    RefLike ref_like { .value = &value };

    EXPECT_EQ(ref_like.deref().as_raw_ptr(), &value);
    EXPECT_EQ(rstd::as<rstd::ops::Deref>(ref_like).deref().as_raw_ptr(), &value);
    EXPECT_EQ(&*ref_like, &value);
    EXPECT_EQ(ref_like.operator->(), &value);
    EXPECT_EQ(ref_like->value, 7);

    ref_like->value = 9;
    EXPECT_EQ(value.value, 9);
}

TEST(Deref, ReferenceWrappersPreserveConstness) {
    Payload value { 11 };
    auto immutable = rstd::ref<Payload>::from_raw_parts(&value);
    auto mutable_  = rstd::mut_ref<Payload>::from_raw_parts(&value);

    static_assert(std::is_same_v<decltype(*immutable), const Payload&>);
    static_assert(std::is_same_v<decltype(*mutable_), Payload&>);

    EXPECT_EQ(immutable->value, 11);
    mutable_->value = 13;
    EXPECT_EQ(value.value, 13);

    const auto& const_mutable = mutable_;
    static_assert(std::is_same_v<decltype(*const_mutable), const Payload&>);
}

TEST(Deref, OptionKeepsPresenceAndBorrowSeparate) {
    Payload value { 17 };
    auto member = rstd::Some(rstd::ref<Payload>::from_raw_parts(&value));

    ASSERT_TRUE(member.is_some());
    EXPECT_EQ((*member)->value, 17);
}

TEST(Deref, UnsizedTraitProjectionKeepsMetadata) {
    int values[] { 2, 3, 5 };
    auto slice = rstd::ref<int[]>::from_raw_parts(values, 3);

    auto projected = slice.deref();
    EXPECT_EQ(projected.as_raw_ptr(), values);
    EXPECT_EQ(projected.len(), 3);
}
