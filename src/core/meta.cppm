module;
#include <type_traits>
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

} // namespace rstd::meta