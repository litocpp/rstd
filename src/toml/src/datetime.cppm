export module rstd.toml:datetime;
export import rstd.core;

export namespace rstd::toml
{

struct LocalDate {
    uint16_t year {};
    uint8_t  month {};
    uint8_t  day {};

    friend constexpr auto operator==(LocalDate, LocalDate) noexcept -> bool = default;
};

struct LocalTime {
    uint8_t  hour {};
    uint8_t  minute {};
    uint8_t  second {};
    uint32_t nanosecond {};

    friend constexpr auto operator==(LocalTime, LocalTime) noexcept -> bool = default;
};

struct LocalDateTime {
    LocalDate date;
    LocalTime time;

    friend constexpr auto operator==(LocalDateTime, LocalDateTime) noexcept -> bool = default;
};

struct OffsetDateTime {
    LocalDateTime local;
    int16_t       offset_minutes {};

    friend constexpr auto operator==(OffsetDateTime, OffsetDateTime) noexcept -> bool = default;
};

} // namespace rstd::toml
