export module rstd.json:number;
export import rstd.core;

export namespace rstd::json
{

class Number {
    enum_detail::storage<u64, i64, f64> value_;

    constexpr explicit Number(u64 value): value_(enum_detail::in_place_index<0>, value) {}
    constexpr explicit Number(i64 value): value_(enum_detail::in_place_index<1>, value) {}
    constexpr explicit Number(f64 value): value_(enum_detail::in_place_index<2>, value) {}

public:
    constexpr Number(const Number&)                = default;
    constexpr Number& operator=(const Number&)     = default;
    constexpr Number(Number&&) noexcept            = default;
    constexpr Number& operator=(Number&&) noexcept = default;

    [[nodiscard]]
    static constexpr auto from_u64(u64 value) noexcept -> Number {
        return Number(value);
    }

    [[nodiscard]]
    static constexpr auto from_i64(i64 value) noexcept -> Number {
        if (value < i64 {}) return Number(value);
        return Number(rstd::as_cast<u64>(value));
    }

    [[nodiscard]]
    static auto from_f64(f64 value) noexcept -> Option<Number> {
        if (! value.is_finite()) return None();
        return Some(Number(value));
    }

    [[nodiscard]]
    constexpr auto is_i64() const noexcept -> bool {
        return value_.index() == 1 ||
               (value_.index() == 0 && value_.get<0>() <= rstd::as_cast<u64>(i64::MAX));
    }

    [[nodiscard]]
    constexpr auto is_u64() const noexcept -> bool {
        return value_.index() == 0;
    }
    [[nodiscard]]
    constexpr auto is_f64() const noexcept -> bool {
        return value_.index() == 2;
    }

    [[nodiscard]]
    constexpr auto as_i64() const noexcept -> Option<i64> {
        if (value_.index() == 1) return Some(i64(value_.get<1>()));
        if (value_.index() == 0 && value_.get<0>() <= rstd::as_cast<u64>(i64::MAX)) {
            return Some(rstd::as_cast<i64>(value_.get<0>()));
        }
        return None();
    }

    [[nodiscard]]
    constexpr auto as_u64() const noexcept -> Option<u64> {
        if (value_.index() == 0) return Some(u64(value_.get<0>()));
        return None();
    }

    [[nodiscard]]
    constexpr auto as_f64() const noexcept -> Option<f64> {
        switch (value_.index()) {
        case 0: return Some(rstd::as_cast<f64>(value_.get<0>()));
        case 1: return Some(rstd::as_cast<f64>(value_.get<1>()));
        case 2: return Some(f64(value_.get<2>()));
        default: rstd::unreachable();
        }
    }

    friend constexpr auto operator==(const Number& left, const Number& right) noexcept -> bool {
        if (left.value_.index() != right.value_.index()) return false;
        switch (left.value_.index()) {
        case 0: return left.value_.get<0>() == right.value_.get<0>();
        case 1: return left.value_.get<1>() == right.value_.get<1>();
        case 2: return left.value_.get<2>() == right.value_.get<2>();
        default: rstd::unreachable();
        }
    }
};

} // namespace rstd::json
