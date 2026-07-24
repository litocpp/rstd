module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.time;

#if RSTD_OS_UNIX
import rstd.core;
import :sys.libc.std;
import :sys.libc.unix;

namespace rstd::sys::pal::unix::time
{

export struct Timespec {
    i64 tv_sec;
    u32 tv_nsec;

private:
    static auto checked_seconds(rstd::int128_t value) noexcept -> Option<i64> {
        if (value < static_cast<rstd::int128_t>(i64::MIN.to_primitive()) ||
            value > static_cast<rstd::int128_t>(i64::MAX.to_primitive())) {
            return None();
        }
        return Some(i64(value));
    }

public:
    static auto now(int clock_id) noexcept -> Timespec {
        libc::timespec ts;
        libc::clock_gettime(clock_id, &ts);
        return { i64(ts.tv_sec), u32(ts.tv_nsec) };
    }

    auto sub_timespec(Timespec other) const noexcept -> rstd::time::Duration {
        if (*this >= other) {
            auto const difference = static_cast<rstd::uint128_t>(
                static_cast<rstd::int128_t>(tv_sec.to_primitive()) -
                static_cast<rstd::int128_t>(other.tv_sec.to_primitive()));
            u64 secs(static_cast<rstd::uint64_t>(difference));
            u32 nanos;
            if (tv_nsec >= other.tv_nsec) {
                nanos = tv_nsec - other.tv_nsec;
            } else {
                nanos = tv_nsec + rstd::time::NANOS_PER_SEC - other.tv_nsec;
                --secs;
            }
            return rstd::time::Duration::new_(secs, nanos);
        } else {
            return rstd::time::Duration::from_secs(u64 {});
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Timespec> {
        auto seconds = checked_seconds(static_cast<rstd::int128_t>(tv_sec.to_primitive()) +
                                       static_cast<rstd::int128_t>(dur.as_secs().to_primitive()));
        if (seconds.is_none()) return None();
        i64 secs  = rstd::move(seconds).unwrap_unchecked();
        u32 nanos = tv_nsec + dur.subsec_nanos();
        if (nanos >= rstd::time::NANOS_PER_SEC) {
            nanos -= rstd::time::NANOS_PER_SEC;
            auto with_carry = secs.checked_add(i64(1));
            if (with_carry.is_none()) return None();
            secs = rstd::move(with_carry).unwrap_unchecked();
        }
        return Some(Timespec { secs, nanos });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Timespec> {
        auto seconds = checked_seconds(static_cast<rstd::int128_t>(tv_sec.to_primitive()) -
                                       static_cast<rstd::int128_t>(dur.as_secs().to_primitive()));
        if (seconds.is_none()) return None();
        i64 secs = rstd::move(seconds).unwrap_unchecked();
        u32 nanos;
        if (tv_nsec >= dur.subsec_nanos()) {
            nanos = tv_nsec - dur.subsec_nanos();
        } else {
            nanos            = tv_nsec + rstd::time::NANOS_PER_SEC - dur.subsec_nanos();
            auto with_borrow = secs.checked_sub(i64(1));
            if (with_borrow.is_none()) return None();
            secs = rstd::move(with_borrow).unwrap_unchecked();
        }
        return Some(Timespec { secs, nanos });
    }

    friend auto operator>=(Timespec a, Timespec b) noexcept -> bool {
        if (a.tv_sec > b.tv_sec) return true;
        if (a.tv_sec < b.tv_sec) return false;
        return a.tv_nsec >= b.tv_nsec;
    }

    friend auto operator==(Timespec a, Timespec b) noexcept -> bool {
        return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
    }

    friend auto operator<=>(Timespec a, Timespec b) noexcept {
        if (auto cmp = a.tv_sec <=> b.tv_sec; cmp != 0) return cmp;
        return a.tv_nsec <=> b.tv_nsec;
    }
};

export struct Instant {
    Timespec t;

    static auto now() noexcept -> Instant { return { Timespec::now(libc::CLOCK_MONOTONIC) }; }

    auto elapsed() const noexcept -> rstd::time::Duration { return now().t.sub_timespec(t); }

    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        return t.sub_timespec(other.t);
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return t.checked_add_duration(dur).map([](Timespec ts) {
            return Instant { ts };
        });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return t.checked_sub_duration(dur).map([](Timespec ts) {
            return Instant { ts };
        });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.t <=> b.t; }
};

export struct UnixTime {
    i64 seconds;
    u32 nanoseconds;
};

export struct SystemTime {
    Timespec t;

    static auto now() noexcept -> SystemTime { return { Timespec::now(libc::CLOCK_REALTIME) }; }

    static auto unix_epoch() noexcept -> SystemTime { return { Timespec { i64 {}, u32 {} } }; }

    static auto from_unix_time(i64 seconds, u32 nanoseconds) noexcept -> Option<SystemTime> {
        if (nanoseconds >= rstd::time::NANOS_PER_SEC) return None();
        return Some(SystemTime { Timespec { seconds, nanoseconds } });
    }

    auto as_unix_time() const noexcept -> UnixTime { return { t.tv_sec, t.tv_nsec }; }

    auto sub_time(SystemTime other) const noexcept
        -> Result<rstd::time::Duration, rstd::time::Duration> {
        if (t >= other.t) {
            return Ok(t.sub_timespec(other.t));
        } else {
            return Err(other.t.sub_timespec(t));
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return t.checked_add_duration(dur).map([](Timespec ts) {
            return SystemTime { ts };
        });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return t.checked_sub_duration(dur).map([](Timespec ts) {
            return SystemTime { ts };
        });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.t <=> b.t; }
};

export auto local_offset_at_unix_time(i64 seconds) noexcept -> Option<i32> {
    auto native = rstd::try_from<libc::time_t>(seconds);
    if (native.is_err()) return None();

    libc::tm local {};
    if (! libc::localtime_into(native.unwrap(), local)) return None();

    auto offset = rstd::try_from<i32>(local.tm_gmtoff);
    if (offset.is_err()) return None();
    return Some(offset.unwrap());
}

} // namespace rstd::sys::pal::unix::time
#endif
