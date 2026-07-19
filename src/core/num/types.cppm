module;

export module rstd.core:num.types;
export import rstd.basic;

export namespace rstd::option
{
template<typename T>
class Option;
}

export namespace rstd
{
using option::Option;

template<typename T, rstd::size_t N>
class array;
} // namespace rstd

export namespace rstd::num
{
enum class FpCategory : rstd::uint8_t
{
    Nan,
    Infinite,
    Zero,
    Subnormal,
    Normal
};
}

#ifndef RSTD_CHECK_INTEGER_OVERFLOW
#error "RSTD_CHECK_INTEGER_OVERFLOW must be defined by the rstd.core target"
#endif

inline constexpr bool CHECK_INTEGER_OVERFLOW = RSTD_CHECK_INTEGER_OVERFLOW;
static_assert(RSTD_CHECK_INTEGER_OVERFLOW == 0 || RSTD_CHECK_INTEGER_OVERFLOW == 1);

constexpr auto should_check_integer_overflow() noexcept -> bool {
    return CHECK_INTEGER_OVERFLOW || rstd::mtp::is_constant_evaluated();
}

template<typename T>
concept RawInteger = rstd::is_raw_int<rstd::mtp::rm_cvf<T>>;

template<RawInteger T>
inline constexpr bool raw_integer_signed = T(-1) < T(0);

template<RawInteger T>
inline constexpr rstd::uint32_t raw_integer_digits =
    sizeof(T) * 8 - static_cast<rstd::uint32_t>(raw_integer_signed<T>);

template<RawInteger T>
constexpr auto raw_integer_max() noexcept -> T {
    constexpr auto bits = static_cast<rstd::uint32_t>(sizeof(T) * 8);
    if constexpr (raw_integer_signed<T>) {
        return static_cast<T>((rstd::uint128_t(1) << (bits - 1)) - 1);
    } else {
        return static_cast<T>(~rstd::uint128_t(0) >> (128 - bits));
    }
}

template<RawInteger T>
constexpr auto raw_integer_min() noexcept -> T {
    if constexpr (raw_integer_signed<T>) {
        return static_cast<T>(-raw_integer_max<T>() - T(1));
    } else {
        return T(0);
    }
}

template<typename T>
struct raw_float_traits;

template<>
struct raw_float_traits<float> {
    static constexpr float max             = __FLT_MAX__;
    static constexpr float min_positive    = __FLT_MIN__;
    static constexpr float epsilon         = __FLT_EPSILON__;
    static constexpr int   radix           = __FLT_RADIX__;
    static constexpr int   mantissa_digits = __FLT_MANT_DIG__;
    static constexpr int   digits          = __FLT_DIG__;
    static constexpr int   min_exponent    = __FLT_MIN_EXP__;
    static constexpr int   max_exponent    = __FLT_MAX_EXP__;
    static constexpr int   min_exponent_10 = __FLT_MIN_10_EXP__;
    static constexpr int   max_exponent_10 = __FLT_MAX_10_EXP__;
};

template<>
struct raw_float_traits<double> {
    static constexpr double max             = __DBL_MAX__;
    static constexpr double min_positive    = __DBL_MIN__;
    static constexpr double epsilon         = __DBL_EPSILON__;
    static constexpr int    radix           = __FLT_RADIX__;
    static constexpr int    mantissa_digits = __DBL_MANT_DIG__;
    static constexpr int    digits          = __DBL_DIG__;
    static constexpr int    min_exponent    = __DBL_MIN_EXP__;
    static constexpr int    max_exponent    = __DBL_MAX_EXP__;
    static constexpr int    min_exponent_10 = __DBL_MIN_10_EXP__;
    static constexpr int    max_exponent_10 = __DBL_MAX_10_EXP__;
};

template<RawInteger To, RawInteger From>
constexpr auto raw_integer_in_range(From value) noexcept -> bool {
    using Target = rstd::mtp::rm_cvf<To>;
    using Source = rstd::mtp::rm_cvf<From>;

    if constexpr (raw_integer_signed<Source>) {
        auto const signed_value = static_cast<rstd::int128_t>(value);
        if (signed_value < 0) {
            if constexpr (! raw_integer_signed<Target>) {
                return false;
            } else {
                return signed_value >= static_cast<rstd::int128_t>(raw_integer_min<Target>());
            }
        }
        return static_cast<rstd::uint128_t>(signed_value) <=
               static_cast<rstd::uint128_t>(raw_integer_max<Target>());
    } else {
        return static_cast<rstd::uint128_t>(value) <=
               static_cast<rstd::uint128_t>(raw_integer_max<Target>());
    }
}

struct u8_tag {};
struct u16_tag {};
struct u32_tag {};
struct u64_tag {};
struct u128_tag {};
struct usize_tag {};
struct i8_tag {};
struct i16_tag {};
struct i32_tag {};
struct i64_tag {};
struct i128_tag {};
struct isize_tag {};

struct U8;
struct U16;
struct U32;
struct U64;
struct U128;
struct Usize;
struct I8;
struct I16;
struct I32;
struct I64;
struct I128;
struct Isize;

template<typename Tag>
struct unsigned_tag {
    using type = Tag;
};

template<typename Tag>
struct signed_tag {
    using type = Tag;
};

template<>
struct signed_tag<u8_tag> {
    using type = i8_tag;
};
template<>
struct signed_tag<u16_tag> {
    using type = i16_tag;
};
template<>
struct signed_tag<u32_tag> {
    using type = i32_tag;
};
template<>
struct signed_tag<u64_tag> {
    using type = i64_tag;
};
template<>
struct signed_tag<u128_tag> {
    using type = i128_tag;
};
template<>
struct signed_tag<usize_tag> {
    using type = isize_tag;
};

template<>
struct unsigned_tag<i8_tag> {
    using type = u8_tag;
};
template<>
struct unsigned_tag<i16_tag> {
    using type = u16_tag;
};
template<>
struct unsigned_tag<i32_tag> {
    using type = u32_tag;
};
template<>
struct unsigned_tag<i64_tag> {
    using type = u64_tag;
};
template<>
struct unsigned_tag<i128_tag> {
    using type = u128_tag;
};
template<>
struct unsigned_tag<isize_tag> {
    using type = usize_tag;
};

template<typename Primitive>
struct unsigned_primitive {
    using type = Primitive;
};

template<>
struct unsigned_primitive<rstd::int8_t> {
    using type = rstd::uint8_t;
};
template<>
struct unsigned_primitive<rstd::int16_t> {
    using type = rstd::uint16_t;
};
template<>
struct unsigned_primitive<rstd::int32_t> {
    using type = rstd::uint32_t;
};
template<>
struct unsigned_primitive<rstd::int64_t> {
    using type = rstd::uint64_t;
};
template<>
struct unsigned_primitive<rstd::int128_t> {
    using type = rstd::uint128_t;
};

template<typename Primitive>
using unsigned_primitive_t = typename unsigned_primitive<Primitive>::type;

template<typename Derived, typename Primitive, typename Tag>
class Integer;

template<typename Tag>
struct integer_type;

