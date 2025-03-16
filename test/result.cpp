#include <gtest/gtest.h>
#include <cstdio>
#include <memory>

import rstd;
using namespace rstd;

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