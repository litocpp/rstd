module;
// c
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cmath>
#include <source_location>

#include <type_traits>
#include <limits>
#include <new>

#include <future>

#include <string_view>
#include <string>
#include <fstream>
#include <iostream>

#include <format>
#include <atomic>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <unordered_set>

#include <mutex>
#include <shared_mutex>

#include <ranges>
#include <iterator>
#include <tuple>
#include <deque>
#include <variant>
#include <functional>
#include <memory>
#include <utility>

#include <optional>
#include <stdexcept>
#include <algorithm>

#include <chrono>
#include <filesystem>
#include <bit>

#include <memory_resource>
#include <coroutine>
#include <new>

#define ALWAYS_INLINE [[gnu::always_inline]] inline

export module rstd.basic:cppstd;

export using ::operator new;
export using ::operator delete;
namespace std
{
export using std::coroutine_handle;
export using std::coroutine_traits;
export using std::is_error_code_enum;
// export for struct binding
export using std::tuple_size;
export using std::tuple_element;
export using std::get;
export using std::get_if;

export using std::strong_ordering;
export using std::weak_ordering;

export using std::iterator;
export using std::back_insert_iterator;
} // namespace std

#ifdef __GNUC__
namespace __gnu_cxx
{
export using __gnu_cxx::__normal_iterator;
export using __gnu_cxx::operator==;
export using __gnu_cxx::operator-;
export using __gnu_cxx::operator+;
} // namespace __gnu_cxx
#endif

export namespace cppstd
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
using std::uintptr_t;

using std::byte;
using std::intptr_t;
using std::ptrdiff_t;
using std::size_t;
using std::strong_ordering;
using std::weak_ordering;

using std::bit_cast;
using std::bit_ceil;
using std::char_traits;
using std::make_unsigned_t;

using std::atomic;
using std::atomic_thread_fence;
using std::memory_order;

using std::back_inserter;

using std::allocator;
using std::allocator_traits;
using std::default_delete;

using std::decay;
using std::decay_t;

// stream
using std::fstream;

namespace ios_base
{
constexpr auto app    = std::ios_base::app;
constexpr auto binary = std::ios_base::binary;
constexpr auto in     = std::ios_base::in;
constexpr auto out    = std::ios_base::out;
constexpr auto trunc  = std::ios_base::trunc;
constexpr auto ate    = std::ios_base::ate;

constexpr auto failbit = std::ios_base::failbit;
constexpr auto badbit  = std::ios_base::badbit;
constexpr auto eofbit  = std::ios_base::eofbit;
constexpr auto goodbit = std::ios_base::goodbit;
} // namespace ios_base

// format
using std::basic_format_context;
using std::basic_format_parse_context;
using std::basic_format_string;
using std::format;
using std::format_args;
using std::format_string;
using std::format_to;
using std::format_to_n;
using std::formatted_size;
using std::formatter;
using std::make_format_args;
using std::vformat;
using std::vformat_to;

// string
using std::basic_string;
using std::basic_string_view;
using std::string;
using std::string_view;

using std::deque;
using std::map;
using std::set;
using std::span;
using std::unordered_set;
using std::vector;

using std::thread;

// source_location
using std::source_location;

// iterator
using std::back_insert_iterator;
using std::iter_value_t;

// function
using std::function;
#if __has_cpp_attribute(__cpp_lib_move_only_function)
using std::move_only_function;
#endif

// future
using std::future;
using std::promise;

using std::hash;

using std::begin;
using std::end;

namespace ranges
{
using std::ranges::begin;
using std::ranges::data;
using std::ranges::distance;
using std::ranges::end;
using std::ranges::range;
using std::ranges::range_value_t;
using std::ranges::size;
} // namespace ranges

// tuple
using std::integer_sequence;
using std::make_integer_sequence;
using std::make_tuple;
using std::tuple;
using std::tuple_element;
using std::tuple_element_t;
using std::tuple_size;
using std::tuple_size_v;

