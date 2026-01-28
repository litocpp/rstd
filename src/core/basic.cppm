export module rstd.core:basic;
export import :cppstd;

namespace rstd
{

export using cppstd::strong_ordering;
export using cppstd::nullptr_t;
export using cppstd::error_code;
export using cppstd::memory_order;
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
export using cppstd::launder;
export using cppstd::copy;
export using cppstd::copy_n;
export using cppstd::exchange;
export using cppstd::destroy_at;
export using cppstd::forward;
export using cppstd::move;
export using cppstd::memcpy;
export using cppstd::memcmp;
export using cppstd::memset;
export using cppstd::strlen;
export using cppstd::strncmp;
export using cppstd::char_traits;
export using cppstd::make_unsigned_t;
export using cppstd::bit_cast;
export using cppstd::default_delete;

export using meta::declval;

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

export template<typename T>
void swap(T& a, T& b) noexcept(meta::is_nothrow_copy_constructible_v<T>) {
    T t(a);
    a = b;
    b = t;
}


} // namespace rstd
