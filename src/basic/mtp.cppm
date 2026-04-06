export module rstd.basic:mtp;
export import :prelude;
export import :mtp.std;

/// @cond undocumented

template<bool, typename T = void>
struct enable_if {};
template<typename T>
struct enable_if<true, T> {
    using type = T;
};
template<bool Cond, typename Tp = void>
using enable_if_t = typename enable_if<Cond, Tp>::type;

template<class T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type         = T;
    using type               = integral_constant;
    consteval value_type operator()() const noexcept { return v; }
};
template<bool v>
using bool_constant = integral_constant<bool, v>;
struct true_type : integral_constant<bool, true> {};
struct false_type : integral_constant<bool, false> {};

namespace rstd::mtp
{
export template<typename...>
using void_t = void;

#if __has_builtin(__is_const)
/// Satisfied when `T` is const-qualified.
export template<typename T>
concept is_const = __is_const(T);
#else
template<typename T>
struct is_const_t : false_type {};
template<typename T>
struct is_const_t<T const> : true_type {};

export template<typename T>
concept is_const = is_const_t<T> {}();
#endif

#if __has_builtin(__is_pointer)
/// Satisfied when `T` is a pointer type.
export template<typename T>
concept is_ptr = __is_pointer(T);
#else

template<typename T>
struct is_ptr_t : false_type {};
template<typename T>
struct is_ptr_t<T*> : true_type {};
template<typename T>
struct is_ptr_t<T* const> : true_type {};
template<typename T>
struct is_ptr_t<T* volatile> : true_type {};
template<typename T>
struct is_ptr_t<T* const volatile> : true_type {};

export template<typename T>
concept is_ptr = is_ptr_t<T> {}();
#endif

#if __has_builtin(__remove_extent)
/// Removes one array extent from `T`.
export template<typename T>
using rm_ext = __remove_extent(T);
#else
template<typename T>
struct remove_extent {
    using type = T;
};
template<typename T, usize Size>
struct remove_extent<T[Size]> {
    using type = T;
};
template<typename T>
struct remove_extent<T[]> {
    using type = T;
};
export template<typename T>
using rm_ext = remove_extent<T>::type;
#endif

} // namespace rstd::mtp

using rstd::mtp::is_const;
using rstd::mtp::is_ptr;
using rstd::mtp::void_t;

template<class T>
struct type_identity {
    using type = T;
};

template<typename T>
struct declval_protector {
    static const bool stop = false;
};

template<typename T>
auto return_nop() -> T;

template<typename T, typename...>
using first_t = T;

template<typename... Bn>
auto or_fn(int) -> first_t<false_type, enable_if_t<! bool(Bn::value)>...>;
template<typename... _Bn>
auto or_fn(...) -> true_type;
template<typename... _Bn>
auto and_fn(int) -> first_t<true_type, enable_if_t<bool(_Bn::value)>...>;
template<typename... _Bn>
auto and_fn(...) -> false_type;

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

template<typename>
struct is_lvalue_reference : false_type {};
template<typename T>
struct is_lvalue_reference<T&> : true_type {};
/// is_rvalue_reference
template<typename>
struct is_rvalue_reference : false_type {};
template<typename T>
struct is_rvalue_reference<T&&> : true_type {};
template<class T, template<class...> class Primary>

struct spec_of : false_type {};
template<template<class...> class Primary, class... Args>
struct spec_of<Primary<Args...>, Primary> : true_type {};

template<typename T, typename = void>
struct add_pointer_helper {
    using type = T;
};
template<typename T>
struct add_pointer_helper<T, void_t<T*>> {
    using type = T*;
};
template<typename T>
struct add_ptr : public add_pointer_helper<T> {};
template<typename T>
struct add_ptr<T&> {
    using type = T*;
};
template<typename T>
struct add_ptr<T&&> {
    using type = T*;
};

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

template<typename Tp>
struct rm_cv {
    using type = Tp;
};
template<typename Tp>
struct rm_cv<const Tp> {
    using type = Tp;
};
template<typename Tp>
struct rm_cv<volatile Tp> {
    using type = Tp;
};
template<typename Tp>
struct rm_cv<const volatile Tp> {
    using type = Tp;
};

