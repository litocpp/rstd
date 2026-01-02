#include <gtest/gtest.h>
#include <charconv>
#include <string>

import rstd;

using namespace rstd;

template<>
struct Impl<str_::FromStr, int> {
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

static auto call(ref_str s) { return s[1]; }

TEST(Str, FromStr) {
    std::string name = "name";
    ref_str     x    = name;

    EXPECT_EQ(from_str<int>("asdlkf").unwrap_err(), "failed");
    EXPECT_EQ(from_str<int>(name).unwrap_err(), "failed");
    EXPECT_EQ(call(std::move(name)), 'a');
    EXPECT_EQ(10, from_str<int>("10").unwrap());
}
