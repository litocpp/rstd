export module rstd.core:literal;
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
export using usizeptr    = cppstd::uintptr_t;

export struct Empty {};
export template<typename>
struct EmptyT {};

} // namespace rstd