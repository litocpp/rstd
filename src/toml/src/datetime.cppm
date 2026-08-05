export module rstd.toml:datetime;
export import rstd.core;

export namespace rstd::toml
{

/// A calendar date without a time or UTC offset.
struct LocalDate {
    /// The four-digit year.
    uint16_t year {};
    /// The month in the range 1 through 12.
    uint8_t month {};
    /// The day of the month.
    uint8_t day {};

    friend constexpr auto operator==(LocalDate, LocalDate) noexcept -> bool = default;
};

/// A time of day without a date or UTC offset.
struct LocalTime {
    /// The hour in the range 0 through 23.
    uint8_t hour {};
    /// The minute in the range 0 through 59.
    uint8_t minute {};
    /// The second in the range 0 through 59.
    uint8_t second {};
    /// The fractional second expressed in nanoseconds.
    uint32_t nanosecond {};

    friend constexpr auto operator==(LocalTime, LocalTime) noexcept -> bool = default;
};

/// A local date and time without a UTC offset.
struct LocalDateTime {
    /// The local calendar date.
    LocalDate date;
    /// The local time of day.
    LocalTime time;

    friend constexpr auto operator==(LocalDateTime, LocalDateTime) noexcept -> bool = default;
};

/// A local date and time paired with a UTC offset.
struct OffsetDateTime {
    /// The local date and time components.
    LocalDateTime local;
    /// The signed UTC offset in minutes.
    int16_t offset_minutes {};

    friend constexpr auto operator==(OffsetDateTime, OffsetDateTime) noexcept -> bool = default;
};

} // namespace rstd::toml
