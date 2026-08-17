export module rstd:sys.pal.windows.time;

import rstd.core;

namespace rstd::sys::pal::windows::time
{

export struct Instant {
    u64 ticks;

    static auto now() noexcept -> Instant;
    auto        elapsed() const noexcept -> rstd::time::Duration;
    auto        duration_since(Instant other) const noexcept -> rstd::time::Duration;
    auto        checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant>;
    auto        checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant>;

    friend auto operator==(Instant a, Instant b) noexcept -> bool;
    friend auto operator<=>(Instant a, Instant b) noexcept -> decltype(u64 {} <=> u64 {});
};

export struct UnixTime {
    i64 seconds;
    u32 nanoseconds;
};

export struct SystemTime {
    u64 intervals;

    static auto now() noexcept -> SystemTime;
    static auto unix_epoch() noexcept -> SystemTime;
    static auto from_unix_time(i64 seconds, u32 nanoseconds) noexcept -> Option<SystemTime>;
    auto        as_unix_time() const noexcept -> UnixTime;
    auto        sub_time(SystemTime other) const noexcept
        -> Result<rstd::time::Duration, rstd::time::Duration>;
    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime>;
    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime>;

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool;
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept -> decltype(u64 {} <=> u64 {});
};

export auto local_offset_at_unix_time(i64 seconds) noexcept -> Option<i32>;

} // namespace rstd::sys::pal::windows::time
