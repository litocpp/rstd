#include <rstd/test/gtest.hpp>
#include <string>
#include <vector>

import rstd.json;

using namespace rstd;
using namespace rstd::json;
using namespace rstd::literals;

namespace
{

auto text(const ::alloc::string::String& value) -> std::string {
    return { reinterpret_cast<const char*>(value.as_raw_ptr()), value.len().to_primitive() };
}

struct StringWriter {
    std::string              output;
    std::vector<std::string> chunks;
    rstd::size_t             reject_on {};

    static auto write(void* context, const rstd::uint8_t* bytes, rstd::size_t length) -> bool {
        auto& self = *static_cast<StringWriter*>(context);
        self.chunks.emplace_back(reinterpret_cast<const char*>(bytes), length);
        if (self.chunks.size() == self.reject_on) return false;
        self.output.append(self.chunks.back());
        return true;
    }
};

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

TEST(JsonSerialize, QuotesEveryControlCharacterWithoutChangingUnicode) {
    auto bytes = ::alloc::vec::Vec<u8>::make();
    for (usize index {}; index < usize(32); ++index) bytes.push(rstd::as_cast<u8>(index));
    auto input = ::alloc::string::String::from_utf8_unchecked(rstd::move(bytes));
    input.push_str("\"\\/\x7f \xc2\x80\xc2\x85\xe2\x80\xa8\xe2\x80\xa9\xf0\x9f\x98\x80"_str);
    const std::string expected =
        "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
        "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
        "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
        "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f"
        "\\\"\\\\/\x7f \xc2\x80\xc2\x85\xe2\x80\xa8\xe2\x80\xa9\xf0\x9f\x98\x80\"";

    StringWriter   writer;
    fmt::Formatter formatter(&writer, StringWriter::write);
    ASSERT_TRUE(write_quoted_string(formatter, input.as_str()));
    EXPECT_EQ(writer.output, expected);
    EXPECT_EQ(text(quote_string(input.as_str())), expected);
    EXPECT_EQ(text(encode_direct(input).unwrap()), expected);
    EXPECT_EQ(text(encode(input).unwrap()), expected);
    auto value = to_value(input).unwrap();
    EXPECT_EQ(text(to_string(value)), expected);
    EXPECT_EQ(text(rstd::format("{}", value)), expected);
    EXPECT_EQ(from_str(quote_string(input.as_str()).as_str()).unwrap(), value);
}

TEST(JsonSerialize, WritesEmptyStringsAndBatchesLongOrdinarySpans) {
    StringWriter   empty_writer;
    fmt::Formatter empty_formatter(&empty_writer, StringWriter::write);
    ASSERT_TRUE(write_quoted_string(empty_formatter, ""_str));
    EXPECT_EQ(empty_writer.output, "\"\"");
    EXPECT_EQ(empty_writer.chunks.size(), rstd::size_t(2));
    EXPECT_EQ(text(quote_string(""_str)), "\"\"");
    auto empty_units = quote_string(slice<u16>());
    ASSERT_EQ(empty_units.len(), usize(2));
    EXPECT_EQ(empty_units[usize()], u16('"'));
    EXPECT_EQ(empty_units[usize(1)], u16('"'));

    auto input = ::alloc::string::String::make();
    for (usize index {}; index < usize(16384); ++index) input.push_str("plain"_str);
    StringWriter   plain_writer;
    fmt::Formatter plain_formatter(&plain_writer, StringWriter::write);
    ASSERT_TRUE(encode_into(plain_formatter, input).is_ok());
    ASSERT_EQ(plain_writer.chunks.size(), rstd::size_t(3));
    EXPECT_EQ(plain_writer.chunks[1], text(input));
    EXPECT_EQ(plain_writer.output, "\"" + text(input) + "\"");

    input.push_str("\n"_str);
    input.push_str("tail\xe9\x9b\xaa"_str);
    StringWriter   escaped_writer;
    fmt::Formatter escaped_formatter(&escaped_writer, StringWriter::write);
    ASSERT_TRUE(write_quoted_string(escaped_formatter, input.as_str()));
    ASSERT_EQ(escaped_writer.chunks.size(), rstd::size_t(5));
    EXPECT_EQ(escaped_writer.chunks[1], plain_writer.chunks[1]);
    EXPECT_EQ(escaped_writer.chunks[2], "\\n");
    EXPECT_EQ(escaped_writer.chunks[3], "tail\xe9\x9b\xaa");
    EXPECT_EQ(escaped_writer.output, text(quote_string(input.as_str())));
    EXPECT_EQ(text(encode_direct(input).unwrap()), escaped_writer.output);
}

TEST(JsonSerialize, StopsImmediatelyAtEveryRejectedStringChunk) {
    const auto     input = "head\xe9\x9b\xaa\n\x01\"\\tail\xf0\x9f\x98\x80"_str;
    StringWriter   accepted;
    fmt::Formatter formatter(&accepted, StringWriter::write);
    ASSERT_TRUE(write_quoted_string(formatter, input));
    ASSERT_EQ(accepted.chunks.size(), rstd::size_t(8));
    auto value = to_value(input).unwrap();
    for (rstd::size_t rejected = 1; rejected <= accepted.chunks.size(); ++rejected) {
        StringWriter writer;
        writer.reject_on = rejected;
        fmt::Formatter target(&writer, StringWriter::write);
        EXPECT_FALSE(write_quoted_string(target, input));
        EXPECT_EQ(writer.chunks.size(), rejected);
        EXPECT_EQ(writer.output, accepted.output.substr(0, writer.output.size()));

        StringWriter direct_writer;
        direct_writer.reject_on = rejected;
        fmt::Formatter direct_target(&direct_writer, StringWriter::write);
        EXPECT_TRUE(encode_into(direct_target, input).is_err());
        EXPECT_EQ(direct_writer.chunks.size(), rejected);
        EXPECT_EQ(direct_writer.output, writer.output);

        StringWriter display_writer;
        display_writer.reject_on = rejected;
        fmt::Formatter display_target(&display_writer, StringWriter::write);
        EXPECT_FALSE(display_target.write_fmt(fmt::Arguments::make("{}", value)));
        EXPECT_EQ(display_writer.chunks.size(), rejected);
        EXPECT_EQ(display_writer.output, writer.output);
    }
}

TEST(JsonSerialize, StopsImmediatelyInObjectKeysAndNestedValues) {
    auto object = Map::make();
    object.insert(::alloc::string::String::make("key\n\x01"_str),
                  to_value("value\t\"\\"_str).unwrap());
    auto value = Value::Object(rstd::move(object));
    for (auto options : { FormatOptions {}, FormatOptions { .pretty = true } }) {
        StringWriter   accepted;
        fmt::Formatter formatter(&accepted, StringWriter::write);
        ASSERT_TRUE(encode_into(formatter, value, options).is_ok());
        EXPECT_EQ(accepted.output, text(to_string(value, options)));
        EXPECT_EQ(text(encode_direct(value, options).unwrap()), accepted.output);
        EXPECT_EQ(accepted.output,
                  options.pretty ? "{\n  \"key\\n\\u0001\": \"value\\t\\\"\\\\\"\n}"
                                 : "{\"key\\n\\u0001\":\"value\\t\\\"\\\\\"}");
        for (rstd::size_t rejected = 1; rejected <= accepted.chunks.size(); ++rejected) {
            StringWriter writer;
            writer.reject_on = rejected;
            fmt::Formatter target(&writer, StringWriter::write);
            EXPECT_TRUE(encode_into(target, value, options).is_err());
            EXPECT_EQ(writer.chunks.size(), rejected);
            EXPECT_EQ(writer.output, accepted.output.substr(0, writer.output.size()));

            StringWriter display_writer;
            display_writer.reject_on = rejected;
            fmt::Formatter display_target(&display_writer, StringWriter::write);
            EXPECT_FALSE(options.pretty
                             ? display_target.write_fmt(fmt::Arguments::make("{:#}", value))
                             : display_target.write_fmt(fmt::Arguments::make("{}", value)));
            EXPECT_EQ(display_writer.chunks.size(), rejected);
            EXPECT_EQ(display_writer.output, writer.output);
        }
    }
}

TEST(JsonSerialize, QuotesUtf16SurrogateBoundariesWithoutNormalizing) {
    rstd::array<u16, 14> units { u16(0xd800), u16(0xd800), u16(0xdc00), u16(0xdfff), u16(0xdbff),
                                 u16(0xdfff), u16(0xdc00), u16('a'),    u16(0xdbff), u16(0xe000),
                                 u16(0xd7ff), u16('"'),    u16('\\'),   u16(0xd800) };
    const std::u16string expected =
        u"\"\\ud800\U00010000\\udfff\U0010ffff\\udc00a\\udbff\ue000\ud7ff\\\"\\\\\\ud800\"";
    auto quoted = quote_string(units.as_slice());
    ASSERT_EQ(quoted.len().to_primitive(), expected.size());
    for (usize index {}; index < quoted.len(); ++index)
        EXPECT_EQ(quoted[index].to_primitive(), expected[index.to_primitive()]);
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
