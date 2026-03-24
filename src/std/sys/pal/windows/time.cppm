module;
#include <chrono>
export module rstd:sys.pal.windows.time;
import rstd.core;

namespace rstd::sys::pal::windows::time
{

export struct Instant {
    using ChronoInstant = std::chrono::steady_clock::time_point;
    ChronoInstant t;

    static auto now() noexcept -> Instant {
        return { std::chrono::steady_clock::now() };
    }

    auto elapsed() const noexcept -> rstd::time::Duration {
        auto d = std::chrono::steady_clock::now() - t;
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
        return rstd::time::Duration::from_nanos((u64)nanos);
    }

    auto duration_since(Instant other) const noexcept -> rstd::time::Duration {
        auto d = t - other.t;
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
        return rstd::time::Duration::from_nanos((u64)nanos);
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return Some(Instant { t + std::chrono::nanoseconds(dur.as_nanos()) });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<Instant> {
        return Some(Instant { t - std::chrono::nanoseconds(dur.as_nanos()) });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.t <=> b.t; }
};

export struct SystemTime {
    using ChronoSystemTime = std::chrono::system_clock::time_point;
    ChronoSystemTime t;

    static auto now() noexcept -> SystemTime {
        return { std::chrono::system_clock::now() };
    }

    auto sub_time(SystemTime other) const noexcept -> Result<rstd::time::Duration, rstd::time::Duration> {
        if (t >= other.t) {
            auto d = t - other.t;
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
            return Ok(rstd::time::Duration::from_nanos((u64)nanos));
        } else {
            auto d = other.t - t;
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
            return Err(rstd::time::Duration::from_nanos((u64)nanos));
        }
    }

    auto checked_add_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return Some(SystemTime { t + std::chrono::nanoseconds(dur.as_nanos()) });
    }

    auto checked_sub_duration(rstd::time::Duration dur) const noexcept -> Option<SystemTime> {
        return Some(SystemTime { t - std::chrono::nanoseconds(dur.as_nanos()) });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool { return a.t == b.t; }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.t <=> b.t; }
};

} // namespace rstd::sys::pal::windows::time
