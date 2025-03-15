#include <gtest/gtest.h>
import rstd.core;
using namespace rstd;

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
    int          value = 42;
    Option<int&> ref(value);
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
    Option<CloneType> original(CloneType { 42 });
    auto              cloned = original.clone();
    EXPECT_TRUE(cloned.is_some());
    EXPECT_EQ((*cloned).value, 42);
}

TEST(Option, Map) {
    Option<int> some(42);
    auto        mapped = some.map([](int x) {
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
    Option<int> some(42);
    EXPECT_EQ(some.unwrap_or(0), 42);

    Option<int> none;
    EXPECT_EQ(none.unwrap_or(0), 0);
}

TEST(Option, UnwrapOrElse) {
    Option<int> some(42);
    EXPECT_EQ(some.unwrap_or_else([]() {
        return 0;
    }),
              42);

    Option<int> none;
    EXPECT_EQ(none.unwrap_or_else([]() {
        return 0;
    }),
              0);
}

TEST(Option, IsSomeAnd) {
    Option<int> some(42);
    EXPECT_TRUE(some.is_some_and([](int x) {
        return x > 40;
    }));
    EXPECT_FALSE(some.is_some_and([](int x) {
        return x < 40;
    }));

    Option<int> none;
    EXPECT_FALSE(none.is_some_and([](int x) {
        return x > 40;
    }));
}

TEST(Option, MoveSemantics) {
    Option<std::unique_ptr<int>> some(std::make_unique<int>(42));
    EXPECT_TRUE(some.is_some());

    auto moved = std::move(some);
    EXPECT_TRUE(moved.is_some());
    EXPECT_EQ(**moved, 42);
}

TEST(Option, ExpectUnwrap) {
    Option<int> some(42);
    EXPECT_EQ(some.unwrap(), 42);
    EXPECT_EQ(some.expect("shouldn't fail"), 42);

    Option<int> none;
    EXPECT_DEATH(none.unwrap(), "");
    EXPECT_DEATH(none.expect("failed"), "failed");
}
