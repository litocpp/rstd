module;
#include <rstd/macro.hpp>
export module rstd:time.parse;
export import :time.calendar;
export import rstd.alloc;
import rstd.parse.core;
using ::alloc::string::String;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::time
{
enum class DateTimeParseErrorKind
{
    Syntax,
    Component,
    Range
};

struct DateTimeParseError {
    DateTimeParseErrorKind kind;
    usize                  offset {};
    Option<ComponentRange> component;

    static auto syntax(usize offset) -> DateTimeParseError {
        return { DateTimeParseErrorKind::Syntax, offset, None() };
    }
    static auto invalid(ComponentRange error, usize offset = usize()) -> DateTimeParseError {
        return { DateTimeParseErrorKind::Component, offset, Some(error) };
    }
    static auto range() -> DateTimeParseError {
        return { DateTimeParseErrorKind::Range, usize(), None() };
    }
};

struct ParsedDateTime {
    // A leap second uses the preceding :59 as its normalized base.
    UnixTime base_time;
    bool     leap_second {};

    auto unix_milliseconds() const -> Result<i64, DateTimeParseError> {
        if (base_time.nanoseconds >= u32(1000000000) ||
            (leap_second && base_time.seconds % i64(60) != i64(59) &&
             base_time.seconds % i64(60) != i64(-1)))
            return Err(DateTimeParseError::range());
        auto seconds  = base_time.seconds;
        auto fraction = i64((base_time.nanoseconds / u32(1000000)).to_primitive()) +
                        i64(leap_second ? 1000 : 0);
        if (seconds < i64() && fraction > i64()) {
            seconds  = seconds + i64(1);
            fraction = fraction - i64(1000);
        }
        auto whole = seconds.checked_mul(i64(1000));
        if (whole.is_none()) return Err(DateTimeParseError::range());
        auto millis = whole->checked_add(fraction);
        if (millis.is_none()) return Err(DateTimeParseError::range());
        return Ok(*millis);
    }
};

struct Rfc3339Options {
    bool  allow_space_separator { true };
    bool  allow_unicode_minus { true };
    usize max_fractional_digits { usize::MAX };
};

auto make_parsed_datetime(CalendarDate date,
                          u8           hour,
                          u8           minute,
                          u8           second,
                          u32          nanosecond,
                          UtcOffset    offset = UtcOffset::UTC)
    -> Result<ParsedDateTime, DateTimeParseError> {
    if (second > u8(60))
        return Err(DateTimeParseError::invalid(ComponentRange::unconditional("second"_str)));
    auto time = Time::from_hms_nano(hour, minute, second == u8(60) ? u8(59) : second, nanosecond);
    if (time.is_err()) return Err(DateTimeParseError::invalid(time.unwrap_err()));
    auto days = days_from_calendar_date(date.year, date.month, date.day);
    if (days.is_err()) return Err(DateTimeParseError::invalid(days.unwrap_err()));
    if (second == u8(60) && offset.whole_seconds() % i32(60) != i32())
        return Err(DateTimeParseError::invalid(ComponentRange::conditional("second"_str)));
    auto seconds = *days * i64(86400) + i64(hour.to_primitive()) * i64(3600) +
                   i64(minute.to_primitive()) * i64(60) + i64(time->second().to_primitive()) -
                   i64(offset.whole_seconds().to_primitive());
    return Ok(ParsedDateTime { { seconds, nanosecond }, second == u8(60) });
}
} // namespace rstd::time

namespace rstd::time
{
class Rfc3339Parser {
    rstd::parse::TextCursor cursor_;
    Rfc3339Options          options_;

    auto number(usize width) -> Result<i32, DateTimeParseError> {
        auto span = rstd::parse::consume_n(cursor_, width, rstd::parse::ascii::digit);
        if (span.is_none()) return Err(DateTimeParseError::syntax(cursor_.furthest_position()));
        return Ok(rstd::from_str<i32>(cursor_.text(*span).unwrap()).unwrap());
    }
    auto literal(ref<str> text) -> Result<empty, DateTimeParseError> {
        if (rstd::parse::consume_literal(cursor_, text).is_none())
            return Err(DateTimeParseError::syntax(cursor_.furthest_position()));
        return Ok(empty {});
    }
    auto next_is(u8 byte) const -> bool {
        auto next = cursor_.peek();
        return next.is_some() && next->get() == byte;
    }

public:
    explicit Rfc3339Parser(ref<str> input, Rfc3339Options options)
        : cursor_(rstd::parse::text_input(input)), options_(options) {}

