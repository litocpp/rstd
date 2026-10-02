#include <rstd/test/gtest.hpp>
#include <string>

import rstd.json;

using namespace rstd;
using namespace rstd::json;
using namespace rstd::literals;

namespace
{

auto text(const ::alloc::string::String& value) -> std::string {
    return { reinterpret_cast<const char*>(value.as_raw_ptr()), value.len().to_primitive() };
}

} // namespace

TEST(JsonSerialize, WritesCompactSortedJson) {
    auto value = from_str(R"({"z":[true,null,"line\n"],"a":1.0})"_str).unwrap();
    EXPECT_EQ(text(to_string(value)), R"({"a":1.0,"z":[true,null,"line\n"]})");
}

TEST(JsonSerialize, WritesPrettyJsonWithRequestedIndent) {
    auto value  = from_str(R"({"z":[true,null],"a":1})"_str).unwrap();
    auto output = to_string(value, FormatOptions { .pretty = true, .indent = usize(4) });
    EXPECT_EQ(text(output),
              "{\n"
              "    \"a\": 1,\n"
              "    \"z\": [\n"
              "        true,\n"
              "        null\n"
              "    ]\n"
              "}");
}

TEST(JsonSerialize, EscapesStringsAndPreservesUtf8) {
    auto value =
        from_str(
            R"({"text":"quote:\" slash:/ backslash:\\ controls:\b\f\n\r\t\u0001 utf8:雪"})"_str)
            .unwrap();
    EXPECT_EQ(text(to_string(value)),
              R"({"text":"quote:\" slash:/ backslash:\\ controls:\b\f\n\r\t\u0001 utf8:雪"})");
}

TEST(JsonSerialize, FormatsNumberKindsAndRoundTrips) {
    auto value =
        from_str(R"([0,-1,18446744073709551615,-0,1.0,1.25,5e-324,1.7976931348623157e308])"_str)
            .unwrap();
    auto output = to_string(value);
    EXPECT_EQ(text(output),
              R"([0,-1,18446744073709551615,-0.0,1.0,1.25,5e-324,1.7976931348623157e+308])");
    EXPECT_EQ(from_str(output.as_str()).unwrap(), value);
}

TEST(JsonSerialize, DisplayUsesTheSameEmitter) {
    auto value = from_str(R"({"a":[1]})"_str).unwrap();
    EXPECT_EQ(text(rstd::format("{}", value)), R"({"a":[1]})");
    EXPECT_EQ(text(rstd::format("{:#}", value)), "{\n  \"a\": [\n    1\n  ]\n}");
}

TEST(JsonSerialize, QuotesUtf16AndRoundTripsEveryCodeUnit) {
    rstd::array<u16, 9> units { u16(0xd800), u16('x'), u16(0xdc00), u16(0xd83d), u16(0xde00),
                                u16('\n'),   u16(),    u16('"'),    u16('\\') };
    auto                quoted = quote_string(units.as_slice());
    const auto          prefix = "\"\\ud800x\\udc00"_str;
    for (usize index {}; index < prefix.size(); ++index)
        EXPECT_EQ(quoted[index], rstd::as_cast<u16>(prefix[index]));
    EXPECT_EQ(quoted[usize(14)], u16(0xd83d));
    EXPECT_EQ(quoted[usize(15)], u16(0xde00));
    rstd::json::Reader reader(quoted.as_slice());
    auto               decoded = reader.parse_string_units().unwrap();
    ASSERT_EQ(decoded.len(), units.as_slice().len());
    for (usize index {}; index < decoded.len(); ++index) EXPECT_EQ(decoded[index], units[index]);
    EXPECT_TRUE(reader.finish().is_ok());
    rstd::array all = rstd::array<u16, 65536> {};
    for (usize index {}; index < all.as_slice().len(); ++index)
        all[index] = rstd::as_cast<u16>(index);
    auto               exhaustive = quote_string(all.as_slice());
    rstd::json::Reader exhaustive_reader(exhaustive.as_slice());
    auto               roundtrip = exhaustive_reader.parse_string_units().unwrap();
    ASSERT_EQ(roundtrip.len(), all.as_slice().len());
    for (usize index {}; index < roundtrip.len(); ++index) ASSERT_EQ(roundtrip[index], all[index]);
}
