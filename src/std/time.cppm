export module rstd:time;
export import rstd.core;
import :sys;

namespace rstd::time
{

export using rstd::time::Duration;

export struct Instant {
    sys::Instant inner;

    static auto now() noexcept -> Instant {
        return { sys::Instant::now() };
    }

    auto elapsed() const noexcept -> Duration {
        return inner.elapsed();
    }

    auto duration_since(Instant other) const noexcept -> Duration {
        return inner.duration_since(other.inner);
    }

    auto checked_add(Duration dur) const noexcept -> Option<Instant> {
        return inner.checked_add_duration(dur).map([](sys::Instant i) { return Instant { i }; });
    }

    auto checked_sub(Duration dur) const noexcept -> Option<Instant> {
        return inner.checked_sub_duration(dur).map([](sys::Instant i) { return Instant { i }; });
    }

    friend auto operator==(Instant a, Instant b) noexcept -> bool { return a.inner == b.inner; }
    friend auto operator<=>(Instant a, Instant b) noexcept { return a.inner <=> b.inner; }

    friend auto operator+(Instant a, Duration b) noexcept -> Instant {
        return a.checked_add(b).unwrap();
    }

    friend auto operator-(Instant a, Duration b) noexcept -> Instant {
        return a.checked_sub(b).unwrap();
    }

    friend auto operator-(Instant a, Instant b) noexcept -> Duration {
        return a.duration_since(b);
    }
};

export struct SystemTime {
    sys::SystemTime inner;

    static auto now() noexcept -> SystemTime {
        return { sys::SystemTime::now() };
    }

    auto duration_since(SystemTime other) const noexcept -> Result<Duration, Duration> {
        return inner.sub_time(other.inner);
    }

    auto elapsed() const noexcept -> Result<Duration, Duration> {
        return now().duration_since(*this);
    }

    auto checked_add(Duration dur) const noexcept -> Option<SystemTime> {
        return inner.checked_add_duration(dur).map([](sys::SystemTime s) { return SystemTime { s }; });
    }

    auto checked_sub(Duration dur) const noexcept -> Option<SystemTime> {
        return inner.checked_sub_duration(dur).map([](sys::SystemTime s) { return SystemTime { s }; });
    }

    friend auto operator==(SystemTime a, SystemTime b) noexcept -> bool { return a.inner == b.inner; }
    friend auto operator<=>(SystemTime a, SystemTime b) noexcept { return a.inner <=> b.inner; }

    friend auto operator+(SystemTime a, Duration b) noexcept -> SystemTime {
        return a.checked_add(b).unwrap();
    }

    friend auto operator-(SystemTime a, Duration b) noexcept -> SystemTime {
        return a.checked_sub(b).unwrap();
    }
};

} // namespace rstd::time
