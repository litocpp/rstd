export module rstd.core:time;
import :cmp;
import :fmt;
import :convert;
import :ops;
import :option;
import :clone;

namespace rstd::time
{

/// Number of nanoseconds in one second.
export const u32 NANOS_PER_SEC   = 1'000'000'000u;
/// Number of nanoseconds in one millisecond.
export const u32 NANOS_PER_MILLI = 1'000'000u;
/// Number of nanoseconds in one microsecond.
export const u32 NANOS_PER_MICRO = 1'000u;
/// Number of milliseconds in one second.
export const u64 MILLIS_PER_SEC  = 1'000u;
/// Number of microseconds in one second.
export const u64 MICROS_PER_SEC  = 1'000'000u;

/// A span of time, with nanosecond precision.
export struct Duration {
    u64 _secs;
    u32 _nanos; // invariant: always < NANOS_PER_SEC

    // ── Constructors ──────────────────────────────────────────────────────
    static constexpr auto new_(u64 secs, u32 nanos) noexcept -> Duration {
        secs  += nanos / NANOS_PER_SEC;
        nanos %= NANOS_PER_SEC;
        return { secs, nanos };
    }

    static constexpr auto from_secs(u64 secs) noexcept -> Duration {
        return { secs, 0 };
    }

    static constexpr auto from_millis(u64 millis) noexcept -> Duration {
        return { millis / MILLIS_PER_SEC, u32(millis % MILLIS_PER_SEC) * NANOS_PER_MILLI };
    }

    static constexpr auto from_micros(u64 micros) noexcept -> Duration {
        return { micros / MICROS_PER_SEC, u32(micros % MICROS_PER_SEC) * NANOS_PER_MICRO };
    }

    static constexpr auto from_nanos(u64 nanos) noexcept -> Duration {
        return { nanos / u64(NANOS_PER_SEC), u32(nanos % u64(NANOS_PER_SEC)) };
    }

    static auto from_secs_f64(double secs) noexcept -> Duration {
        if (secs < 0.0) return { 0, 0 };
        auto whole = static_cast<u64>(secs);
        auto nanos = static_cast<u32>((secs - static_cast<double>(whole)) * double(NANOS_PER_SEC) + 0.5);
        if (nanos >= NANOS_PER_SEC) { nanos = NANOS_PER_SEC - 1; }
        return { whole, nanos };
    }

    // ── Queries ───────────────────────────────────────────────────────────
    constexpr auto is_zero()       const noexcept -> bool  { return _secs == 0 && _nanos == 0; }
    constexpr auto as_secs()       const noexcept -> u64   { return _secs; }
    constexpr auto subsec_millis() const noexcept -> u32   { return _nanos / NANOS_PER_MILLI; }
    constexpr auto subsec_micros() const noexcept -> u32   { return _nanos / NANOS_PER_MICRO; }
    constexpr auto subsec_nanos()  const noexcept -> u32   { return _nanos; }
    constexpr auto as_millis()     const noexcept -> u64   { return _secs * MILLIS_PER_SEC + u64(_nanos / NANOS_PER_MILLI); }
    constexpr auto as_micros()     const noexcept -> u64   { return _secs * MICROS_PER_SEC + u64(_nanos / NANOS_PER_MICRO); }
    // as_nanos returns u128 matching Rust (u64 overflows after ~584 years).
    constexpr auto as_nanos()      const noexcept -> u128  { return u128(_secs) * u128(NANOS_PER_SEC) + u128(_nanos); }
    constexpr auto as_secs_f64()   const noexcept -> double {
        return static_cast<double>(_secs) + static_cast<double>(_nanos) / double(NANOS_PER_SEC);
    }

    // ── Comparison ────────────────────────────────────────────────────────
    friend constexpr auto operator==(Duration a, Duration b) noexcept -> bool {
        return a._secs == b._secs && a._nanos == b._nanos;
    }

