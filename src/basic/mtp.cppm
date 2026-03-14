export module rstd.basic:mtp;
export import :prelude;

#if ! __has_builtin(__is_nothrow_destructible)
export import :cppstd;
#endif

/// @cond undocumented

template<class T>
struct type_identity {
    using type = T;
};

template<typename T>
struct declval_protector {
    static const bool __stop = false;
};

template<typename T>
auto return_nop() -> T;

template<class T> // Note that “cv void&” is a substitution failure
auto try_add_lvalue_reference(int) -> type_identity<T&>;
template<class T> // Handle T = cv void case
auto try_add_lvalue_reference(...) -> type_identity<T>;

template<class T>
auto try_add_rvalue_reference(int) -> type_identity<T&&>;
template<class T>
auto try_add_rvalue_reference(...) -> type_identity<T>;

template<class T>
struct add_ref : decltype(try_add_lvalue_reference<T>(0)) {};

template<class T>
struct add_ref_rv : decltype(try_add_rvalue_reference<T>(0)) {};

template<bool B, class T, class F>
struct conditional {
    using type = T;
};

template<class T, class F>
struct conditional<false, T, F> {
    using type = F;
};

template<typename T>
constexpr bool eq(type_identity<T>, type_identity<T>) {
    return true;
}
template<typename T, typename U>
constexpr bool eq(type_identity<T>, type_identity<U>) {
    return false;
}

template<class T, T v>
struct integral_constant {
    using value_type = T;
    constexpr value_type operator()() const noexcept { return v; }
};
struct true_type : integral_constant<bool, true> {};
struct false_type : integral_constant<bool, false> {};

template<typename>
struct is_lvalue_reference : false_type {};

template<typename _Tp>
struct is_lvalue_reference<_Tp&> : true_type {};

/// is_rvalue_reference
template<typename>
struct is_rvalue_reference : false_type {};

template<typename _Tp>
struct is_rvalue_reference<_Tp&&> : true_type {};

template<class T, template<class...> class Primary>
struct spec_of : false_type {};

template<template<class...> class Primary, class... Args>
struct spec_of<Primary<Args...>, Primary> : true_type {};

template<typename T>
struct add_const {
    using type = T const;
};
template<typename T>
struct rm_const {
    using type = T;
};
template<typename T>
struct rm_const<T const> {
    using type = T;
};

template<typename T>
struct rm_ref {
    using type = T;
};

template<typename T>
struct rm_ref<T&> {
    using type = T;
};

template<typename T>
struct rm_ref<T&&> {
    using type = T;
};

/// @endcond

