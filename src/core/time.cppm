export module rstd.core:time;
import :num.types;
import :num.integer_methods;
import :cmp;
import :fmt;
import :convert;
import :ops;
import :option;
import :clone;

namespace rstd::time
{

/// Number of nanoseconds in one second.
export inline constexpr u32 NANOS_PER_SEC { rstd::uint32_t(1'000'000'000) };
/// Number of nanoseconds in one millisecond.
export inline constexpr u32 NANOS_PER_MILLI { rstd::uint32_t(1'000'000) };
/// Number of nanoseconds in one microsecond.
export inline constexpr u32 NANOS_PER_MICRO { rstd::uint32_t(1'000) };
/// Number of milliseconds in one second.
export inline constexpr u64 MILLIS_PER_SEC { rstd::uint64_t(1'000) };
/// Number of microseconds in one second.
export inline constexpr u64 MICROS_PER_SEC { rstd::uint64_t(1'000'000) };

/// A span of time, with nanosecond precision.
export struct Duration {
    u64 _secs;
    u32 _nanos; // invariant: always < NANOS_PER_SEC

    // ── Constructors ──────────────────────────────────────────────────────
    static constexpr auto new_(u64 secs, u32 nanos) noexcept -> Duration {
        secs += u64((nanos / NANOS_PER_SEC).to_primitive());
        nanos %= NANOS_PER_SEC;
        return { secs, nanos };
    }

    static constexpr auto from_secs(u64 secs) noexcept -> Duration { return { secs, u32() }; }

    static constexpr auto from_millis(u64 millis) noexcept -> Duration {
        auto const remainder = (millis % MILLIS_PER_SEC).to_primitive();
        return { millis / MILLIS_PER_SEC, u32(remainder) * NANOS_PER_MILLI };
    }

    static constexpr auto from_micros(u64 micros) noexcept -> Duration {
        auto const remainder = (micros % MICROS_PER_SEC).to_primitive();
        return { micros / MICROS_PER_SEC, u32(remainder) * NANOS_PER_MICRO };
    }

    static constexpr auto from_nanos(u64 nanos) noexcept -> Duration {
        auto const per_second = u64(NANOS_PER_SEC.to_primitive());
        return { nanos / per_second, u32((nanos % per_second).to_primitive()) };
    }

    static auto from_secs_f64(double secs) noexcept -> Duration {
        if (__builtin_isnan(secs) || secs <= 0.0) return { u64(), u32() };
        auto const maximum = static_cast<double>(u64::MAX.to_primitive());
        if (secs >= maximum) {
            return { u64::MAX, NANOS_PER_SEC - u32(1) };
        }
        auto const whole = static_cast<rstd::uint64_t>(secs);
        auto       nanos =
            static_cast<rstd::uint32_t>((secs - static_cast<double>(whole)) *
                                            static_cast<double>(NANOS_PER_SEC.to_primitive()) +
                                        0.5);
        if (nanos >= NANOS_PER_SEC.to_primitive()) {
            if (whole == u64::MAX.to_primitive()) {
                return { u64::MAX, NANOS_PER_SEC - u32(1) };
            }
            return { u64(whole + rstd::uint64_t(1)), u32() };
        }
        return { u64(whole), u32(nanos) };
    }

    // ── Queries ───────────────────────────────────────────────────────────
    constexpr auto is_zero() const noexcept -> bool { return _secs == u64() && _nanos == u32(); }
    constexpr auto as_secs() const noexcept -> u64 { return _secs; }
    constexpr auto subsec_millis() const noexcept -> u32 { return _nanos / NANOS_PER_MILLI; }
    constexpr auto subsec_micros() const noexcept -> u32 { return _nanos / NANOS_PER_MICRO; }
    constexpr auto subsec_nanos() const noexcept -> u32 { return _nanos; }
    constexpr auto as_millis() const noexcept -> u64 {
        return _secs * MILLIS_PER_SEC + u64((_nanos / NANOS_PER_MILLI).to_primitive());
    }
    constexpr auto as_micros() const noexcept -> u64 {
        return _secs * MICROS_PER_SEC + u64((_nanos / NANOS_PER_MICRO).to_primitive());
    }
    // as_nanos returns u128 matching Rust (u64 overflows after ~584 years).
    constexpr auto as_nanos() const noexcept -> u128 {
        return u128(_secs.to_primitive()) * u128(NANOS_PER_SEC.to_primitive()) +
               u128(_nanos.to_primitive());
    }
    constexpr auto as_secs_f64() const noexcept -> double {
        return static_cast<double>(_secs.to_primitive()) +
               static_cast<double>(_nanos.to_primitive()) /
                   static_cast<double>(NANOS_PER_SEC.to_primitive());
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
        u32        nanos = _nanos + rhs._nanos;
        bool const carry = nanos >= NANOS_PER_SEC;
        if (carry) nanos -= NANOS_PER_SEC;

        auto secs = _secs.checked_add(rhs._secs);
        if (secs.is_none()) return None();
        auto value = rstd::move(secs).unwrap_unchecked();
        if (carry) {
            auto with_carry = value.checked_add(u64(1));
            if (with_carry.is_none()) return None();
            value = rstd::move(with_carry).unwrap_unchecked();
        }
        return Some(Duration { value, nanos });
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

    constexpr auto checked_mul(u32 rhs) const noexcept -> Option<Duration> {
        auto const factor     = u128(rhs.to_primitive());
        auto const per_second = u128(NANOS_PER_SEC.to_primitive());
        auto       total = (u128(_secs.to_primitive()) * per_second + u128(_nanos.to_primitive()))
                               .checked_mul(factor);
        if (total.is_none()) return None();
        auto const value = rstd::move(total).unwrap_unchecked();
        auto const secs  = value / per_second;
        if (secs > u128(u64::MAX.to_primitive())) return None();
        return Some(
            Duration { u64(secs.to_primitive()), u32((value % per_second).to_primitive()) });
    }

    constexpr auto checked_div(u32 rhs) const noexcept -> Option<Duration> {
        if (rhs == u32()) return None();
        auto const per_second = u128(NANOS_PER_SEC.to_primitive());
        auto const total = u128(_secs.to_primitive()) * per_second + u128(_nanos.to_primitive());
        auto const value = total / u128(rhs.to_primitive());
        return Some(Duration { u64((value / per_second).to_primitive()),
                               u32((value % per_second).to_primitive()) });
    }

    constexpr auto saturating_add(Duration rhs) const noexcept -> Duration {
        auto r = checked_add(rhs);
        return r.is_some() ? rstd::move(r).unwrap_unchecked()
                           : Duration { u64::MAX, NANOS_PER_SEC - u32(1) };
    }

    constexpr auto saturating_sub(Duration rhs) const noexcept -> Duration {
        auto r = checked_sub(rhs);
        return r.is_some() ? rstd::move(r).unwrap_unchecked() : Duration { u64(), u32() };
    }

    constexpr auto saturating_mul(u32 rhs) const noexcept -> Duration {
        auto result = checked_mul(rhs);
        return result.is_some() ? rstd::move(result).unwrap_unchecked()
                                : Duration { u64::MAX, NANOS_PER_SEC - u32(1) };
    }

    // ── Arithmetic operators ──────────────────────────────────────────────
    friend constexpr auto operator+(Duration a, Duration b) -> Duration {
        return rstd::move(a.checked_add(b)).unwrap();
    }

    friend constexpr auto operator-(Duration a, Duration b) -> Duration {
        return rstd::move(a.checked_sub(b)).unwrap();
    }

    friend constexpr auto operator*(Duration a, u32 b) -> Duration {
        return rstd::move(a.checked_mul(b)).unwrap();
    }

    friend constexpr auto operator*(u32 b, Duration a) -> Duration { return a * b; }

    friend constexpr auto operator/(Duration a, u32 b) -> Duration {
        return rstd::move(a.checked_div(b)).unwrap();
    }

    constexpr auto& operator+=(Duration rhs) noexcept {
        *this = *this + rhs;
        return *this;
    }
    constexpr auto& operator-=(Duration rhs) noexcept {
        *this = *this - rhs;
        return *this;
    }
    constexpr auto& operator*=(u32 rhs) noexcept {
        *this = *this * rhs;
        return *this;
    }
    constexpr auto& operator/=(u32 rhs) noexcept {
        *this = *this / rhs;
        return *this;
    }
};

/// A duration of zero time.
export inline constexpr Duration Duration_ZERO = { u64(), u32() };
/// The maximum representable duration.
export inline constexpr Duration Duration_MAX = { u64::MAX, NANOS_PER_SEC - u32(1) };
/// A duration of exactly one second.
export inline constexpr Duration Duration_SECOND = { u64(1), u32() };
/// A duration of exactly one millisecond.
export inline constexpr Duration Duration_MILLISECOND = { u64(), NANOS_PER_MILLI };
/// A duration of exactly one microsecond.
export inline constexpr Duration Duration_MICROSECOND = { u64(), NANOS_PER_MICRO };
/// A duration of exactly one nanosecond.
export inline constexpr Duration Duration_NANOSECOND = { u64(), u32(1) };

} // namespace rstd::time
