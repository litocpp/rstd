module;
#include <cstdint>
#include <cstddef>
#include <tuple>
#include <source_location>
export module rstd.basic:prelude;

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
    using u128 = unsigned __int128;

    using i128 = __int128;

    using idx   = ::ptrdiff_t;
    using usize = ::size_t;
    using isize = ::intptr_t;
    using std::byte;

    using voidp       = void*;
    using const_voidp = void const*;
    using usizeptr    = ::uintptr_t;

    struct empty {};
    template<typename>
    struct emptyT {};

    using std::nullptr_t;
    using std::numeric_limits;
    using std::source_location;

    using std::get;
    using std::tuple;
    using std::tuple_element;
    using std::tuple_size;

    using std::align_val_t;

    using std::partial_ordering;
    using std::strong_ordering;
    using std::weak_ordering;

    using std::forward;
    using std::move;
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

using idx   = ::ptrdiff_t;
using usize = ::size_t;
using isize = ::intptr_t;
using std::byte;
using voidp       = void*;
using const_voidp = void const*;
using usizeptr    = ::uintptr_t;
using rstd::empty;
using rstd::emptyT;

using std::nullptr_t;
using std::numeric_limits;
using std::source_location;

using std::tuple;
using std::tuple_element;
using std::tuple_size;

using std::align_val_t;

using std::partial_ordering;
using std::strong_ordering;
using std::weak_ordering;

} // namespace prelude

} // namespace rstd