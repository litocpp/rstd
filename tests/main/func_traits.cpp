#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd;

template<typename T>
concept HasFunctionSignature = requires { typename mtp::func_traits<T>::signature; };

struct QualifiedCallable {
    auto operator()(int, bool) const& noexcept -> long;
};

struct OverloadedCallable {
    void operator()(int);
    void operator()(bool);
};

static_assert(mtp::func_traits<void()>::arity == 0);
static_assert(mtp::func_traits<int (*)(bool, long) noexcept>::is_noexcept);
static_assert(mtp::same_as<mtp::func_traits<int (*)(bool, long)>::argument<1>, long>);
static_assert(mtp::same_as<mtp::func_traits<QualifiedCallable>::signature, long(int, bool)>);
static_assert(mtp::same_as<mtp::func_traits<decltype(&QualifiedCallable::operator())>::owner,
                           QualifiedCallable>);
static_assert(! HasFunctionSignature<int>);
static_assert(! HasFunctionSignature<OverloadedCallable>);
static_assert(! HasFunctionSignature<decltype([](auto value) {
    return value;
})>);
static_assert(mtp::same_as<mtp::func_traits<int (*)(int, bool)>::to_dyn, int (*)(voidp, bool)>);
static_assert(mtp::same_as<mtp::func_traits<void (*)() noexcept>::to_dyn, void (*)() noexcept>);
static_assert(mtp::same_as<mtp::func_traits<void (*)(int*)>::primary, void>);
using MemberTraits = mtp::func_traits<decltype(&QualifiedCallable::operator())>;
static_assert(MemberTraits::is_member && MemberTraits::arity == 2);
static_assert(mtp::same_as<MemberTraits::primary, const QualifiedCallable&>);
static_assert(mtp::same_as<MemberTraits::to_dyn, long (*)(voidp, int, bool) noexcept>);
static_assert(! mtp::func_traits<QualifiedCallable>::is_member);
static_assert(mtp::func_traits<QualifiedCallable>::is_noexcept);

TEST(FuncTraits, DescribesCapturedAndMutableFunctions) {
    auto function = [owned = boxed::Box<int>::make(3)](bool value) mutable -> int {
        return value ? *owned : 0;
    };
    using Traits = mtp::func_traits<decltype(function)&>;
    static_assert(Traits::arity == 1);
    static_assert(mtp::same_as<Traits::ret, int>);
    static_assert(mtp::same_as<Traits::argument<0>, bool>);
    EXPECT_EQ(function(true), 3);
}
