module;
#include <rstd/enum.hpp>

export module rstd.toml:serialize;
import rstd.alloc;
export import :value;
import :parser;

using ::alloc::string::String;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::toml
{

class SerializeError {
    String message_;

public:
    explicit SerializeError(String message): message_(rstd::move(message)) {}

    auto message() const noexcept -> ref<str> { return message_.as_str(); }
};

template<typename T>
using SerializeResult = Result<T, SerializeError>;

auto to_string(const Value& document) -> SerializeResult<String>;
auto to_value_string(const Value& value) -> SerializeResult<String>;
auto to_key_string(const KeyPath& key) -> String;

} // namespace rstd::toml

namespace rstd::toml
{

constexpr auto is_leap_year_for_serialization(uint16_t year) noexcept -> bool {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr auto days_in_month_for_serialization(uint16_t year, uint8_t month) noexcept -> uint8_t {
    constexpr uint8_t DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < uint8_t(1) || month > uint8_t(12)) return uint8_t();
    if (month == uint8_t(2) && is_leap_year_for_serialization(year)) return uint8_t(29);
    return DAYS[month - 1];
}

constexpr auto valid_date(LocalDate value) noexcept -> bool {
    return value.year <= uint16_t(9999) && value.month >= uint8_t(1) &&
           value.month <= uint8_t(12) && value.day >= uint8_t(1) &&
           value.day <= days_in_month_for_serialization(value.year, value.month);
}

constexpr auto valid_time(LocalTime value) noexcept -> bool {
    return value.hour <= uint8_t(23) && value.minute <= uint8_t(59) &&
           value.second <= uint8_t(59) && value.nanosecond <= uint32_t(999999999);
}

class TomlEmitter {
    String                 output_;
    Option<SerializeError> error_;

    auto fail(ref<str> message) -> bool {
        error_ = Some(SerializeError(String::make(message)));
        return false;
    }

    void write(ref<str> value) { output_.push_str(value); }

    void write_ascii(char value) { output_.push_ascii(u8(value)); }

    void write_fixed(uint32_t value, usize width) {
        char digits[10];
        auto remaining = value;
        for (usize index = width; index != usize(); --index) {
            digits[index.to_primitive() - 1] = static_cast<char>('0' + remaining % 10);
            remaining /= 10;
        }
        output_.push_str(
            ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(digits), width));
    }

    void write_string(ref<str> value) {
        static constexpr auto HEX = "0123456789abcdef"_str;
        write_ascii('"');
        for (auto current : value.as_bytes()) {
            switch (current.to_primitive()) {
            case '"': write("\\\""_str); break;
            case '\\': write("\\\\"_str); break;
            case '\b': write("\\b"_str); break;
            case '\t': write("\\t"_str); break;
            case '\n': write("\\n"_str); break;
            case '\f': write("\\f"_str); break;
            case '\r': write("\\r"_str); break;
            default:
                if (current < u8(0x20) || current == u8(0x7f)) {
                    write("\\u00"_str);
                    write_ascii(
                        static_cast<char>(HEX[usize(current.to_primitive() >> 4)].to_primitive()));
                    write_ascii(static_cast<char>(
                        HEX[usize(current.to_primitive() & 0x0f)].to_primitive()));
                } else {
                    output_.push_ascii(current);
                }
            }
        }
        write_ascii('"');
    }