using std::bind;
using std::enable_shared_from_this;
using std::invoke;
using std::make_shared;
using std::make_unique;
using std::monostate;
using std::shared_ptr;
using std::transform;
using std::unique_ptr;
using std::variant;
using std::visit;
using std::weak_ptr;

using std::exception;
using std::exception_ptr;
using std::rethrow_exception;

using std::lock_guard;
using std::mutex;
using std::shared_mutex;
using std::unique_lock;

// optional
using std::nullopt;
using std::nullopt_t;
using std::nullptr_t;
using std::optional;

template<std::size_t... Ints>
using index_sequence = std::integer_sequence<std::size_t, Ints...>;
template<std::size_t N>
using make_index_sequence = std::make_integer_sequence<std::size_t, N>;

using std::align_val_t;
using std::allocator_traits;
using std::max_align_t;
using std::numeric_limits;

using std::error_category;
using std::error_code;

namespace chrono
{
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;
using std::chrono::system_clock;
using std::chrono::time_point;
using std::chrono::operator-;
using std::chrono::operator>;
using std::chrono::operator<;
using std::chrono::operator<=;
using std::chrono::operator>=;
} // namespace chrono

namespace filesystem
{
using std::filesystem::create_directories;
using std::filesystem::exists;
using std::filesystem::path;
using std::filesystem::remove;
} // namespace filesystem

using std::abort;
using std::apply;
using std::get;
using std::get_if;
using std::invoke;
using std::runtime_error;

using std::addressof;
using std::construct_at;
using std::copy;
using std::copy_n;
using std::declval;
using std::destroy_at;
using std::exchange;
using std::forward;
using std::launder;
using std::move;
using std::swap;

using std::fabs;
using std::max;
using std::min;

using std::find;
using std::find_if;
using std::from_chars;

// c stdio
using std::fflush;
using std::fwrite;

using std::memcmp;
using std::memcpy;
using std::memset;
using std::strlen;
using std::strncmp;

using std::less;
using std::lexicographical_compare_three_way;

using ::stderr;
using ::stdout;

namespace pmr
{
using std::pmr::deque;
using std::pmr::get_default_resource;
using std::pmr::memory_resource;
using std::pmr::polymorphic_allocator;
using std::pmr::synchronized_pool_resource;
using std::pmr::vector;

} // namespace pmr

namespace views
{
using std::views::transform;
}

} // namespace cppstd

export namespace cppstd
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

using std::is_aggregate;
using std::is_aggregate_v;
using std::is_arithmetic;
using std::is_arithmetic_v;
using std::is_array;
using std::is_array_v;
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
using std::destructible;
using std::is_const;
using std::is_const_v;
using std::is_constructible_v;
using std::is_default_constructible_v;
using std::is_nothrow_constructible;
using std::is_nothrow_constructible_v;
using std::is_nothrow_destructible;
using std::is_nothrow_destructible_v;

// copy
using std::is_copy_constructible;
using std::is_copy_constructible_v;
using std::is_nothrow_copy_constructible;
using std::is_nothrow_copy_constructible_v;
using std::is_trivially_copy_constructible;
using std::is_trivially_copy_constructible_v;
using std::is_trivially_copyable;
using std::is_trivially_copyable_v;

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

using std::is_nothrow_assignable;
using std::is_nothrow_assignable_v;

using std::is_convertible_v;
using std::is_invocable;
using std::is_invocable_v;

using std::remove_const_t;
using std::remove_cvref;
using std::remove_cvref_t;
using std::remove_extent_t;
using std::remove_pointer_t;

using std::integral;
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
using std::is_standard_layout_v;

using std::decay;
using std::decay_t;

using std::declval;

} // namespace cppstd

export namespace rstd
{

using ::stderr;
using ::stdout;

} // namespace rstd

