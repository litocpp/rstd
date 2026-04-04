#include <gtest/gtest.h>

import rstd;

using rstd::thread::builder::Builder;

TEST(Thread, Basic) {
    rstd::i32 x { 0 };

    auto handle = Builder::make().spawn([&] {
        x = 1;
    });
    handle.unwrap().join().unwrap();

    EXPECT_EQ(x, 1);
}