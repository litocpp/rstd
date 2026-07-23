module;
#include <rstd/macro.hpp>

export module rstd.core:array;
import :num.types;
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

export template<typename T, rstd::size_t N>
class array;

export template<typename T, rstd::size_t N>
class ArrayIntoIter;

namespace array_detail
{

template<typename T, rstd::size_t N>
struct Storage {
    typename mut_ptr<T>::storage_type values[N];
};

template<typename T>
struct Storage<T, 0> {};

} // namespace array_detail

/// An owned fixed-size sequence, analogous to Rust's `[T; N]`.
export template<typename T, rstd::size_t N>
class array {
    array_detail::Storage<T, N> m_storage;

    template<rstd::size_t I>
    constexpr decltype(auto) element_unchecked() noexcept {
        static_assert(I < N, "array index out of bounds");
        return mut_ptr<T>::from_raw_parts(data() + I).get();
    }

    template<rstd::size_t I>
    constexpr decltype(auto) element_unchecked() const noexcept {
        static_assert(I < N, "array index out of bounds");
        return ptr<T>::from_raw_parts(data() + I).get();
    }

    template<rstd::size_t... Is>
    constexpr auto clone_impl(mtp::index_sequence<Is...>) const -> array {
        return array { rstd::as<clone::Clone>(element_unchecked<Is>()).clone()... };
    }

    template<rstd::size_t... Is>
    constexpr auto each_ref_impl(mtp::index_sequence<Is...>) const -> array<ref<T>, N> {
        return array<ref<T>, N> { as_ptr().add(usize(Is)).as_ref()... };
    }

    template<rstd::size_t... Is>
    constexpr auto each_mut_impl(mtp::index_sequence<Is...>) -> array<mut_ref<T>, N> {
        return array<mut_ref<T>, N> { as_mut_ptr().add(usize(Is)).as_mut_ref()... };
    }

    template<typename F, rstd::size_t... Is>
    constexpr auto map_impl(F& function, mtp::index_sequence<Is...>) {
        using U = mtp::rm_cvf<decltype(function(mtp::declval<T>()))>;
        return array<U, N> { function(rstd::move(element_unchecked<Is>()))... };
    }

    template<typename F, rstd::size_t... Is>
    static constexpr auto from_fn_impl(F& function, mtp::index_sequence<Is...>) -> array {
        return array { function(usize(Is))... };
    }

    template<rstd::size_t... Is>
    static constexpr auto repeat_impl(const T& value, mtp::index_sequence<Is...>) -> array {
        return array { ((void)Is, rstd::as<clone::Clone>(value).clone())... };
    }

public:
    USE_TRAIT(array)

    using value_type = T;
    using Target     = T[];
    using IntoIter   = ArrayIntoIter<T, N>;

    static constexpr rstd::size_t LENGTH = N;

    constexpr array() = default;

    template<typename... Us>
        requires(N > 0 && ! mtp::same_as<T, u8> && sizeof...(Us) == N &&
                 (mtp::init<T, Us &&> && ...))
    constexpr array(Us&&... values) noexcept((mtp::noex_init<T, Us&&> && ...))
        : m_storage { { rstd::forward<Us>(values)... } } {}

    template<typename... Us>
        requires(N > 0 && mtp::same_as<T, u8> && sizeof...(Us) == N &&
                 (mtp::init<T, Us &&> && ...))
    constexpr array(Us&&... values) noexcept((mtp::noex_init<T, Us&&> && ...))
        : m_storage { { u8(rstd::forward<Us>(values)).to_byte()... } } {}

    constexpr array(const array&)                    = default;
    constexpr array(array&&)                         = default;
    constexpr auto operator=(const array&) -> array& = default;
    constexpr auto operator=(array&&) -> array&      = default;

    constexpr auto len() const noexcept -> usize { return usize(N); }
    constexpr auto is_empty() const noexcept -> bool { return N == 0; }

    constexpr auto data() noexcept [[clang::lifetimebound]]
        -> typename mut_ptr<T>::value_type* {
        if constexpr (N == 0) {
            return nullptr;
        } else {
            return m_storage.values;
        }
    }

