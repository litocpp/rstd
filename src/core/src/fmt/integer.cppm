export module rstd.core:fmt.integer;
import :num.types;
export import :fmt;

auto write_integer_radix(rstd::fmt::Formatter&   formatter,
                         rstd::uint128_t         value,
                         rstd::fmt::Presentation presentation) -> bool;

template<typename T>
    requires(rstd::num::Integer<T> || rstd::is_raw_int<T>)
auto integer_bits(T value) -> rstd::uint128_t {
    auto raw = [&] {
        if constexpr (rstd::num::Integer<T>)
            return value.to_primitive();
        else
            return value;
    }();
    // Truncate sign extension to the source integer's width.
    constexpr auto mask = ~rstd::uint128_t(0) >> (128 - sizeof(raw) * 8);
    return static_cast<rstd::uint128_t>(raw) & mask;
}

namespace rstd
{

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::LowerHex, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(
            formatter, integer_bits(this->self()), fmt::Presentation::LowerHex);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::UpperHex, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(
            formatter, integer_bits(this->self()), fmt::Presentation::UpperHex);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Octal, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(formatter, integer_bits(this->self()), fmt::Presentation::Octal);
    }
};

} // namespace rstd
