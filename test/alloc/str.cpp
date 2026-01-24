#include <gtest/gtest.h>
#include <charconv>
#include <string>

import rstd;

using namespace rstd;
using rstd::str;
namespace rstd
{
template<>
struct Impl<str_::FromStr, int> {
    using Err  = ref<str>;
    using Self = int;
    static auto from_str(ref<str> str_) -> Result<Self, Err> {
        int  num       = 0;
        auto p         = as_cast<const char*>(str_.data());
        auto [ptr, ec] = std::from_chars(p, p + str_.size(), num);
        if (ec == std::errc()) {
            return Ok(num);
        } else {
            return rstd::Err(ref<str> { "failed" });
        }
    }
};
} // namespace rstd

static auto call(ref<str> s) { return s[1]; }

TEST(Str, FromStr) {
    std::string name = "name";

    EXPECT_EQ(from_str<int>("asdlkf").unwrap_err(), "failed");
    EXPECT_EQ(from_str<int>(name).unwrap_err(), "failed");
    EXPECT_EQ(call(std::move(name)), 'a');
    EXPECT_EQ(10, from_str<int>("10").unwrap());
}
