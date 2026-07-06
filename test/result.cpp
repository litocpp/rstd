#include <gtest/gtest.h>
#include <cstdio>
#include <memory>

import rstd;
using namespace rstd;

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

struct InClassOnlyClone : DefaultInClass<InClassOnlyClone, clone::Clone> {
    int value;

    explicit InClassOnlyClone(int v): value(v) {}
    InClassOnlyClone(const InClassOnlyClone&)            = delete;
    InClassOnlyClone& operator=(const InClassOnlyClone&) = delete;
    InClassOnlyClone(InClassOnlyClone&&)                 = default;
    InClassOnlyClone& operator=(InClassOnlyClone&&)      = default;

    auto clone() const -> InClassOnlyClone { return InClassOnlyClone { value }; }
};

static_assert(Impled<InClassOnlyClone, clone::Clone>);

} // namespace

TEST(Result, BasicOperations) {
    Result<int, float> x = Ok(3);
    EXPECT_TRUE(x.is_ok());
    EXPECT_FALSE(x.is_err());
    EXPECT_EQ(x.unwrap(), 3);
}

TEST(Result, CloneOperations) {
    Result<int, float> x = Ok(3);
    auto               m = x.clone();
    auto               n = x.clone();

    EXPECT_EQ(x.unwrap(), 3);
    EXPECT_EQ(m.unwrap(), 3);
    EXPECT_EQ(n.unwrap(), 3);

    Result<InClassOnlyClone, float> in_class = Ok(InClassOnlyClone { 9 });
    auto                            cloned   = in_class.clone();
    ASSERT_TRUE(cloned.is_ok());
    EXPECT_EQ(cloned.unwrap().value, 9);
}

TEST(Result, CloneFromOperations) {
    Result<int, float> ok  = Ok(7);
    Result<int, float> err = Err(1.5f);

    err.clone_from(ok);
    ASSERT_TRUE(err.is_ok());
    EXPECT_EQ(err.unwrap(), 7);

    Result<int, float> next_err = Err(2.5f);
    ok.clone_from(next_err);
    ASSERT_TRUE(ok.is_err());
    EXPECT_EQ(ok.unwrap_err(), 2.5f);
}

TEST(Result, UniquePtr) {
    Result<std::unique_ptr<int>, float> up = Ok(std::make_unique<int>(3));
    EXPECT_EQ(*up.unwrap(), 3);

    // Test move semantics
    EXPECT_EQ(up.unwrap().get(), nullptr);
}

TEST(Result, Reference) {
    Result<std::unique_ptr<int>, float> up     = Ok(std::make_unique<int>(3));
    auto                                up_ref = up.as_ref();

    EXPECT_EQ(*up_ref.unwrap(), 3);
    // Verify reference doesn't destroy original
    EXPECT_EQ(*up_ref.unwrap(), 3);
}

TEST(Result, MapOperations) {
    Result<std::unique_ptr<int>, float> up     = Ok(std::make_unique<int>(3));
    auto                                up_ref = up.as_ref();

    // Test map
    EXPECT_EQ(up_ref.map([](auto& t) -> int {
        return *t + 100;
    }),
              Ok(103));

    // Test map_or
    EXPECT_EQ(up_ref.map_or(4,
                            [](auto& t) -> int {
                                return *t + 100;
                            }),
              103);
}

TEST(Result, CloneAfterRef) {
    Result<std::unique_ptr<int>, float> up           = Ok(std::make_unique<int>(3));
    auto                                up_ref       = up.as_ref();
    auto                                up_ref_clone = up_ref.clone();
    EXPECT_EQ(*up_ref_clone.unwrap(), 3);
}

TEST(Result, MapWithMove) {
    Result<int, float> n = Ok(3);
    EXPECT_EQ(n.map([](auto t) -> int {
        return t + 100;
    }),
              Ok(103));
}

TEST(Result, ErrorCase) {
    Result<int, std::string> err = Err("error message");
    EXPECT_TRUE(err.is_err());
    EXPECT_FALSE(err.is_ok());
}

TEST(Result, MapOrWithError) {
    Result<int, std::string> err = Err("error message");
    EXPECT_EQ(err.map_or(42,
                         [](auto t) {
                             return t;
                         }),
              42);
}

TEST(Result, ReferenceValue) {
    int                       value = 3;
    Result<int&, std::string> ref   = Ok<int&>(value);

    ASSERT_TRUE(ref.is_ok());
    EXPECT_EQ(*ref, 3);

    value = 9;
    EXPECT_EQ(ref.unwrap(), 9);
}

TEST(Result, MoveAssignmentChangesVariantAndDropsOldPayload) {
    int drops = 0;
    {
        Result<DropCounter, DropCounter> value = Ok(DropCounter { drops });
        EXPECT_TRUE(value.is_ok());

        value = Result<DropCounter, DropCounter>(Err(DropCounter { drops }));
        EXPECT_TRUE(value.is_err());
        EXPECT_EQ(drops, 1);
    }
    EXPECT_EQ(drops, 2);
}
