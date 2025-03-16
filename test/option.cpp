#include <gtest/gtest.h>
import rstd.core;
using namespace rstd;

static_assert(meta::same_as<decltype(Some(Some<int>(1))), Option<Option<int>>>);
static_assert(meta::same_as<decltype(Some(None<int>())), Option<Option<int>>>);
static_assert(meta::same_as<decltype(None(None<int>())), Option<Option<int>>>);

TEST(Option, Basic) {
    Option<int> none;
    EXPECT_TRUE(none.is_none());
    EXPECT_FALSE(none.is_some());

    auto some = Some<int>(42);
    EXPECT_FALSE(some.is_none());
    EXPECT_TRUE(some.is_some());
    EXPECT_EQ(*some, 42);
}

TEST(Option, Reference) {
    int  value = 42;
    auto ref   = Some<int&>(value);
    static_assert(meta::same_as<decltype(ref), option::Option<int&>>);
    static_assert(meta::same_as<decltype(Some(value)), option::Option<int>>);
    {
        std::unique_ptr<int> ptr;
        static_assert(meta::same_as<decltype(Some(ptr)), option::Option<std::unique_ptr<int>&>>);
        static_assert(
            meta::same_as<decltype(Some(std::move(ptr))), option::Option<std::unique_ptr<int>>>);
    }
    EXPECT_TRUE(ref.is_some());
    EXPECT_EQ(*ref, 42);

    value = 100;
    EXPECT_EQ(*ref, 100);
}

struct CloneType;
template<>
struct rstd::Impl<clone::Clone, CloneType> {
    static auto clone(TraitPtr self) -> CloneType;
};

struct CloneType : WithTrait<CloneType, clone::Clone> {
    int value;
    explicit CloneType(int v): value(v) {}
};

auto rstd::Impl<clone::Clone, CloneType>::clone(TraitPtr self) -> CloneType {
    return CloneType { self.as_ref<CloneType>().value };
}

TEST(Option, Clone) {
    auto original = Some(CloneType { 42 });
    auto cloned   = original.clone();
    EXPECT_TRUE(cloned.is_some());
    EXPECT_EQ((*cloned).value, 42);
}

TEST(Option, Map) {
    auto some   = Some(42);
    auto mapped = some.map([](int x) {
        return x * 2;
    });
    EXPECT_TRUE(mapped.is_some());
    EXPECT_EQ(*mapped, 84);

    Option<int> none;
    auto        mapped_none = none.map([](int x) {
        return x * 2;
    });
    EXPECT_TRUE(mapped_none.is_none());
}

TEST(Option, UnwrapOr) {
    auto some = Some(42);
    EXPECT_EQ(some.unwrap_or(0), 42);

    auto none = None<int>();
    EXPECT_EQ(none.unwrap_or(0), 0);
}

TEST(Option, UnwrapOrElse) {
    auto some = Some(42);
    EXPECT_EQ(some.unwrap_or_else([]() {
        return 0;
    }),
              42);

    auto none = None<int>();
    EXPECT_EQ(none.unwrap_or_else([]() {
        return 0;
    }),
              0);
}

TEST(Option, IsSomeAnd) {
    auto some = Some(42);
    EXPECT_TRUE(some.is_some_and([](int x) {
        return x > 40;
    }));
    EXPECT_FALSE(some.is_some_and([](int x) {
        return x < 40;
    }));

    auto none = None<int>();
    EXPECT_FALSE(none.is_some_and([](int x) {
        return x > 40;
    }));
}

TEST(Option, MoveSemantics) {
    auto some = Some<std::unique_ptr<int>>(std::make_unique<int>(42));
    EXPECT_TRUE(some.is_some());

    auto moved = std::move(some);
    EXPECT_TRUE(moved.is_some());
    EXPECT_EQ(**moved, 42);
}

TEST(Option, ExpectUnwrap) {
    auto some = Some(42);
    EXPECT_EQ(some.unwrap(), 42);
    EXPECT_EQ(some.expect("shouldn't fail"), 42);

    auto none = None<int>();
    EXPECT_DEATH(none.unwrap(), "");
    EXPECT_DEATH(none.expect("failed"), "failed");
}

TEST(Option, OkOr) {
    auto some = Some(42);
    auto none = None<int>();
    EXPECT_EQ(some.ok_or(1), Ok(42));
    EXPECT_EQ(none.ok_or(1), Err(1));

    EXPECT_EQ(none.ok_or_else([] {
        return 1;
    }),
              Err(1));
}

TEST(Option, Transpose) {
    auto opt = Some(Ok(42));
    EXPECT_EQ(opt.transpose(), Ok(Some(42)));
}

TEST(Option, Flatten) {
    auto opt = Some(Some(42));
    EXPECT_EQ(opt.flatten(), Some(42));
}