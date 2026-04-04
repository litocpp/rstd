module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.time;

#if RSTD_OS_UNIX
import rstd.core;
import :sys.libc.std;

namespace rstd::sys::pal::unix::time
{

struct Timespec {
    i64 tv_sec;
    u32 tv_nsec;

    static auto now(int clock_id) noexcept -> Timespec {
        libc::timespec ts;
        libc::clock_gettime(clock_id, &ts);
        return { (i64)ts.tv_sec, (u32)ts.tv_nsec };
    }

    auto sub_timespec(Timespec other) const noexcept -> rstd::time::Duration {
        if (*this >= other) {
            u64 secs = (u64)(tv_sec - other.tv_sec);
            u32 nanos;
            if (tv_nsec >= other.tv_nsec) {
                nanos = tv_nsec - other.tv_nsec;
            } else {
                nanos = tv_nsec + rstd::time::NANOS_PER_SEC - other.tv_nsec;
                secs -= 1;
            }
            return rstd::time::Duration::new_(secs, nanos);
        } else {
            // Rust's sub_timespec returns Result<Duration, Duration> (Ok if self >= other, Err otherwise)
            // For simplicity here, just return zero or handle it in SystemTime
            return rstd::time::Duration::from_secs(0);
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Timespec> {
        i64 secs = tv_sec + (i64)dur.as_secs();
        u32 nanos = tv_nsec + dur.subsec_nanos();
        if (nanos >= rstd::time::NANOS_PER_SEC) {
            nanos -= rstd::time::NANOS_PER_SEC;
            secs += 1;
        }
        return Some(Timespec { secs, nanos });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Timespec> {
        i64 secs = tv_sec - (i64)dur.as_secs();
        u32 nanos;
        if (tv_nsec >= dur.subsec_nanos()) {
            nanos = tv_nsec - dur.subsec_nanos();
        } else {
            nanos = tv_nsec + rstd::time::NANOS_PER_SEC - dur.subsec_nanos();
            secs -= 1;
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

    static auto now() noexcept -> Instant {
        return { Timespec::now(libc::M_CLOCK_MONOTONIC) };
    }

    auto elapsed() const noexcept -> rstd::time::Duration {
        return now().t.sub_timespec(t);
    }

    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        return t.sub_timespec(other.t);
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return t.checked_add_duration(dur).map([](Timespec ts) { return Instant { ts }; });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return t.checked_sub_duration(dur).map([](Timespec ts) { return Instant { ts }; });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.t <=> b.t; }
};

export struct SystemTime {
    Timespec t;

    static auto now() noexcept -> SystemTime {
        return { Timespec::now(libc::M_CLOCK_REALTIME) };
    }

    auto sub_time(SystemTime other) const noexcept -> Result<rstd::time::Duration, rstd::time::Duration> {
        if (t >= other.t) {
            return Ok(t.sub_timespec(other.t));
        } else {
            return Err(other.t.sub_timespec(t));
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return t.checked_add_duration(dur).map([](Timespec ts) { return SystemTime { ts }; });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return t.checked_sub_duration(dur).map([](Timespec ts) { return SystemTime { ts }; });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.t <=> b.t; }
};

} // namespace rstd::sys::pal::unix::time
#endif
