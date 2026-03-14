export module rstd.basic:basic;
export import :mtp;
export import :cppstd;

namespace rstd
{

export template<typename T>
[[gnu::always_inline]]
constexpr auto addressof(T& val) noexcept -> T* {
    return __builtin_addressof(val);
}

export template<typename T, typename U = T>
constexpr inline T exchange(T&  obj,
                            U&& new_val) noexcept(mtp::noex_move<T> && mtp::noex_assign<T&, U>) {
    T old = rstd::move(obj);
    obj   = rstd::forward<U>(new_val);
    return old;
}

export template<typename T, typename... Args>
    requires(! mtp::is_array_dst<T>) && requires { ::new ((void*)0) T(mtp::declval<Args>()...); }
constexpr T* construct_at(T* location,
                          Args&&... args) noexcept(noexcept(::new ((void*)0)
                                                                T(mtp::declval<Args>()...))) {
    void* loc = location;
    if constexpr (mtp::is_array<T>) {
        static_assert(sizeof...(Args) == 0,
                      "std::construct_at for array "
                      "types must not use any arguments to initialize the "
                      "array");
        return ::new (loc) T[1]();
    } else
        return ::new (loc) T(rstd::forward<Args>(args)...);
}
export template<typename T>
constexpr inline void destroy_at(T* location) {
    if constexpr (__cplusplus > 201703L && mtp::is_array<T>) {
        for (auto& x : *location) rstd::destroy_at(rstd::addressof(x));
    } else
        location->~T();
}

export template<typename T>
[[nodiscard]]
constexpr T* launder(T* __p) noexcept {
    if constexpr (__is_same(const volatile T, const volatile void))
        static_assert(! __is_same(const volatile T, const volatile void),
                      "std::launder argument must not be a void pointer");
    else if constexpr (__is_function(T))
        static_assert(! __is_function(T), "std::launder argument must not be a function pointer");
    else
        return __builtin_launder(__p);
    return nullptr;
}

export constexpr auto strlen(char const* s) noexcept -> usize {
    if (mtp::is_constant_evaluated()) {
        usize i = 0;
        while (s[i] != '\0') ++i;
        return i;
    } else {
        return __builtin_strlen(s);
    }
}

/// Create a value of type `To` from the bits of `from`.
/**
 * @tparam _To   A trivially-copyable type.
 * @param __from A trivially-copyable object of the same size as `_To`.
 * @return       An object of type `_To`.
 */
export template<typename To, typename From>
[[nodiscard]]
constexpr To bit_cast(const From& from) noexcept
    requires(sizeof(To) == sizeof(From)) && mtp::triv_copy<To> && mtp::triv_copy<From>
{
    return __builtin_bit_cast(To, from);
}

export [[noreturn]]
inline void unreachable() {
    // Uses compiler specific extensions if possible.
    // Even if no extension is used, undefined behavior is still raised by
    // an empty function body and the noreturn attribute.
#if defined(_MSC_VER) && ! defined(__clang__) // MSVC
    __assume(false);
#else // GCC, Clang
    __builtin_unreachable();
#endif
}

export template<typename T>
void swap(T& a, T& b) noexcept(mtp::noex_copy<T>) {
    T t(a);
    a = b;
    b = t;
}

} // namespace rstd
