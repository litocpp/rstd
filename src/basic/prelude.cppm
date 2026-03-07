module;
#include <stdint.h>
#include <stddef.h>
export module rstd.basic:prelude;
export import :cppstd;

namespace rstd
{
export {
    using i8  = ::int8_t;
    using i16 = ::int16_t;
    using i32 = ::int32_t;
    using i64 = ::int64_t;

    using u8  = ::uint8_t;
    using u16 = ::uint16_t;
    using u32 = ::uint32_t;
    using u64 = ::uint64_t;

    using idx         = ::ptrdiff_t;
    using usize       = ::size_t;
    using isize       = ::intptr_t;
    using byte        = cppstd::byte;
    using voidp       = void*;
    using const_voidp = void const*;
    using usizeptr    = ::uintptr_t;

    struct empty {};
    template<typename>
    struct emptyT {};
}

// used for using namespace
export namespace prelude
{
using i8  = ::int8_t;
using i16 = ::int16_t;
using i32 = ::int32_t;
using i64 = ::int64_t;

using u8  = ::uint8_t;
using u16 = ::uint16_t;
using u32 = ::uint32_t;
using u64 = ::uint64_t;

using idx         = ::ptrdiff_t;
using usize       = ::size_t;
using isize       = ::intptr_t;
using byte        = cppstd::byte;
using voidp       = void*;
using const_voidp = void const*;
using usizeptr    = ::uintptr_t;
using rstd::empty;
using rstd::emptyT;

} // namespace literal

} // namespace rstd