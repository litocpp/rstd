module;
#include <rstd/macro.hpp>

export module rstd.core:array;
export import :clone;
export import :cmp;
export import :convert;
export import :iter;
export import :option;
export import :panicking;
export import :slice;
export import rstd.basic;

namespace rstd
{

export template<typename T, usize N>
class array;

export template<typename T, usize N>
class ArrayIntoIter;

namespace array_detail
{

template<typename T, usize N>
struct Storage {
    T values[N];
};

template<typename T>
struct Storage<T, 0> {};

} // namespace array_detail

/// An owned fixed-size sequence, analogous to Rust's `[T; N]`.
export template<typename T, usize N>
class array {
    array_detail::Storage<T, N> m_storage;

    template<usize I>
    constexpr auto element_unchecked() noexcept -> T& {
        static_assert(I < N, "array index out of bounds");
        return m_storage.values[I];
    }

    template<usize I>
    constexpr auto element_unchecked() const noexcept -> const T& {
        static_assert(I < N, "array index out of bounds");
        return m_storage.values[I];
    }

    template<usize... Is>
    constexpr auto clone_impl(mtp::index_sequence<Is...>) const -> array {
        return array { rstd::as<clone::Clone>(element_unchecked<Is>()).clone()... };
    }

    template<usize... Is>
    constexpr auto each_ref_impl(mtp::index_sequence<Is...>) const -> array<ref<T>, N> {
        return array<ref<T>, N> { ref<T>::from_raw_parts(
            rstd::addressof(element_unchecked<Is>()))... };
    }

    template<usize... Is>
    constexpr auto each_mut_impl(mtp::index_sequence<Is...>) -> array<mut_ref<T>, N> {
        return array<mut_ref<T>, N> { mut_ref<T>::from_raw_parts(
            rstd::addressof(element_unchecked<Is>()))... };
    }

    template<typename F, usize... Is>
    constexpr auto map_impl(F& function, mtp::index_sequence<Is...>) {
        using U = mtp::rm_cvf<decltype(function(mtp::declval<T>()))>;
        return array<U, N> { function(rstd::move(element_unchecked<Is>()))... };
    }

    template<typename F, usize... Is>
    static constexpr auto from_fn_impl(F& function, mtp::index_sequence<Is...>) -> array {
        return array { function(Is)... };
    }

    template<usize... Is>
    static constexpr auto repeat_impl(const T& value, mtp::index_sequence<Is...>) -> array {
        return array { ((void)Is, rstd::as<clone::Clone>(value).clone())... };
    }

public:
    USE_TRAIT(array)

    using value_type = T;
    using Target     = T[];
    using IntoIter   = ArrayIntoIter<T, N>;

    static constexpr usize LENGTH = N;

    constexpr array() = default;

    template<typename... Us>
        requires(N > 0 && sizeof...(Us) == N && (mtp::init<T, Us &&> && ...))
    constexpr array(Us&&... values) noexcept((mtp::noex_init<T, Us&&> && ...))
        : m_storage { { rstd::forward<Us>(values)... } } {}

    constexpr array(const array&)                    = default;
    constexpr array(array&&)                         = default;
    constexpr auto operator=(const array&) -> array& = default;
    constexpr auto operator=(array&&) -> array&      = default;

    constexpr auto len() const noexcept -> usize { return N; }
    constexpr auto is_empty() const noexcept -> bool { return N == 0; }

    constexpr auto data() noexcept -> T* {
        if constexpr (N == 0) {
            return nullptr;
        } else {
            return m_storage.values;
        }
    }

    constexpr auto data() const noexcept -> const T* {
        if constexpr (N == 0) {
            return nullptr;
        } else {
            return m_storage.values;
        }
    }

    constexpr auto begin() noexcept -> T* { return data(); }
    constexpr auto begin() const noexcept -> const T* { return data(); }

    constexpr auto end() noexcept -> T* {
        if constexpr (N == 0) return data();
        return data() + N;
    }

