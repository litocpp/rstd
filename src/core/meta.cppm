module;
#include <type_traits>
#include <concepts>
export module rstd.core:meta;

namespace rstd::meta
{

export template<typename T, T v>
using integral_constant = std::integral_constant<T, v>;

export template<bool v>
using bool_constant = integral_constant<bool, v>;

export using true_type = bool_constant<true>;

export using false_type = bool_constant<false>;

export template<bool Cond, typename T = void>
using enable_if = std::enable_if<Cond, T>;

export template<bool Cond, typename T = void>
using enable_if_t = typename enable_if<Cond, T>::type;

export template<bool Cond, typename If, typename Else>
using conditional_t = std::conditional_t<Cond, If, Else>;

export template<typename... Bn>
inline constexpr bool conjunction_v = std::conjunction<Bn...>::value;

export template<typename... Bn>
inline constexpr bool disjunction_v = std::disjunction<Bn...>::value;

export template<typename P>
inline constexpr bool negation_v = std::negation<P>::value;

export template<typename T, typename U>
concept same_as = std::is_same_v<T, U>;

export template<class T>
using is_union = std::is_union<T>;
export template<typename T>
inline constexpr bool is_union_v = is_union<T>::value;

export template<class T>
using is_class = std::is_class<T>;
export template<typename T>
inline constexpr bool is_class_v = is_class<T>::value;

export template<typename Base, typename Derived>
using is_base_of = std::is_base_of<Base, Derived>;

export template<typename Base, typename Derived>
inline constexpr bool is_base_of_v = is_base_of<Base, Derived>::value;

export template<class T, template<class...> class Primary>
struct is_specialization_of : false_type {};

export template<template<class...> class Primary, class... Args>
struct is_specialization_of<Primary<Args...>, Primary> : true_type {};

export template<class T, template<class...> class Primary>
inline constexpr bool is_specialization_of_v = is_specialization_of<T, Primary>::value;

export template<class T, template<class...> class Primary>
concept special_of = is_specialization_of<T, Primary>::value;

export template<typename T>
using is_integral = std::is_integral<T>;
export template<typename T>
inline constexpr bool is_integral_v = std::is_integral_v<T>;

export template<typename T>
using is_floating_point = std::is_floating_point<T>;
export template<typename T>
inline constexpr bool is_floating_point_v = std::is_floating_point_v<T>;

export template<typename T>
using is_arithmetic = std::is_arithmetic<T>;
export template<typename T>
inline constexpr bool is_arithmetic_v = std::is_arithmetic_v<T>;

export template<typename T>
using is_reference = std::is_reference<T>;
export template<typename T>
inline constexpr bool is_reference_v = std::is_reference_v<T>;

export template<class T>
using is_lvalue_reference = std::is_lvalue_reference<T>;
export template<class T>
inline constexpr bool is_lvalue_reference_v = std::is_lvalue_reference_v<T>;
export template<typename T>
using is_rvalue_reference = std::is_rvalue_reference<T>;
export template<typename T>
inline constexpr bool is_rvalue_reference_v = std::is_rvalue_reference_v<T>;

export template<typename T>
using is_pointer = std::is_pointer<T>;
export template<typename T>
inline constexpr bool is_pointer_v = std::is_pointer_v<T>;

export template<class T>
using add_const = std::add_const<T>;
export template<class T>
using add_const_t = std::add_const<T>::type;

export template<class T>
using add_pointer = std::add_pointer<T>;
export template<class T>
using add_pointer_t = std::add_pointer<T>::type;

export template<class T>
using add_lvalue_reference = std::add_lvalue_reference<T>;
export template<class T>
using add_lvalue_reference_t = std::add_lvalue_reference_t<T>;
export template<class T>
using add_rvalue_reference = std::add_rvalue_reference<T>;
export template<class T>
using add_rvalue_reference_t = std::add_rvalue_reference_t<T>;

export template<class T>
using remove_reference = std::remove_reference<T>;
export template<class T>
using remove_reference_t = std::remove_reference<T>::type;

export template<class T>
using remove_cv = std::remove_cv<T>;
export template<class T>
using remove_cv_t = std::remove_cv<T>::type;

export template<class T>
using is_const = std::is_const<T>;
export template<class T>
inline constexpr bool is_const_v = std::is_const_v<T>;

export template<class T>
concept nothrow_destructible = std::is_nothrow_destructible_v<T>;

export template<typename T, typename... Args>
inline constexpr bool is_constructible_v = std::is_constructible_v<T, Args...>;

export template<typename T>
inline constexpr bool is_default_constructible_v = std::is_default_constructible_v<T>;
export template<typename T, typename... Args>
concept constructible_from = nothrow_destructible<T> && is_constructible_v<T, Args...>;

export template<typename T, typename... Args>
concept nothrow_constructible = std::is_nothrow_constructible_v<T, Args...>;

export template<typename T, typename... Args>
inline constexpr bool is_nothrow_constructible_v = std::is_nothrow_constructible_v<T, Args...>;

// copy
export template<class T>
using is_copy_constructible = std::is_copy_constructible<T>;
export template<class T>
using is_trivially_copy_constructible = std::is_trivially_copy_constructible<T>;
export template<class T>
using is_nothrow_copy_constructible = std::is_nothrow_copy_constructible<T>;
export template<class T>
inline constexpr bool is_copy_constructible_v = std::is_copy_constructible_v<T>;
export template<class T>
inline constexpr bool is_trivially_copy_constructible_v = std::is_trivially_copy_constructible_v<T>;
export template<class T>
inline constexpr bool is_nothrow_copy_constructible_v = std::is_nothrow_copy_constructible_v<T>;

export template<typename T>
concept nothrow_copy_constructible = is_nothrow_copy_constructible_v<T>;

export template<typename T>
concept custom_copy_constructible =
    (is_copy_constructible_v<T> && ! is_trivially_copy_constructible_v<T>);

// move
export template<class T>
using is_move_constructible = std::is_move_constructible<T>;
export template<class T>
using is_trivially_move_constructible = std::is_trivially_move_constructible<T>;
export template<class T>
using is_nothrow_move_constructible = std::is_nothrow_move_constructible<T>;
export template<class T>
inline constexpr bool is_move_constructible_v = std::is_move_constructible_v<T>;
export template<class T>
inline constexpr bool is_trivially_move_constructible_v = std::is_trivially_move_constructible_v<T>;
export template<class T>
inline constexpr bool is_nothrow_move_constructible_v = std::is_nothrow_move_constructible_v<T>;

export template<typename T>
concept nothrow_move_constructible = is_nothrow_move_constructible_v<T>;
export template<typename T>
concept custom_move_constructible =
    (is_move_constructible_v<T> && ! is_trivially_move_constructible_v<T>);

export template<class T>
using is_nothrow_copy_assignable = std::is_nothrow_copy_assignable<T>;
export template<class T>
inline constexpr bool is_nothrow_copy_assignable_v = std::is_nothrow_copy_assignable_v<T>;
export template<class T>
using is_trivially_copy_assignable = std::is_trivially_copy_assignable<T>;
export template<class T>
inline constexpr bool is_trivially_copy_assignable_v = std::is_trivially_copy_assignable_v<T>;
export template<class T>
using is_copy_assignable = std::is_copy_assignable<T>;
export template<class T>
inline constexpr bool is_copy_assignable_v = std::is_copy_assignable_v<T>;

export template<typename T>
concept nothrow_copy_assignable = is_nothrow_copy_assignable_v<T>;
export template<typename T>
concept custom_copy_assignable = (is_copy_assignable_v<T> && ! is_trivially_copy_assignable_v<T>);

export template<class T>
using is_nothrow_move_assignable = std::is_nothrow_move_assignable<T>;
export template<class T>
inline constexpr bool is_nothrow_move_assignable_v = std::is_nothrow_move_assignable_v<T>;
export template<class T>
using is_trivially_move_assignable = std::is_trivially_move_assignable<T>;
export template<class T>
inline constexpr bool is_trivially_move_assignable_v = std::is_trivially_move_assignable_v<T>;
export template<class T>
using is_move_assignable = std::is_move_assignable<T>;
export template<class T>
inline constexpr bool is_move_assignable_v = std::is_move_assignable_v<T>;

export template<typename T>
concept nothrow_move_assignable = is_nothrow_move_assignable_v<T>;
export template<typename T>
concept custom_move_assignable = (is_move_assignable_v<T> && ! is_trivially_move_assignable_v<T>);

export template<class T>
concept trivially_value = is_trivially_copy_constructible_v<T> && ! is_reference_v<T>;

export template<class T>
using remove_cvref = std::remove_cvref<T>;
export template<class T>
using remove_cvref_t = std::remove_cvref_t<T>;

export template<class T>
using remove_const_t = std::remove_const_t<T>;

export template<class T>
using is_void = std::is_void<T>;
export template<class T>
inline constexpr bool is_void_v = std::is_void_v<T>;

export template<class From, class To>
concept convertible_to =
    std::is_convertible_v<From, To> && requires { static_cast<To>(std::declval<From>()); };

export template<class F, class... ArgTypes>
using invoke_result = std::invoke_result<F, ArgTypes...>;

export template<class F, class... ArgTypes>
using invoke_result_t = std::invoke_result_t<F, ArgTypes...>;

export template<class T>
struct type_identity {
    using type = T;
};
export template<class T>
using type_identity_t = type_identity<T>::type;

export template<typename T>
concept semiregular = std::semiregular<T>;

export template<typename T>
inline constexpr bool is_trivially_destructible_v = std::is_trivially_destructible_v<T>;
export template<typename T>
inline constexpr bool is_nothrow_default_constructible_v =
    std::is_nothrow_default_constructible_v<T>;
;

} // namespace rstd::meta