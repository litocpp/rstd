module;
#include <format>
export module rstd.core:time;
import :cmp;
import :fmt;
import :convert;
import :ops;
import :option;
import :clone;

namespace rstd::time
{

export const u32 NANOS_PER_SEC = 1'000'000'000;
export const u32 NANOS_PER_MILLI = 1'000'000;
export const u32 NANOS_PER_MICRO = 1'000;
export const u64 MILLIS_PER_SEC = 1'000;
export const u64 MICROS_PER_SEC = 1'000'000;

export struct Duration {
    u64 _secs;
    u32 _nanos;

    static constexpr auto new_(u64 secs, u32 nanos) noexcept -> Duration {
        if (nanos >= NANOS_PER_SEC) {
            secs += nanos / NANOS_PER_SEC;
            nanos %= NANOS_PER_SEC;
        }
        return { secs, nanos };
    }

    static constexpr auto from_secs(u64 secs) noexcept -> Duration {
        return { secs, 0 };
    }

    static constexpr auto from_millis(u64 millis) noexcept -> Duration {
        return { millis / MILLIS_PER_SEC, (u32)(millis % MILLIS_PER_SEC) * NANOS_PER_MILLI };
    }

    static constexpr auto from_micros(u64 micros) noexcept -> Duration {
        return { micros / MICROS_PER_SEC, (u32)(micros % MICROS_PER_SEC) * NANOS_PER_MICRO };
    }

    static constexpr auto from_nanos(u64 nanos) noexcept -> Duration {
        return { nanos / (u64)NANOS_PER_SEC, (u32)(nanos % (u64)NANOS_PER_SEC) };
    }

    constexpr auto is_zero() const noexcept -> bool {
        return _secs == 0 && _nanos == 0;
    }

    constexpr auto as_secs() const noexcept -> u64 {
        return _secs;
    }

    constexpr auto subsec_millis() const noexcept -> u32 {
        return _nanos / NANOS_PER_MILLI;
    }

    constexpr auto subsec_micros() const noexcept -> u32 {
        return _nanos / NANOS_PER_MICRO;
    }

    constexpr auto subsec_nanos() const noexcept -> u32 {
        return _nanos;
    }

    constexpr auto as_millis() const noexcept -> u64 {
        return _secs * MILLIS_PER_SEC + (u64)(_nanos / NANOS_PER_MILLI);
    }

    constexpr auto as_micros() const noexcept -> u64 {
        return _secs * MICROS_PER_SEC + (u64)(_nanos / NANOS_PER_MICRO);
    }

    constexpr auto as_nanos() const noexcept -> u64 {
        return _secs * (u64)NANOS_PER_SEC + (u64)_nanos;
    }

    friend constexpr auto operator==(Duration a, Duration b) noexcept -> bool {
        return a._secs == b._secs && a._nanos == b._nanos;
    }

    friend constexpr auto operator<=>(Duration a, Duration b) noexcept {
        if (auto cmp = a._secs <=> b._secs; cmp != 0) return cmp;
        return a._nanos <=> b._nanos;
    }

    friend constexpr auto operator+(Duration a, Duration b) noexcept -> Duration {
        u64 secs = a._secs + b._secs;
        u32 nanos = a._nanos + b._nanos;
        if (nanos >= NANOS_PER_SEC) {
            nanos -= NANOS_PER_SEC;
            secs += 1;
        }
        return { secs, nanos };
    }

    friend constexpr auto operator-(Duration a, Duration b) noexcept -> Duration {
        u64 secs = a._secs - b._secs;
        u32 nanos;
        if (a._nanos >= b._nanos) {
            nanos = a._nanos - b._nanos;
        } else {
            nanos = a._nanos + NANOS_PER_SEC - b._nanos;
            secs -= 1;
        }
        return { secs, nanos };
    }

    // Multiplication and division by scalar
    friend constexpr auto operator*(Duration a, u32 b) noexcept -> Duration {
        u128 total_nanos = (u128)a._nanos * b;
        u128 total_secs = (u128)a._secs * b + (total_nanos / NANOS_PER_SEC);
        return { (u64)total_secs, (u32)(total_nanos % NANOS_PER_SEC) };
    }

    friend constexpr auto operator/(Duration a, u32 b) noexcept -> Duration {
        u128 total_nanos = (u128)a._secs * NANOS_PER_SEC + a._nanos;
        u128 result_nanos = total_nanos / b;
        return { (u64)(result_nanos / NANOS_PER_SEC), (u32)(result_nanos % NANOS_PER_SEC) };
    }
};

} // namespace rstd::time

namespace std
{
export template<>
struct formatter<rstd::time::Duration> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) { return ctx.begin(); }

    template<typename Context>
    auto format(const rstd::time::Duration& d, Context& ctx) const {
        return std::format_to(ctx.out(), "{}s {}ns", d.as_secs(), d.subsec_nanos());
    }
};
} // namespace std
