export module rstd.core:meta;
export import :std;

namespace rstd::meta
{

// custom
template<typename T, typename... Args>
concept nothrow_constructible = is_nothrow_constructible_v<T, Args...>;
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

template<class T>
concept trivially_value = is_trivially_copy_constructible_v<T> && ! is_reference_v<T>;

template<class From, class To>
concept convertible_to =
    is_convertible_v<From, To> && requires { static_cast<To>(rstd::declval<From>()); };

template<typename>
struct is_tuple : false_type {};
template<typename... T>
struct is_tuple<cppstd::tuple<T...>> : true_type {};

template<typename T>
concept is_tuple_v = is_tuple<T>::value;

export template<class T, template<class...> class Primary>
struct is_specialization_of : false_type {};

export template<template<class...> class Primary, class... Args>
struct is_specialization_of<Primary<Args...>, Primary> : true_type {};

export template<class T, template<class...> class Primary>
inline constexpr bool is_specialization_of_v = is_specialization_of<T, Primary>::value;

export template<class T, template<class...> class Primary>
concept special_of = is_specialization_of<T, Primary>::value;

} // namespace rstd::meta