template<>
struct integer_type<u8_tag> {
    using type = U8;
};
template<>
struct integer_type<u16_tag> {
    using type = U16;
};
template<>
struct integer_type<u32_tag> {
    using type = U32;
};
template<>
struct integer_type<u64_tag> {
    using type = U64;
};
template<>
struct integer_type<u128_tag> {
    using type = U128;
};
template<>
struct integer_type<usize_tag> {
    using type = Usize;
};
template<>
struct integer_type<i8_tag> {
    using type = I8;
};
template<>
struct integer_type<i16_tag> {
    using type = I16;
};
template<>
struct integer_type<i32_tag> {
    using type = I32;
};
template<>
struct integer_type<i64_tag> {
    using type = I64;
};
template<>
struct integer_type<i128_tag> {
    using type = I128;
};
template<>
struct integer_type<isize_tag> {
    using type = Isize;
};

template<typename Tag>
using unsigned_integer_t = typename integer_type<typename unsigned_tag<Tag>::type>::type;

template<typename Tag>
using signed_integer_t = typename integer_type<typename signed_tag<Tag>::type>::type;

[[noreturn]]
void panic_overflow();
[[noreturn]]
void panic_divide_by_zero();
[[noreturn]]
void panic_invalid_shift();
[[noreturn]]
void panic_invalid_float_clamp();

template<typename Primitive>
constexpr auto add_with_overflow(Primitive lhs, Primitive rhs) noexcept
    -> rstd::tuple<Primitive, bool> {
    Primitive  value {};
    bool const overflow = __builtin_add_overflow(lhs, rhs, &value);
    return { value, overflow };
}

template<typename Primitive>
constexpr auto sub_with_overflow(Primitive lhs, Primitive rhs) noexcept
    -> rstd::tuple<Primitive, bool> {
    Primitive  value {};
    bool const overflow = __builtin_sub_overflow(lhs, rhs, &value);
    return { value, overflow };
}

template<typename Primitive>
constexpr auto mul_with_overflow(Primitive lhs, Primitive rhs) noexcept
    -> rstd::tuple<Primitive, bool> {
    Primitive  value {};
    bool const overflow = __builtin_mul_overflow(lhs, rhs, &value);
    return { value, overflow };
}

template<typename Primitive>
constexpr auto from_unsigned_bits(unsigned_primitive_t<Primitive> value) noexcept -> Primitive {
    return __builtin_bit_cast(Primitive, value);
}

template<typename Primitive>
constexpr auto to_unsigned_bits(Primitive value) noexcept -> unsigned_primitive_t<Primitive> {
    return __builtin_bit_cast(unsigned_primitive_t<Primitive>, value);
}

template<typename Derived, typename Primitive, typename Tag>
class Integer {
    static_assert(RawInteger<Primitive>);

    Primitive value_ {};

    template<typename, typename, typename>
    friend class Integer;

    constexpr auto self() noexcept -> Derived& { return static_cast<Derived&>(*this); }
    constexpr auto self() const noexcept -> Derived const& {
        return static_cast<Derived const&>(*this);
    }

public:
    using integer_wrapper_tag = rstd::mtp::detail::integer_wrapper_tag;
    using primitive_type      = Primitive;
    using Self                = Derived;
    using Unsigned            = unsigned_integer_t<Tag>;
    using Signed              = signed_integer_t<Tag>;
    using ByteArray           = rstd::array<U8, sizeof(Primitive)>;

    static constexpr bool           IS_SIGNED = raw_integer_signed<Primitive>;
    static constexpr rstd::uint32_t BIT_WIDTH = sizeof(Primitive) * 8;

protected:
    constexpr Integer() noexcept = default;
    explicit constexpr Integer(Primitive value) noexcept: value_(value) {}

public:
    [[nodiscard]]
    constexpr auto to_primitive() const noexcept -> Primitive {
        return value_;
    }

