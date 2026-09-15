export module rstd.core:iter.range;
import :num.types;
import :num.convert;
export import :iter.traits;

namespace rstd::iter
{

/// Half-open integer range `[start, end)`, analogous to Rust's `start..end`.
export template<num::Integer T>
struct Range : DefaultInClass<Range<T>, Iterator> {
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;
    static constexpr bool PROVEN_EXACT_SIZE =
        sizeof(typename T::primitive_type) <= sizeof(rstd::size_t);
    T start;
    T fin;

private:
    static constexpr auto one() noexcept -> T { return T(typename T::primitive_type(1)); }

    constexpr auto distance() const noexcept -> typename T::Unsigned {
        using Unsigned          = typename T::Unsigned;
        using UnsignedPrimitive = typename T::Unsigned::primitive_type;
        auto const start_bits   = static_cast<UnsignedPrimitive>(start.to_primitive());
        auto const end_bits     = static_cast<UnsignedPrimitive>(fin.to_primitive());
        return Unsigned(UnsignedPrimitive(end_bits - start_bits));
    }

    constexpr auto converted_distance() const -> Option<usize> {
        using Unsigned = typename T::Unsigned;
        if constexpr (num::detail::lossless_integer_conversion<Unsigned, usize>) {
            return Some(Impl<convert::From<Unsigned>, usize>::from(distance()));
        } else {
            auto converted = rstd::try_from<usize>(distance());
            if (converted.is_err()) return None();
            return Some(converted.unwrap());
        }
    }

public:
    constexpr Range(T s, T e): start(s), fin(e) {}

    constexpr auto next() -> Option<Item> {
        if (start >= fin) return rstd::None();
        auto value = start;
        start += one();
        return rstd::Some(value);
    }

    constexpr auto next_back() -> Option<Item> {
        if (start >= fin) return rstd::None();
        fin -= one();
        return rstd::Some(fin);
    }

    constexpr auto size_hint() const -> SizeHint {
        if (start >= fin) return { usize(), rstd::Some(usize()) };
        auto converted = converted_distance();
        if (converted.is_none()) return { usize::MAX, rstd::None() };
        auto n = converted.unwrap();
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize
        requires(sizeof(typename T::primitive_type) <= sizeof(rstd::size_t))
    {
        if (start >= fin) return usize();
        return converted_distance().unwrap();
    }

    constexpr bool is_empty() const { return start >= fin; }
    constexpr bool contains(const T& v) const { return v >= start && v < fin; }
};

template<num::Integer T>
    requires Range<T>::PROVEN_EXACT_SIZE
struct IteratorTraversal<Range<T>> {
    static auto advance(Range<T>& iterator, usize n)
        -> Result<empty, num::nonzero::NonZero<usize>> {
        auto length = iterator.len();
        if (n >= length) {
            iterator.start = iterator.fin;
            if (n > length) return Err(num::nonzero::NonZero<usize>::make_unchecked(n - length));
            return Ok(empty {});
        }
        using P = typename T::primitive_type;
        using U = typename T::Unsigned::primitive_type;
        // Unsigned arithmetic handles ranges spanning the signed zero boundary.
        auto bits      = U(U(iterator.start.to_primitive()) + U(n.to_primitive()));
        iterator.start = T(static_cast<P>(bits));
        return Ok(empty {});
    }
    static auto count(Range<T>& iterator) -> usize {
        auto length    = iterator.len();
        iterator.start = iterator.fin;
        return length;
    }
    static auto last(Range<T>& iterator) -> Option<T> {
        auto item      = iterator.next_back();
        iterator.start = iterator.fin;
        return item;
    }
};

/// Creates the range `[start, end)`.
export template<num::Integer T>
constexpr auto range(T start, T end) -> Range<T> {
    return Range<T>(start, end);
}

} // namespace rstd::iter
