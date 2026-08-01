module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.windows.time;

#if RSTD_OS_WINDOWS
import rstd.core;
import :sys.libc.windows;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::windows::time
{

inline constexpr u128
    QPC_NANOS_PER_SEC(static_cast<rstd::uint128_t>(rstd::time::NANOS_PER_SEC.to_primitive()));
inline constexpr u128 FILETIME_INTERVALS_PER_SEC(static_cast<rstd::uint128_t>(10'000'000));
inline constexpr u32  FILETIME_NANOS_PER_INTERVAL(rstd::uint32_t(100));

static constexpr auto widen(u64 value) noexcept -> u128 {
    return u128(value.to_primitive());
}

static constexpr auto widen(u32 value) noexcept -> u128 {
    return u128(value.to_primitive());
}

static constexpr auto signed_widen(u64 value) noexcept -> i128 {
    return i128(value.to_primitive());
}

static constexpr auto signed_widen(i64 value) noexcept -> i128 {
    return i128(value.to_primitive());
}

static constexpr auto signed_widen(u32 value) noexcept -> i128 {
    return i128(value.to_primitive());
}

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

static auto duration_to_qpc_ticks(rstd::time::Duration duration, u64 frequency) noexcept
    -> Option<u128> {
    auto whole = widen(duration.as_secs()).checked_mul(widen(frequency));
    if (whole.is_none()) return None();

    auto fractional = widen(duration.subsec_nanos()).checked_mul(widen(frequency));
    if (fractional.is_none()) return None();
    fractional = fractional->checked_div(QPC_NANOS_PER_SEC);
    if (fractional.is_none()) return None();
    return whole->checked_add(*fractional);
}

static auto duration_to_filetime_intervals(rstd::time::Duration duration) noexcept -> Option<u128> {
    auto whole = widen(duration.as_secs()).checked_mul(FILETIME_INTERVALS_PER_SEC);
    if (whole.is_none()) return None();

    auto fractional = widen(duration.subsec_nanos()) / widen(FILETIME_NANOS_PER_INTERVAL);
    return whole->checked_add(fractional);
}

export struct Instant {
    u64 ticks; // QueryPerformanceCounter ticks

    static auto now() noexcept -> Instant {
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return { u64(li.QuadPart) };
    }

    auto elapsed() const noexcept -> rstd::time::Duration { return now().duration_since(*this); }

    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        auto const diff = ticks >= other.ticks ? ticks - other.ticks : u64();
        auto const freq = qpc_freq();
        auto const secs = diff / freq;
        auto const rem  = diff % freq;
        auto const nanos =
            rstd::try_from<u32>(widen(rem) * QPC_NANOS_PER_SEC / widen(freq)).unwrap();
        return rstd::time::Duration::new_(secs, nanos);
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        auto add_ticks = duration_to_qpc_ticks(dur, qpc_freq());
        if (add_ticks.is_none()) return None();
        auto result = widen(ticks).checked_add(*add_ticks);
        if (result.is_none()) return None();
        auto narrowed = rstd::try_from<u64>(*result);
        if (narrowed.is_err()) return None();
        return Some(Instant { narrowed.unwrap() });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        auto sub_ticks = duration_to_qpc_ticks(dur, qpc_freq());
        if (sub_ticks.is_none()) return None();
        auto result = widen(ticks).checked_sub(*sub_ticks);
        if (result.is_none()) return None();
        return Some(Instant { rstd::try_from<u64>(*result).unwrap() });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.ticks == b.ticks; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.ticks <=> b.ticks; }
};

export struct UnixTime {
    i64 seconds;
    u32 nanoseconds;
};

// 100-nanosecond intervals since 1601-01-01 (FILETIME epoch).
export struct SystemTime {
    u64 intervals;

    static auto now() noexcept -> SystemTime {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        auto const v = (u64(ft.dwHighDateTime) << u64(32)) | u64(ft.dwLowDateTime);
        return { v };
    }

