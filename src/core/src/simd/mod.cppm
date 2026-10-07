export module rstd.core:simd;
export import :array;
import :num.convert;

export namespace rstd::simd
{

template<typename T>
concept IntegerElement =
    mtp::same_as<T, u8> || mtp::same_as<T, u16> || mtp::same_as<T, u32> || mtp::same_as<T, u64> ||
    mtp::same_as<T, usize> || mtp::same_as<T, i8> || mtp::same_as<T, i16> || mtp::same_as<T, i32> ||
    mtp::same_as<T, i64> || mtp::same_as<T, isize>;

template<typename T>
concept UnsignedElement = IntegerElement<T> && ! T::IS_SIGNED;

template<typename T>
concept FloatElement = mtp::same_as<T, f32> || mtp::same_as<T, f64>;

template<typename T>
concept Element = IntegerElement<T> || FloatElement<T>;

template<size_t N>
concept LaneCount = N > 0 && N <= 64 && (N & (N - 1)) == 0;

template<Element T, size_t N>
    requires LaneCount<N>
class Simd;

/// A lane predicate with an unspecified object representation.
template<size_t N>
    requires LaneCount<N>
class Mask {
    using Bits = bool __attribute__((ext_vector_type(N)));
    Bits bits_ {};

    explicit Mask(Bits bits) noexcept: bits_(bits) {}

    template<Element T, size_t M>
        requires LaneCount<M>
    friend class Simd;

public:
    Mask() noexcept = default;
    static constexpr usize LANES { N };

    static auto splat(bool value) noexcept -> Mask { return Mask(Bits(value)); }

    /// Selects the first min(count, N) lanes.
    static auto first_n(usize count) noexcept -> Mask {
        // Construct through integer lanes: Clang bool-vector element writes are unsupported.
        using Indices = uint8_t __attribute__((ext_vector_type(N)));
        Indices indices {};
        for (size_t i = 0; i < N; ++i) indices[i] = uint8_t(i);
        auto limit = count.to_primitive() < N ? count.to_primitive() : N;
        return Mask(__builtin_convertvector(indices < Indices(uint8_t(limit)), Bits));
    }

    auto operator[](usize index) const noexcept -> bool {
        if (index.to_primitive() >= N) panic { "SIMD mask index out of bounds" };
        using Lanes = uint8_t __attribute__((ext_vector_type(N)));
        return __builtin_convertvector(bits_, Lanes)[index.to_primitive()] != 0;
    }

    auto any() const noexcept -> bool { return __builtin_popcountg(bits_) != 0; }
    auto all() const noexcept -> bool { return __builtin_popcountg(bits_) == N; }
    auto count() const noexcept -> usize { return usize(__builtin_popcountg(bits_)); }
    auto first_set() const noexcept -> Option<usize> {
        auto index = __builtin_ctzg(bits_, int(N));
        if (index == N) return None();
        return Some(usize(index));
    }

    auto operator&(Mask rhs) const noexcept -> Mask { return Mask(bits_ & rhs.bits_); }
    auto operator|(Mask rhs) const noexcept -> Mask { return Mask(bits_ | rhs.bits_); }
    auto operator^(Mask rhs) const noexcept -> Mask { return Mask(bits_ ^ rhs.bits_); }
    auto operator~() const noexcept -> Mask { return Mask(~bits_); }
    auto operator==(Mask rhs) const noexcept -> bool { return ! (*this ^ rhs).any(); }

    /// Both arguments are evaluated before selection.
    template<Element T>
    auto select(Simd<T, N> yes, Simd<T, N> no) const noexcept -> Simd<T, N>;
};

/// A fixed lane count; integer arithmetic wraps in every build configuration.
template<Element T, size_t N>
    requires LaneCount<N>
class Simd {
    using Primitive   = typename T::primitive_type;
    using Raw         = Primitive __attribute__((ext_vector_type(N)));
    using Unsigned    = mtp::cond<sizeof(Primitive) == 1,
                                  uint8_t,
                                  mtp::cond<sizeof(Primitive) == 2,
                                            uint16_t,
                                            mtp::cond<sizeof(Primitive) == 4, uint32_t, uint64_t>>>;
    using UnsignedRaw = Unsigned __attribute__((ext_vector_type(N)));
    Raw values_ {};

    explicit Simd(const Raw& values) noexcept: values_(values) {}
    friend class Mask<N>;
    template<Element U, size_t M>
        requires LaneCount<M>
    friend class Simd;

