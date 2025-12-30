export module rstd.core:basic;
export import :std;

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

export using cppstd::numeric_limits;
export using cppstd::allocator_traits;
export using cppstd::source_location;
export using cppstd::align_val_t;

export struct Empty {};

export template<typename>
struct EmptyT {};

export void rstd_assert(bool, const source_location = source_location::current());

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
