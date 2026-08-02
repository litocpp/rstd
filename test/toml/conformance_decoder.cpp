#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
import rstd.test;
import rstd.toml;

using namespace rstd::literals;

namespace
{

using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;
using String    = rstd::string::String;
using Toml      = rstd::toml::Value;

auto string_json(rstd::ref<rstd::str> value) -> Json {
    return Json::String(String::make(value));
}

auto tagged(rstd::ref<rstd::str> type, String value) -> Json {
    auto result = JsonMap::make();
    result.insert(String::make("type"_str), string_json(type));
    result.insert(String::make("value"_str), Json::String(rstd::move(value)));
    return Json::Object(rstd::move(result));
}

void append_fixed(String& output, rstd::uint32_t value, rstd::size_t width) {
    char digits[9];
    for (rstd::size_t index = width; index > 0; --index) {
        digits[index - 1] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    output.push_str(rstd::ref<rstd::str>::from_raw_parts_unchecked(
        reinterpret_cast<const rstd::byte*>(digits), rstd::usize(width)));
}

auto date_string(rstd::toml::LocalDate value) -> String {
    auto output = String::make();
    append_fixed(output, value.year, 4);
    output.push_ascii(rstd::u8('-'));
    append_fixed(output, value.month, 2);
    output.push_ascii(rstd::u8('-'));
    append_fixed(output, value.day, 2);
    return output;
}

auto time_string(rstd::toml::LocalTime value) -> String {
    auto output = String::make();
    append_fixed(output, value.hour, 2);
    output.push_ascii(rstd::u8(':'));
    append_fixed(output, value.minute, 2);
    output.push_ascii(rstd::u8(':'));
    append_fixed(output, value.second, 2);
    if (value.nanosecond != rstd::uint32_t {}) {
        char fraction[9];
        auto remaining = value.nanosecond;
        for (rstd::size_t index = 9; index > 0; --index) {
            fraction[index - 1] = static_cast<char>('0' + remaining % 10);
            remaining /= 10;
        }
        rstd::size_t length = 9;
        while (length > 0 && fraction[length - 1] == '0') --length;
        output.push_ascii(rstd::u8('.'));
        output.push_str(rstd::ref<rstd::str>::from_raw_parts_unchecked(
            reinterpret_cast<const rstd::byte*>(fraction), rstd::usize(length)));
    }
    return output;
}

auto local_datetime_string(rstd::toml::LocalDateTime value) -> String {
    auto output = date_string(value.date);
    output.push_ascii(rstd::u8('T'));
    auto time = time_string(value.time);
    output.push_str(time.as_str());
    return output;
}

auto offset_datetime_string(rstd::toml::OffsetDateTime value) -> String {
    auto output = local_datetime_string(value.local);
    auto offset = value.offset_minutes;
    if (offset == 0) {
        output.push_ascii(rstd::u8('Z'));
        return output;
    }
    output.push_ascii(rstd::u8(offset < 0 ? '-' : '+'));
    const auto minutes = static_cast<rstd::uint32_t>(offset < 0 ? -offset : offset);
    append_fixed(output, minutes / 60, 2);
    output.push_ascii(rstd::u8(':'));
    append_fixed(output, minutes % 60, 2);
    return output;
}

auto convert(const Toml& value) -> Json {
    if (auto item = value.as_str(); item.is_some()) {
        return tagged("string"_str, String::make(*item));
    }
    if (auto item = value.as_integer(); item.is_some()) {
        return tagged("integer"_str, rstd::format("{}", *item));
    }
    if (auto item = value.as_float(); item.is_some()) {
        if (item->is_nan()) {
            return tagged("float"_str,
                          String::make(item->is_sign_negative() ? "-nan"_str : "nan"_str));
        }
        if (item->is_infinite()) {
            return tagged("float"_str,
                          String::make(item->is_sign_negative() ? "-inf"_str : "inf"_str));
        }
        return tagged("float"_str, rstd::format("{:?}", *item));
    }
    if (auto item = value.as_bool(); item.is_some()) {
        return tagged("bool"_str, String::make(*item ? "true"_str : "false"_str));
    }
    if (auto item = value.as_offset_datetime(); item.is_some()) {
        return tagged("datetime"_str, offset_datetime_string(*item));
    }
    if (auto item = value.as_local_datetime(); item.is_some()) {
        return tagged("datetime-local"_str, local_datetime_string(*item));
    }
    if (auto item = value.as_local_date(); item.is_some()) {
        return tagged("date-local"_str, date_string(*item));
    }
    if (auto item = value.as_local_time(); item.is_some()) {
        return tagged("time-local"_str, time_string(*item));
    }
    if (auto array = value.as_array(); array.is_some()) {
        auto result = JsonArray::with_capacity((**array).len());
        for (const auto& item : **array) result.push(convert(item));
        return Json::Array(rstd::move(result));
    }
    auto table  = value.as_table();
    auto result = JsonMap::make();
    if (table.is_some()) {
        auto entries = (**table).iter();
        for (auto entry = entries.next(); entry.is_some(); entry = entries.next()) {
            result.insert((*entry).template get<0>()->clone(),
                          convert(*(*entry).template get<1>()));
        }
    }
    return Json::Object(rstd::move(result));
}

auto read_input() -> rstd::Result<rstd::vec::Vec<rstd::u8>, rstd::io::error::Error> {
    auto       input = rstd::io::stdin();
    auto       bytes = rstd::vec::Vec<rstd::u8>::make();
    auto buffer = rstd::array<rstd::u8, 8192> {};
    for (;;) {
        auto read = rstd::as<rstd::io::Read>(input).read(buffer.as_mut_slice());
        if (read.is_err()) return rstd::Err(rstd::move(read).unwrap_err());
        if (*read == rstd::usize {}) return rstd::Ok(rstd::move(bytes));
        bytes.extend_from_slice(rstd::slice<rstd::u8>::from_raw_parts(buffer.data(), *read));
    }
}

} // namespace

auto decode() -> int {
    auto input = read_input();
    if (input.is_err()) {
        rstd::io::eprintln("stdin read failed: {}", rstd::move(input).unwrap_err());
        return 2;
    }
    auto parsed = rstd::toml::from_slice(input->as_slice());
    if (parsed.is_err()) {
        rstd::io::eprintln("{}", rstd::move(parsed).unwrap_err());
        return 1;
    }
    auto json = rstd::json::to_string(convert(*parsed));
    rstd::io::print("{}", json.as_str());
    return 0;
}

TEST(TomlConformance, Version220) {
    auto configured = rstd::env::var("RSTD_TOML_TEST_EXECUTABLE"_str);
    if (configured.is_none() || configured->is_empty()) {
        GTEST_SKIP() << "RSTD_TOML_TEST_EXECUTABLE is not configured";
    }

    auto version =
        rstd::process::Command::make(configured->as_str()).arg("version"_str).output();
    if (version.is_err()) {
        FAIL() << "toml-test version failed";
    }
    auto version_text = rstd::string::String::from_utf8(rstd::move(version->stdout_buf));
    if (version_text.is_err() ||
        ! version_text->as_str().starts_with("toml-test v2.2.0;"_str)) {
        FAIL() << "expected toml-test v2.2.0";
    }

    auto arguments = rstd::env::args();
    auto program   = arguments.next();
    ASSERT_TRUE(program.is_some());
    auto status = rstd::process::Command::make(configured->as_str())
                      .arg("test"_str)
                      .arg("-toml"_str)
                      .arg("1.1"_str)
                      .arg("-color"_str)
                      .arg("never"_str)
                      .arg("-decoder"_str)
                      .arg(program->as_str())
                      .arg("--decoder"_str)
                      .status();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status->success());
}

int main(int argc, char** argv) {
    rstd::env::args_init(argc, argv);
    auto arguments = rstd::env::args();
    (void)arguments.next();
    auto mode = arguments.next();
    if (mode.is_some() && mode->as_str() == "--decoder"_str) return decode();
    return rstd::test::run_registered().to_primitive();
}