    template<typename F>
    auto classify_lanes(F predicate) const noexcept -> Mask<N> {
        using Lanes = uint8_t __attribute__((ext_vector_type(N)));
        Lanes result {};
        for (size_t i = 0; i < N; ++i) result[i] = predicate(T(values_[i]));
        return Mask<N>(__builtin_convertvector(result, typename Mask<N>::Bits));
    }

public:
    using value_type = T;
    using bits_type  = mtp::cond<sizeof(Primitive) == 4, u32, u64>;
    static constexpr usize LANES { N };
    Simd() noexcept = default;

    static auto splat(T value) noexcept -> Simd { return Simd(Raw(value.to_primitive())); }

    static auto from_array(const array<T, N>& values) noexcept -> Simd {
        return from_slice(values.as_slice());
    }
    auto to_array() const noexcept -> array<T, N> {
        array<T, N> result {};
        copy_to_slice(result.as_mut_slice());
        return result;
    }

    /// Loads N elements, panicking if the slice is too short.
    static auto from_slice(slice<T> values) noexcept -> Simd {
        if (values.len().to_primitive() < N) panic { "SIMD input slice too short" };
        Simd result;
        for (size_t i = 0; i < N; ++i) result.values_[i] = values[usize(i)].to_primitive();
        return result;
    }

    /// Missing lanes retain the corresponding fallback value.
    static auto load_or(slice<T> values, Simd fallback) noexcept -> Simd {
        if (values.len().to_primitive() >= N) return from_slice(values);
        for (size_t i = 0; i < values.len().to_primitive(); ++i)
            fallback.values_[i] = values[usize(i)].to_primitive();
        return fallback;
    }

    /// Writes N elements, panicking before writing if the slice is too short.
    void copy_to_slice(mut_ref<T[]> output) const noexcept {
        if (output.len().to_primitive() < N) panic { "SIMD output slice too short" };
        for (size_t i = 0; i < N; ++i) output[usize(i)] = T(values_[i]);
    }

    /// Writes only min(output.len(), N) elements.
    void store_partial(mut_ref<T[]> output) const noexcept {
        if (output.len().to_primitive() >= N) {
            copy_to_slice(output);
            return;
        }
        for (size_t i = 0; i < output.len().to_primitive(); ++i) output[usize(i)] = T(values_[i]);
    }

    auto operator[](usize index) const noexcept -> T {
        if (index.to_primitive() >= N) panic { "SIMD lane index out of bounds" };
        return T(values_[index.to_primitive()]);
    }
    auto with_lane(usize index, T value) const noexcept -> Simd {
        if (index.to_primitive() >= N) panic { "SIMD lane index out of bounds" };
        auto result                          = *this;
        result.values_[index.to_primitive()] = value.to_primitive();
        return result;
    }

    auto simd_eq(Simd rhs) const noexcept -> Mask<N> {
        return Mask<N>(__builtin_convertvector(values_ == rhs.values_, typename Mask<N>::Bits));
    }
    auto simd_ne(Simd rhs) const noexcept -> Mask<N> { return ~simd_eq(rhs); }
    auto simd_lt(Simd rhs) const noexcept -> Mask<N> {
        return Mask<N>(__builtin_convertvector(values_ < rhs.values_, typename Mask<N>::Bits));
    }
    auto simd_le(Simd rhs) const noexcept -> Mask<N> {
        return Mask<N>(__builtin_convertvector(values_ <= rhs.values_, typename Mask<N>::Bits));
    }
    auto simd_gt(Simd rhs) const noexcept -> Mask<N> { return rhs.simd_lt(*this); }
    auto simd_ge(Simd rhs) const noexcept -> Mask<N> { return rhs.simd_le(*this); }
    auto operator==(Simd rhs) const noexcept -> bool { return simd_eq(rhs).all(); }

