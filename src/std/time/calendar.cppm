export module rstd:time.calendar;
export import rstd.error;
import :sys.pal;

namespace rstd::time::calendar_detail
{

constexpr auto floor_div(rstd::int64_t value, rstd::int64_t divisor) noexcept -> rstd::int64_t {
    auto quotient  = value / divisor;
    auto remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

constexpr auto rem_euclid(rstd::int64_t value, rstd::int64_t divisor) noexcept -> rstd::int64_t {
    auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

constexpr auto days_from_civil(rstd::int64_t year, rstd::uint8_t month, rstd::uint8_t day) noexcept
    -> rstd::int64_t {
    year -= month <= 2;
    auto const era            = floor_div(year, 400);
    auto const year_of_era    = year - era * 400;
    auto const adjusted_month = static_cast<rstd::int64_t>(month) + (month > 2 ? -3 : 9);
    auto const day_of_year = (153 * adjusted_month + 2) / 5 + static_cast<rstd::int64_t>(day) - 1;
    auto const day_of_era  = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146'097 + day_of_era - 719'468;
}

struct CivilDate {
    rstd::int64_t year;
    rstd::uint8_t month;
    rstd::uint8_t day;
};

constexpr auto civil_from_days(rstd::int64_t days) noexcept -> CivilDate {
    auto const shifted    = days + 719'468;
    auto const era        = shifted >= 0 ? shifted / 146'097 : (shifted - 146'096) / 146'097;
    auto const day_of_era = shifted - era * 146'097;
    auto const year_of_era =
        (day_of_era - day_of_era / 1'460 + day_of_era / 36'524 - day_of_era / 146'096) / 365;
    auto       year        = year_of_era + era * 400;
    auto const day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    auto const month_prime = (5 * day_of_year + 2) / 153;
    auto const day         = day_of_year - (153 * month_prime + 2) / 5 + 1;
    auto const month       = month_prime + (month_prime < 10 ? 3 : -9);
    year += month <= 2;
    return { year, static_cast<rstd::uint8_t>(month), static_cast<rstd::uint8_t>(day) };
}

} // namespace rstd::time::calendar_detail

export namespace rstd::time
{

using namespace rstd::literals;

struct UnixTime {
    i64 seconds {};
    u32 nanoseconds {};
};

class ComponentRange {
    ref<str> name_;
    bool     conditional_ {};

public:
    static constexpr auto unconditional(ref<str> name) noexcept -> ComponentRange {
        return { name, false };
    }

    static constexpr auto conditional(ref<str> name) noexcept -> ComponentRange {
        return { name, true };
    }

    constexpr auto name() const noexcept -> ref<str> { return name_; }
    constexpr auto is_conditional() const noexcept -> bool { return conditional_; }

    friend constexpr auto operator==(ComponentRange, ComponentRange) noexcept -> bool = default;

private:
    constexpr ComponentRange(ref<str> name, bool conditional) noexcept
        : name_(name), conditional_(conditional) {}
};

struct IndeterminateOffset {
    friend constexpr auto operator==(IndeterminateOffset, IndeterminateOffset) noexcept
        -> bool = default;
};

class Month {
public:
    enum Value : rstd::uint8_t
    {
        January = 1,
        February,
        March,
        April,
        May,
        June,
        July,
        August,
        September,
        October,
        November,
        December,
    };

    constexpr Month(Value value) noexcept: value_(value) {}

    static constexpr auto from_number(u8 number) noexcept -> Result<Month, ComponentRange> {
        auto const raw = number.to_primitive();
        if (raw < January || raw > December) {
            return Err(ComponentRange::unconditional("month"_str));
        }
        return Ok(Month(static_cast<Value>(raw)));
    }

    constexpr auto number() const noexcept -> u8 { return u8(static_cast<rstd::uint8_t>(value_)); }

    friend constexpr auto operator==(Month, Month) noexcept -> bool = default;
    friend constexpr auto operator<=>(Month, Month) noexcept        = default;

private:
    Value value_;
};

class OffsetDateTime;
class UtcOffset;

class Date {
    i32 year_;
    u16 ordinal_;

    struct MonthDay {
        Month month;
        u8    day;
    };

    constexpr Date(i32 year, u16 ordinal) noexcept: year_(year), ordinal_(ordinal) {}

    static constexpr auto is_leap_year(i32 year) noexcept -> bool {
        auto const raw = year.to_primitive();
        return raw % 4 == 0 && (raw % 25 != 0 || raw % 16 == 0);
    }

    static constexpr auto days_in_month(i32 year, Month month) noexcept -> u8 {
        switch (month.number().to_primitive()) {
        case 2: return u8(is_leap_year(year) ? 29 : 28);
        case 4:
        case 6:
        case 9:
        case 11: return u8(30);
        default: return u8(31);
        }
    }

    constexpr auto month_day() const noexcept -> MonthDay {
        auto remaining = ordinal_;
        for (u8 number(1); number <= u8(12); ++number) {
            auto       month = Month::from_number(number);
            auto const count = u16(days_in_month(year_, *month).to_primitive());
            if (remaining <= count) {
                return { *month, u8(static_cast<rstd::uint8_t>(remaining.to_primitive())) };
            }
            remaining -= count;
        }
        __builtin_unreachable();
    }

    constexpr auto days_since_unix_epoch() const noexcept -> i64 {
        auto const parts = month_day();
        return i64(calendar_detail::days_from_civil(
            year_.to_primitive(), parts.month.number().to_primitive(), parts.day.to_primitive()));
    }

    static constexpr auto from_days_since_unix_epoch(i64 days) noexcept
        -> Result<Date, ComponentRange> {
        auto const civil = calendar_detail::civil_from_days(days.to_primitive());
        if (civil.year < MIN_YEAR.to_primitive() || civil.year > MAX_YEAR.to_primitive()) {
            return Err(ComponentRange::unconditional("timestamp"_str));
        }
        auto month = Month::from_number(u8(civil.month));
        if (month.is_err()) return Err(ComponentRange::unconditional("timestamp"_str));
        return from_calendar_date(i32(civil.year), *month, u8(civil.day));
    }

    friend class OffsetDateTime;

public:
    inline static constexpr i32 MIN_YEAR { -9999 };
    inline static constexpr i32 MAX_YEAR { 9999 };

    static constexpr auto from_calendar_date(i32 year, Month month, u8 day) noexcept
        -> Result<Date, ComponentRange> {
        if (year < MIN_YEAR || year > MAX_YEAR) {
            return Err(ComponentRange::unconditional("year"_str));
        }
        if (month.number() < u8(1) || month.number() > u8(12)) {
            return Err(ComponentRange::unconditional("month"_str));
        }
        auto const month_days = days_in_month(year, month);
        if (day == u8() || day > month_days) {
            return Err(ComponentRange::conditional("day"_str));
        }

        u16 ordinal(static_cast<rstd::uint16_t>(day.to_primitive()));
        for (u8 number(1); number < month.number(); ++number) {
            auto previous = Month::from_number(number);
            ordinal += u16(days_in_month(year, *previous).to_primitive());
        }
        return Ok(Date(year, ordinal));
    }

    static constexpr auto from_ordinal_date(i32 year, u16 ordinal) noexcept
        -> Result<Date, ComponentRange> {
        if (year < MIN_YEAR || year > MAX_YEAR) {
            return Err(ComponentRange::unconditional("year"_str));
        }
        auto const maximum = u16(is_leap_year(year) ? 366 : 365);
        if (ordinal == u16() || ordinal > maximum) {
            return Err(ComponentRange::conditional("ordinal"_str));
        }
        return Ok(Date(year, ordinal));
    }

    constexpr auto year() const noexcept -> i32 { return year_; }
    constexpr auto month() const noexcept -> Month { return month_day().month; }
    constexpr auto day() const noexcept -> u8 { return month_day().day; }
    constexpr auto ordinal() const noexcept -> u16 { return ordinal_; }

    friend constexpr auto operator==(Date, Date) noexcept -> bool = default;
    friend constexpr auto operator<=>(Date, Date) noexcept        = default;
};

class Time {
    u8  hour_ {};
    u8  minute_ {};
    u8  second_ {};
    u32 nanosecond_ {};

    constexpr Time(u8 hour, u8 minute, u8 second, u32 nanosecond) noexcept
        : hour_(hour), minute_(minute), second_(second), nanosecond_(nanosecond) {}

public:
    constexpr Time() noexcept = default;

    static constexpr auto from_hms_nano(u8 hour, u8 minute, u8 second, u32 nanosecond) noexcept
        -> Result<Time, ComponentRange> {
        if (hour >= u8(24)) return Err(ComponentRange::unconditional("hour"_str));
        if (minute >= u8(60)) return Err(ComponentRange::unconditional("minute"_str));
        if (second >= u8(60)) return Err(ComponentRange::unconditional("second"_str));
        if (nanosecond >= NANOS_PER_SEC) {
            return Err(ComponentRange::unconditional("nanosecond"_str));
        }
        return Ok(Time(hour, minute, second, nanosecond));
    }

    constexpr auto hour() const noexcept -> u8 { return hour_; }
    constexpr auto minute() const noexcept -> u8 { return minute_; }
    constexpr auto second() const noexcept -> u8 { return second_; }
    constexpr auto nanosecond() const noexcept -> u32 { return nanosecond_; }

    friend constexpr auto operator==(Time, Time) noexcept -> bool = default;
    friend constexpr auto operator<=>(Time, Time) noexcept        = default;
};

class PlainDateTime {
    Date date_;
    Time time_;

    constexpr PlainDateTime(Date date, Time time) noexcept: date_(date), time_(time) {}

public:
    static constexpr auto make(Date date, Time time) noexcept -> PlainDateTime {
        return PlainDateTime(date, time);
    }

    constexpr auto date() const noexcept -> Date { return date_; }
    constexpr auto time() const noexcept -> Time { return time_; }
    constexpr auto assume_utc() const noexcept -> OffsetDateTime;
    constexpr auto assume_offset(UtcOffset offset) const noexcept -> OffsetDateTime;

    friend constexpr auto operator==(PlainDateTime, PlainDateTime) noexcept -> bool = default;
    friend constexpr auto operator<=>(PlainDateTime, PlainDateTime) noexcept        = default;
};

class UtcOffset {
    i32 seconds_ {};

    constexpr explicit UtcOffset(i32 seconds) noexcept: seconds_(seconds) {}

public:
    static const UtcOffset UTC;

    constexpr UtcOffset() noexcept = default;

    static constexpr auto from_whole_seconds(i32 seconds) noexcept
        -> Result<UtcOffset, ComponentRange> {
        if (seconds < i32(-86'399) || seconds > i32(86'399)) {
            return Err(ComponentRange::unconditional("offset"_str));
        }
        return Ok(UtcOffset(seconds));
    }

    static auto local_offset_at(OffsetDateTime datetime) noexcept
        -> Result<UtcOffset, IndeterminateOffset>;

    constexpr auto whole_seconds() const noexcept -> i32 { return seconds_; }
    constexpr auto is_utc() const noexcept -> bool { return seconds_ == i32(); }

    friend constexpr auto operator==(UtcOffset, UtcOffset) noexcept -> bool = default;
    friend constexpr auto operator<=>(UtcOffset, UtcOffset) noexcept        = default;
};

inline constexpr UtcOffset UtcOffset::UTC {};

class OffsetDateTime {
    PlainDateTime local_;
    UtcOffset     offset_;

    constexpr OffsetDateTime(PlainDateTime local, UtcOffset offset) noexcept
        : local_(local), offset_(offset) {}

    static constexpr auto
    from_local_seconds(i64 seconds, u32 nanoseconds, UtcOffset offset) noexcept
        -> Result<OffsetDateTime, ComponentRange> {
        if (nanoseconds >= NANOS_PER_SEC) {
            return Err(ComponentRange::unconditional("nanosecond"_str));
        }

        constexpr rstd::int64_t SECONDS_PER_DAY = 86'400;
        auto const days = calendar_detail::floor_div(seconds.to_primitive(), SECONDS_PER_DAY);
        auto const within_day =
            calendar_detail::rem_euclid(seconds.to_primitive(), SECONDS_PER_DAY);

        auto date = Date::from_days_since_unix_epoch(i64(days));
        if (date.is_err()) return Err(ComponentRange::unconditional("timestamp"_str));

        auto const hour   = within_day / 3'600;
        auto const minute = (within_day % 3'600) / 60;
        auto const second = within_day % 60;
        auto       time   = Time::from_hms_nano(u8(hour), u8(minute), u8(second), nanoseconds);
        if (time.is_err()) return Err(ComponentRange::unconditional("timestamp"_str));

        return Ok(OffsetDateTime(PlainDateTime::make(*date, *time), offset));
    }

    friend class PlainDateTime;

public:
    static const OffsetDateTime UNIX_EPOCH;

    static auto now_utc() noexcept -> OffsetDateTime;
    static auto now_local() noexcept -> Result<OffsetDateTime, IndeterminateOffset>;

    static constexpr auto from_unix_time(i64 seconds, u32 nanoseconds) noexcept
        -> Result<OffsetDateTime, ComponentRange> {
        return from_local_seconds(seconds, nanoseconds, UtcOffset {});
    }

    constexpr auto checked_to_offset(UtcOffset offset) const noexcept -> Option<OffsetDateTime> {
        auto const timestamp = unix_time();
        auto       local_seconds =
            timestamp.seconds.checked_add(i64(offset.whole_seconds().to_primitive()));
        if (local_seconds.is_none()) return None();
        auto converted = from_local_seconds(*local_seconds, timestamp.nanoseconds, offset);
        if (converted.is_err()) return None();
        return Some(*converted);
    }

    constexpr auto date() const noexcept -> Date { return local_.date(); }
    constexpr auto time() const noexcept -> Time { return local_.time(); }
    constexpr auto offset() const noexcept -> UtcOffset { return offset_; }

    constexpr auto unix_time() const noexcept -> UnixTime {
        auto const date_seconds = date().days_since_unix_epoch() * i64(86'400);
        auto const time_seconds = i64(time().hour().to_primitive()) * i64(3'600) +
                                  i64(time().minute().to_primitive()) * i64(60) +
                                  i64(time().second().to_primitive());
        return { date_seconds + time_seconds - i64(offset_.whole_seconds().to_primitive()),
                 time().nanosecond() };
    }

    friend constexpr auto operator==(OffsetDateTime left, OffsetDateTime right) noexcept -> bool {
        return left.unix_time().seconds == right.unix_time().seconds &&
               left.unix_time().nanoseconds == right.unix_time().nanoseconds;
    }

    friend constexpr auto operator<=>(OffsetDateTime left, OffsetDateTime right) noexcept {
        auto const left_time  = left.unix_time();
        auto const right_time = right.unix_time();
        if (auto order = left_time.seconds <=> right_time.seconds; order != 0) return order;
        return left_time.nanoseconds <=> right_time.nanoseconds;
    }
};

inline constexpr OffsetDateTime OffsetDateTime::UNIX_EPOCH =
    *OffsetDateTime::from_unix_time(i64(), u32());

constexpr auto PlainDateTime::assume_utc() const noexcept -> OffsetDateTime {
    return OffsetDateTime(*this, UtcOffset {});
}

constexpr auto PlainDateTime::assume_offset(UtcOffset offset) const noexcept -> OffsetDateTime {
    return OffsetDateTime(*this, offset);
}

inline auto UtcOffset::local_offset_at(OffsetDateTime datetime) noexcept
    -> Result<UtcOffset, IndeterminateOffset> {
    auto seconds = sys::pal::local_offset_at_unix_time(datetime.unix_time().seconds);
    if (seconds.is_none()) return Err(IndeterminateOffset {});
    auto offset = from_whole_seconds(*seconds);
    if (offset.is_err()) return Err(IndeterminateOffset {});
    return Ok(offset.unwrap());
}

inline auto OffsetDateTime::now_local() noexcept -> Result<OffsetDateTime, IndeterminateOffset> {
    auto utc    = now_utc();
    auto offset = UtcOffset::local_offset_at(utc);
    if (offset.is_err()) return Err(offset.unwrap_err());
    auto local = utc.checked_to_offset(offset.unwrap());
    if (local.is_none()) return Err(IndeterminateOffset {});
    return Ok(*local);
}

} // namespace rstd::time

namespace rstd
{

template<>
struct Impl<fmt::Display, time::ComponentRange> : ImplBase<time::ComponentRange> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(
            fmt::Arguments::make("{} was not in range", this->self().name()));
    }
};

template<>
struct Impl<fmt::Display, time::IndeterminateOffset> : ImplBase<time::IndeterminateOffset> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "the system's UTC offset could not be determined";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, time::ComponentRange>
    : LinkClassMethod<error::Error, time::ComponentRange> {};

template<>
struct Impl<error::Error, time::IndeterminateOffset>
    : LinkClassMethod<error::Error, time::IndeterminateOffset> {};

} // namespace rstd