    void write_key(ref<str> key) {
        auto bare = ! key.is_empty();
        for (auto value : key.as_bytes()) {
            bare =
                bare &&
                ((value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z')) ||
                 (value >= u8('0') && value <= u8('9')) || value == u8('_') || value == u8('-'));
        }
        if (bare)
            write(key);
        else
            write_string(key);
    }

    void write_path(const KeyPath& path) {
        for (usize index {}; index < path.len(); ++index) {
            if (index != usize()) write_ascii('.');
            write_key(path[index].as_str());
        }
    }

    auto write_date(LocalDate value) -> bool {
        if (! valid_date(value)) return fail("invalid TOML local date"_str);
        write_fixed(uint32_t(value.year), usize(4));
        write_ascii('-');
        write_fixed(uint32_t(value.month), usize(2));
        write_ascii('-');
        write_fixed(uint32_t(value.day), usize(2));
        return true;
    }

    auto write_time(LocalTime value) -> bool {
        if (! valid_time(value)) return fail("invalid TOML local time"_str);
        write_fixed(uint32_t(value.hour), usize(2));
        write_ascii(':');
        write_fixed(uint32_t(value.minute), usize(2));
        write_ascii(':');
        write_fixed(uint32_t(value.second), usize(2));
        if (value.nanosecond == uint32_t()) return true;

        char fraction[9];
        auto remaining = value.nanosecond;
        for (usize index = usize(9); index != usize(); --index) {
            fraction[index.to_primitive() - 1] = static_cast<char>('0' + remaining % 10);
            remaining /= 10;
        }
        usize length { 9 };
        while (length != usize() && fraction[length.to_primitive() - 1] == '0') --length;
        write_ascii('.');
        write(ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(fraction), length));
        return true;
    }

    auto write_local_datetime(LocalDateTime value) -> bool {
        if (! write_date(value.date)) return false;
        write_ascii('T');
        return write_time(value.time);
    }

    auto write_offset_datetime(OffsetDateTime value) -> bool {
        if (value.offset_minutes < int16_t(-1439) || value.offset_minutes > int16_t(1439)) {
            return fail("invalid TOML UTC offset"_str);
        }
        if (! write_local_datetime(value.local)) return false;
        if (value.offset_minutes == int16_t()) {
            write_ascii('Z');
            return true;
        }
        auto minutes = value.offset_minutes;
        write_ascii(minutes < 0 ? '-' : '+');
        if (minutes < 0) minutes = static_cast<int16_t>(-minutes);
        write_fixed(uint32_t(minutes / 60), usize(2));
        write_ascii(':');
        write_fixed(uint32_t(minutes % 60), usize(2));
        return true;
    }

    void write_float(f64 value) {
        if (value.is_nan()) {
            write(value.is_sign_negative() ? "-nan"_str : "nan"_str);
            return;
        }
        if (value.is_infinite()) {
            write(value.is_sign_negative() ? "-inf"_str : "inf"_str);
            return;
        }
        auto formatted = rstd::format("{:?}", value);
        write(formatted.as_str());
        if (! formatted.as_str().contains("."_str) && ! formatted.as_str().contains("e"_str) &&
            ! formatted.as_str().contains("E"_str)) {
            write(".0"_str);
        }
    }

    auto write_inline_table(const Table& table) -> bool {
        write_ascii('{');
        for (auto [index, item] : table.iter().enumerate()) {
            auto [key, value] = item;
            if (index != usize()) write(", "_str);
            write_key(key->as_str());
            write(" = "_str);
            if (! write_inline_value(*value)) return false;
        }
        write_ascii('}');
        return true;
    }

    auto write_array(const Array& array) -> bool {
        write_ascii('[');
        for (auto [index, value] : array.iter().enumerate()) {
            if (index != usize()) write(", "_str);
            if (! write_inline_value(*value)) return false;
        }
        write_ascii(']');
        return true;
    }

    auto write_inline_value(const Value& value) -> bool {
        RSTD_MATCH(value) {
            RSTD_CASE(Integer, item) {
                write(rstd::format("{}", item).as_str());
                return true;
            }
            RSTD_CASE(Float, item) {
                write_float(item);
                return true;
            }
            RSTD_CASE(Boolean, item) {
                write(item ? "true"_str : "false"_str);
                return true;
            }
            RSTD_CASE(String, item) {
                write_string(item.as_str());
                return true;
            }
            RSTD_CASE(OffsetDateTime, item) {
                return write_offset_datetime(item);
            }
            RSTD_CASE(LocalDateTime, item) {
                return write_local_datetime(item);
            }
            RSTD_CASE(LocalDate, item) {
                return write_date(item);
            }
            RSTD_CASE(LocalTime, item) {
                return write_time(item);
            }
            RSTD_CASE(Array, item) {
                return write_array(item);
            }
            RSTD_CASE(Table, item) {
                return write_inline_table(item);
            }
        }
        rstd::unreachable();
    }

