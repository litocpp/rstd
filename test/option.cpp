#include <gtest/gtest.h>
#include <memory>
import rstd.core;
using namespace rstd;

static_assert(mtp::same_as<decltype(Some(Some<int>(1))), Option<Option<int>>>);
static_assert(mtp::same_as<decltype(Some(None<int>())), Option<Option<int>>>);
static_assert(mtp::same_as<decltype(None(None<int>())), Option<Option<int>>>);
static_assert(sizeof(Option<int&>) == sizeof(int*));
static_assert(sizeof(Option<num::nonzero::NonZero<u64>>) == sizeof(u64));
static_assert(sizeof(Option<ptr_::non_null::NonNull<int>>) == sizeof(ptr_::non_null::NonNull<int>));

namespace
{

struct DropCounter {
    int* drops;

    explicit DropCounter(int& count): drops(&count) {}
    DropCounter(const DropCounter&)            = delete;
    DropCounter& operator=(const DropCounter&) = delete;
    DropCounter(DropCounter&& other) noexcept: drops(other.drops) { other.drops = nullptr; }
    DropCounter& operator=(DropCounter&& other) noexcept {
        drops       = other.drops;
        other.drops = nullptr;
        return *this;
    }
    ~DropCounter() {
        if (drops != nullptr) {
            ++*drops;
        }
    }
};

} // namespace

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
    static_assert(mtp::same_as<decltype(ref), option::Option<int&>>);
    static_assert(mtp::same_as<decltype(Some(value)), option::Option<int>>);
    {
        std::unique_ptr<int> ptr;
        static_assert(mtp::same_as<decltype(Some(ptr)), option::Option<std::unique_ptr<int>&>>);
        static_assert(
            mtp::same_as<decltype(Some(std::move(ptr))), option::Option<std::unique_ptr<int>>>);
    }
    EXPECT_TRUE(ref.is_some());
    EXPECT_EQ(*ref, 42);

    value = 100;
    EXPECT_EQ(*ref, 100);

    const auto& cref = ref;
    static_assert(mtp::same_as<decltype(*cref), const int&>);
    EXPECT_EQ(*cref, 100);
}

struct CloneType : DefaultInClass<CloneType, clone::Clone> {
    int value;
    explicit CloneType(int v): value(v) {}
    auto clone() const -> CloneType { return CloneType { value }; }
};

template<>
struct rstd::Impl<clone::Clone, CloneType> : LinkClassRequiredWithDefault<clone::Clone, CloneType> {
};

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

TEST(Option, AsRef) {
    Option<std::unique_ptr<int>> up     = Some(std::make_unique<int>(3));
    auto                         up_ref = up.as_ref();

    EXPECT_EQ(*up_ref.unwrap(), 3);
    // Verify reference doesn't destroy original
    EXPECT_EQ(*up_ref.unwrap(), 3);
}

TEST(Option, AndThen) {
    auto some   = Some(42);
    auto mapped = some.and_then([](int x) {
        return Some(x * 2.0);
    });
    EXPECT_TRUE(mapped.is_some());
    EXPECT_EQ(*mapped, 84.0);
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

TEST(Option, TakeLeavesNone) {
    auto value = Some<std::unique_ptr<int>>(std::make_unique<int>(7));
    auto taken = value.take();

    EXPECT_TRUE(value.is_none());
    ASSERT_TRUE(taken.is_some());
    EXPECT_EQ(**taken, 7);
}

TEST(Option, NonZeroNicheStorage) {
    using NonZeroU64 = num::nonzero::NonZero<u64>;

    auto value = Some(NonZeroU64::make_unchecked(11));
    ASSERT_TRUE(value.is_some());
    EXPECT_EQ(value.unwrap().get(), 11u);

    value = None<NonZeroU64>();
    EXPECT_TRUE(value.is_none());

    value      = Some(NonZeroU64::make_unchecked(12));
    auto taken = value.take();
    EXPECT_TRUE(value.is_none());
    ASSERT_TRUE(taken.is_some());
    EXPECT_EQ(taken.unwrap().get(), 12u);
}

TEST(Option, SelfMoveAssignmentKeepsValue) {
    auto value = Some<std::unique_ptr<int>>(std::make_unique<int>(9));
    auto same  = &value;
    value      = std::move(*same);

    ASSERT_TRUE(value.is_some());
    ASSERT_NE(*value, nullptr);
    EXPECT_EQ(**value, 9);
}

TEST(Option, DropsNonTrivialValue) {
    int drops = 0;
    {
        auto value = Some(DropCounter { drops });
        EXPECT_TRUE(value.is_some());
    }
    EXPECT_EQ(drops, 1);
}

TEST(Option, ExpectUnwrap) {
    auto some = Some(42);
    EXPECT_EQ(some.unwrap(), 42);
    EXPECT_EQ(some.expect("shouldn't fail"), 42);

    auto none = None<int>();
    EXPECT_DEATH(none.unwrap(), "");
    // EXPECT_DEATH(none.expect("failed"), "failed");
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
