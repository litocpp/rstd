module;
#include <compare>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <tuple>
#include <source_location>
export module rstd.basic:prelude;

/// Global placement new operator.
export using ::operator new;
/// Global delete operator.
export using ::operator delete;
namespace std
{
/// Provides the number of elements in a tuple-like type.
export using std::tuple_size;
/// Provides the type of the I-th element in a tuple-like type.
export using std::tuple_element;
/// Extracts the I-th element from a tuple-like type.
export using std::get;
/// A totally ordered comparison result type.
export using std::strong_ordering;
export using std::compare_three_way;
export using std::nullptr_t;
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
    using std::intptr_t;
    using std::uintptr_t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using int128_t  = __int128;
    using uint128_t = unsigned __int128;
#pragma GCC diagnostic pop

    /// Raw byte storage type.
    using std::byte;

    /// Mutable void pointer.
    using voidp = void*;
    /// Const void pointer.
    using const_voidp = void const*;
    /// A zero-sized type, analogous to Rust's `()` (unit).
    struct empty {};
    /// A zero-sized phantom type parameterized on `T`.
    template<typename>
    struct emptyT {};

    /// The type of `nullptr`.
    using std::nullptr_t;
    /// Captures information about the source code location.
    using std::source_location;

    /// Provides access to an array created by list initialization.
    using std::initializer_list;

    /// Extracts the I-th element from a tuple-like type.
    using std::get;
    /// Provides the type of the I-th element in a tuple-like type.
    using std::tuple_element;
    /// Provides the number of elements in a tuple-like type.
    using std::tuple_size;

    /// Alignment value type for sized deallocation.
    using std::align_val_t;

    /// Function object performing three-way comparison.
    using std::compare_three_way;
    /// A partially ordered comparison result type.
    using std::partial_ordering;
    /// A totally ordered comparison result type.
    using std::strong_ordering;
    /// A weakly ordered comparison result type.
    using std::weak_ordering;

    /// Forwards lvalues as either lvalues or rvalues, preserving value category.
    using std::forward;
    /// Converts a value to an rvalue, enabling move semantics.
    using std::move;
}

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

using std::nullptr_t;
using std::source_location;
using rstd::initializer_list;

using std::tuple_element;
using std::tuple_size;

using std::align_val_t;

using std::compare_three_way;
using std::partial_ordering;
using std::strong_ordering;
using std::weak_ordering;

} // namespace prelude

} // namespace rstd