    auto is_array_of_tables(const Value& value) const -> bool {
        auto array = value.as_array();
        if (array.is_none() || (**array).is_empty()) return false;
        for (const auto& item : **array) {
            if (! item.is_table()) return false;
        }
        return true;
    }

    auto is_child(const Value& value) const -> bool {
        return value.is_table() || is_array_of_tables(value);
    }

    auto write_assignments(const Table& table) -> bool {
        for (auto [key, value_ref] : table.iter()) {
            const auto& value = *value_ref;
            if (is_child(value)) continue;
            write_key(key->as_str());
            write(" = "_str);
            if (! write_inline_value(value)) return false;
            write_ascii('\n');
        }
        return true;
    }

    void begin_section(const KeyPath& path, bool array) {
        if (! output_.is_empty()) write_ascii('\n');
        write(array ? "[["_str : "["_str);
        write_path(path);
        write(array ? "]]\n"_str : "]\n"_str);
    }

    auto write_children(const Table& table, KeyPath& path) -> bool {
        for (auto [key, value_ref] : table.iter()) {
            const auto& value = *value_ref;
            if (! is_child(value)) continue;
            path.push(key->clone());
            if (value.is_table()) {
                begin_section(path, false);
                if (! write_assignments(**value.as_table()) ||
                    ! write_children(**value.as_table(), path)) {
                    return false;
                }
            } else {
                auto array = value.as_array().unwrap();
                for (const auto& element : *array) {
                    begin_section(path, true);
                    auto table_value = element.as_table().unwrap();
                    if (! write_assignments(*table_value) || ! write_children(*table_value, path)) {
                        return false;
                    }
                }
            }
            (void)path.pop();
        }
        return true;
    }

public:
    void write_key_path(const KeyPath& path) { write_path(path); }

    auto write_document(const Table& table) -> bool {
        if (! write_assignments(table)) return false;
        auto path = KeyPath::make();
        if (! write_children(table, path)) return false;
        if (output_.is_empty()) write_ascii('\n');
        return true;
    }

    auto write_value(const Value& value) -> bool { return write_inline_value(value); }

    auto finish() -> SerializeResult<String> {
        if (error_.is_some()) return Err(rstd::move(error_).unwrap());
        return Ok(rstd::move(output_));
    }
};

auto to_string(const Value& document) -> SerializeResult<String> {
    auto table = document.as_table();
    if (table.is_none()) {
        return Err(SerializeError(String::make("TOML document root must be a table"_str)));
    }
    auto emitter = TomlEmitter {};
    (void)emitter.write_document(**table);
    return emitter.finish();
}

auto to_value_string(const Value& value) -> SerializeResult<String> {
    auto emitter = TomlEmitter {};
    (void)emitter.write_value(value);
    return emitter.finish();
}

auto to_key_string(const KeyPath& key) -> String {
    auto emitter = TomlEmitter {};
    emitter.write_key_path(key);
    return emitter.finish().unwrap();
}

} // namespace rstd::toml

namespace rstd
{

template<>
struct Impl<fmt::Display, toml::SerializeError> : ImplBase<toml::SerializeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().message());
    }
};

template<>
struct Impl<fmt::Debug, toml::SerializeError> : ImplBase<toml::SerializeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, toml::SerializeError>
    : DefaultInImpl<error::Error, toml::SerializeError> {};

} // namespace rstd
