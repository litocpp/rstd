#include <rstd/test/gtest.hpp>

import rstd;

using rstd::thread::builder::Builder;

TEST(Thread, Basic) {
    auto x = rstd::i32 {};

    auto handle = Builder::make().spawn([&] {
        x = rstd::i32(1);
    });
    handle.unwrap().join().unwrap();

    EXPECT_EQ(x, rstd::i32(1));
}
