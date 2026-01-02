export module rstd.core:core;
export import :cppstd;

namespace rstd
{

export using i8  = cppstd::int8_t;
export using i16 = cppstd::int16_t;
export using i32 = cppstd::int32_t;
export using i64 = cppstd::int64_t;

export using u8  = cppstd::uint8_t;
export using u16 = cppstd::uint16_t;
export using u32 = cppstd::uint32_t;
export using u64 = cppstd::uint64_t;

export using idx         = cppstd::ptrdiff_t;
export using usize       = cppstd::size_t;
export using isize       = cppstd::ptrdiff_t;
export using byte        = cppstd::byte;
export using voidp       = void*;
export using const_voidp = void const*;

export template<typename T>
using Atomic = cppstd::atomic<T>;

export using cppstd::nullptr_t;
export using cppstd::error_code;
export using cppstd::atomic;
export using cppstd::memory_order;
export using cppstd::atomic_thread_fence;
export using cppstd::numeric_limits;
export using cppstd::allocator;
export using cppstd::allocator_traits;
export using cppstd::source_location;
export using cppstd::align_val_t;

export using cppstd::invoke;
export using cppstd::max;
export using cppstd::min;
export using cppstd::get;
export using cppstd::get_if;
export using cppstd::addressof;
export using cppstd::construct_at;
export using cppstd::copy;
export using cppstd::copy_n;
export using cppstd::exchange;
export using cppstd::declval;
export using cppstd::destroy_at;
export using cppstd::forward;
export using cppstd::move;
export using cppstd::swap;
export using cppstd::memcpy;
export using cppstd::memset;
export using cppstd::strlen;
export using cppstd::strncmp;
export using cppstd::char_traits;

export template<typename T>
struct ref {
    T& p;
};
export template<typename T>
struct ptr {
    T* p;
};
export struct Empty {};

export template<typename>
struct EmptyT {};

export template<typename T>
using param_t = meta::conditional_t<meta::is_trivially_copy_constructible_v<T>, T, T&&>;
export template<typename T>
using param_ref_t = meta::conditional_t<meta::is_trivially_copy_constructible_v<T>, T, T&>;

export template<typename T>
[[nodiscard, gnu::always_inline]]
param_t<T> param_forward(param_ref_t<T> t) {
    if constexpr (meta::is_trivially_copy_constructible_v<T>) {
        return t;
    } else {
        return static_cast<T&&>(t);
    }
}

static_assert(meta::is_trivially_copy_constructible_v<i32>);
static_assert(meta::is_trivially_copy_constructible_v<i32&>);

export [[noreturn]] inline void unreachable() {
    // Uses compiler specific extensions if possible.
    // Even if no extension is used, undefined behavior is still raised by
    // an empty function body and the noreturn attribute.
#if defined(_MSC_VER) && ! defined(__clang__) // MSVC
    __assume(false);
#else // GCC, Clang
    __builtin_unreachable();
#endif
}

} // namespace rstd