    auto operator+(Simd rhs) const noexcept -> Simd {
        if constexpr (IntegerElement<T>) {
            auto result = __builtin_bit_cast(UnsignedRaw, values_) +
                          __builtin_bit_cast(UnsignedRaw, rhs.values_);
            return Simd(__builtin_bit_cast(Raw, result));
        } else {
            return Simd(values_ + rhs.values_);
        }
    }
    auto operator-(Simd rhs) const noexcept -> Simd {
        if constexpr (IntegerElement<T>) {
            auto result = __builtin_bit_cast(UnsignedRaw, values_) -
                          __builtin_bit_cast(UnsignedRaw, rhs.values_);
            return Simd(__builtin_bit_cast(Raw, result));
        } else {
            return Simd(values_ - rhs.values_);
        }
    }
    auto operator*(Simd rhs) const noexcept -> Simd {
        if constexpr (IntegerElement<T>) {
            auto result = __builtin_bit_cast(UnsignedRaw, values_) *
                          __builtin_bit_cast(UnsignedRaw, rhs.values_);
            return Simd(__builtin_bit_cast(Raw, result));
        } else {
            return Simd(values_ * rhs.values_);
        }
    }
    auto operator&(Simd rhs) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(values_ & rhs.values_);
    }
    auto operator|(Simd rhs) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(values_ | rhs.values_);
    }
    auto operator^(Simd rhs) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(values_ ^ rhs.values_);
    }
    auto operator~() const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(~values_);
    }

    auto operator/(Simd rhs) const noexcept -> Simd
        requires FloatElement<T>
    {
        return Simd(values_ / rhs.values_);
    }

    auto operator-() const noexcept -> Simd
        requires FloatElement<T>
    {
        return Simd(-values_);
    }

    auto abs() const noexcept -> Simd
        requires FloatElement<T>
    {
        return Simd(__builtin_elementwise_abs(values_));
    }

    auto copysign(Simd sign) const noexcept -> Simd
        requires FloatElement<T>
    {
        return Simd(__builtin_elementwise_copysign(values_, sign.values_));
    }

    /// Fused multiplication and addition with a single rounding.
    auto mul_add(Simd multiplier, Simd addend) const noexcept -> Simd
        requires FloatElement<T>
    {
        return Simd(__builtin_elementwise_fma(values_, multiplier.values_, addend.values_));
    }

    auto min(Simd rhs) const noexcept -> Simd {
        return Simd(__builtin_elementwise_min(values_, rhs.values_));
    }
    auto max(Simd rhs) const noexcept -> Simd {
        return Simd(__builtin_elementwise_max(values_, rhs.values_));
    }

    auto is_nan() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_nan();
        });
    }

    auto is_infinite() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_infinite();
        });
    }

    auto is_finite() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_finite();
        });
    }

    auto is_normal() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_normal();
        });
    }

    auto is_subnormal() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_subnormal();
        });
    }

    auto is_sign_negative() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_sign_negative();
        });
    }

    auto is_sign_positive() const noexcept -> Mask<N>
        requires FloatElement<T>
    {
        return classify_lanes([](T value) {
            return value.is_sign_positive();
        });
    }

    auto wrapping_neg() const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd() - *this;
    }

    auto wrapping_abs() const noexcept -> Simd
        requires(IntegerElement<T> && T::IS_SIGNED)
    {
        return Simd(__builtin_elementwise_abs(values_));
    }

    auto saturating_add(Simd rhs) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(__builtin_elementwise_add_sat(values_, rhs.values_));
    }

    auto saturating_sub(Simd rhs) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return Simd(__builtin_elementwise_sub_sat(values_, rhs.values_));
    }

    template<UnsignedElement U>
    auto wrapping_shl(Simd<U, N> counts) const noexcept -> Simd
        requires IntegerElement<T>
    {
        using CountRaw  = typename Simd<U, N>::Raw;
        auto normalized = counts.values_ & CountRaw(sizeof(Primitive) * 8 - 1);
        auto shifts     = __builtin_convertvector(normalized, UnsignedRaw);
        return Simd(__builtin_bit_cast(Raw, __builtin_bit_cast(UnsignedRaw, values_) << shifts));
    }

    template<UnsignedElement U>
    auto wrapping_shr(Simd<U, N> counts) const noexcept -> Simd
        requires IntegerElement<T>
    {
        using CountRaw  = typename Simd<U, N>::Raw;
        auto normalized = counts.values_ & CountRaw(sizeof(Primitive) * 8 - 1);
        return Simd(values_ >> __builtin_convertvector(normalized, Raw));
    }

    auto wrapping_shl(u64 count) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return wrapping_shl(Simd<u64, N>::splat(count));
    }

    auto wrapping_shr(u64 count) const noexcept -> Simd
        requires IntegerElement<T>
    {
        return wrapping_shr(Simd<u64, N>::splat(count));
    }

    template<Element U>
    auto cast() const noexcept -> Simd<U, N> {
        Simd<U, N> result;
        for (size_t i = 0; i < N; ++i)
            result.values_[i] = rstd::as_cast<U>(T(values_[i])).to_primitive();
        return result;
    }

    template<Element U>
        requires(sizeof(typename U::primitive_type) == sizeof(Primitive))
    auto bit_cast() const noexcept -> Simd<U, N> {
        return Simd<U, N>(__builtin_bit_cast(typename Simd<U, N>::Raw, values_));
    }

    auto to_bits() const noexcept -> Simd<bits_type, N>
        requires FloatElement<T>
    {
        return bit_cast<bits_type>();
    }

    static auto from_bits(Simd<bits_type, N> bits) noexcept -> Simd
        requires FloatElement<T>
    {
        return bits.template bit_cast<T>();
    }

    template<size_t... Indices>
        requires(LaneCount<sizeof...(Indices)> && ((Indices < N) && ...))
    auto swizzle() const noexcept -> Simd<T, sizeof...(Indices)> {
        return Simd<T, sizeof...(Indices)>(__builtin_shufflevector(values_, values_, Indices...));
    }

    template<size_t... Indices>
        requires(LaneCount<sizeof...(Indices)> && ((Indices < 2 * N) && ...))
    auto swizzle(Simd other) const noexcept -> Simd<T, sizeof...(Indices)> {
        return Simd<T, sizeof...(Indices)>(
            __builtin_shufflevector(values_, other.values_, Indices...));
    }

    auto concat(Simd other) const noexcept
        requires(N <= 32)
    {
        return [&]<size_t... I>(mtp::index_sequence<I...>) {
            return swizzle<I...>(other);
        }(mtp::make_index_sequence<2 * N> {});
    }

    auto split() const noexcept
        requires(N > 1)
    {
        return [&]<size_t... I>(mtp::index_sequence<I...>) {
            return tuple { swizzle<I...>(), swizzle<(I + N / 2)...>() };
        }(mtp::make_index_sequence<N / 2> {});
    }

    /// Returns the low and high halves of a0,b0,a1,b1,... .
    auto interleave(Simd other) const noexcept {
        return [&]<size_t... I>(mtp::index_sequence<I...>) {
            return tuple { swizzle<(I / 2 + (I % 2) * N)...>(other),
                           swizzle<((I + N) / 2 + ((I + N) % 2) * N)...>(other) };
        }(mtp::make_index_sequence<N> {});
    }

    /// Inverse of interleave, with *this as the low half.
    auto deinterleave(Simd high) const noexcept {
        return [&]<size_t... I>(mtp::index_sequence<I...>) {
            return tuple { swizzle<(2 * I)...>(high), swizzle<(2 * I + 1)...>(high) };
        }(mtp::make_index_sequence<N> {});
    }

    auto reduce_min() const noexcept -> T {
        if constexpr (N == 1)
            return T(values_[0]);
        else {
            auto [low, high] = split();
            return low.reduce_min().min(high.reduce_min());
        }
    }
    auto reduce_max() const noexcept -> T {
        if constexpr (N == 1)
            return T(values_[0]);
        else {
            auto [low, high] = split();
            return low.reduce_max().max(high.reduce_max());
        }
    }
    auto reduce_and() const noexcept -> T
        requires IntegerElement<T>
    {
        return T(__builtin_reduce_and(values_));
    }
    auto reduce_or() const noexcept -> T
        requires IntegerElement<T>
    {
        return T(__builtin_reduce_or(values_));
    }
    auto reduce_xor() const noexcept -> T
        requires IntegerElement<T>
    {
        return T(__builtin_reduce_xor(values_));
    }

    /// Balanced pairwise addition; no initial +0 and no fast-math assumptions.
    auto reduce_sum_unordered() const noexcept -> T
        requires FloatElement<T>
    {
#pragma clang fp reassociate(off)
#pragma clang fp contract(off)
        if constexpr (N == 1)
            return T(values_[0]);
        else {
            auto [low, high] = split();
            return low.reduce_sum_unordered() + high.reduce_sum_unordered();
        }
    }

    auto reduce_sum() const noexcept -> T
        requires IntegerElement<T>
    {
        auto value = __builtin_reduce_add(__builtin_bit_cast(UnsignedRaw, values_));
        return T(__builtin_bit_cast(Primitive, value));
    }

    /// Adds lanes from index zero upwards to initial, without reassociation.
    auto reduce_sum_ordered(T initial = T()) const noexcept -> T
        requires FloatElement<T>
    {
#pragma clang fp reassociate(off)
#pragma clang fp contract(off)
        auto result = initial.to_primitive();
        for (size_t i = 0; i < N; ++i) result += values_[i];
        return T(result);
    }
};

template<size_t N>
    requires LaneCount<N>
template<Element T>
auto Mask<N>::select(Simd<T, N> yes, Simd<T, N> no) const noexcept -> Simd<T, N> {
    using S =
        mtp::cond<sizeof(typename T::primitive_type) == 1,
                  int8_t,
                  mtp::cond<sizeof(typename T::primitive_type) == 2,
                            int16_t,
                            mtp::cond<sizeof(typename T::primitive_type) == 4, int32_t, int64_t>>>;
    using Predicate = S __attribute__((ext_vector_type(N)));
    auto predicate  = -__builtin_convertvector(bits_, Predicate);
    return Simd<T, N>(predicate ? yes.values_ : no.values_);
}

} // namespace rstd::simd
