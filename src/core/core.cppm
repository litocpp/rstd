module;
#include <cstdint>
#include <cstddef>
export module rstd.core;
export import :trait;

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
export using const_voidp = const void*;

} // namespace rstd