template<typename Tp>
struct is_array_known_bounds : public false_type {};
template<typename Tp, rstd::usize Size>
struct is_array_known_bounds<Tp[Size]> : public true_type {};
template<typename T>
struct is_array_unknown_bounds : public false_type {};
template<typename T>
struct is_array_unknown_bounds<T[]> : public true_type {};

// Decay trait for arrays and functions, used for perfect forwarding
// in make_pair, make_tuple, etc.
template<typename _Up>
struct decay_selector : conditional<is_const<const _Up>, // false for functions
                                    rm_cv<_Up>,          // N.B. DR 705.
                                    add_ptr<_Up>>::type  // function decays to pointer
{};
template<typename Up, rstd::usize Nm>
struct decay_selector<Up[Nm]> {
    using type = Up*;
};
template<typename Up>
struct decay_selector<Up[]> {
    using type = Up*;
};
/// decay
template<typename T>
struct decay {
    using type = typename decay_selector<T>::type;
};
template<typename T>
struct decay<T&> {
    using type = typename decay_selector<T>::type;
};
template<typename T>
struct decay<T&&> {
    using type = typename decay_selector<T>::type;
};
/// @endcond

namespace rstd::mtp
{

/// Compile-time type tag for `T`.
export template<typename T>
inline constexpr type_identity<T> type_c {};

/// \name type modifier
/// @{
/// Applies array-to-pointer, function-to-pointer, and cv-removal transformations.
export template<typename T>
using decay =
#if __has_builtin(__is_nothrow_destructible)
    __decay(T);
#else
    ::decay<T>::type;
#endif
/// Adds a pointer indirection to `T`.
export template<typename T>
using add_ptr =
#if __has_builtin(__add_pointer)
    __add_pointer(T);
#else
    ::add_ptr<T>;
#endif
/// Adds an lvalue reference to `T`.
export template<typename T>
using add_ref = ::add_ref<T>::type;
/// Adds an rvalue reference to `T`.
export template<typename T>
using add_ref_rv = ::add_ref_rv<T>::type;
/// Adds `const` qualification to `T`.
export template<typename T>
using add_const = ::add_const<T>::type;
/// Removes `const` qualification from `T`.
export template<typename T>
using rm_const = ::rm_const<T>::type;
/// Removes top-level `const` and `volatile` qualifiers from `T`.
export template<typename T>
using rm_cv = __remove_cv(T);
/// Removes one level of pointer indirection from `T`.
export template<typename T>
using rm_ptr = __remove_pointer(T);
/// Removes reference qualification from `T`.
export template<typename T>
using rm_ref = ::rm_ref<T>::type;
/// Removes reference and cv qualifiers from `T`.
export template<typename T>
using rm_cvf = __remove_cvref(T);

/// @}

/// Utility to simplify expressions used in unevaluated operands
export template<typename T>
auto declval() noexcept -> add_ref_rv<T> {
    static_assert(declval_protector<T>::stop, "declval() must not be used!");
    return return_nop<add_ref_rv<T>>();
}

/// Detects whether the current invocation occurs within a constant-evaluated context.
export [[gnu::always_inline]]
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

/// Satisfied when `T` and `U` are the same type.
export template<typename T, typename U>
concept same_as = __is_same(T, U);

/// Satisfied when `T` is the same as all of `Rest...`.
export template<typename T, typename... Rest>
concept same = (__is_same(T, Rest) && ...);
/// Satisfied when `T` is the same as any of `Rest...`.
export template<typename T, typename... Rest>
concept any = (__is_same(T, Rest) || ...);

/// Satisfied when `From` is implicitly and explicitly convertible to `To`.
export template<typename From, typename To>
concept convertible_to =
    __is_convertible(From, To) && requires { static_cast<To>(mtp::declval<From>()); };

/// Satisfied when `T` and `U` can be compared with `==` and `!=`.
export template<typename T, typename U>
concept equalable = requires(const T& a, const U& b) {
    { a == b } -> convertible_to<bool>;
    { a != b } -> convertible_to<bool>;
};

/// Satisfied when `S` has the same size and alignment as `T` and is standard-layout.
export template<typename S, typename T>
concept transparent = sizeof(S) == sizeof(T) && alignof(S) == alignof(T) && __is_standard_layout(S);

/// Satisfied when `T` is constructible from `Args...`.
export template<typename T, typename... Args>
concept init = __is_constructible(T, Args...);
/// Satisfied when `T` is nothrow destructible.
export template<typename T>
concept drop =
#if __has_builtin(__is_nothrow_destructible)
    __is_nothrow_destructible(T);
#else
    noexcept(declval<T&>().~T());
#endif

/// Satisfied when `T` is copy constructible.
export template<typename T>
concept copy = __is_constructible(T, add_ref<add_const<T>>);

/// Satisfied when `T` is move constructible.
export template<typename T>
concept move = __is_constructible(T, add_ref_rv<T>);

/// Satisfied when `U` can be assigned to `T`.
export template<typename T, typename U>
concept assign = __is_assignable(T, U);
/// Satisfied when `T` is copy assignable.
export template<typename T>
concept assign_copy = assign<add_ref<T>, add_ref<add_const<T>>>;
/// Satisfied when `T` is move assignable.
export template<typename T>
concept assign_move = assign<add_ref<T>, add_ref_rv<T>>;

/// \name Trivial
/// @{

/// Is trivial
export template<typename T>
concept triv = __is_trivial(T);

/// Is trivially constructible
export template<typename T, typename... Args>
concept triv_init = __is_trivially_constructible(T, Args...);

/// Is trivially destructible
export template<typename T>
concept triv_drop =
#if __has_builtin(__is_trivially_destructible)
    __is_trivially_destructible(T);
#else
    __has_trivial_destructor(T);
#endif

/// Is trivially copyable
export template<typename T>
concept triv_copy = triv_init<T, add_ref<add_const<T>>>;
//__is_trivially_copyable(T);

/// Is trivially moveable
export template<typename T>
concept triv_move = triv_init<T, add_ref_rv<T>>;
/// Is trivially assignable
export template<typename T, typename U>
concept triv_assign = __is_trivially_assignable(T, U);

/// Is trivially copy assignable.
export template<typename T>
concept triv_assign_copy = triv_assign<add_ref<T>, add_ref<add_const<T>>>;
/// Is trivially move assignable.
export template<typename T>
concept triv_assign_move = triv_assign<add_ref<T>, add_ref_rv<T>>;
/// @}

/// \name NoExp
/// @{

/// Is nothrow constructible
export template<typename T, typename... Args>
concept noex_init = __is_nothrow_constructible(T, Args...);

/// Is nothrow assignable
export template<typename T, typename U>
concept noex_assign = __is_nothrow_assignable(T, U);

/// Is nothrow copy construcible
export template<typename T>
concept noex_copy = noex_init<T, add_ref<add_const<T>>>;

/// Is nothrow move construcible
export template<typename T>
concept noex_move = noex_init<T, add_ref_rv<T>>;

/// Is nothrow copy assignable
export template<typename T>
concept noex_assign_copy = noex_assign<add_ref<T>, add_ref<add_const<T>>>;
/// Is nothrow move assignable
export template<typename T>
concept noex_assign_move = noex_assign<add_ref<T>, add_ref_rv<T>>;

/// Is no throw destructible
export template<typename T>
concept noex_drop =
#if __has_builtin(__is_nothrow_destructible)
    __is_nothrow_destructible(T);
#else
    noexcept(declval<T&>().~T());
#endif
/// @}

/// \name User
/// @{
/// Has a user-defined (non-trivial) copy constructor.
export template<typename T>
concept user_copy = init<T, T const&> && (! triv_copy<T>);
/// Has a user-defined (non-trivial) move constructor.
export template<typename T>
concept user_move = init<T, T&&> && (! triv_move<T>);
/// Has a user-defined (non-trivial) copy assignment operator.
export template<typename T>
concept user_assign_copy = assign<T, T const&> && (! triv_assign_copy<T>);
/// Has a user-defined (non-trivial) move assignment operator.
export template<typename T>
concept user_assign_move = assign<T, T&&> && (! triv_assign_move<T>);

/// @}

/// \name Is
/// @{
/// Satisfied when `T` is `void` (ignoring cv/ref qualifiers).
export template<typename T>
concept is_void = same<rm_cvf<T>, void>;
/// Satisfied when `T` is an integral type.
export template<typename T>
concept is_int = requires(T t, T* p, void (*f)(T)) // T* parameter excludes reference types
{
    reinterpret_cast<T>(t); // Exclude class types
    f(0);                   // Exclude enumeration types
    p + t;                  // Exclude everything not yet excluded but integral types
};
/// Satisfied when `T` is a floating-point type.
export template<typename T>
concept is_float = any<rm_cvf<T>, float, double, long double>;
/// Satisfied when `T` is an arithmetic (integral or floating-point) type.
export template<typename T>
concept is_arithmetic = is_int<T> || is_float<T>;

/// Satisfied when `T` is a reference type (lvalue or rvalue).
export template<typename T>
concept is_ref = __is_reference(T);
/// Satisfied when `T` is an lvalue reference.
export template<typename T>
concept is_ref_lv = ::is_lvalue_reference<T> {}();
/// Satisfied when `T` is an rvalue reference.
export template<typename T>
concept is_ref_rv = ::is_rvalue_reference<T> {}();
/// Satisfied when `T` is an enumeration type.
export template<typename T>
concept is_enum = __is_enum(T);
/// Satisfied when `T` is a union type.
export template<typename T>
concept is_union = __is_union(T);
/// Satisfied when `T` is a class or struct type.
export template<typename T>
concept is_class = __is_class(T);
/// Satisfied when `T` is an array type.
export template<typename T>
concept is_array = __is_array(T);
/// Satisfied when `T` is an array type with unknown bound.
export template<typename T>
concept is_array_dst =
#if __has_builtin(__is_unbounded_array)
    __is_unbounded_array(T);
#else
    ::is_array_unknown_bounds<T> {}();
#endif

/// Satisfied when `T` is a tuple-like type (has `tuple_size`).
export template<typename T>
concept is_tuple = requires { std::tuple_size<T>(); };
/// Satisfied when `T` is an aggregate type.
export template<typename T>
concept is_aggregate = __is_aggregate(rm_cv<T>);
/// Satisfied when `Base` is a base class of `Derived`.
export template<typename Base, typename Derived>
concept is_base_of = __is_base_of(Base, Derived);

export template<typename T>
concept is_func = __is_function(T);
/// @}

/// \name Logic
/// @{
/// Selects `Iftrue` or `Iffalse` based on a compile-time boolean condition.
export template<bool Cond, typename Iftrue, typename Iffalse>
using cond = ::conditional<Cond, Iftrue, Iffalse>::type;

export template<typename... Bn>
struct or_ : decltype(or_fn<Bn...>(0)) {};
export template<typename... Bn>
struct and_ : decltype(and_fn<Bn...>(0)) {};
export template<typename Pp>
struct not_ : bool_constant<! bool(Pp::value)> {};
/// @}

/// \name Access
/// @{
/// Retrieves the underlying integer type of an enum.
export template<typename T>
    requires is_enum<T>
using underlying = __underlying_type(T);

/// The number of elements in a tuple-like type.
export template<typename T>
constexpr auto tuple_size = std::tuple_size<T>::value;
/// The type of the I-th element in a tuple-like type.
export template<usize I, typename T>
using tuple_element = std::tuple_element<I, T>::type;
/// @}

/// Satisfied when `T` is a specialization of the template `Primary`.
export template<class T, template<class...> class Primary>
concept spec_of = ::spec_of<T, Primary> {}();

/// Maps `void` to `empty`, otherwise yields `T`.
export template<typename T>
using void_empty_t = cond<is_void<T>, empty, T>;

/// Propagates the const-ness of `T` onto `U`.
export template<typename T, typename U>
using follow_const_t =
    mtp::cond<mtp::is_const<T>, mtp::add_const<mtp::rm_const<U>>, mtp::rm_const<U>>;

/// Satisfied when `T` is a complete type.
export template<typename T>
concept complete = requires { sizeof(T); };

template<typename T>
concept complete_or_unbounded =
    complete<T> || is_ref<T> || is_func<T> || is_void<T> || (is_array_unknown_bounds<T> {}());

/// \name traits
/// @{

/// Extracts metadata from function pointer and member function pointer types.
export template<typename T>
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

/// Retrieves the I-th value from a non-type template parameter pack at compile time.
export template<usize I, auto... Vals>
consteval auto get_auto() {
    static_assert(I < sizeof...(Vals), "out of range");
    return get_auto_impl<I, Vals...>();
}

} // namespace rstd::mtp