    friend constexpr auto operator<=>(Duration a, Duration b) noexcept {
        if (auto c = a._secs <=> b._secs; c != 0) return c;
        return a._nanos <=> b._nanos;
    }

    // ── Checked / saturating arithmetic ──────────────────────────────────
    constexpr auto checked_add(Duration rhs) const noexcept -> Option<Duration> {
        u32 nanos = _nanos + rhs._nanos;
        u64 carry = nanos >= NANOS_PER_SEC ? 1u : 0u;
        if (carry) nanos -= NANOS_PER_SEC;
        u64 secs = _secs + rhs._secs + carry;
        if (secs < _secs + carry) return None(); // overflow (rare)
        return Some(Duration { secs, nanos });
    }

    constexpr auto checked_sub(Duration rhs) const noexcept -> Option<Duration> {
        if (_secs < rhs._secs || (_secs == rhs._secs && _nanos < rhs._nanos)) return None();
        u64 secs = _secs - rhs._secs;
        u32 nanos;
        if (_nanos >= rhs._nanos) {
            nanos = _nanos - rhs._nanos;
        } else {
            nanos = _nanos + NANOS_PER_SEC - rhs._nanos;
            --secs;
        }
        return Some(Duration { secs, nanos });
    }

    constexpr auto saturating_add(Duration rhs) const noexcept -> Duration {
        auto r = checked_add(rhs);
        return r.is_some() ? r.unwrap_unchecked() : Duration { u64(-1), NANOS_PER_SEC - 1 };
    }

    constexpr auto saturating_sub(Duration rhs) const noexcept -> Duration {
        auto r = checked_sub(rhs);
        return r.is_some() ? r.unwrap_unchecked() : Duration { 0, 0 };
    }

    // ── Arithmetic operators ──────────────────────────────────────────────
    friend constexpr auto operator+(Duration a, Duration b) noexcept -> Duration {
        return a.saturating_add(b);
    }

    friend constexpr auto operator-(Duration a, Duration b) noexcept -> Duration {
        return a.saturating_sub(b);
    }

    friend constexpr auto operator*(Duration a, u32 b) noexcept -> Duration {
        u128 total = u128(a._secs) * u128(b) * u128(NANOS_PER_SEC) + u128(a._nanos) * u128(b);
        return { u64(total / NANOS_PER_SEC), u32(total % NANOS_PER_SEC) };
    }

    friend constexpr auto operator*(u32 b, Duration a) noexcept -> Duration { return a * b; }

    friend constexpr auto operator/(Duration a, u32 b) noexcept -> Duration {
        u128 total = u128(a._secs) * u128(NANOS_PER_SEC) + u128(a._nanos);
        total /= u128(b);
        return { u64(total / NANOS_PER_SEC), u32(total % NANOS_PER_SEC) };
    }

    constexpr auto& operator+=(Duration rhs) noexcept { *this = *this + rhs; return *this; }
    constexpr auto& operator-=(Duration rhs) noexcept { *this = *this - rhs; return *this; }
    constexpr auto& operator*=(u32 rhs)      noexcept { *this = *this * rhs; return *this; }
    constexpr auto& operator/=(u32 rhs)      noexcept { *this = *this / rhs; return *this; }
};

/// A duration of zero time.
export inline constexpr Duration Duration_ZERO        = { 0, 0 };
/// The maximum representable duration.
export inline constexpr Duration Duration_MAX         = { u64(-1), NANOS_PER_SEC - 1 };
/// A duration of exactly one second.
export inline constexpr Duration Duration_SECOND      = { 1, 0 };
/// A duration of exactly one millisecond.
export inline constexpr Duration Duration_MILLISECOND = { 0, NANOS_PER_MILLI };
/// A duration of exactly one microsecond.
export inline constexpr Duration Duration_MICROSECOND = { 0, NANOS_PER_MICRO };
/// A duration of exactly one nanosecond.
export inline constexpr Duration Duration_NANOSECOND  = { 0, 1 };

} // namespace rstd::time