    [[nodiscard]]
    constexpr auto as_ptr() const& noexcept [[clang::lifetimebound]] -> Primitive const* {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto as_mut_ptr() & noexcept [[clang::lifetimebound]] -> Primitive* {
        return &value_;
    }

    auto as_ptr() const&& -> Primitive const* = delete;
    auto as_mut_ptr() && -> Primitive*        = delete;

    [[nodiscard]]
    constexpr auto checked_add(Self rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_sub(Self rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_mul(Self rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_div(Self rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_rem(Self rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_neg() const noexcept -> rstd::Option<Self>;
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto checked_shl(Shift rhs) const noexcept -> rstd::Option<Self>;
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto checked_shr(Shift rhs) const noexcept -> rstd::Option<Self>;
    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
    [[nodiscard]]
    constexpr auto checked_pow(Exponent rhs) const noexcept -> rstd::Option<Self>;
    [[nodiscard]]
    constexpr auto checked_next_power_of_two() const noexcept -> rstd::Option<Self>;

    [[nodiscard]]
    constexpr auto checked_add_unsigned(Unsigned rhs) const noexcept -> rstd::Option<Self>
        requires IS_SIGNED;
    [[nodiscard]]
    constexpr auto checked_sub_unsigned(Unsigned rhs) const noexcept -> rstd::Option<Self>
        requires IS_SIGNED;
    [[nodiscard]]
    constexpr auto checked_add_signed(Signed rhs) const noexcept -> rstd::Option<Self>
        requires(! IS_SIGNED);
    [[nodiscard]]
    constexpr auto checked_sub_signed(Signed rhs) const noexcept -> rstd::Option<Self>
        requires(! IS_SIGNED);

    [[nodiscard]]
    constexpr auto overflowing_add(Self rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto [value, overflow] = add_with_overflow(value_, rhs.value_);
        return { Self(value), overflow };
    }

    [[nodiscard]]
    constexpr auto overflowing_sub(Self rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto [value, overflow] = sub_with_overflow(value_, rhs.value_);
        return { Self(value), overflow };
    }

    [[nodiscard]]
    constexpr auto overflowing_mul(Self rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto [value, overflow] = mul_with_overflow(value_, rhs.value_);
        return { Self(value), overflow };
    }

    [[nodiscard]]
    constexpr auto overflowing_div(Self rhs) const -> rstd::tuple<Self, bool> {
        if (rhs.value_ == 0) panic_divide_by_zero();
        if constexpr (IS_SIGNED) {
            if (value_ == Self::MIN.value_ && rhs.value_ == Primitive(-1)) {
                return { Self::MIN, true };
            }
        }
        return { Self(Primitive(value_ / rhs.value_)), false };
    }

    [[nodiscard]]
    constexpr auto overflowing_rem(Self rhs) const -> rstd::tuple<Self, bool> {
        if (rhs.value_ == 0) panic_divide_by_zero();
        if constexpr (IS_SIGNED) {
            if (value_ == Self::MIN.value_ && rhs.value_ == Primitive(-1)) return { Self(), true };
        }
        return { Self(Primitive(value_ % rhs.value_)), false };
    }

    [[nodiscard]]
    constexpr auto overflowing_neg() const noexcept -> rstd::tuple<Self, bool> {
        auto [value, overflow] = sub_with_overflow(Primitive(0), value_);
        return { Self(value), overflow };
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto overflowing_shl(Shift rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto       amount   = rhs.to_primitive();
        bool const overflow = amount >= BIT_WIDTH;
        amount %= BIT_WIDTH;
        auto const bits = unsigned_primitive_t<Primitive>(to_unsigned_bits(value_) << amount);
        return { Self(from_unsigned_bits<Primitive>(bits)), overflow };
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto overflowing_shr(Shift rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto       amount   = rhs.to_primitive();
        bool const overflow = amount >= BIT_WIDTH;
        amount %= BIT_WIDTH;
        if constexpr (IS_SIGNED) {
            return { Self(Primitive(value_ >> amount)), overflow };
        } else {
            return { Self(Primitive(value_ >> amount)), overflow };
        }
    }

    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
    [[nodiscard]]
    constexpr auto overflowing_pow(Exponent rhs) const noexcept -> rstd::tuple<Self, bool> {
        auto remaining = rhs.to_primitive();
        Self value(Primitive(1));
        Self base     = self();
        bool overflow = false;
        while (remaining != 0) {
            if ((remaining & 1u) != 0) {
                auto [next, next_overflow] = value.overflowing_mul(base);
                value                      = next;
                overflow |= next_overflow;
            }
            remaining >>= 1u;
            if (remaining != 0) {
                auto [next, next_overflow] = base.overflowing_mul(base);
                base                       = next;
                overflow |= next_overflow;
            }
        }
        return { value, overflow };
    }

    [[nodiscard]]
    constexpr auto overflowing_add_unsigned(Unsigned rhs) const noexcept -> rstd::tuple<Self, bool>
        requires IS_SIGNED
    {
        using UnsignedPrimitive = unsigned_primitive_t<Primitive>;
        const auto lhs_bits     = to_unsigned_bits(value_);
        const auto rhs_value    = static_cast<UnsignedPrimitive>(rhs.to_primitive());
        const auto sign_bit     = UnsignedPrimitive(1) << (BIT_WIDTH - 1);
        const auto maximum      = sign_bit - UnsignedPrimitive(1);
        const auto capacity     = value_ < 0
                                      ? UnsignedPrimitive(UnsignedPrimitive(0) - lhs_bits + maximum)
                                      : UnsignedPrimitive(maximum - lhs_bits);
        return { Self(from_unsigned_bits<Primitive>(UnsignedPrimitive(lhs_bits + rhs_value))),
                 rhs_value > capacity };
    }

    [[nodiscard]]
    constexpr auto overflowing_sub_unsigned(Unsigned rhs) const noexcept -> rstd::tuple<Self, bool>
        requires IS_SIGNED
    {
        using UnsignedPrimitive = unsigned_primitive_t<Primitive>;
        const auto lhs_bits     = to_unsigned_bits(value_);
        const auto rhs_value    = static_cast<UnsignedPrimitive>(rhs.to_primitive());
        const auto sign_bit     = UnsignedPrimitive(1) << (BIT_WIDTH - 1);
        const auto capacity     = lhs_bits ^ sign_bit;
        return { Self(from_unsigned_bits<Primitive>(UnsignedPrimitive(lhs_bits - rhs_value))),
                 rhs_value > capacity };
    }

    [[nodiscard]]
    constexpr auto overflowing_add_signed(Signed rhs) const noexcept -> rstd::tuple<Self, bool>
        requires(! IS_SIGNED)
    {
        using SignedPrimitive = typename Signed::primitive_type;
        const auto rhs_value  = rhs.to_primitive();
        const auto rhs_bits = static_cast<Primitive>(to_unsigned_bits<SignedPrimitive>(rhs_value));
        if (rhs_value < 0) {
            const auto magnitude = Primitive(Primitive(0) - rhs_bits);
            return { Self(Primitive(value_ - magnitude)), magnitude > value_ };
        }
        return { Self(Primitive(value_ + rhs_bits)), rhs_bits > Self::MAX.value_ - value_ };
    }

    [[nodiscard]]
    constexpr auto overflowing_sub_signed(Signed rhs) const noexcept -> rstd::tuple<Self, bool>
        requires(! IS_SIGNED)
    {
        using SignedPrimitive = typename Signed::primitive_type;
        const auto rhs_value  = rhs.to_primitive();
        const auto rhs_bits = static_cast<Primitive>(to_unsigned_bits<SignedPrimitive>(rhs_value));
        if (rhs_value < 0) {
            const auto magnitude = Primitive(Primitive(0) - rhs_bits);
            return { Self(Primitive(value_ + magnitude)), magnitude > Self::MAX.value_ - value_ };
        }
        return { Self(Primitive(value_ - rhs_bits)), rhs_bits > value_ };
    }

    [[nodiscard]]
    constexpr auto wrapping_add(Self rhs) const noexcept -> Self {
        return get<0>(overflowing_add(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_sub(Self rhs) const noexcept -> Self {
        return get<0>(overflowing_sub(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_mul(Self rhs) const noexcept -> Self {
        return get<0>(overflowing_mul(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_div(Self rhs) const -> Self {
        return get<0>(overflowing_div(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_rem(Self rhs) const -> Self {
        return get<0>(overflowing_rem(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_neg() const noexcept -> Self {
        return get<0>(overflowing_neg());
    }
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto wrapping_shl(Shift rhs) const noexcept -> Self {
        return get<0>(overflowing_shl(rhs));
    }
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto wrapping_shr(Shift rhs) const noexcept -> Self {
        return get<0>(overflowing_shr(rhs));
    }
    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
    [[nodiscard]]
    constexpr auto wrapping_pow(Exponent rhs) const noexcept -> Self {
        return get<0>(overflowing_pow(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_add_unsigned(Unsigned rhs) const noexcept -> Self
        requires IS_SIGNED
    {
        return get<0>(overflowing_add_unsigned(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_sub_unsigned(Unsigned rhs) const noexcept -> Self
        requires IS_SIGNED
    {
        return get<0>(overflowing_sub_unsigned(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_add_signed(Signed rhs) const noexcept -> Self
        requires(! IS_SIGNED)
    {
        return get<0>(overflowing_add_signed(rhs));
    }
    [[nodiscard]]
    constexpr auto wrapping_sub_signed(Signed rhs) const noexcept -> Self
        requires(! IS_SIGNED)
    {
        return get<0>(overflowing_sub_signed(rhs));
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto rotate_left(Shift rhs) const noexcept -> Self {
        auto amount = rhs.to_primitive() % BIT_WIDTH;
        if (amount == 0) return self();
        auto const bits = to_unsigned_bits(value_);
        return Self(from_unsigned_bits<Primitive>(
            unsigned_primitive_t<Primitive>((bits << amount) | (bits >> (BIT_WIDTH - amount)))));
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    [[nodiscard]]
    constexpr auto rotate_right(Shift rhs) const noexcept -> Self {
        auto amount = rhs.to_primitive() % BIT_WIDTH;
        if (amount == 0) return self();
        auto const bits = to_unsigned_bits(value_);
        return Self(from_unsigned_bits<Primitive>(
            unsigned_primitive_t<Primitive>((bits >> amount) | (bits << (BIT_WIDTH - amount)))));
    }

    [[nodiscard]]
    constexpr auto saturating_add(Self rhs) const noexcept -> Self {
        auto [value, overflow] = overflowing_add(rhs);
        if (! overflow) return value;
        if constexpr (IS_SIGNED) return rhs.value_ < 0 ? Self::MIN : Self::MAX;
        return Self::MAX;
    }

    [[nodiscard]]
    constexpr auto saturating_sub(Self rhs) const noexcept -> Self {
        auto [value, overflow] = overflowing_sub(rhs);
        if (! overflow) return value;
        if constexpr (IS_SIGNED) return rhs.value_ < 0 ? Self::MAX : Self::MIN;
        return Self::MIN;
    }

    [[nodiscard]]
    constexpr auto saturating_mul(Self rhs) const noexcept -> Self {
        auto [value, overflow] = overflowing_mul(rhs);
        if (! overflow) return value;
        if constexpr (IS_SIGNED) {
            return (value_ < 0) != (rhs.value_ < 0) ? Self::MIN : Self::MAX;
        }
        return Self::MAX;
    }

    [[nodiscard]]
    constexpr auto saturating_div(Self rhs) const -> Self {
        if (rhs.value_ == 0) panic_divide_by_zero();
        if constexpr (IS_SIGNED) {
            if (value_ == Self::MIN.value_ && rhs.value_ == Primitive(-1)) return Self::MAX;
        }
        return Self(Primitive(value_ / rhs.value_));
    }

    [[nodiscard]]
    constexpr auto saturating_neg() const noexcept -> Self {
        auto [value, overflow] = overflowing_neg();
        if (! overflow) return value;
        if constexpr (IS_SIGNED) return Self::MAX;
        return Self::MIN;
    }

    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
    [[nodiscard]]
    constexpr auto saturating_pow(Exponent rhs) const noexcept -> Self {
        auto [value, overflow] = overflowing_pow(rhs);
        if (! overflow) return value;
        if constexpr (IS_SIGNED) {
            return value_ < 0 && (rhs.to_primitive() & 1u) != 0 ? Self::MIN : Self::MAX;
        }
        return Self::MAX;
    }

    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, U32>
    [[nodiscard]]
    constexpr auto pow(Exponent rhs) const -> Self {
        auto [value, overflow] = overflowing_pow(rhs);
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return value;
    }

    [[nodiscard]]
    constexpr auto saturating_add_unsigned(Unsigned rhs) const noexcept -> Self
        requires IS_SIGNED
    {
        auto [value, overflow] = overflowing_add_unsigned(rhs);
        return overflow ? Self::MAX : value;
    }

    [[nodiscard]]
    constexpr auto saturating_sub_unsigned(Unsigned rhs) const noexcept -> Self
        requires IS_SIGNED
    {
        auto [value, overflow] = overflowing_sub_unsigned(rhs);
        return overflow ? Self::MIN : value;
    }

    [[nodiscard]]
    constexpr auto saturating_add_signed(Signed rhs) const noexcept -> Self
        requires(! IS_SIGNED)
    {
        auto [value, overflow] = overflowing_add_signed(rhs);
        return overflow ? (rhs.to_primitive() < 0 ? Self::MIN : Self::MAX) : value;
    }

    [[nodiscard]]
    constexpr auto saturating_sub_signed(Signed rhs) const noexcept -> Self
        requires(! IS_SIGNED)
    {
        auto [value, overflow] = overflowing_sub_signed(rhs);
        return overflow ? (rhs.to_primitive() < 0 ? Self::MAX : Self::MIN) : value;
    }

    [[nodiscard]]
    constexpr auto abs() const -> Self {
        if constexpr (! IS_SIGNED) return self();
        if (value_ == Self::MIN.value_) {
            if (should_check_integer_overflow()) panic_overflow();
            return self();
        }
        return value_ < 0 ? Self(Primitive(-value_)) : self();
    }

    [[nodiscard]]
    constexpr auto unsigned_abs() const noexcept -> Unsigned {
        if constexpr (! IS_SIGNED) {
            return Unsigned(value_);
        } else {
            auto const bits = to_unsigned_bits(value_);
            return Unsigned(value_ < 0 ? unsigned_primitive_t<Primitive>(0) - bits : bits);
        }
    }

    [[nodiscard]]
    constexpr auto abs_diff(Self rhs) const noexcept -> Unsigned {
        if (*this >= rhs) {
            return Unsigned(unsigned_primitive_t<Primitive>(to_unsigned_bits(value_) -
                                                            to_unsigned_bits(rhs.value_)));
        }
        return Unsigned(unsigned_primitive_t<Primitive>(to_unsigned_bits(rhs.value_) -
                                                        to_unsigned_bits(value_)));
    }

    [[nodiscard]]
    constexpr auto count_ones() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto count_zeros() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto leading_zeros() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto leading_ones() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto trailing_zeros() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto trailing_ones() const noexcept -> U32;

    [[nodiscard]]
    constexpr auto reverse_bits() const noexcept -> Self {
        auto                            source = to_unsigned_bits(value_);
        unsigned_primitive_t<Primitive> result = 0;
        for (rstd::uint32_t bit = 0; bit != BIT_WIDTH; ++bit) {
            result = unsigned_primitive_t<Primitive>((result << 1) | (source & 1));
            source >>= 1;
        }
        return Self(from_unsigned_bits<Primitive>(result));
    }

    [[nodiscard]]
    constexpr auto swap_bytes() const noexcept -> Self {
        auto                            source = to_unsigned_bits(value_);
        unsigned_primitive_t<Primitive> result = 0;
        for (rstd::size_t byte = 0; byte != sizeof(Primitive); ++byte) {
            result = unsigned_primitive_t<Primitive>((result << 8) | (source & 0xff));
            source >>= 8;
        }
        return Self(from_unsigned_bits<Primitive>(result));
    }

    [[nodiscard]]
    constexpr auto is_power_of_two() const noexcept -> bool {
        auto const bits = to_unsigned_bits(value_);
        return value_ > 0 && (bits & (bits - 1)) == 0;
    }

    [[nodiscard]]
    constexpr auto next_power_of_two() const -> Self {
        using UnsignedPrimitive = unsigned_primitive_t<Primitive>;
        if (value_ <= Primitive(1)) return Self(Primitive(1));
        auto bits = UnsignedPrimitive(to_unsigned_bits(value_) - UnsignedPrimitive(1));
        for (rstd::uint32_t shift = 1; shift < BIT_WIDTH; shift <<= 1) bits |= bits >> shift;
        auto [result, overflow] = add_with_overflow(bits, UnsignedPrimitive(1));
        if constexpr (IS_SIGNED)
            overflow |= result > static_cast<UnsignedPrimitive>(Self::MAX.value_);
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return Self(from_unsigned_bits<Primitive>(result));
    }

    [[nodiscard]]
    constexpr auto to_be_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    constexpr auto to_le_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    constexpr auto to_ne_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    static constexpr auto from_be_bytes(ByteArray bytes) noexcept -> Self;
    [[nodiscard]]
    static constexpr auto from_le_bytes(ByteArray bytes) noexcept -> Self;
    [[nodiscard]]
    static constexpr auto from_ne_bytes(ByteArray bytes) noexcept -> Self;

    friend constexpr auto operator==(Self lhs, Self rhs) noexcept -> bool {
        return lhs.value_ == rhs.value_;
    }

    friend constexpr auto operator<=>(Self lhs, Self rhs) noexcept -> std::strong_ordering {
        return lhs.value_ <=> rhs.value_;
    }

    friend constexpr auto operator+(Self lhs, Self rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_add(rhs);
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return value;
    }

    friend constexpr auto operator-(Self lhs, Self rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_sub(rhs);
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return value;
    }

    friend constexpr auto operator*(Self lhs, Self rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_mul(rhs);
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return value;
    }

    friend constexpr auto operator/(Self lhs, Self rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_div(rhs);
        if (overflow) panic_overflow();
        return value;
    }

    friend constexpr auto operator%(Self lhs, Self rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_rem(rhs);
        if (overflow) panic_overflow();
        return value;
    }

    friend constexpr auto operator-(Self value) -> Self {
        auto [result, overflow] = value.overflowing_neg();
        if (overflow && should_check_integer_overflow()) panic_overflow();
        return result;
    }

    friend constexpr auto operator~(Self value) noexcept -> Self {
        return Self(from_unsigned_bits<Primitive>(~to_unsigned_bits(value.value_)));
    }

    friend constexpr auto operator&(Self lhs, Self rhs) noexcept -> Self {
        return Self(from_unsigned_bits<Primitive>(to_unsigned_bits(lhs.value_) &
                                                  to_unsigned_bits(rhs.value_)));
    }

    friend constexpr auto operator|(Self lhs, Self rhs) noexcept -> Self {
        return Self(from_unsigned_bits<Primitive>(to_unsigned_bits(lhs.value_) |
                                                  to_unsigned_bits(rhs.value_)));
    }

    friend constexpr auto operator^(Self lhs, Self rhs) noexcept -> Self {
        return Self(from_unsigned_bits<Primitive>(to_unsigned_bits(lhs.value_) ^
                                                  to_unsigned_bits(rhs.value_)));
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    friend constexpr auto operator<<(Self lhs, Shift rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_shl(rhs);
        if (overflow && should_check_integer_overflow()) panic_invalid_shift();
        return value;
    }

    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    friend constexpr auto operator>>(Self lhs, Shift rhs) -> Self {
        auto [value, overflow] = lhs.overflowing_shr(rhs);
        if (overflow && should_check_integer_overflow()) panic_invalid_shift();
        return value;
    }

    constexpr auto operator++() & -> Self&
        requires(! IS_SIGNED)
    {
        return self() += Self(Primitive(1));
    }

    constexpr auto operator++(int) & -> Self
        requires(! IS_SIGNED)
    {
        auto previous = self();
        self() += Self(Primitive(1));
        return previous;
    }

    constexpr auto operator--() & -> Self&
        requires(! IS_SIGNED)
    {
        return self() -= Self(Primitive(1));
    }

    constexpr auto operator--(int) & -> Self
        requires(! IS_SIGNED)
    {
        auto previous = self();
        self() -= Self(Primitive(1));
        return previous;
    }

    constexpr auto operator+=(Self rhs) -> Self& {
        self() = self() + rhs;
        return self();
    }
    constexpr auto operator-=(Self rhs) -> Self& {
        self() = self() - rhs;
        return self();
    }
    constexpr auto operator*=(Self rhs) -> Self& {
        self() = self() * rhs;
        return self();
    }
    constexpr auto operator/=(Self rhs) -> Self& {
        self() = self() / rhs;
        return self();
    }
    constexpr auto operator%=(Self rhs) -> Self& {
        self() = self() % rhs;
        return self();
    }
    constexpr auto operator&=(Self rhs) noexcept -> Self& {
        self() = self() & rhs;
        return self();
    }
    constexpr auto operator|=(Self rhs) noexcept -> Self& {
        self() = self() | rhs;
        return self();
    }
    constexpr auto operator^=(Self rhs) noexcept -> Self& {
        self() = self() ^ rhs;
        return self();
    }
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    constexpr auto operator<<=(Shift rhs) -> Self& {
        self() = self() << rhs;
        return self();
    }
    template<typename Shift>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Shift>, U64>
    constexpr auto operator>>=(Shift rhs) -> Self& {
        self() = self() >> rhs;
        return self();
    }
};

#define RSTD_INTEGER_TYPE(NAME, PRIMITIVE, TAG)                                                \
    struct NAME final : Integer<NAME, PRIMITIVE, TAG> {                                        \
        using Base                = Integer<NAME, PRIMITIVE, TAG>;                             \
        constexpr NAME() noexcept = default;                                                   \
        explicit constexpr NAME(PRIMITIVE value) noexcept: Base(value) {}                      \
        template<RawInteger Source>                                                            \
            requires(! rstd::mtp::same<rstd::mtp::rm_cvf<Source>, bool>)                       \
        explicit constexpr NAME(Source value) noexcept: Base(static_cast<PRIMITIVE>(value)) {} \
        static const NAME MIN;                                                                 \
        static const NAME MAX;                                                                 \
        static const U32  BITS;                                                                \
    }

RSTD_INTEGER_TYPE(U8, rstd::uint8_t, u8_tag);
RSTD_INTEGER_TYPE(U16, rstd::uint16_t, u16_tag);
RSTD_INTEGER_TYPE(U32, rstd::uint32_t, u32_tag);
RSTD_INTEGER_TYPE(U64, rstd::uint64_t, u64_tag);
RSTD_INTEGER_TYPE(U128, rstd::uint128_t, u128_tag);
RSTD_INTEGER_TYPE(Usize, rstd::size_t, usize_tag);
RSTD_INTEGER_TYPE(I8, rstd::int8_t, i8_tag);
RSTD_INTEGER_TYPE(I16, rstd::int16_t, i16_tag);
RSTD_INTEGER_TYPE(I32, rstd::int32_t, i32_tag);
RSTD_INTEGER_TYPE(I64, rstd::int64_t, i64_tag);
RSTD_INTEGER_TYPE(I128, rstd::int128_t, i128_tag);
RSTD_INTEGER_TYPE(Isize, rstd::ptrdiff_t, isize_tag);

#undef RSTD_INTEGER_TYPE

#define RSTD_INTEGER_CONSTANTS(NAME, PRIMITIVE)                       \
    inline constexpr NAME NAME::MIN { raw_integer_min<PRIMITIVE>() }; \
    inline constexpr NAME NAME::MAX { raw_integer_max<PRIMITIVE>() }; \
    inline constexpr U32  NAME::BITS {                                \
        static_cast<rstd::uint32_t>(sizeof(PRIMITIVE) * 8)            \
    }

RSTD_INTEGER_CONSTANTS(U8, rstd::uint8_t);
RSTD_INTEGER_CONSTANTS(U16, rstd::uint16_t);
RSTD_INTEGER_CONSTANTS(U32, rstd::uint32_t);
RSTD_INTEGER_CONSTANTS(U64, rstd::uint64_t);
RSTD_INTEGER_CONSTANTS(U128, rstd::uint128_t);
RSTD_INTEGER_CONSTANTS(Usize, rstd::size_t);
RSTD_INTEGER_CONSTANTS(I8, rstd::int8_t);
RSTD_INTEGER_CONSTANTS(I16, rstd::int16_t);
RSTD_INTEGER_CONSTANTS(I32, rstd::int32_t);
RSTD_INTEGER_CONSTANTS(I64, rstd::int64_t);
RSTD_INTEGER_CONSTANTS(I128, rstd::int128_t);
RSTD_INTEGER_CONSTANTS(Isize, rstd::ptrdiff_t);

#undef RSTD_INTEGER_CONSTANTS

struct f32_tag {};
struct f64_tag {};

struct F32;
struct F64;

template<typename Derived, typename Primitive, typename Tag>
class Floating {
    static_assert(rstd::mtp::is_float<Primitive>);

    Primitive value_ {};

    constexpr auto self() noexcept -> Derived& { return static_cast<Derived&>(*this); }
    constexpr auto self() const noexcept -> Derived const& {
        return static_cast<Derived const&>(*this);
    }

public:
    using primitive_type = Primitive;
    using Self           = Derived;
    using Bits           = rstd::mtp::cond<sizeof(Primitive) == 4, U32, U64>;
    using ByteArray      = rstd::array<U8, sizeof(Primitive)>;

protected:
    constexpr Floating() noexcept = default;
    explicit constexpr Floating(Primitive value) noexcept: value_(value) {}

public:
    [[nodiscard]]
    constexpr auto to_primitive() const noexcept -> Primitive {
        return value_;
    }

    [[nodiscard]]
    constexpr auto as_ptr() const& noexcept [[clang::lifetimebound]] -> Primitive const* {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto as_mut_ptr() & noexcept [[clang::lifetimebound]] -> Primitive* {
        return &value_;
    }

    auto as_ptr() const&& -> Primitive const* = delete;
    auto as_mut_ptr() && -> Primitive*        = delete;

    [[nodiscard]]
    constexpr auto is_nan() const noexcept -> bool {
        return __builtin_isnan(value_);
    }

    [[nodiscard]]
    constexpr auto is_infinite() const noexcept -> bool {
        return __builtin_isinf(value_);
    }

    [[nodiscard]]
    constexpr auto is_finite() const noexcept -> bool {
        return __builtin_isfinite(value_);
    }

    [[nodiscard]]
    constexpr auto is_normal() const noexcept -> bool {
        return __builtin_isnormal(value_);
    }

    [[nodiscard]]
    constexpr auto is_subnormal() const noexcept -> bool {
        return classify() == rstd::num::FpCategory::Subnormal;
    }

    [[nodiscard]]
    constexpr auto is_sign_positive() const noexcept -> bool {
        return ! __builtin_signbit(value_);
    }

    [[nodiscard]]
    constexpr auto is_sign_negative() const noexcept -> bool {
        return __builtin_signbit(value_);
    }

    [[nodiscard]]
    constexpr auto classify() const noexcept -> rstd::num::FpCategory {
        if (is_nan()) return rstd::num::FpCategory::Nan;
        if (is_infinite()) return rstd::num::FpCategory::Infinite;
        if (value_ == Primitive(0)) return rstd::num::FpCategory::Zero;
        if (! is_normal()) return rstd::num::FpCategory::Subnormal;
        return rstd::num::FpCategory::Normal;
    }

    [[nodiscard]]
    constexpr auto to_bits() const noexcept -> Bits {
        using BitsPrimitive = typename Bits::primitive_type;
        return Bits(__builtin_bit_cast(BitsPrimitive, value_));
    }

    [[nodiscard]]
    static constexpr auto from_bits(Bits bits) noexcept -> Self {
        return Self(__builtin_bit_cast(Primitive, bits.to_primitive()));
    }

    [[nodiscard]]
    constexpr auto abs() const noexcept -> Self {
        return Self(__builtin_fabs(value_));
    }

    [[nodiscard]]
    constexpr auto min(Self rhs) const noexcept -> Self {
        return Self(__builtin_fmin(value_, rhs.value_));
    }

    [[nodiscard]]
    constexpr auto max(Self rhs) const noexcept -> Self {
        return Self(__builtin_fmax(value_, rhs.value_));
    }

    [[nodiscard]]
    constexpr auto clamp(Self minimum, Self maximum) const -> Self {
        if (minimum.is_nan() || maximum.is_nan() || minimum > maximum) {
            panic_invalid_float_clamp();
        }
        return self() < minimum ? minimum : self() > maximum ? maximum : self();
    }

    [[nodiscard]]
    constexpr auto floor() const noexcept -> Self {
        return Self(__builtin_floor(value_));
    }

    [[nodiscard]]
    constexpr auto ceil() const noexcept -> Self {
        return Self(__builtin_ceil(value_));
    }

    [[nodiscard]]
    constexpr auto round() const noexcept -> Self {
        return Self(__builtin_round(value_));
    }

    [[nodiscard]]
    constexpr auto trunc() const noexcept -> Self {
        return Self(__builtin_trunc(value_));
    }

    [[nodiscard]]
    constexpr auto fract() const noexcept -> Self {
        return self() - trunc();
    }

    [[nodiscard]]
    constexpr auto sqrt() const noexcept -> Self {
        return Self(__builtin_sqrt(value_));
    }

    template<typename Exponent>
        requires rstd::mtp::same<rstd::mtp::rm_cvf<Exponent>, I32>
    [[nodiscard]]
    constexpr auto powi(Exponent exponent) const noexcept -> Self {
        auto const     raw       = exponent.to_primitive();
        bool const     negative  = raw < 0;
        rstd::uint32_t remaining = negative ? rstd::uint32_t(0) - static_cast<rstd::uint32_t>(raw)
                                            : static_cast<rstd::uint32_t>(raw);
        Self           value(Primitive(1));
        Self           base = self();
        while (remaining != 0) {
            if ((remaining & 1u) != 0) value *= base;
            remaining >>= 1u;
            if (remaining != 0) base *= base;
        }
        return negative ? Self(Primitive(1)) / value : value;
    }

    [[nodiscard]]
    constexpr auto powf(Self exponent) const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_powf(value_, exponent.value_));
        } else {
            return Self(__builtin_pow(value_, exponent.value_));
        }
    }

    [[nodiscard]]
    constexpr auto exp() const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_expf(value_));
        } else {
            return Self(__builtin_exp(value_));
        }
    }

    [[nodiscard]]
    constexpr auto exp2() const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_exp2f(value_));
        } else {
            return Self(__builtin_exp2(value_));
        }
    }

    [[nodiscard]]
    constexpr auto ln() const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_logf(value_));
        } else {
            return Self(__builtin_log(value_));
        }
    }

    [[nodiscard]]
    constexpr auto log2() const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_log2f(value_));
        } else {
            return Self(__builtin_log2(value_));
        }
    }

    [[nodiscard]]
    constexpr auto log10() const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_log10f(value_));
        } else {
            return Self(__builtin_log10(value_));
        }
    }

    [[nodiscard]]
    constexpr auto log(Self base) const noexcept -> Self {
        if constexpr (sizeof(Primitive) == 4) {
            return Self(__builtin_logf(value_) / __builtin_logf(base.value_));
        } else {
            return Self(__builtin_log(value_) / __builtin_log(base.value_));
        }
    }

    [[nodiscard]]
    constexpr auto total_cmp(Self rhs) const noexcept -> std::strong_ordering {
        using SignedBits = rstd::mtp::cond<sizeof(Primitive) == 4, rstd::int32_t, rstd::int64_t>;
        using UnsignedBits =
            rstd::mtp::cond<sizeof(Primitive) == 4, rstd::uint32_t, rstd::uint64_t>;
        auto transform = [](Primitive value) constexpr -> SignedBits {
            auto bits   = __builtin_bit_cast(SignedBits, value);
            auto toggle = static_cast<UnsignedBits>(bits >> (sizeof(Primitive) * 8 - 1)) >> 1;
            return __builtin_bit_cast(SignedBits, static_cast<UnsignedBits>(bits) ^ toggle);
        };
        return transform(value_) <=> transform(rhs.value_);
    }

    [[nodiscard]]
    constexpr auto to_be_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    constexpr auto to_le_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    constexpr auto to_ne_bytes() const noexcept -> ByteArray;
    [[nodiscard]]
    static constexpr auto from_be_bytes(ByteArray bytes) noexcept -> Self;
    [[nodiscard]]
    static constexpr auto from_le_bytes(ByteArray bytes) noexcept -> Self;
    [[nodiscard]]
    static constexpr auto from_ne_bytes(ByteArray bytes) noexcept -> Self;

    friend constexpr auto operator==(Self lhs, Self rhs) noexcept -> bool {
        return lhs.value_ == rhs.value_;
    }

    friend constexpr auto operator<=>(Self lhs, Self rhs) noexcept -> rstd::partial_ordering {
        return lhs.value_ <=> rhs.value_;
    }

    friend constexpr auto operator+(Self lhs, Self rhs) noexcept -> Self {
        return Self(Primitive(lhs.value_ + rhs.value_));
    }

    friend constexpr auto operator-(Self lhs, Self rhs) noexcept -> Self {
        return Self(Primitive(lhs.value_ - rhs.value_));
    }

    friend constexpr auto operator*(Self lhs, Self rhs) noexcept -> Self {
        return Self(Primitive(lhs.value_ * rhs.value_));
    }

    friend constexpr auto operator/(Self lhs, Self rhs) noexcept -> Self {
        return Self(Primitive(lhs.value_ / rhs.value_));
    }

    friend constexpr auto operator-(Self value) noexcept -> Self { return Self(-value.value_); }

    constexpr auto operator+=(Self rhs) noexcept -> Self& {
        self() = self() + rhs;
        return self();
    }
    constexpr auto operator-=(Self rhs) noexcept -> Self& {
        self() = self() - rhs;
        return self();
    }
    constexpr auto operator*=(Self rhs) noexcept -> Self& {
        self() = self() * rhs;
        return self();
    }
    constexpr auto operator/=(Self rhs) noexcept -> Self& {
        self() = self() / rhs;
        return self();
    }
};

#define RSTD_FLOAT_TYPE(NAME, PRIMITIVE, TAG)                             \
    struct NAME final : Floating<NAME, PRIMITIVE, TAG> {                  \
        using Base                = Floating<NAME, PRIMITIVE, TAG>;       \
        constexpr NAME() noexcept = default;                              \
        explicit constexpr NAME(PRIMITIVE value) noexcept: Base(value) {} \
        static const NAME MIN;                                            \
        static const NAME MAX;                                            \
        static const NAME MIN_POSITIVE;                                   \
        static const NAME EPSILON;                                        \
        static const NAME INFINITY_;                                      \
        static const NAME NEG_INFINITY;                                   \
        static const NAME NAN_;                                           \
        static const U32  RADIX;                                          \
        static const U32  MANTISSA_DIGITS;                                \
        static const U32  DIGITS;                                         \
        static const I32  MIN_EXP;                                        \
        static const I32  MAX_EXP;                                        \
        static const I32  MIN_10_EXP;                                     \
        static const I32  MAX_10_EXP;                                     \
        struct consts {                                                   \
            static const NAME PI;                                         \
            static const NAME TAU;                                        \
            static const NAME FRAC_PI_2;                                  \
            static const NAME FRAC_PI_3;                                  \
            static const NAME FRAC_PI_4;                                  \
            static const NAME FRAC_PI_6;                                  \
            static const NAME FRAC_PI_8;                                  \
            static const NAME FRAC_1_PI;                                  \
            static const NAME FRAC_2_PI;                                  \
            static const NAME FRAC_2_SQRT_PI;                             \
            static const NAME SQRT_2;                                     \
            static const NAME FRAC_1_SQRT_2;                              \
            static const NAME E;                                          \
            static const NAME LOG2_10;                                    \
            static const NAME LOG2_E;                                     \
            static const NAME LOG10_2;                                    \
            static const NAME LOG10_E;                                    \
            static const NAME LN_2;                                       \
            static const NAME LN_10;                                      \
        };                                                                \
    }

RSTD_FLOAT_TYPE(F32, float, f32_tag);
RSTD_FLOAT_TYPE(F64, double, f64_tag);

#undef RSTD_FLOAT_TYPE

#define RSTD_FLOAT_LIMITS(NAME, PRIMITIVE, HUGE, NAN)                                       \
    inline constexpr NAME NAME::MIN { -raw_float_traits<PRIMITIVE>::max };                  \
    inline constexpr NAME NAME::MAX { raw_float_traits<PRIMITIVE>::max };                   \
    inline constexpr NAME NAME::MIN_POSITIVE { raw_float_traits<PRIMITIVE>::min_positive }; \
    inline constexpr NAME NAME::EPSILON { raw_float_traits<PRIMITIVE>::epsilon };           \
    inline constexpr NAME NAME::INFINITY_ { HUGE() };                                       \
    inline constexpr NAME NAME::NEG_INFINITY { -HUGE() };                                   \
    inline constexpr NAME NAME::NAN_ { NAN("") };                                           \
    inline constexpr U32  NAME::RADIX { static_cast<rstd::uint32_t>(                        \
        raw_float_traits<PRIMITIVE>::radix) };                                              \
    inline constexpr U32  NAME::MANTISSA_DIGITS { static_cast<rstd::uint32_t>(              \
        raw_float_traits<PRIMITIVE>::mantissa_digits) };                                    \
    inline constexpr U32  NAME::DIGITS { static_cast<rstd::uint32_t>(                       \
        raw_float_traits<PRIMITIVE>::digits) };                                             \
    inline constexpr I32  NAME::MIN_EXP { static_cast<rstd::int32_t>(                       \
        raw_float_traits<PRIMITIVE>::min_exponent) };                                       \
    inline constexpr I32  NAME::MAX_EXP { static_cast<rstd::int32_t>(                       \
        raw_float_traits<PRIMITIVE>::max_exponent) };                                       \
    inline constexpr I32  NAME::MIN_10_EXP { static_cast<rstd::int32_t>(                    \
        raw_float_traits<PRIMITIVE>::min_exponent_10) };                                    \
    inline constexpr I32  NAME::MAX_10_EXP {                                                \
        static_cast<rstd::int32_t>(raw_float_traits<PRIMITIVE>::max_exponent_10)            \
    }

RSTD_FLOAT_LIMITS(F32, float, __builtin_huge_valf, __builtin_nanf);
RSTD_FLOAT_LIMITS(F64, double, __builtin_huge_val, __builtin_nan);

#undef RSTD_FLOAT_LIMITS

#define RSTD_FLOAT_CONST(TYPE, NAME, VALUE)               \
    inline constexpr TYPE TYPE::consts::NAME {            \
        static_cast<typename TYPE::primitive_type>(VALUE) \
    }

#define RSTD_FLOAT_CONSTS(TYPE)                                                     \
    RSTD_FLOAT_CONST(TYPE, PI, 3.14159265358979323846264338327950288L);             \
    RSTD_FLOAT_CONST(TYPE, TAU, 6.28318530717958647692528676655900577L);            \
    RSTD_FLOAT_CONST(TYPE, FRAC_PI_2, 1.57079632679489661923132169163975144L);      \
    RSTD_FLOAT_CONST(TYPE, FRAC_PI_3, 1.04719755119659774615421446109316763L);      \
    RSTD_FLOAT_CONST(TYPE, FRAC_PI_4, 0.785398163397448309615660845819875721L);     \
    RSTD_FLOAT_CONST(TYPE, FRAC_PI_6, 0.52359877559829887307710723054658381L);      \
    RSTD_FLOAT_CONST(TYPE, FRAC_PI_8, 0.39269908169872415480783042290993786L);      \
    RSTD_FLOAT_CONST(TYPE, FRAC_1_PI, 0.318309886183790671537767526745028724L);     \
    RSTD_FLOAT_CONST(TYPE, FRAC_2_PI, 0.636619772367581343075535053490057448L);     \
    RSTD_FLOAT_CONST(TYPE, FRAC_2_SQRT_PI, 1.12837916709551257389615890312154517L); \
    RSTD_FLOAT_CONST(TYPE, SQRT_2, 1.41421356237309504880168872420969808L);         \
    RSTD_FLOAT_CONST(TYPE, FRAC_1_SQRT_2, 0.707106781186547524400844362104849039L); \
    RSTD_FLOAT_CONST(TYPE, E, 2.71828182845904523536028747135266250L);              \
    RSTD_FLOAT_CONST(TYPE, LOG2_10, 3.32192809488736234787031942948939018L);        \
    RSTD_FLOAT_CONST(TYPE, LOG2_E, 1.44269504088896340735992468100189214L);         \
    RSTD_FLOAT_CONST(TYPE, LOG10_2, 0.301029995663981195213738894724493027L);       \
    RSTD_FLOAT_CONST(TYPE, LOG10_E, 0.434294481903251827651128918916605082L);       \
    RSTD_FLOAT_CONST(TYPE, LN_2, 0.693147180559945309417232121458176568L);          \
    RSTD_FLOAT_CONST(TYPE, LN_10, 2.30258509299404568401799145468436421L)

RSTD_FLOAT_CONSTS(F32);
RSTD_FLOAT_CONSTS(F64);

#undef RSTD_FLOAT_CONSTS
#undef RSTD_FLOAT_CONST

static_assert(sizeof(U8) == sizeof(rstd::byte));
static_assert(alignof(U8) == alignof(rstd::byte));
static_assert(rstd::mtp::is_standard_layout<U8>);
static_assert(rstd::mtp::triv_copyable<U8>);
static_assert(rstd::mtp::triv_drop<U8>);
static_assert(rstd::mtp::has_unique_object_representations<U8>);
static_assert(alignof(U64) == alignof(rstd::uint64_t));
static_assert(rstd::mtp::is_standard_layout<I32>);
static_assert(rstd::mtp::triv_copyable<I32>);
static_assert(rstd::mtp::triv_drop<I32>);
static_assert(U8::MAX.to_primitive() == rstd::uint8_t(255));
static_assert(I8::MIN.to_primitive() == rstd::int8_t(-128));
static_assert(U64::BITS.to_primitive() == rstd::uint32_t(64));
static_assert(sizeof(F32) == sizeof(float));
static_assert(rstd::mtp::is_standard_layout<F64>);
static_assert(F32(1.5f).to_bits().to_primitive() == rstd::uint32_t(0x3fc00000));
static_assert(F64::INFINITY_.is_infinite());
static_assert(F32::consts::PI.to_primitive() > 3.14f);
static_assert(F64::MAX.is_finite());
static_assert(U8(rstd::uint8_t(255)).wrapping_add(U8(rstd::uint8_t(1))) == U8());
static_assert(I8(rstd::int8_t(-128)).wrapping_neg() == I8(rstd::int8_t(-128)));
static_assert(U32(rstd::uint32_t(1)).overflowing_shl(U64(rstd::uint64_t(32))).template get<1>());

export namespace rstd
{
using u8    = ::U8;
using u16   = ::U16;
using u32   = ::U32;
using u64   = ::U64;
using u128  = ::U128;
using usize = ::Usize;
using i8    = ::I8;
using i16   = ::I16;
using i32   = ::I32;
using i64   = ::I64;
using i128  = ::I128;
using isize = ::Isize;
using f32   = ::F32;
using f64   = ::F64;
} // namespace rstd

export namespace rstd::prelude
{
using rstd::u8;
using rstd::u16;
using rstd::u32;
using rstd::u64;
using rstd::u128;
using rstd::usize;
using rstd::i8;
using rstd::i16;
using rstd::i32;
using rstd::i64;
using rstd::i128;
using rstd::isize;
using rstd::f32;
using rstd::f64;
} // namespace rstd::prelude

export namespace rstd::num
{
template<typename T>
concept Integer = mtp::is_int<T>;

template<typename T>
concept UnsignedInteger = mtp::same<mtp::rm_cvf<T>, u8> || mtp::same<mtp::rm_cvf<T>, u16> ||
                          mtp::same<mtp::rm_cvf<T>, u32> || mtp::same<mtp::rm_cvf<T>, u64> ||
                          mtp::same<mtp::rm_cvf<T>, u128> || mtp::same<mtp::rm_cvf<T>, usize>;

template<typename T>
concept SignedInteger = Integer<T> && (! UnsignedInteger<T>);

template<typename T>
concept Float = mtp::same<mtp::rm_cvf<T>, f32> || mtp::same<mtp::rm_cvf<T>, f64>;

template<typename T>
concept Numeric = Integer<T> || Float<T>;

template<typename T>
concept PrimitiveInteger =
    rstd::is_raw_int<mtp::rm_cvf<T>> && (! mtp::same<mtp::rm_cvf<T>, bool>) &&
    (! mtp::same<mtp::rm_cvf<T>, char>) && (! mtp::same<mtp::rm_cvf<T>, wchar_t>) &&
    (! mtp::same<mtp::rm_cvf<T>, char8_t>) && (! mtp::same<mtp::rm_cvf<T>, char16_t>) &&
    (! mtp::same<mtp::rm_cvf<T>, char32_t>);

template<typename T>
concept PrimitiveFloat = mtp::same<mtp::rm_cvf<T>, float> || mtp::same<mtp::rm_cvf<T>, double> ||
                         mtp::same<mtp::rm_cvf<T>, long double>;
} // namespace rstd::num
