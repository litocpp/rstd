#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd;
using namespace rstd::literals;
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
        if (str_ == "10"_str) return Ok(ParsedNumber { 10 });
        return rstd::Err("failed"_str);
    }
};
} // namespace rstd

TEST(Str, FromStr) {
    EXPECT_EQ(from_str<ParsedNumber>("asdlkf"_str).unwrap_err(), "failed"_str);
    EXPECT_EQ(10, from_str<ParsedNumber>("10"_str).unwrap().value);
}
