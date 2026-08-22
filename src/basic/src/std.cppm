module;
#include <cstddef>
#include <cstdint>

#include <initializer_list>
#include <compare>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

export module rstd.basic:std;

export using ::operator new;
export using ::operator delete;

namespace std
{
export using std::tuple_size;
export using std::tuple_element;
export using std::get;
export using std::strong_ordering;
export using std::compare_three_way;
export using std::nullptr_t;
export using std::iter_value_t;
export using std::construct_at;
} // namespace std

namespace rstd
{
export {
    using std::size_t;
    using std::ptrdiff_t;
    using std::int8_t;
    using std::int16_t;
    using std::int32_t;
    using std::int64_t;
    using std::uint8_t;
    using std::uint16_t;
    using std::uint32_t;
    using std::uint64_t;
    using std::uint_least32_t;
    using std::intptr_t;
    using std::uintptr_t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using int128_t  = __int128;
    using uint128_t = unsigned __int128;
#pragma GCC diagnostic pop

    using std::byte;

    using voidp       = void*;
    using const_voidp = void const*;
    struct empty {};
    template<typename>
    struct emptyT {};

    using std::nullptr_t;
    using std::initializer_list;

    using std::get;
    using std::tuple_element;
    using std::tuple_size;

    using std::align_val_t;

    using std::compare_three_way;
    using std::partial_ordering;
    using std::strong_ordering;
    using std::weak_ordering;

    using std::forward;
    using std::move;
}

} // namespace rstd

export namespace rstd::mtp
{

using std::invoke_result;
using std::invoke_result_t;
using std::is_invocable;
using std::is_invocable_v;
using std::iter_value_t;

} // namespace rstd::mtp
