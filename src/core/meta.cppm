export module rstd.core:meta;
export import :core;

export namespace rstd::meta
{

// custom
template<typename T, typename... Args>
concept nothrow_constructible = is_nothrow_constructible_v<T, Args...>;
template<typename T>
concept nothrow_destructible = is_nothrow_destructible_v<T>;
template<typename T>
concept nothrow_copy_constructible = is_nothrow_copy_constructible_v<T>;
template<typename T>
concept custom_copy_constructible =
    (is_copy_constructible_v<T> && ! is_trivially_copy_constructible_v<T>);

template<typename T>
concept nothrow_move_constructible = is_nothrow_move_constructible_v<T>;
template<typename T>
concept custom_move_constructible =
    (is_move_constructible_v<T> && ! is_trivially_move_constructible_v<T>);

template<typename T>
concept nothrow_copy_assignable = is_nothrow_copy_assignable_v<T>;
template<typename T>
concept custom_copy_assignable = (is_copy_assignable_v<T> && ! is_trivially_copy_assignable_v<T>);

template<typename T>
concept nothrow_move_assignable = is_nothrow_move_assignable_v<T>;
template<typename T>
concept custom_move_assignable = (is_move_assignable_v<T> && ! is_trivially_move_assignable_v<T>);

template<typename T>
concept trivially_value = is_trivially_copy_constructible_v<T> && ! is_reference_v<T>;

template<typename From, typename To>
concept convertible_to =
    is_convertible_v<From, To> && requires { static_cast<To>(rstd::declval<From>()); };

template<typename S, typename T>
concept transparent =
    sizeof(S) == sizeof(T) && alignof(S) == alignof(T) && meta::is_standard_layout_v<S>;

template<typename>
struct is_tuple : false_type {};
template<typename... T>
struct is_tuple<cppstd::tuple<T...>> : true_type {};

template<typename T>
concept is_tuple_v = is_tuple<T>::value;

template<class T, template<class...> class Primary>
struct is_specialization_of : false_type {};

template<template<class...> class Primary, class... Args>
struct is_specialization_of<Primary<Args...>, Primary> : true_type {};

template<class T, template<class...> class Primary>
inline constexpr bool is_specialization_of_v = is_specialization_of<T, Primary>::value;

template<class T, template<class...> class Primary>
concept special_of = is_specialization_of<T, Primary>::value;

template<typename T>
struct FuncTraits;

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (*)(T, Args...) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = meta::conditional_t<meta::is_pointer_v<T>, void, T>;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename Ret, bool Ne>
struct FuncTraits<Ret (*)(void) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = void;

    using to_dyn = Ret (*)(void) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

} // namespace rstd::meta

namespace rstd::meta
{

template<usize I, auto First, auto... Rest>
consteval auto get_auto_impl() {
    if constexpr (I == 0) {
        return First;
    } else if constexpr (sizeof...(Rest) == 0) {
        static_assert(false, "out of range");
    } else {
        return get_auto_impl<I - 1, Rest...>();
    }
}

export template<usize I, auto... Vals>
consteval auto get_auto() {
    static_assert(I < sizeof...(Vals), "out of range");
    return get_auto_impl<I, Vals...>();
}

} // namespace rstd::meta