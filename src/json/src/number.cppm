export module rstd.json:number;
export import rstd.core;

export namespace rstd::json
{

/// A finite JSON number stored as an unsigned integer, signed integer, or floating-point value.
class Number {
    enum class Representation : rstd::uint8_t
    {
        Unsigned,
        Signed,
        Float,
    };

    using Storage = Choice<choice_case<Representation::Unsigned, u64>,
                           choice_case<Representation::Signed, i64>,
                           choice_case<Representation::Float, f64>>;

    Storage value_;

    constexpr explicit Number(u64 value): value_(Storage::with<Representation::Unsigned>(value)) {}
    constexpr explicit Number(i64 value): value_(Storage::with<Representation::Signed>(value)) {}
    constexpr explicit Number(f64 value): value_(Storage::with<Representation::Float>(value)) {}

public:
    constexpr Number(const Number&)                = default;
    constexpr Number& operator=(const Number&)     = default;
    constexpr Number(Number&&) noexcept            = default;
    constexpr Number& operator=(Number&&) noexcept = default;

    /// Creates a number from an unsigned 64-bit integer.
    [[nodiscard]]
    static constexpr auto from_u64(u64 value) noexcept -> Number {
        return Number(value);
    }

    /// Creates a number from a signed 64-bit integer.
    [[nodiscard]]
    static constexpr auto from_i64(i64 value) noexcept -> Number {
        if (value < i64 {}) return Number(value);
        return Number(rstd::as_cast<u64>(value));
    }

    /// Creates a number from a finite floating-point value.
    [[nodiscard]]
    static auto from_f64(f64 value) noexcept -> Option<Number> {
        if (! value.is_finite()) return None();
        return Some(Number(value));
    }

    /// Returns whether the number can be represented as an `i64`.
    [[nodiscard]]
    constexpr auto is_i64() const noexcept -> bool {
        return value_.index() == 1 ||
               (value_.index() == 0 &&
                value_.as<Representation::Unsigned>() <= rstd::as_cast<u64>(i64::MAX));
    }

    /// Returns whether the number is stored as an unsigned integer.
    [[nodiscard]]
    constexpr auto is_u64() const noexcept -> bool {
        return value_.index() == 0;
    }
    /// Returns whether the number is stored as a floating-point value.
    [[nodiscard]]
    constexpr auto is_f64() const noexcept -> bool {
        return value_.index() == 2;
    }

    /// Returns the number as an `i64` when it is in range.
    [[nodiscard]]
    constexpr auto as_i64() const noexcept -> Option<i64> {
        if (value_.index() == 1) return Some(i64(value_.as<Representation::Signed>()));
        if (value_.index() == 0 &&
            value_.as<Representation::Unsigned>() <= rstd::as_cast<u64>(i64::MAX)) {
            return Some(rstd::as_cast<i64>(value_.as<Representation::Unsigned>()));
        }
        return None();
    }

    /// Returns the number as a `u64` when it is stored unsigned.
    [[nodiscard]]
    constexpr auto as_u64() const noexcept -> Option<u64> {
        if (value_.index() == 0) return Some(u64(value_.as<Representation::Unsigned>()));
        return None();
    }

    /// Converts the stored number to an `f64`.
    [[nodiscard]]
    constexpr auto as_f64() const noexcept -> Option<f64> {
        switch (value_.index()) {
        case 0: return Some(rstd::as_cast<f64>(value_.as<Representation::Unsigned>()));
        case 1: return Some(rstd::as_cast<f64>(value_.as<Representation::Signed>()));
        case 2: return Some(f64(value_.as<Representation::Float>()));
        default: rstd::unreachable();
        }
    }

    /// Compares stored numeric values without rounding integers to floating point.
    /// Unlike operator==, this ignores the storage representation and the sign of zero.
    [[nodiscard]]
    constexpr auto numeric_cmp(const Number& other) const noexcept -> strong_ordering {
        if (is_f64()) {
            if (other.is_f64()) {
                auto left  = as_f64().unwrap();
                auto right = other.as_f64().unwrap();
                if (left < right) return strong_ordering::less;
                if (left > right) return strong_ordering::greater;
                return strong_ordering::equal;
            }
            auto reversed = other.numeric_cmp(*this);
            if (reversed < 0) return strong_ordering::greater;
            if (reversed > 0) return strong_ordering::less;
            return strong_ordering::equal;
        }
        if (! other.is_f64()) {
            if (is_u64() && other.is_u64()) return *as_u64() <=> *other.as_u64();
            if (is_u64()) return strong_ordering::greater;
            if (other.is_u64()) return strong_ordering::less;
            return *as_i64() <=> *other.as_i64();
        }
        auto real = other.as_f64().unwrap();
        if (is_u64()) {
            if (real < f64()) return strong_ordering::greater;
            if (real >= f64(0x1p64)) return strong_ordering::less;
            auto integer  = rstd::as_cast<u64>(real);
            auto compared = *as_u64() <=> integer;
            if (compared != 0) return compared;
            return real == rstd::as_cast<f64>(integer) ? strong_ordering::equal
                                                       : strong_ordering::less;
        }
        if (real >= f64()) return strong_ordering::less;
        if (real < f64(-0x1p63)) return strong_ordering::greater;
        auto integer  = rstd::as_cast<i64>(real);
        auto compared = *as_i64() <=> integer;
        if (compared != 0) return compared;
        return real == rstd::as_cast<f64>(integer) ? strong_ordering::equal
                                                   : strong_ordering::greater;
    }

    friend constexpr auto operator==(const Number& left, const Number& right) noexcept -> bool {
        if (left.value_.index() != right.value_.index()) return false;
        switch (left.value_.index()) {
        case 0:
            return left.value_.as<Representation::Unsigned>() ==
                   right.value_.as<Representation::Unsigned>();
        case 1:
            return left.value_.as<Representation::Signed>() ==
                   right.value_.as<Representation::Signed>();
        case 2:
            return left.value_.as<Representation::Float>() ==
                   right.value_.as<Representation::Float>();
        default: rstd::unreachable();
        }
    }
};

} // namespace rstd::json
