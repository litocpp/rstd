module;
#include <cstdint>
#include <cstddef>
#include <source_location>
#include <string_view>
export module rstd.core:basic;

namespace rstd
{

export using i8  = std::int8_t;
export using i16 = std::int16_t;
export using i32 = std::int32_t;
export using i64 = std::int64_t;

export using u8  = std::uint8_t;
export using u16 = std::uint16_t;
export using u32 = std::uint32_t;
export using u64 = std::uint64_t;

export using idx         = std::ptrdiff_t;
export using usize       = std::size_t;
export using isize       = std::ptrdiff_t;
export using byte        = std::byte;
export using voidp       = void*;
export using const_voidp = void const*;

export using source_location = std::source_location;

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