    constexpr auto end() const noexcept -> const T* {
        if constexpr (N == 0) return data();
        return data() + N;
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> { return ptr<T>::from_raw_parts(data()); }
    constexpr auto as_mut_ptr() noexcept -> mut_ptr<T> {
        return mut_ptr<T>::from_raw_parts(data());
    }

    constexpr auto as_slice() const noexcept -> slice<T> {
        return slice<T>::from_raw_parts(data(), N);
    }

    constexpr auto as_mut_slice() noexcept -> mut_ref<T[]> {
        return mut_ref<T[]>::from_raw_parts(data(), N);
    }

    constexpr auto deref() const noexcept -> ref<Target> { return as_slice(); }
    constexpr auto deref_mut() noexcept -> mut_ref<Target> { return as_mut_slice(); }

    constexpr auto at(usize index) -> T& {
        if (index >= N) rstd::panic { "array index out of bounds" };
        return data()[index];
    }

    constexpr auto at(usize index) const -> const T& {
        if (index >= N) rstd::panic { "array index out of bounds" };
        return data()[index];
    }

    constexpr auto operator[](usize index) -> T& { return at(index); }
    constexpr auto operator[](usize index) const -> const T& { return at(index); }

    constexpr auto get(usize index) const noexcept -> Option<ref<T>> {
        if (index >= N) return None();
        return Some(ref<T>::from_raw_parts(data() + index));
    }

    constexpr auto get_mut(usize index) noexcept -> Option<mut_ref<T>> {
        if (index >= N) return None();
        return Some(mut_ref<T>::from_raw_parts(data() + index));
    }

    constexpr auto first() const noexcept -> Option<ref<T>> { return get(0); }
    constexpr auto first_mut() noexcept -> Option<mut_ref<T>> { return get_mut(0); }

    constexpr auto last() const noexcept -> Option<ref<T>> {
        if constexpr (N == 0) {
            return None();
        } else {
            return Some(ref<T>::from_raw_parts(data() + N - 1));
        }
    }

    constexpr auto last_mut() noexcept -> Option<mut_ref<T>> {
        if constexpr (N == 0) {
            return None();
        } else {
            return Some(mut_ref<T>::from_raw_parts(data() + N - 1));
        }
    }

    template<usize I>
    constexpr auto get() & noexcept -> T& {
        return element_unchecked<I>();
    }

    template<usize I>
    constexpr auto get() const& noexcept -> const T& {
        return element_unchecked<I>();
    }

    template<usize I>
    constexpr auto get() && noexcept -> T&& {
        return rstd::move(element_unchecked<I>());
    }

    constexpr auto iter() const -> iter::SliceIter<T> { return { begin(), end() }; }
    constexpr auto iter_mut() -> iter::SliceIterMut<T> { return { begin(), end() }; }

    auto into_iter() -> IntoIter;

    constexpr auto clone() const -> array
        requires Impled<T, clone::Clone>
    {
        return clone_impl(mtp::make_index_sequence<N> {});
    }

    constexpr void clone_from(array& source)
        requires Impled<T, clone::Clone> && mtp::assign_move<T>
    {
        *this = source.clone();
    }

    constexpr auto each_ref() const -> array<ref<T>, N> {
        return each_ref_impl(mtp::make_index_sequence<N> {});
    }

    constexpr auto each_mut() -> array<mut_ref<T>, N> {
        return each_mut_impl(mtp::make_index_sequence<N> {});
    }

    template<typename F>
    constexpr auto map(F function) && {
        return map_impl(function, mtp::make_index_sequence<N> {});
    }

    template<typename F>
        requires mtp::init<T, decltype(mtp::declval<F&>()(usize {}))>
    static constexpr auto from_fn(F function) -> array {
        return from_fn_impl(function, mtp::make_index_sequence<N> {});
    }

    static constexpr auto repeat(const T& value) -> array
        requires Impled<T, clone::Clone>
    {
        return repeat_impl(value, mtp::make_index_sequence<N> {});
    }
};

/// An owning iterator over an `array<T, N>`.
export template<typename T, usize N>
class ArrayIntoIter : public DefaultInClass<ArrayIntoIter<T, N>, iter::Iterator> {
    array<T, N> m_values;
    usize       m_front { 0 };
    usize       m_back { N };

public:
    using Item = T;

    explicit constexpr ArrayIntoIter(array<T, N> values): m_values(rstd::move(values)) {}

    constexpr auto next() -> Option<Item> {
        if (m_front == m_back) return None();
        return Some(rstd::move(m_values[m_front++]));
    }

    constexpr auto next_back() -> Option<Item> {
        if (m_front == m_back) return None();
        --m_back;
        return Some(rstd::move(m_values[m_back]));
    }

    constexpr auto size_hint() const -> iter::SizeHint {
        auto remaining = len();
        return { remaining, Some(remaining) };
    }

    constexpr auto len() const noexcept -> usize { return m_back - m_front; }
};

template<typename T, usize N>
auto array<T, N>::into_iter() -> IntoIter {
    return IntoIter { rstd::move(*this) };
}

export template<usize I, typename T, usize N>
constexpr auto get(array<T, N>& values) noexcept -> T& {
    return values.template get<I>();
}

export template<usize I, typename T, usize N>
constexpr auto get(const array<T, N>& values) noexcept -> const T& {
    return values.template get<I>();
}

export template<usize I, typename T, usize N>
constexpr auto get(array<T, N>&& values) noexcept -> T&& {
    return rstd::move(values).template get<I>();
}

export namespace array_
{

template<usize N, typename F>
constexpr auto from_fn(F function) {
    using T = mtp::rm_cvf<decltype(function(usize {}))>;
    return array<T, N>::from_fn(rstd::move(function));
}

} // namespace array_

template<typename T, usize N>
struct Impl<iter::IntoIterator, array<T, N>> : ImplBase<array<T, N>> {
    auto into_iter() -> ArrayIntoIter<T, N> { return this->self().into_iter(); }
};

template<typename T, typename U, usize N>
    requires mtp::equalable<T, U>
struct Impl<cmp::PartialEq<array<U, N>>, array<T, N>>
    : DefaultInImpl<cmp::PartialEq<array<U, N>>, array<T, N>> {
    auto eq(const array<U, N>& other) const noexcept -> bool {
        for (usize i = 0; i < N; ++i) {
            if (! (this->self()[i] == other[i])) return false;
        }
        return true;
    }
};

template<typename T, usize N>
struct Impl<convert::AsRef<T[]>, array<T, N>> : ImplBase<array<T, N>> {
    auto as_ref() const noexcept -> ref<T[]> { return this->self().as_slice(); }
};

template<typename T, usize N>
struct Impl<convert::AsMut<T[]>, array<T, N>> : ImplBase<array<T, N>> {
    auto as_mut() noexcept -> mut_ref<T[]> { return this->self().as_mut_slice(); }
};

} // namespace rstd

namespace std
{

template<typename T, ::rstd::usize N>
struct tuple_size<::rstd::array<T, N>> {
    static constexpr ::rstd::usize value = N;
};

template<::rstd::usize I, typename T, ::rstd::usize N>
struct tuple_element<I, ::rstd::array<T, N>> {
    static_assert(I < N, "array index out of bounds");
    using type = T;
};

} // namespace std
