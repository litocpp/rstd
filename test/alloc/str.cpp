#include <gtest/gtest.h>
#include <charconv>
#include <string>

import rstd;

using namespace rstd;

template<>
struct Impl<str::FromStr, int> {
    using Err  = ref_str;
    using Self = int;
    static auto from_str(ref_str str) -> Result<Self, Err> {
        int num        = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), num);
        if (ec == std::errc()) {
            return Ok(num);
        } else {
            return rstd::Err(ref_str { "failed" });
        }
    }
};

TEST(Str, FromStr) {
    std::string name = "ttt";
    auto        x    = name;

    EXPECT_EQ(from_str<int>("asdlkf").unwrap_err(), "failed");
    EXPECT_EQ(10, from_str<int>("10").unwrap());
}
