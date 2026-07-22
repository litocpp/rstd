import rstd;
import rstd.json;
import rstd.toml;

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
    result.insert(String::make("type"), string_json(type));
    result.insert(String::make("value"), Json::String(rstd::move(value)));
    return Json::Object(rstd::move(result));
}

void append_fixed(String& output, rstd::uint32_t value, rstd::size_t width) {
    char digits[9];
    for (rstd::size_t index = width; index > 0; --index) {
        digits[index - 1] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    output.push_str(rstd::ref<rstd::str>::from_raw_parts(digits, rstd::usize(width)));
}

auto date_string(rstd::toml::LocalDate value) -> String {
    auto output = String::make();
    append_fixed(output, value.year, 4);
    output.push_back('-');
    append_fixed(output, value.month, 2);
    output.push_back('-');
    append_fixed(output, value.day, 2);
    return output;
}

auto time_string(rstd::toml::LocalTime value) -> String {
    auto output = String::make();
    append_fixed(output, value.hour, 2);
    output.push_back(':');
    append_fixed(output, value.minute, 2);
    output.push_back(':');
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
        output.push_back('.');
        output.push_str(rstd::ref<rstd::str>::from_raw_parts(fraction, rstd::usize(length)));
    }
    return output;
}

auto local_datetime_string(rstd::toml::LocalDateTime value) -> String {
    auto output = date_string(value.date);
    output.push_back('T');
    auto time = time_string(value.time);
    output.push_str(time.as_str());
    return output;
}

auto offset_datetime_string(rstd::toml::OffsetDateTime value) -> String {
    auto output = local_datetime_string(value.local);
    auto offset = value.offset_minutes;
    if (offset == 0) {
        output.push_back('Z');
        return output;
    }
    output.push_back(offset < 0 ? '-' : '+');
    const auto minutes = static_cast<rstd::uint32_t>(offset < 0 ? -offset : offset);
    append_fixed(output, minutes / 60, 2);
    output.push_back(':');
    append_fixed(output, minutes % 60, 2);
    return output;
}

auto convert(const Toml& value) -> Json {
    if (auto item = value.as_str(); item.is_some()) {
        return tagged("string", String::make(*item));
    }
    if (auto item = value.as_integer(); item.is_some()) {
        return tagged("integer", rstd::format("{}", *item));
    }
    if (auto item = value.as_float(); item.is_some()) {
        if (item->is_nan()) {
            return tagged("float", String::make(item->is_sign_negative() ? "-nan" : "nan"));
        }
        if (item->is_infinite()) {
            return tagged("float", String::make(item->is_sign_negative() ? "-inf" : "inf"));
        }
        return tagged("float", rstd::format("{:?}", *item));
    }
    if (auto item = value.as_bool(); item.is_some()) {
        return tagged("bool", String::make(*item ? "true" : "false"));
    }
    if (auto item = value.as_offset_datetime(); item.is_some()) {
        return tagged("datetime", offset_datetime_string(*item));
    }
    if (auto item = value.as_local_datetime(); item.is_some()) {
        return tagged("datetime-local", local_datetime_string(*item));
    }
    if (auto item = value.as_local_date(); item.is_some()) {
        return tagged("date-local", date_string(*item));
    }
    if (auto item = value.as_local_time(); item.is_some()) {
        return tagged("time-local", time_string(*item));
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
    rstd::byte buffer[8192];
    for (;;) {
        auto read = rstd::as<rstd::io::Read>(input).read(
            rstd::mut_ref<rstd::byte[]>::from_raw_parts(buffer, rstd::usize(8192)));
        if (read.is_err()) return rstd::Err(rstd::move(read).unwrap_err());
        if (*read == rstd::usize {}) return rstd::Ok(rstd::move(bytes));
        bytes.extend_from_bytes(rstd::slice<rstd::byte>::from_raw_parts(buffer, *read));
    }
}

} // namespace

int main() {
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
