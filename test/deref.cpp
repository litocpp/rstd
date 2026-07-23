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

    Payload* value;

    auto deref() const noexcept -> rstd::ref<Payload> {
        return rstd::ref<Payload>::from_raw_parts(value);
    }

    auto deref_mut() noexcept -> rstd::mut_ref<Payload> {
        return rstd::mut_ref<Payload>::from_raw_parts(value);
    }
};

struct SameNamedOnly {
    Payload* value;

    auto deref() const noexcept -> rstd::ref<Payload> {
        return rstd::ref<Payload>::from_raw_parts(value);
    }
};

struct MissingTarget {
    auto deref() const noexcept -> int { return 0; }
};

} // namespace

namespace deref_test
{

struct ExternalPayload {
    int value;
};

struct ExternalRefLike {
    USE_TRAIT(ExternalRefLike)

    ExternalPayload* value;
};

} // namespace deref_test

namespace rstd
{

template<>
struct Impl<ops::Deref, ::RefLike> : ImplBase<::RefLike> {
    using Target = ::Payload;

    auto deref() const noexcept -> ref<Target> { return self().deref(); }
};

template<>
struct Impl<ops::DerefMut, ::RefLike> : ImplBase<::RefLike> {
    auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<::RefLike>> {
        return self().deref_mut();
    }
};

template<>
struct Impl<ops::Deref, deref_test::ExternalRefLike> : ImplBase<deref_test::ExternalRefLike> {
    using Target = deref_test::ExternalPayload;

    auto deref() const noexcept -> ref<Target> { return ref<Target>::from_raw_parts(self().value); }
};

template<>
struct Impl<ops::DerefMut, deref_test::ExternalRefLike> : ImplBase<deref_test::ExternalRefLike> {
    auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<deref_test::ExternalRefLike>> {
        return mut_ref<ops::deref_target_t<deref_test::ExternalRefLike>>::from_raw_parts(
            self().value);
    }
};

} // namespace rstd

template<typename T>
concept HasClassDerefTarget = requires { typename T::Target; };

static_assert(! HasClassDerefTarget<RefLike>);
static_assert(! HasClassDerefTarget<deref_test::ExternalRefLike>);
static_assert(! HasClassDerefTarget<rstd::ref<Payload>>);
static_assert(! HasClassDerefTarget<rstd::mut_ref<Payload>>);
static_assert(! HasClassDerefTarget<rstd::array<int, 3>>);
static_assert(! HasClassDerefTarget<::alloc::boxed::Box<int>>);
static_assert(! HasClassDerefTarget<::alloc::vec::Vec<int>>);
static_assert(! HasClassDerefTarget<::alloc::sync::Arc<int>>);
static_assert(! HasClassDerefTarget<::alloc::rc::Rc<int>>);
static_assert(! HasClassDerefTarget<::alloc::string::String>);
static_assert(! HasClassDerefTarget<rstd::sync::MutexGuard<int>>);

static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<rstd::ref<Payload>>, Payload>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<rstd::mut_ref<Payload>>, Payload>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<rstd::array<int, 3>>, int[]>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<::alloc::boxed::Box<int>>, int>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<::alloc::vec::Vec<int>>, int[]>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<::alloc::sync::Arc<int>>, int>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<::alloc::rc::Rc<int>>, int>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<::alloc::string::String>, rstd::str>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<rstd::sync::MutexGuard<int>>, int>);

static_assert(rstd::Impled<RefLike, rstd::ops::Deref>);
static_assert(rstd::Impled<RefLike, rstd::ops::DerefMut>);
static_assert(! rstd::Impled<SameNamedOnly, rstd::ops::Deref>);
static_assert(! rstd::Impled<MissingTarget, rstd::ops::Deref>);
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
static_assert(rstd::Impled<rstd::mut_ref<rstd::dyn<rstd::FnMut<void()>>>, rstd::ops::DerefMut>);
static_assert(rstd::Impled<rstd::sync::MutexGuard<int>, rstd::ops::DerefMut>);
static_assert(rstd::Impled<deref_test::ExternalRefLike, rstd::ops::Deref>);
static_assert(rstd::Impled<deref_test::ExternalRefLike, rstd::ops::DerefMut>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<deref_test::ExternalRefLike>,
                                 deref_test::ExternalPayload>);

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

TEST(Deref, ExternalImplOwnsAssociatedTarget) {
    deref_test::ExternalPayload value { 23 };
    deref_test::ExternalRefLike ref_like { .value = &value };

    EXPECT_EQ(ref_like->value, 23);
    ref_like->value = 29;
    EXPECT_EQ(value.value, 29);
}

TEST(Deref, ReferenceWrappersPreserveConstness) {
    Payload value { 11 };
    auto    immutable = rstd::ref<Payload>::from_raw_parts(&value);
    auto    mutable_  = rstd::mut_ref<Payload>::from_raw_parts(&value);

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
    auto    member = rstd::Some(rstd::ref<Payload>::from_raw_parts(&value));

    ASSERT_TRUE(member.is_some());
    EXPECT_EQ((*member)->value, 17);
}

TEST(Deref, UnsizedTraitProjectionKeepsMetadata) {
    int  values[] { 2, 3, 5 };
    auto slice = rstd::ref<int[]>::from_raw_parts(values, rstd::usize(3));

    auto projected = slice.deref();
    EXPECT_EQ(projected.as_raw_ptr(), values);
    EXPECT_EQ(projected.len(), rstd::usize(3));
}

TEST(Deref, SliceReportsWhetherEmpty) {
    int  values[] { 2, 3, 5 };
    auto populated = rstd::slice<int>::from_raw_parts(values, rstd::usize(3));
    auto empty     = rstd::slice<int>::from_raw_parts(values, rstd::usize());
    auto mutable_  = rstd::mut_ref<int[]>::from_raw_parts(values, rstd::usize());

    EXPECT_FALSE(populated.is_empty());
    EXPECT_TRUE(empty.is_empty());
    EXPECT_TRUE(mutable_.is_empty());
}
