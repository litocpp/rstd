module;
#include <rstd/macro.hpp>
export module rstd:time;
export import rstd.core;
import :sys;
import :sys.libc.std;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#elif RSTD_OS_WINDOWS
import :sys.libc.windows;
#endif

namespace rstd::time
{

/// A span of time, with nanosecond precision.
export using rstd::time::Duration;

/// A measurement of a monotonically nondecreasing clock.
export struct Instant {
    sys::Instant inner;

    /// Returns an instant corresponding to "now".
    static auto now() noexcept -> Instant { return { sys::Instant::now() }; }

    /// Returns the amount of time elapsed since this instant was created.
    auto elapsed() const noexcept -> Duration { return inner.elapsed(); }

    /// Returns the amount of time elapsed from another instant to this one.
    /// \param other The earlier instant.
    /// \return The duration between the two instants.
    auto duration_since(Instant other) const noexcept -> Duration {
        return inner.duration_since(other.inner);
    }

    /// Returns Some(instant) if the addition does not overflow, or None otherwise.
    /// \param dur The duration to add.
    auto checked_add(Duration dur) const noexcept -> Option<Instant> {
        return inner.checked_add_duration(dur).map([](sys::Instant i) {
            return Instant { i };
        });
    }

    /// Returns Some(instant) if the subtraction does not underflow, or None otherwise.
    /// \param dur The duration to subtract.
    auto checked_sub(Duration dur) const noexcept -> Option<Instant> {
        return inner.checked_sub_duration(dur).map([](sys::Instant i) {
            return Instant { i };
        });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.inner == b.inner; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.inner <=> b.inner; }

    friend auto operator+(Instant a, Duration b) noexcept -> Instant {
        return a.checked_add(b).unwrap();
    }

    friend auto operator-(Instant a, Duration b) noexcept -> Instant {
        return a.checked_sub(b).unwrap();
    }

    friend auto operator-(Instant a, Instant b) noexcept -> Duration { return a.duration_since(b); }
};

/// A measurement of the system clock, useful for talking to external entities like the filesystem or other processes.
export struct SystemTime {
    sys::SystemTime inner;

    /// Returns the system time corresponding to "now".
    static auto now() noexcept -> SystemTime { return { sys::SystemTime::now() }; }

    /// Returns the system time corresponding to 1970-01-01 00:00:00 UTC.
    static auto unix_epoch() noexcept -> SystemTime { return { sys::SystemTime::unix_epoch() }; }

    /// Returns the duration since an earlier SystemTime, or an Err if `other` is later.
    /// \param other The earlier system time.
    auto duration_since(SystemTime other) const noexcept -> Result<Duration, Duration> {
        return inner.sub_time(other.inner);
    }

    /// Returns the elapsed time since this SystemTime was created.
    auto elapsed() const noexcept -> Result<Duration, Duration> {
        return now().duration_since(*this);
    }

    /// Returns Some(time) if the addition does not overflow, or None otherwise.
    /// \param dur The duration to add.
    auto checked_add(Duration dur) const noexcept -> Option<SystemTime> {
        return inner.checked_add_duration(dur).map([](sys::SystemTime s) {
            return SystemTime { s };
        });
    }

    /// Returns Some(time) if the subtraction does not underflow, or None otherwise.
    /// \param dur The duration to subtract.
    auto checked_sub(Duration dur) const noexcept -> Option<SystemTime> {
        return inner.checked_sub_duration(dur).map([](sys::SystemTime s) {
            return SystemTime { s };
        });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool {
        return a.inner == b.inner;
    }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.inner <=> b.inner; }

    friend auto operator+(SystemTime a, Duration b) noexcept -> SystemTime {
        return a.checked_add(b).unwrap();
    }

    friend auto operator-(SystemTime a, Duration b) noexcept -> SystemTime {
        return a.checked_sub(b).unwrap();
    }
};

/// Writes the current UTC time as RFC3339 (`YYYY-MM-DDTHH:MM:SSZ`) into `out` (exactly 20 bytes, no terminator).
export inline auto format_rfc3339_utc_now(char out[20]) noexcept -> void {
    auto secs = sys::libc::time(nullptr);
    auto tmv  = sys::libc::gmtime_utc(secs);

    auto w2 = [](char* p, int v) noexcept {
        p[0] = char('0' + (v / 10) % 10);
        p[1] = char('0' + v % 10);
    };
    int year = tmv.tm_year + 1900;
    out[0]   = char('0' + (year / 1000) % 10);
    out[1]   = char('0' + (year / 100) % 10);
    out[2]   = char('0' + (year / 10) % 10);
    out[3]   = char('0' + year % 10);
    out[4]   = '-';
    w2(out + 5, tmv.tm_mon + 1);
    out[7] = '-';
    w2(out + 8, tmv.tm_mday);
    out[10] = 'T';
    w2(out + 11, tmv.tm_hour);
    out[13] = ':';
    w2(out + 14, tmv.tm_min);
    out[16] = ':';
    w2(out + 17, tmv.tm_sec);
    out[19] = 'Z';
}

} // namespace rstd::time