    static auto unix_epoch() noexcept -> SystemTime { return { u64(116'444'736'000'000'000) }; }

    static auto from_unix_time(i64 seconds, u32 nanoseconds) noexcept -> Option<SystemTime> {
        if (nanoseconds >= rstd::time::NANOS_PER_SEC) return None();

        auto seconds_as_intervals = signed_widen(seconds).checked_mul(i128(10'000'000));
        if (seconds_as_intervals.is_none()) return None();
        auto value = signed_widen(unix_epoch().intervals).checked_add(*seconds_as_intervals);
        if (value.is_none()) return None();
        value = value->checked_add(signed_widen(nanoseconds / FILETIME_NANOS_PER_INTERVAL));
        if (value.is_none()) return None();

        auto narrowed = rstd::try_from<u64>(*value);
        if (narrowed.is_err()) return None();
        return Some(SystemTime { narrowed.unwrap() });
    }

    auto as_unix_time() const noexcept -> UnixTime {
        auto value     = signed_widen(intervals).checked_sub(signed_widen(unix_epoch().intervals));
        auto seconds   = value->checked_div(i128(10'000'000)).unwrap();
        auto remainder = value->checked_rem(i128(10'000'000)).unwrap();
        if (remainder < i128()) {
            seconds -= i128(1);
            remainder += i128(10'000'000);
        }
        return { rstd::try_from<i64>(seconds).unwrap(),
                 rstd::try_from<u32>(remainder * i128(100)).unwrap() };
    }

    auto sub_time(SystemTime other) const noexcept
        -> Result<rstd::time::Duration, rstd::time::Duration> {
        if (intervals >= other.intervals) {
            auto const diff  = widen(intervals - other.intervals);
            auto const secs  = rstd::try_from<u64>(diff / FILETIME_INTERVALS_PER_SEC).unwrap();
            auto const nanos = rstd::try_from<u32>(diff % FILETIME_INTERVALS_PER_SEC).unwrap() *
                               FILETIME_NANOS_PER_INTERVAL;
            return Ok(rstd::time::Duration::new_(secs, nanos));
        } else {
            auto const diff  = widen(other.intervals - intervals);
            auto const secs  = rstd::try_from<u64>(diff / FILETIME_INTERVALS_PER_SEC).unwrap();
            auto const nanos = rstd::try_from<u32>(diff % FILETIME_INTERVALS_PER_SEC).unwrap() *
                               FILETIME_NANOS_PER_INTERVAL;
            return Err(rstd::time::Duration::new_(secs, nanos));
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        auto add = duration_to_filetime_intervals(dur);
        if (add.is_none()) return None();
        auto result = widen(intervals).checked_add(*add);
        if (result.is_none()) return None();
        auto narrowed = rstd::try_from<u64>(*result);
        if (narrowed.is_err()) return None();
        return Some(SystemTime { narrowed.unwrap() });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        auto sub = duration_to_filetime_intervals(dur);
        if (sub.is_none()) return None();
        auto result = widen(intervals).checked_sub(*sub);
        if (result.is_none()) return None();
        return Some(SystemTime { rstd::try_from<u64>(*result).unwrap() });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool {
        return a.intervals == b.intervals;
    }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept {
        return a.intervals <=> b.intervals;
    }
};

static auto filetime_value(FILETIME value) noexcept -> u64 {
    return (u64(value.dwHighDateTime) << u64(32)) | u64(value.dwLowDateTime);
}

static auto make_filetime(u64 value) noexcept -> FILETIME {
    auto const raw = value.to_primitive();
    return { .dwLowDateTime  = static_cast<DWORD>(raw),
             .dwHighDateTime = static_cast<DWORD>(raw >> 32) };
}

export auto local_offset_at_unix_time(i64 seconds) noexcept -> Option<i32> {
    auto instant = SystemTime::from_unix_time(seconds, u32());
    if (instant.is_none()) return None();

    auto       utc_file = make_filetime(instant->intervals);
    SYSTEMTIME utc {};
    if (! FileTimeToSystemTime(&utc_file, &utc)) return None();

    SYSTEMTIME local {};
    if (! SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return None();

    FILETIME normalized_utc {};
    FILETIME normalized_local {};
    if (! SystemTimeToFileTime(&utc, &normalized_utc) ||
        ! SystemTimeToFileTime(&local, &normalized_local)) {
        return None();
    }

    auto const difference = signed_widen(filetime_value(normalized_local)) -
                            signed_widen(filetime_value(normalized_utc));
    auto       offset     = rstd::try_from<i32>(difference / i128(10'000'000));
    if (offset.is_err()) return None();
    return Some(offset.unwrap());
}

} // namespace rstd::sys::pal::windows::time
#endif
