module;
#include <rstd/macro.hpp>
export module rstd:sys.pal;
import rstd.core;

#if RSTD_OS_UNIX
import :sys.pal.unix;
#if RSTD_OS_LINUX
import :sys.pal.unix.futex;
#endif
export namespace rstd::sys::pal
{
namespace backend = unix;
}
#elif RSTD_OS_WINDOWS
import :sys.pal.windows;
import :sys.pal.windows.futex;
export namespace rstd::sys::pal
{
namespace backend = windows;
}
#elif RSTD_OS_UNKNOWN
export namespace rstd::sys::pal::unknown
{

struct Instant {
    rstd::time::Duration value {};

    static auto now() noexcept -> Instant { rstd::panic { "monotonic time is unsupported" }; }
    auto        elapsed() const noexcept -> rstd::time::Duration {
        rstd::panic { "monotonic time is unsupported" };
    }
    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        if (value < other.value) return rstd::time::Duration {};
        return value - other.value;
    }
    auto checked_add_duration(rstd::time::Duration duration) const noexcept -> Option<Instant> {
        return value.checked_add(duration).map([](rstd::time::Duration result) {
            return Instant { result };
        });
    }
    auto checked_sub_duration(rstd::time::Duration duration) const noexcept -> Option<Instant> {
        return value.checked_sub(duration).map([](rstd::time::Duration result) {
            return Instant { result };
        });
    }

    friend auto operator==(Instant, Instant) noexcept -> bool = default;
    friend auto operator<=>(Instant, Instant) noexcept        = default;
};

struct UnixTime {
    i64 seconds {};
    u32 nanoseconds {};
};

struct SystemTime {
    i64 seconds {};
    u32 nanoseconds {};

    static auto now() noexcept -> SystemTime { rstd::panic { "system time is unsupported" }; }
    static constexpr auto unix_epoch() noexcept -> SystemTime { return {}; }
    static constexpr auto from_unix_time(i64 seconds, u32 nanoseconds) noexcept
        -> Option<SystemTime> {
        if (nanoseconds >= rstd::time::NANOS_PER_SEC) return None();
        return Some(SystemTime { seconds, nanoseconds });
    }
    constexpr auto as_unix_time() const noexcept -> UnixTime { return { seconds, nanoseconds }; }
    auto           sub_time(SystemTime other) const noexcept
        -> Result<rstd::time::Duration, rstd::time::Duration> {
        auto left       = static_cast<rstd::int128_t>(seconds.to_primitive()) *
                              rstd::time::NANOS_PER_SEC.to_primitive() +
                          nanoseconds.to_primitive();
        auto right      = static_cast<rstd::int128_t>(other.seconds.to_primitive()) *
                              rstd::time::NANOS_PER_SEC.to_primitive() +
                          other.nanoseconds.to_primitive();
        auto difference = left >= right ? left - right : right - left;
        auto duration   = rstd::time::Duration::new_(
            u64(static_cast<rstd::uint64_t>(difference / rstd::time::NANOS_PER_SEC.to_primitive())),
            u32(static_cast<rstd::uint32_t>(difference %
                                            rstd::time::NANOS_PER_SEC.to_primitive())));
        if (left >= right) {
            return Ok<rstd::time::Duration, rstd::time::Duration>(rstd::move(duration));
        }
        return Err<rstd::time::Duration, rstd::time::Duration>(rstd::move(duration));
    }
    auto checked_add_duration(rstd::time::Duration duration) const noexcept -> Option<SystemTime> {
        auto result_seconds = seconds.checked_add(i64(duration.as_secs().to_primitive()));
        if (result_seconds.is_none()) return None();
        auto result_nanoseconds = nanoseconds + duration.subsec_nanos();
        if (result_nanoseconds >= rstd::time::NANOS_PER_SEC) {
            result_nanoseconds -= rstd::time::NANOS_PER_SEC;
            result_seconds = result_seconds->checked_add(i64(1));
            if (result_seconds.is_none()) return None();
        }
        return Some(SystemTime { *result_seconds, result_nanoseconds });
    }
    auto checked_sub_duration(rstd::time::Duration duration) const noexcept -> Option<SystemTime> {
        auto result_seconds = seconds.checked_sub(i64(duration.as_secs().to_primitive()));
        if (result_seconds.is_none()) return None();
        auto result_nanoseconds = nanoseconds;
        if (result_nanoseconds < duration.subsec_nanos()) {
            result_nanoseconds += rstd::time::NANOS_PER_SEC;
            result_seconds = result_seconds->checked_sub(i64(1));
            if (result_seconds.is_none()) return None();
        }
        return Some(SystemTime { *result_seconds, result_nanoseconds - duration.subsec_nanos() });
    }

    friend auto operator==(SystemTime, SystemTime) noexcept -> bool = default;
    friend auto operator<=>(SystemTime, SystemTime) noexcept        = default;
};

inline auto local_offset_at_unix_time(i64) noexcept -> Option<i32> {
    return None();
}

} // namespace rstd::sys::pal::unknown

export namespace rstd::sys::pal
{
namespace backend = unknown;
}
#endif

export namespace rstd::sys::pal
{
using backend::Instant;
using backend::SystemTime;
using backend::local_offset_at_unix_time;

#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
namespace futex
{
using backend::futex::Duration;
using backend::futex::Primitive;
using backend::futex::Futex;
using backend::futex::SmallPrimitive;
using backend::futex::SmallFutex;
using backend::futex::futex_wait;
using backend::futex::futex_wake;
using backend::futex::futex_wake_all;
} // namespace futex
#endif

#if ! RSTD_OS_UNKNOWN
using backend::Mutex;
using backend::Condvar;
using backend::abort_internal;
using backend::exit_internal;
using backend::getpid_internal;
using backend::getenv_internal;
using backend::setenv_internal;
using backend::unsetenv_internal;
using backend::ArgcArgv;
using backend::args_capture;
using backend::args_argc_argv;
#endif

} // namespace rstd::sys::pal
