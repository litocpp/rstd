#include <gtest/gtest.h>
#include <memory>

import rstd;
using namespace rstd;

TEST(ResultTest, Test) {
    Result<int, float>                  x  = Ok(3);
    Result<std::unique_ptr<int>, float> up = Ok(std::make_unique<int>(3));

    auto m = x.clone();
    auto n = x.clone();

    EXPECT_EQ(x.unwrap(), 3);
    EXPECT_EQ(m.unwrap(), 3);
    EXPECT_EQ(*up.unwrap(), 3);

    // moved, up is now nullptr
    EXPECT_EQ(up.unwrap().get(), nullptr);

    up = Ok(std::make_unique<int>(3));
    auto up_ref = up.as_ref();

    EXPECT_EQ(n.map([](auto t) -> int {
        return t + 100;
    }),
              Ok(103));
}