export namespace rstd::mtp
{

using cppstd::false_type;
using cppstd::integral_constant;
using cppstd::true_type;

template<bool v>
using bool_constant = integral_constant<bool, v>;

using cppstd::conjunction_v;
using cppstd::disjunction_v;
using cppstd::negation_v;

using cppstd::same_as;

using cppstd::underlying_type;
using cppstd::underlying_type_t;

using cppstd::is_aggregate;
using cppstd::is_aggregate_v;
using cppstd::is_arithmetic;
using cppstd::is_arithmetic_v;
using cppstd::is_array;
using cppstd::is_array_v;
using cppstd::is_base_of;
using cppstd::is_base_of_v;
using cppstd::is_class;
using cppstd::is_class_v;
using cppstd::is_enum;
using cppstd::is_enum_v;
using cppstd::is_floating_point;
using cppstd::is_floating_point_v;
using cppstd::is_integral;
using cppstd::is_integral_v;
using cppstd::is_lvalue_reference;
using cppstd::is_lvalue_reference_v;
using cppstd::is_pointer;
using cppstd::is_pointer_v;
using cppstd::is_reference;
using cppstd::is_reference_v;
using cppstd::is_rvalue_reference;
using cppstd::is_rvalue_reference_v;
using cppstd::is_union;
using cppstd::is_union_v;

using cppstd::add_const;
using cppstd::add_const_t;

using cppstd::add_pointer;
using cppstd::add_pointer_t;

using cppstd::add_lvalue_reference;
using cppstd::add_lvalue_reference_t;
using cppstd::add_rvalue_reference;
using cppstd::add_rvalue_reference_t;

using cppstd::remove_reference;
using cppstd::remove_reference_t;

using cppstd::remove_cv;
using cppstd::remove_cv_t;

using cppstd::constructible_from;
using cppstd::destructible;
using cppstd::is_const;
using cppstd::is_const_v;
using cppstd::is_constructible_v;
using cppstd::is_default_constructible_v;
using cppstd::is_nothrow_constructible;
using cppstd::is_nothrow_constructible_v;
using cppstd::is_nothrow_destructible;
using cppstd::is_nothrow_destructible_v;

// copy
using cppstd::is_copy_constructible;
using cppstd::is_copy_constructible_v;
using cppstd::is_nothrow_copy_constructible;
using cppstd::is_nothrow_copy_constructible_v;
using cppstd::is_trivially_copy_constructible;
using cppstd::is_trivially_copy_constructible_v;
using cppstd::is_trivially_copyable;
using cppstd::is_trivially_copyable_v;

// move
using cppstd::is_move_constructible;
using cppstd::is_move_constructible_v;
using cppstd::is_nothrow_move_constructible;
using cppstd::is_nothrow_move_constructible_v;
using cppstd::is_trivially_move_constructible;
using cppstd::is_trivially_move_constructible_v;

using cppstd::is_copy_assignable;
using cppstd::is_copy_assignable_v;
using cppstd::is_nothrow_copy_assignable;
using cppstd::is_nothrow_copy_assignable_v;
using cppstd::is_trivially_copy_assignable;
using cppstd::is_trivially_copy_assignable_v;

using cppstd::is_move_assignable;
using cppstd::is_move_assignable_v;
using cppstd::is_nothrow_move_assignable;
using cppstd::is_nothrow_move_assignable_v;
using cppstd::is_trivially_move_assignable;
using cppstd::is_trivially_move_assignable_v;

using cppstd::is_nothrow_assignable;
using cppstd::is_nothrow_assignable_v;

using cppstd::is_convertible_v;
using cppstd::is_invocable;
using cppstd::is_invocable_v;

using cppstd::remove_const_t;
using cppstd::remove_cvref;
using cppstd::remove_cvref_t;
using cppstd::remove_extent_t;
using cppstd::remove_pointer_t;

using cppstd::integral;
using cppstd::is_void;
using cppstd::is_void_v;

using cppstd::invoke_result;
using cppstd::invoke_result_t;
using cppstd::is_nothrow_default_constructible_v;
using cppstd::is_trivially_destructible_v;
using cppstd::semiregular;
using cppstd::type_identity;
using cppstd::type_identity_t;

using cppstd::alignment_of_v;
using cppstd::default_delete;
using cppstd::is_standard_layout_v;

using cppstd::decay;
using cppstd::decay_t;

using cppstd::declval;

} // namespace rstd::mtp