module;
// c
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <source_location>

#include <type_traits>
#include <limits>
#include <new>

#include <string_view>
#include <string>
#include <format>

#include <iterator>
#include <tuple>
#include <functional>
#include <memory>
#include <utility>

#include <optional>
#include <stdexcept>
#include <algorithm>

#define ALWAYS_INLINE [[gnu::always_inline]]

export module rstd.core:cppstd;

export namespace rstd::cppstd
{

// basic type
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::int8_t;

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

using std::byte;
using std::ptrdiff_t;
using std::size_t;

// format
using std::basic_format_context;
using std::basic_format_parse_context;
using std::basic_format_string;
using std::format_string;
using std::make_format_args;
using std::vformat;

// string
using std::basic_string;
using std::basic_string_view;
using std::string;
using std::string_view;

// source_location
using std::source_location;

// iterator
using std::back_insert_iterator;

// tuple
using std::integer_sequence;
using std::make_integer_sequence;
using std::make_tuple;
using std::tuple;
using std::tuple_element;
using std::tuple_element_t;
using std::tuple_size;

// optional
using std::optional;
using std::nullopt;
using std::nullopt_t;

template<std::size_t... Ints>
using index_sequence = std::integer_sequence<std::size_t, Ints...>;
template<std::size_t N>
using make_index_sequence = std::make_integer_sequence<std::size_t, N>;

using std::align_val_t;
using std::numeric_limits;
using std::allocator_traits;

using std::abort;
using std::apply;
using std::get;
using std::invoke;
using std::runtime_error;

using std::addressof;
using std::construct_at;
using std::declval;
using std::destroy_at;
using std::forward;
using std::move;
using std::swap;

using std::max;
using std::min;
// c stdio
using std::fflush;
using std::fwrite;

using ::stderr;
using ::stdout;

} // namespace rstd::cppstd

export namespace rstd
{

using std::max;
using std::min;

using std::addressof;
using std::construct_at;
using std::declval;
using std::destroy_at;
using std::forward;
using std::move;
using std::swap;

using ::stderr;
using ::stdout;

} // namespace rstd

export namespace rstd::meta
{

using std::false_type;
using std::integral_constant;
using std::true_type;

template<bool v>
using bool_constant = integral_constant<bool, v>;

using std::enable_if;
using std::enable_if_t;

using std::conditional_t;
using std::conjunction;
using std::conjunction_v;
using std::disjunction;
using std::disjunction_v;
using std::negation;
using std::negation_v;

using std::same_as;

using std::underlying_type;
using std::underlying_type_t;

using std::is_arithmetic;
using std::is_arithmetic_v;
using std::is_base_of;
using std::is_base_of_v;
using std::is_class;
using std::is_class_v;
using std::is_enum;
using std::is_enum_v;
using std::is_floating_point;
using std::is_floating_point_v;
using std::is_integral;
using std::is_integral_v;
using std::is_lvalue_reference;
using std::is_lvalue_reference_v;
using std::is_pointer;
using std::is_pointer_v;
using std::is_reference;
using std::is_reference_v;
using std::is_rvalue_reference;
using std::is_rvalue_reference_v;
using std::is_union;
using std::is_union_v;
using std::is_array;
using std::is_array_v;

using std::add_const;
using std::add_const_t;

using std::add_pointer;
using std::add_pointer_t;

using std::add_lvalue_reference;
using std::add_lvalue_reference_t;
using std::add_rvalue_reference;
using std::add_rvalue_reference_t;

using std::remove_reference;
using std::remove_reference_t;

using std::remove_cv;
using std::remove_cv_t;

using std::constructible_from;
using std::is_const;
using std::is_const_v;
using std::is_constructible_v;
using std::is_default_constructible_v;
using std::is_nothrow_constructible;
using std::is_nothrow_constructible_v;
using std::is_nothrow_destructible;

// copy
using std::is_copy_constructible;
using std::is_copy_constructible_v;
using std::is_nothrow_copy_constructible;
using std::is_nothrow_copy_constructible_v;
using std::is_trivially_copy_constructible;
using std::is_trivially_copy_constructible_v;

// move
using std::is_move_constructible;
using std::is_move_constructible_v;
using std::is_nothrow_move_constructible;
using std::is_nothrow_move_constructible_v;
using std::is_trivially_move_constructible;
using std::is_trivially_move_constructible_v;

using std::is_copy_assignable;
using std::is_copy_assignable_v;
using std::is_nothrow_copy_assignable;
using std::is_nothrow_copy_assignable_v;
using std::is_trivially_copy_assignable;
using std::is_trivially_copy_assignable_v;

using std::is_move_assignable;
using std::is_move_assignable_v;
using std::is_nothrow_move_assignable;
using std::is_nothrow_move_assignable_v;
using std::is_trivially_move_assignable;
using std::is_trivially_move_assignable_v;

using std::is_convertible_v;

using std::remove_const_t;
using std::remove_cvref;
using std::remove_cvref_t;
using std::remove_extent_t;

using std::is_void;
using std::is_void_v;

using std::invoke_result;
using std::invoke_result_t;
using std::is_nothrow_default_constructible_v;
using std::is_trivially_destructible_v;
using std::semiregular;
using std::type_identity;
using std::type_identity_t;

using std::alignment_of_v;
using std::default_delete;

} // namespace rstd::meta