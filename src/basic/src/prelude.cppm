export module rstd.basic:prelude;
export import :source_location;
export import :std;

namespace rstd
{
// used for using namespace
export namespace prelude
{
using rstd::size_t;
using rstd::ptrdiff_t;
using rstd::int8_t;
using rstd::int16_t;
using rstd::int32_t;
using rstd::int64_t;
using rstd::uint8_t;
using rstd::uint16_t;
using rstd::uint32_t;
using rstd::uint64_t;
using rstd::intptr_t;
using rstd::uintptr_t;
using rstd::int128_t;
using rstd::uint128_t;

using rstd::byte;
using voidp       = void*;
using const_voidp = void const*;
using rstd::empty;
using rstd::emptyT;

using rstd::nullptr_t;
using rstd::source_location;
using rstd::initializer_list;

using rstd::tuple_element;
using rstd::tuple_size;

using rstd::align_val_t;

using rstd::compare_three_way;
using rstd::partial_ordering;
using rstd::strong_ordering;
using rstd::weak_ordering;

} // namespace prelude

} // namespace rstd
