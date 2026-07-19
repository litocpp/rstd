export module rstd.core:num.integer_methods;
import :num.types;
import :option;
import :array;

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_add(Self rhs) const noexcept
    -> rstd::Option<Self> {
    auto [value, overflow] = overflowing_add(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_sub(Self rhs) const noexcept
    -> rstd::Option<Self> {
    auto [value, overflow] = overflowing_sub(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_mul(Self rhs) const noexcept
    -> rstd::Option<Self> {
    auto [value, overflow] = overflowing_mul(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_div(Self rhs) const noexcept
    -> rstd::Option<Self> {
    if (rhs.value_ == 0) return rstd::option::None<Self>();
    if constexpr (IS_SIGNED) {
        if (value_ == Self::MIN.value_ && rhs.value_ == Primitive(-1)) {
            return rstd::option::None<Self>();
        }
    }
    return rstd::option::Some(Self(Primitive(value_ / rhs.value_)));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_rem(Self rhs) const noexcept
    -> rstd::Option<Self> {
    if (rhs.value_ == 0) return rstd::option::None<Self>();
    if constexpr (IS_SIGNED) {
        if (value_ == Self::MIN.value_ && rhs.value_ == Primitive(-1)) {
            return rstd::option::None<Self>();
        }
    }
    return rstd::option::Some(Self(Primitive(value_ % rhs.value_)));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_neg() const noexcept
    -> rstd::Option<Self> {
    auto [value, overflow] = overflowing_neg();
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
template<typename Shift>
    requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
constexpr auto Integer<Derived, Primitive, Tag>::checked_shl(Shift rhs) const noexcept
    -> rstd::Option<Self> {
    if (rhs.to_primitive() >= BIT_WIDTH) return rstd::option::None<Self>();
    return rstd::option::Some(wrapping_shl(rhs));
}

template<typename Derived, typename Primitive, typename Tag>
template<typename Shift>
    requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
constexpr auto Integer<Derived, Primitive, Tag>::checked_shr(Shift rhs) const noexcept
    -> rstd::Option<Self> {
    if (rhs.to_primitive() >= BIT_WIDTH) return rstd::option::None<Self>();
    return rstd::option::Some(wrapping_shr(rhs));
}

template<typename Derived, typename Primitive, typename Tag>
template<typename Exponent>
    requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
constexpr auto Integer<Derived, Primitive, Tag>::checked_pow(Exponent rhs) const noexcept
    -> rstd::Option<Self> {
    auto [value, overflow] = overflowing_pow(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::count_ones() const noexcept -> U32 {
    rstd::uint32_t count;
    if constexpr (sizeof(Primitive) <= sizeof(unsigned int)) {
        count = __builtin_popcount(static_cast<unsigned int>(to_unsigned_bits(value_)));
    } else if constexpr (sizeof(Primitive) <= sizeof(unsigned long long)) {
        count = __builtin_popcountll(static_cast<unsigned long long>(to_unsigned_bits(value_)));
    } else {
        auto const bits = to_unsigned_bits(value_);
        count           = __builtin_popcountll(static_cast<unsigned long long>(bits)) +
                          __builtin_popcountll(static_cast<unsigned long long>(bits >> 64));
    }
    return U32(count);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::count_zeros() const noexcept -> U32 {
    return U32(BIT_WIDTH - count_ones().to_primitive());
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::leading_zeros() const noexcept -> U32 {
    const auto     bits  = to_unsigned_bits(value_);
    rstd::uint32_t count = 0;
    for (rstd::uint32_t bit = BIT_WIDTH; bit != 0; --bit) {
        if ((bits & (unsigned_primitive_t<Primitive>(1) << (bit - 1))) != 0) break;
        ++count;
    }
    return U32(count);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::leading_ones() const noexcept -> U32 {
    return (~self()).leading_zeros();
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::trailing_zeros() const noexcept -> U32 {
    const auto     bits  = to_unsigned_bits(value_);
    rstd::uint32_t count = 0;
    for (; count != BIT_WIDTH; ++count) {
        if ((bits & (unsigned_primitive_t<Primitive>(1) << count)) != 0) break;
    }
    return U32(count);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::trailing_ones() const noexcept -> U32 {
    return (~self()).trailing_zeros();
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_next_power_of_two() const noexcept
    -> rstd::Option<Self> {
    using UnsignedPrimitive = unsigned_primitive_t<Primitive>;
    if (value_ <= Primitive(1)) return rstd::option::Some(Self(Primitive(1)));
    auto bits = UnsignedPrimitive(to_unsigned_bits(value_) - UnsignedPrimitive(1));
    for (rstd::uint32_t shift = 1; shift < BIT_WIDTH; shift <<= 1) bits |= bits >> shift;
    auto [result, overflow] = add_with_overflow(bits, UnsignedPrimitive(1));
    if constexpr (IS_SIGNED) {
        overflow |= result > static_cast<UnsignedPrimitive>(Self::MAX.value_);
    }
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(Self(from_unsigned_bits<Primitive>(result)));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_add_unsigned(Unsigned rhs) const noexcept
    -> rstd::Option<Self>
    requires IS_SIGNED
{
    auto [value, overflow] = overflowing_add_unsigned(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_sub_unsigned(Unsigned rhs) const noexcept
    -> rstd::Option<Self>
    requires IS_SIGNED
{
    auto [value, overflow] = overflowing_sub_unsigned(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_add_signed(Signed rhs) const noexcept
    -> rstd::Option<Self>
    requires(! IS_SIGNED)
{
    auto [value, overflow] = overflowing_add_signed(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::checked_sub_signed(Signed rhs) const noexcept
    -> rstd::Option<Self>
    requires(! IS_SIGNED)
{
    auto [value, overflow] = overflowing_sub_signed(rhs);
    if (overflow) return rstd::option::None<Self>();
    return rstd::option::Some(value);
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::to_le_bytes() const noexcept -> ByteArray {
    ByteArray bytes {};
    auto      bits = to_unsigned_bits(value_);
    for (rstd::size_t index = 0; index != sizeof(Primitive); ++index) {
        bytes.data()[index] = U8(static_cast<rstd::uint8_t>(bits));
        bits >>= 8;
    }
    return bytes;
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::to_be_bytes() const noexcept -> ByteArray {
    ByteArray bytes {};
    auto      bits = to_unsigned_bits(value_);
    for (rstd::size_t index = 0; index != sizeof(Primitive); ++index) {
        bytes.data()[sizeof(Primitive) - index - 1] = U8(static_cast<rstd::uint8_t>(bits));
        bits >>= 8;
    }
    return bytes;
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::to_ne_bytes() const noexcept -> ByteArray {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return to_le_bytes();
#else
    return to_be_bytes();
#endif
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::from_le_bytes(ByteArray bytes) noexcept -> Self {
    unsigned_primitive_t<Primitive> bits = 0;
    for (rstd::size_t index = sizeof(Primitive); index != 0; --index) {
        bits =
            unsigned_primitive_t<Primitive>((bits << 8) | bytes.data()[index - 1].to_primitive());
    }
    return Self(from_unsigned_bits<Primitive>(bits));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::from_be_bytes(ByteArray bytes) noexcept -> Self {
    unsigned_primitive_t<Primitive> bits = 0;
    for (rstd::size_t index = 0; index != sizeof(Primitive); ++index) {
        bits = unsigned_primitive_t<Primitive>((bits << 8) | bytes.data()[index].to_primitive());
    }
    return Self(from_unsigned_bits<Primitive>(bits));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Integer<Derived, Primitive, Tag>::from_ne_bytes(ByteArray bytes) noexcept -> Self {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return from_le_bytes(bytes);
#else
    return from_be_bytes(bytes);
#endif
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::to_le_bytes() const noexcept -> ByteArray {
    return to_bits().to_le_bytes();
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::to_be_bytes() const noexcept -> ByteArray {
    return to_bits().to_be_bytes();
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::to_ne_bytes() const noexcept -> ByteArray {
    return to_bits().to_ne_bytes();
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::from_le_bytes(ByteArray bytes) noexcept -> Self {
    return from_bits(Bits::from_le_bytes(bytes));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::from_be_bytes(ByteArray bytes) noexcept -> Self {
    return from_bits(Bits::from_be_bytes(bytes));
}

template<typename Derived, typename Primitive, typename Tag>
constexpr auto Floating<Derived, Primitive, Tag>::from_ne_bytes(ByteArray bytes) noexcept -> Self {
    return from_bits(Bits::from_ne_bytes(bytes));
}

[[maybe_unused]]
auto checked_methods_compile() -> bool {
    auto const add_ok       = U8(rstd::uint8_t(254)).checked_add(U8(rstd::uint8_t(1)));
    auto const add_overflow = U8(rstd::uint8_t(255)).checked_add(U8(rstd::uint8_t(1)));
    auto const neg_overflow = I8(rstd::int8_t(-128)).checked_neg();
    auto const shift_error  = U32(rstd::uint32_t(1)).checked_shl(U64(rstd::uint64_t(32)));
    return add_ok.is_some() && add_overflow.is_none() && neg_overflow.is_none() &&
           shift_error.is_none();
}
