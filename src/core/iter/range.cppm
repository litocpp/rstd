export module rstd.core:iter.range;
export import :iter.traits;

namespace rstd::iter
{

/// Half-open integer range `[start, end)`, analogous to Rust's `start..end`.
export template<class T>
struct Range : WithTraitDefault<Range<T>, Iterator> {
    using Item = T;
    T start;
    T fin;

    constexpr Range(T s, T e): start(s), fin(e) {}

    constexpr auto next() -> Option<Item> {
        if (start >= fin) return rstd::None();
        return rstd::Some(start++);
    }

    constexpr auto next_back() -> Option<Item> {
        if (start >= fin) return rstd::None();
        --fin;
        return rstd::Some(T(fin));
    }

    constexpr auto size_hint() const -> SizeHint {
        usize n = start >= fin ? 0 : static_cast<usize>(fin - start);
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize {
        return start >= fin ? 0 : static_cast<usize>(fin - start);
    }

    constexpr bool is_empty() const { return start >= fin; }
    constexpr bool contains(const T& v) const { return v >= start && v < fin; }
};

/// Creates the range `[start, end)`.
export template<class T>
constexpr auto range(T start, T end) -> Range<T> {
    return Range<T>(start, end);
}

} // namespace rstd::iter