    constexpr auto data() const noexcept [[clang::lifetimebound]] -> typename ptr<T>::value_type* {
        if constexpr (N == 0) {
            return nullptr;
        } else {
            return m_storage.values;
        }
    }

    constexpr auto begin() noexcept [[clang::lifetimebound]] -> mut_ptr<T> { return as_mut_ptr(); }
    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<T> { return as_ptr(); }

    constexpr auto end() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return as_mut_ptr().add(usize(N));
    }

    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return as_ptr().add(usize(N));
    }

    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return ptr<T>::from_raw_parts(data());
    }
    constexpr auto as_mut_ptr() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return mut_ptr<T>::from_raw_parts(data());
    }

    constexpr auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<T> {
        return slice<T>::from_raw_parts(data(), usize(N));
    }

    constexpr auto as_mut_slice() noexcept [[clang::lifetimebound]] -> mut_ref<T[]> {
        return mut_ref<T[]>::from_raw_parts(data(), usize(N));
    }

    constexpr auto deref() const noexcept [[clang::lifetimebound]] -> ref<Target> {
        return as_slice();
    }
    constexpr auto deref_mut() noexcept [[clang::lifetimebound]] -> mut_ref<Target> {
        return as_mut_slice();
    }

    constexpr decltype(auto) at(usize index) [[clang::lifetimebound]] {
        if (index.to_primitive() >= N) rstd::panic { "array index out of bounds" };
        return as_mut_ptr().add(index).get();
    }

    constexpr decltype(auto) at(usize index) const [[clang::lifetimebound]] {
        if (index.to_primitive() >= N) rstd::panic { "array index out of bounds" };
        return as_ptr().add(index).get();
    }

    constexpr decltype(auto) operator[](usize index) [[clang::lifetimebound]] {
        return at(index);
    }
    constexpr decltype(auto) operator[](usize index) const [[clang::lifetimebound]] {
        return at(index);
    }

    constexpr auto get(usize index) const noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        if (index.to_primitive() >= N) return None();
        return Some(as_ptr().add(index).as_ref());
    }

    constexpr auto get_mut(usize index) noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        if (index.to_primitive() >= N) return None();
        return Some(as_mut_ptr().add(index).as_mut_ref());
    }

    constexpr auto first() const noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        return get(usize());
    }
    constexpr auto first_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        return get_mut(usize());
    }

    constexpr auto last() const noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        if constexpr (N == 0) {
            return None();
        } else {
            return Some(as_ptr().add(usize(N - 1)).as_ref());
        }
    }

    constexpr auto last_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        if constexpr (N == 0) {
            return None();
        } else {
            return Some(as_mut_ptr().add(usize(N - 1)).as_mut_ref());
        }
    }

    template<rstd::size_t I>
    constexpr decltype(auto) get() & noexcept [[clang::lifetimebound]] {
        return element_unchecked<I>();
    }

    template<rstd::size_t I>
    constexpr decltype(auto) get() const& noexcept [[clang::lifetimebound]] {
        return element_unchecked<I>();
    }

    template<rstd::size_t I>
    constexpr decltype(auto) get() && noexcept [[clang::lifetimebound]] {
        if constexpr (mtp::same_as<T, u8>) {
            return u8(element_unchecked<I>());
        } else {
            return rstd::move(element_unchecked<I>());
        }
    }

    constexpr auto iter() const [[clang::lifetimebound]] -> iter::SliceIter<T> {
        return { as_ptr(), as_ptr().add(usize(N)) };
    }
    constexpr auto iter_mut() [[clang::lifetimebound]] -> iter::SliceIterMut<T> {
        return { as_mut_ptr(), as_mut_ptr().add(usize(N)) };
    }

    auto into_iter() -> IntoIter;

    constexpr auto clone() const -> array
        requires Impled<T, clone::Clone>
    {
        return clone_impl(mtp::make_index_sequence<N> {});
    }

    constexpr void clone_from(const array& source)
        requires Impled<T, clone::Clone> && mtp::assign_move<T>
    {
        *this = source.clone();
    }

    constexpr auto each_ref() const [[clang::lifetimebound]] -> array<ref<T>, N> {
        return each_ref_impl(mtp::make_index_sequence<N> {});
    }

    constexpr auto each_mut() [[clang::lifetimebound]] -> array<mut_ref<T>, N> {
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
export template<typename T, rstd::size_t N>
class ArrayIntoIter : public DefaultInClass<ArrayIntoIter<T, N>, iter::Iterator> {
    array<T, N>  m_values;
    rstd::size_t m_front { 0 };
    rstd::size_t m_back { N };

public:
    using Item = T;

    explicit constexpr ArrayIntoIter(array<T, N> values): m_values(rstd::move(values)) {}

    constexpr auto next() -> Option<Item> {
        if (m_front == m_back) return None();
        T value = rstd::move(m_values[usize(m_front++)]);
        return Some(rstd::move(value));
    }

    constexpr auto next_back() -> Option<Item> {
        if (m_front == m_back) return None();
        --m_back;
        T value = rstd::move(m_values[usize(m_back)]);
        return Some(rstd::move(value));
    }

    constexpr auto size_hint() const -> iter::SizeHint {
        auto remaining = len();
        return { remaining, Some(remaining) };
    }

    constexpr auto len() const noexcept -> usize { return usize(m_back - m_front); }
};

template<typename T, rstd::size_t N>
auto array<T, N>::into_iter() -> IntoIter {
    return IntoIter { rstd::move(*this) };
}

export template<rstd::size_t I, typename T, rstd::size_t N>
constexpr decltype(auto) get(array<T, N>& values [[clang::lifetimebound]]) noexcept {
    return values.template get<I>();
}

export template<rstd::size_t I, typename T, rstd::size_t N>
constexpr decltype(auto) get(const array<T, N>& values [[clang::lifetimebound]]) noexcept {
    return values.template get<I>();
}

export template<rstd::size_t I, typename T, rstd::size_t N>
constexpr decltype(auto) get(array<T, N>&& values [[clang::lifetimebound]]) noexcept {
    return rstd::move(values).template get<I>();
}

export namespace array_
{

template<rstd::size_t N, typename F>
constexpr auto from_fn(F function) {
    using T = mtp::rm_cvf<decltype(function(usize {}))>;
    return array<T, N>::from_fn(rstd::move(function));
}

} // namespace array_

template<typename T, rstd::size_t N>
struct Impl<iter::IntoIterator, array<T, N>> : ImplBase<array<T, N>> {
    auto into_iter() -> ArrayIntoIter<T, N> { return this->self().into_iter(); }
};

template<typename T, typename U, rstd::size_t N>
    requires mtp::equalable<T, U>
struct Impl<cmp::PartialEq<array<U, N>>, array<T, N>>
    : DefaultInImpl<cmp::PartialEq<array<U, N>>, array<T, N>> {
    auto eq(const array<U, N>& other) const noexcept -> bool {
        for (rstd::size_t i = 0; i < N; ++i) {
            if (! (this->self()[usize(i)] == other[usize(i)])) return false;
        }
        return true;
    }
};

template<typename T, rstd::size_t N>
struct Impl<convert::AsRef<T[]>, array<T, N>> : ImplBase<array<T, N>> {
    auto as_ref() const noexcept -> ref<T[]> { return this->self().as_slice(); }
};

template<typename T, rstd::size_t N>
struct Impl<convert::AsMut<T[]>, array<T, N>> : ImplBase<array<T, N>> {
    auto as_mut() noexcept -> mut_ref<T[]> { return this->self().as_mut_slice(); }
};

} // namespace rstd

namespace std
{

template<typename T, rstd::size_t N>
struct tuple_size<::rstd::array<T, N>> {
    static constexpr rstd::size_t value = N;
};

template<rstd::size_t I, typename T, rstd::size_t N>
struct tuple_element<I, ::rstd::array<T, N>> {
    static_assert(I < N, "array index out of bounds");
    using type = T;
};

} // namespace std
