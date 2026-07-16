#include <gtest/gtest.h>

import rstd;

using namespace rstd;
using rstd::str;

struct ParsedNumber {
    int value;
};

namespace rstd
{
template<>
struct Impl<str_::FromStr, ParsedNumber> {
    using Err  = ref<str>;
    using Self = ParsedNumber;
    static auto from_str(ref<str> str_) -> Result<Self, Err> {
        if (str_ == "10") return Ok(ParsedNumber { 10 });
        return rstd::Err(ref<str> { "failed" });
    }
};
} // namespace rstd

TEST(Str, FromStr) {
    EXPECT_EQ(from_str<ParsedNumber>("asdlkf").unwrap_err(), "failed");
    EXPECT_EQ(10, from_str<ParsedNumber>("10").unwrap().value);
}