    auto parse() -> Result<ParsedDateTime, DateTimeParseError> {
        auto year = rstd_try(number(usize(4)));
        rstd_try(literal("-"_str));
        auto month = rstd_try(number(usize(2)));
        rstd_try(literal("-"_str));
        auto day = rstd_try(number(usize(2)));
        bool separator =
            rstd::parse::consume_if(cursor_, rstd::parse::one_of(u8('T'), u8('t'))).is_some();
        if (! separator && options_.allow_space_separator)
            separator = rstd::parse::consume_literal(cursor_, " "_str).is_some();
        if (! separator) return Err(DateTimeParseError::syntax(cursor_.position()));
        auto hour = rstd_try(number(usize(2)));
        rstd_try(literal(":"_str));
        auto minute = rstd_try(number(usize(2)));
        rstd_try(literal(":"_str));
        auto second = rstd_try(number(usize(2)));
        u32  nanos;
        if (next_is(u8('.'))) {
            (void)cursor_.take();
            auto span = rstd::parse::consume_while_one(cursor_, rstd::parse::ascii::digit);
            if (span.is_none()) return Err(DateTimeParseError::syntax(cursor_.position()));
            if (span->len() > options_.max_fractional_digits)
                return Err(DateTimeParseError::syntax(span->begin));
            usize count;
            for (auto digit : cursor_.view(*span)) {
                if (count == usize(9)) break;
                nanos = nanos * u32(10) + u32((digit - u8('0')).to_primitive());
                ++count;
            }
            while (count < usize(9)) {
                nanos *= u32(10);
                ++count;
            }
        }
        UtcOffset offset;
        if (rstd::parse::consume_if(cursor_, rstd::parse::one_of(u8('Z'), u8('z'))).is_none()) {
            auto sign_position = cursor_.position();
            bool negative      = false;
            if (rstd::parse::consume_literal(cursor_, "+"_str).is_none()) {
                negative = rstd::parse::consume_literal(cursor_, "-"_str).is_some();
                if (! negative && options_.allow_unicode_minus)
                    negative = rstd::parse::consume_literal(cursor_, "\xe2\x88\x92"_str).is_some();
                if (! negative) return Err(DateTimeParseError::syntax(cursor_.position()));
            }
            auto hours = rstd_try(number(usize(2)));
            rstd_try(literal(":"_str));
            auto minutes = rstd_try(number(usize(2)));
            if (hours > i32(23) || minutes > i32(59))
                return Err(DateTimeParseError::invalid(ComponentRange::unconditional("offset"_str),
                                                       sign_position));
            auto seconds = hours * i32(3600) + minutes * i32(60);
            if (negative) seconds = -seconds;
            offset = UtcOffset::from_whole_seconds(seconds).unwrap();
        }
        if (! cursor_.is_eof()) return Err(DateTimeParseError::syntax(cursor_.position()));
        auto parsed_month = Month::from_number(u8(month.to_primitive()));
        if (parsed_month.is_err())
            return Err(DateTimeParseError::invalid(parsed_month.unwrap_err(), usize(5)));
        return make_parsed_datetime({ year, *parsed_month, u8(day.to_primitive()) },
                                    u8(hour.to_primitive()),
                                    u8(minute.to_primitive()),
                                    u8(second.to_primitive()),
                                    nanos,
                                    offset);
    }
};

auto write_datetime_number(String& text, u64 value, usize width) -> void {
    char32_t digits[20];
    usize    size;
    do {
        digits[size.to_primitive()] = char32_t('0' + (value % u64(10)).to_primitive());
        ++size;
        value /= u64(10);
    } while (value != u64());
    for (auto i = size; i < width; ++i) text.push(U'0');
    while (size != usize()) text.push(digits[(--size).to_primitive()]);
}
} // namespace rstd::time

export namespace rstd::time
{
auto parse_rfc3339(ref<str> text, Rfc3339Options options = {})
    -> Result<ParsedDateTime, DateTimeParseError> {
    return Rfc3339Parser(text, options).parse();
}

auto format_rfc3339(ParsedDateTime value, bool use_z = false)
    -> Result<String, DateTimeParseError> {
    if (value.base_time.nanoseconds >= u32(1000000000)) return Err(DateTimeParseError::range());
    auto seconds = value.base_time.seconds.to_primitive();
    auto days    = seconds / 86400;
    auto clock   = seconds % 86400;
    if (clock < 0) {
        --days;
        clock += 86400;
    }
    if (value.leap_second && clock % 60 != 59) return Err(DateTimeParseError::range());
    auto date = calendar_date_from_days(i64(days));
    if (date.is_err()) return Err(DateTimeParseError::invalid(date.unwrap_err()));
    String text;
    text.reserve(usize(48));
    auto year = rstd::int64_t(date->year.to_primitive());
    if (year < 0 || year > 9999) text.push(year < 0 ? U'-' : U'+');
    write_datetime_number(text, u64(year < 0 ? -year : year), usize(4));
    text.push(U'-');
    write_datetime_number(text, u64(date->month.number().to_primitive()), usize(2));
    text.push(U'-');
    write_datetime_number(text, u64(date->day.to_primitive()), usize(2));
    text.push(U'T');
    write_datetime_number(text, u64(clock / 3600), usize(2));
    text.push(U':');
    write_datetime_number(text, u64(clock / 60 % 60), usize(2));
    text.push(U':');
    write_datetime_number(text, u64(clock % 60 + (value.leap_second ? 1 : 0)), usize(2));
    auto nanos = value.base_time.nanoseconds;
    if (nanos != u32()) {
        text.push(U'.');
        if (nanos % u32(1000000) == u32())
            write_datetime_number(text, u64((nanos / u32(1000000)).to_primitive()), usize(3));
        else if (nanos % u32(1000) == u32())
            write_datetime_number(text, u64((nanos / u32(1000)).to_primitive()), usize(6));
        else
            write_datetime_number(text, u64(nanos.to_primitive()), usize(9));
    }
    text.push_str(use_z ? "Z"_str : "+00:00"_str);
    return Ok(rstd::move(text));
}
} // namespace rstd::time

export namespace rstd
{
template<>
struct Impl<fmt::Display, time::DateTimeParseError> : ImplBase<time::DateTimeParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.component.is_some())
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid datetime component at byte {}: {}", error.offset, *error.component));
        if (error.kind == time::DateTimeParseErrorKind::Range)
            return formatter.write_str("datetime is out of range"_str);
        return formatter.write_fmt(
            fmt::Arguments::make("invalid datetime at byte {}", error.offset));
    }
};
template<>
struct Impl<fmt::Debug, time::DateTimeParseError> : Impl<fmt::Display, time::DateTimeParseError> {};
template<>
struct Impl<error::Error, time::DateTimeParseError>
    : DefaultInImpl<error::Error, time::DateTimeParseError> {};
} // namespace rstd
