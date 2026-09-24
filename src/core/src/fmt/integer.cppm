export module rstd.core:fmt.integer;
import :num.types;
export import :fmt;

auto write_hex(rstd::fmt::Formatter& formatter, rstd::uint128_t value, bool upper) -> bool;

template<typename T>
    requires(rstd::num::Integer<T> || rstd::is_raw_int<T>)
auto hex_bits(T value) -> rstd::uint128_t {
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
        return write_hex(formatter, hex_bits(this->self()), false);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::UpperHex, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_hex(formatter, hex_bits(this->self()), true);
    }
};

} // namespace rstd
