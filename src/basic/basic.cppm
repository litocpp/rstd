export module rstd.basic:basic;
export import :mtp;

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

export template<typename T>
constexpr auto min(T a, T b) noexcept -> T {
    return a > b ? b : a;
}

template<typename InputIter1, typename InputIter2, typename Compare>
[[nodiscard]]
constexpr auto lexicographical_compare_three_way(InputIter1 first1, InputIter1 last1,
                                                 InputIter2 first2, InputIter2 last2,
                                                 Compare comp) {
    if constexpr (mtp::is_ptr<InputIter1> && mtp::is_ptr<InputIter2>) {
        if (! mtp::is_constant_evaluated()) {
            using T1 = mtp::iter_value_t<InputIter1>;
            using T2 = mtp::iter_value_t<InputIter2>;

            if constexpr (mtp::same<T1, T2> && mtp::is_int<T1> && sizeof(T1) == 1) {
                auto len1    = last1 - first1;
                auto len2    = last2 - first2;
                auto min_len = rstd::min(len1, len2);

                if (min_len > 0) {
                    int result = __builtin_memcmp(first1, first2, min_len);
                    if (result != 0) {
                        return result <=> 0;
                    }
                }
                // 如果前缀相同，则比较长度
                return len1 <=> len2;
            }
        }
    }

    while (first1 != last1) {
        if (first2 == last2) {
            return rstd::strong_ordering::greater;
        }

        if (auto cmp = comp(*first1, *first2); cmp != 0) {
            return cmp;
        }

        ++first1;
        ++first2;
    }

    return (first1 == last1) <=> (first2 == last2);
}

export template<typename InputIter1, typename InputIter2>
constexpr auto lexicographical_compare_three_way(InputIter1 first1, InputIter1 last1,
                                                 InputIter2 first2, InputIter2 last2) {
    return rstd::lexicographical_compare_three_way(
        first1, last1, first2, last2, rstd::compare_three_way {});
}

} // namespace rstd