export namespace rstd::mtp
{
template<typename T>
inline constexpr type_identity<T> type_c {};

template<typename T>
concept complete = requires { sizeof(T); };

/// \name type modifier
/// @{
template<typename T>
using decay = __decay(T);
template<typename T>
using add_ptr = __add_pointer(T);
template<typename T>
using add_ref = ::add_ref<T>::type;
template<typename T>
using add_ref_rv = ::add_ref_rv<T>::type;
template<typename T>
using add_const = ::add_const<T>::type;
template<typename T>
using rm_const = ::rm_const<T>::type;
template<typename T>
using rm_cv = __remove_cv(T);
template<typename T>
using rm_ptr = __remove_pointer(T);
template<typename T>
using rm_ref = ::rm_ref<T>::type;
template<typename T>
using rm_cvf = __remove_cvref(T);
template<typename T>
using rm_ext = __remove_extent(T);
/// @}

/// Utility to simplify expressions used in unevaluated operands
template<typename T>
auto declval() noexcept -> add_ref_rv<T> {
    static_assert(declval_protector<T>::__stop, "declval() must not be used!");
    return return_nop<add_ref_rv<T>>();
}

[[gnu::always_inline]]
constexpr inline bool is_constant_evaluated() noexcept {
#if __cpp_if_consteval >= 202106L
    if consteval {
        return true;
    } else {
        return false;
    }
#elif __cplusplus >= 201103L && __has_builtin(__builtin_is_constant_evaluated)
    return __builtin_is_constant_evaluated();
#else
    return false;
#endif
}

// concepts

template<typename T, typename U>
concept same_as = __is_same(T, U);

template<typename T, typename... Rest>
concept same = (__is_same(T, Rest) && ...);
template<typename T, typename... Rest>
concept any = (__is_same(T, Rest) || ...);

template<typename From, typename To>
concept convertible_to =
    __is_convertible(From, To) && requires { static_cast<To>(mtp::declval<From>()); };

template<typename T, typename U>
concept equalable = requires(const T& a, const U& b) {
    { a == b } -> convertible_to<bool>;
    { a != b } -> convertible_to<bool>;
};

template<typename S, typename T>
concept transparent = sizeof(S) == sizeof(T) && alignof(S) == alignof(T) && __is_standard_layout(S);

template<typename T, typename... Args>
concept init = __is_constructible(T, Args...);
template<typename T>
concept drop =
#if __has_builtin(__is_nothrow_destructible)
    __is_nothrow_destructible(T);
#else
    cppstd::is_nothrow_destructible_v<T>;
#endif

template<typename T>
concept copy = __is_constructible(T, add_ref<add_const<T>>);

template<typename T>
concept move = __is_constructible(T, add_ref_rv<T>);

template<typename T, typename U>
concept assign = __is_assignable(T, U);
template<typename T>
concept assign_copy = assign<add_ref<T>, add_ref<add_const<T>>>;
template<typename T>
concept assign_move = assign<add_ref<T>, add_ref_rv<T>>;

/// \name Trivial
/// @{

/// Is trivial
template<typename T>
concept triv = __is_trivial(T);

/// Is trivially constructible
template<typename T, typename... Args>
concept triv_init = __is_trivially_constructible(T, Args...);

/// Is trivially destructible
template<typename T>
concept triv_drop =
#if __has_builtin(__is_trivially_destructible)
    __is_trivially_destructible(T);
#else
    __has_trivial_destructor(T);
#endif

/// Is trivially copyable
template<typename T>
concept triv_copy = triv_init<T, add_ref<add_const<T>>>;
//__is_trivially_copyable(T);

/// Is trivially moveable
template<typename T>
concept triv_move = triv_init<T, add_ref_rv<T>>;
/// Is trivially assignable
template<typename T, typename U>
concept triv_assign = __is_trivially_assignable(T, U);

template<typename T>
concept triv_assign_copy = triv_assign<add_ref<T>, add_ref<add_const<T>>>;
template<typename T>
concept triv_assign_move = triv_assign<add_ref<T>, add_ref_rv<T>>;
/// @}

/// \name NoExp
/// @{

/// Is nothrow constructible
template<typename T, typename... Args>
concept noex_init = __is_nothrow_constructible(T, Args...);

/// Is nothrow assignable
template<typename T, typename U>
concept noex_assign = __is_nothrow_assignable(T, U);

/// Is nothrow copy construcible
template<typename T>
concept noex_copy = noex_init<T, add_ref<add_const<T>>>;

/// Is nothrow move construcible
template<typename T>
concept noex_move = noex_init<T, add_ref_rv<T>>;

/// Is nothrow copy assignable
template<typename T>
concept noex_assign_copy = noex_assign<add_ref<T>, add_ref<add_const<T>>>;
/// Is nothrow move assignable
template<typename T>
concept noex_assign_move = noex_assign<add_ref<T>, add_ref_rv<T>>;

/// Is no throw destructible
template<typename T>
concept noex_drop =
#if __has_builtin(__is_nothrow_destructible)
    __is_nothrow_destructible(T);
#else
    cppstd::is_nothrow_destructible_v<T>;
#endif
/// @}

/// \name User
/// @{
/// User copy construcible
template<typename T>
concept user_copy = init<T, T const&> && (! triv_copy<T>);

template<typename T>
concept user_move = init<T, T&&> && (! triv_move<T>);

template<typename T>
concept user_assign_copy = assign<T, T const&> && (! triv_assign_copy<T>);
template<typename T>
concept user_assign_move = assign<T, T&&> && (! triv_assign_move<T>);

/// @}

/// \name Logic
/// @{
/// Is trivially assignable
template<bool Cond, typename Iftrue, typename Iffalse>
using cond = ::conditional<Cond, Iftrue, Iffalse>::type;
/// @}

/// \name Is
/// @{
template<typename T>
concept is_void = same<rm_cvf<T>, void>;
// Note: this implementation uses C++20 facilities
template<typename T>
concept is_int = requires(T t, T* p, void (*f)(T)) // T* parameter excludes reference types
{
    reinterpret_cast<T>(t); // Exclude class types
    f(0);                   // Exclude enumeration types
    p + t;                  // Exclude everything not yet excluded but integral types
};
template<typename T>
concept is_float = any<rm_cvf<T>, float, double, long double>;
template<typename T>
concept is_arithmetic = is_int<T> || is_float<T>;

template<typename T>
concept is_const = __is_const(T);
template<typename T>
concept is_ptr = __is_pointer(T);
template<typename T>
concept is_ref = __is_reference(T);
template<typename T>
concept is_ref_lv = ::is_lvalue_reference<T> {}();
template<typename T>
concept is_ref_rv = ::is_rvalue_reference<T> {}();
template<typename T>
concept is_enum = __is_enum(T);
template<typename T>
concept is_union = __is_union(T);
template<typename T>
concept is_class = __is_class(T);
template<typename T>
concept is_array = __is_array(T);
template<typename T>
concept is_array_dst = __is_unbounded_array(T);
template<typename T>
concept is_tuple = requires { rstd::tuple_size<T>(); };
template<typename T>
concept is_aggregate = __is_aggregate(rm_cv<T>);
template<typename Base, typename Derived>
concept is_base_of = __is_base_of(Base, Derived);
/// @}

/// \name Access
/// @{
template<typename T>
    requires is_enum<T>
using underlying = __underlying_type(T);

template<typename T>
constexpr auto tuple_size = rstd::tuple_size<T>::value;
template<usize I, typename T>
using tuple_element = rstd::tuple_element<I, T>::type;
/// @}

template<class T, template<class...> class Primary>
concept spec_of = ::spec_of<T, Primary> {}();

template<typename T>
using void_empty_t = cond<is_void<T>, empty, T>;

template<typename T, typename U>
using follow_const_t =
    mtp::cond<mtp::is_const<T>, mtp::add_const<mtp::rm_const<U>>, mtp::rm_const<U>>;

/// \name traits
/// @{

template<typename T>
struct func_traits {
    static_assert(false);
};
/// @}

} // namespace rstd::mtp

namespace rstd::mtp
{

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (*)(T, Args...) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = mtp::cond<mtp::is_ptr<T>, void, T>;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename Ret, bool Ne>
struct func_traits<Ret (*)(void) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = void;

    using to_dyn = Ret (*)(void) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) const noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) const & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct func_traits<Ret (T::*)(Args...) const && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

} // namespace rstd::mtp

namespace rstd::mtp
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

} // namespace rstd::mtp
