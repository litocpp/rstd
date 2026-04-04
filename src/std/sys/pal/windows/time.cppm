module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#endif
export module rstd:sys.pal.windows.time;

#if RSTD_OS_WINDOWS
import rstd.core;

namespace rstd::sys::pal::windows::time
{

// QueryPerformanceFrequency ticks per second (cached at first use).
[[gnu::always_inline]]
static inline auto qpc_freq() noexcept -> u64 {
    static u64 freq = [] {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return u64(li.QuadPart);
    }();
    return freq;
}

export struct Instant {
    u64 ticks; // QueryPerformanceCounter ticks

    static auto now() noexcept -> Instant {
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return { u64(li.QuadPart) };
    }

    auto elapsed() const noexcept -> rstd::time::Duration {
        return now().duration_since(*this);
    }

    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        u64 diff  = ticks >= other.ticks ? ticks - other.ticks : 0;
        u64 freq  = qpc_freq();
        u64 secs  = diff / freq;
        u64 rem   = diff % freq;
        // rem/freq < 1 s; multiply first to avoid losing precision.
        u32 nanos = u32(rem * u64(rstd::time::NANOS_PER_SEC) / freq);
        return rstd::time::Duration::new_(secs, nanos);
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        u64 freq      = qpc_freq();
        u64 add_ticks = dur.as_secs() * freq +
                        u64(dur.subsec_nanos()) * freq / u64(rstd::time::NANOS_PER_SEC);
        u64 result = ticks + add_ticks;
        if (result < ticks) return None(); // overflow
        return Some(Instant { result });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        u64 freq      = qpc_freq();
        u64 sub_ticks = dur.as_secs() * freq +
                        u64(dur.subsec_nanos()) * freq / u64(rstd::time::NANOS_PER_SEC);
        if (sub_ticks > ticks) return None();
        return Some(Instant { ticks - sub_ticks });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.ticks == b.ticks; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.ticks <=> b.ticks; }
};

// 100-nanosecond intervals since 1601-01-01 (FILETIME epoch).
export struct SystemTime {
    u64 intervals;

    static auto now() noexcept -> SystemTime {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        u64 v = (u64(ft.dwHighDateTime) << 32) | u64(ft.dwLowDateTime);
        return { v };
    }

    auto sub_time(SystemTime other) const noexcept -> Result<rstd::time::Duration, rstd::time::Duration> {
        constexpr u64 PER_SEC  = 10'000'000u; // 100ns intervals per second
        constexpr u32 NS_PER   = 100u;        // nanoseconds per interval
        if (intervals >= other.intervals) {
            u64 diff  = intervals - other.intervals;
            u64 secs  = diff / PER_SEC;
            u32 nanos = u32((diff % PER_SEC) * NS_PER);
            return Ok(rstd::time::Duration::new_(secs, nanos));
        } else {
            u64 diff  = other.intervals - intervals;
            u64 secs  = diff / PER_SEC;
            u32 nanos = u32((diff % PER_SEC) * NS_PER);
            return Err(rstd::time::Duration::new_(secs, nanos));
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        constexpr u64 PER_SEC = 10'000'000u;
        constexpr u64 NS_PER  = 100u;
        u64 add = dur.as_secs() * PER_SEC + u64(dur.subsec_nanos()) / NS_PER;
        u64 result = intervals + add;
        if (result < intervals) return None();
        return Some(SystemTime { result });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        constexpr u64 PER_SEC = 10'000'000u;
        constexpr u64 NS_PER  = 100u;
        u64 sub = dur.as_secs() * PER_SEC + u64(dur.subsec_nanos()) / NS_PER;
        if (sub > intervals) return None();
        return Some(SystemTime { intervals - sub });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool { return a.intervals == b.intervals; }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.intervals <=> b.intervals; }
};

} // namespace rstd::sys::pal::windows::time
#endif
