module;
// c
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// c++11
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// c++17
#include <filesystem>
#include <memory_resource>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <variant>

// c++20
#include <bit>
#include <coroutine>
#include <format>
#include <ranges>
#include <source_location>
#include <span>

#define ALWAYS_INLINE [[gnu::always_inline]] inline

export module cppstd;

export using ::operator new;
export using ::operator delete;
namespace std
{
// c++11
export using std::is_error_code_enum;
// export for struct binding
export using std::tuple_element;
export using std::tuple_size;
export using std::back_insert_iterator;
export using std::iterator;

// c++20
export using std::coroutine_handle;
export using std::coroutine_traits;
export using std::strong_ordering;
export using std::weak_ordering;
} // namespace std

export namespace cppstd
{

// ==== c ====

// <cstdint>
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

// <cstddef>
using std::nullptr_t;
using std::ptrdiff_t;
using std::size_t;

// <cstdlib>
using std::abort;

// <cstdio>
using std::fflush;
using std::fwrite;

// <cstring>
using std::memcmp;
using std::memcpy;
using std::memset;
using std::strlen;
using std::strncmp;

// <cmath>
using std::fabs;

// ==== c++11 ====

// <type_traits>
using std::false_type;
using std::integral_constant;
using std::true_type;

template<bool v>
using bool_constant = integral_constant<bool, v>;

using std::enable_if;
using std::enable_if_t;

using std::conditional_t;

using std::decay;
using std::decay_t;

using std::underlying_type;
using std::underlying_type_t;

using std::make_unsigned_t;

using std::is_arithmetic;
using std::is_arithmetic_v;
using std::is_array;
using std::is_array_v;
using std::is_base_of;
using std::is_base_of_v;
using std::is_class;
using std::is_class_v;
using std::is_const;
using std::is_const_v;
using std::is_constructible_v;
using std::is_default_constructible_v;
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
using std::is_standard_layout_v;
using std::is_union;
using std::is_union_v;
using std::is_void;
using std::is_void_v;

using std::add_const;
using std::add_const_t;
using std::add_lvalue_reference;
using std::add_lvalue_reference_t;
using std::add_pointer;
using std::add_pointer_t;
using std::add_rvalue_reference;
using std::add_rvalue_reference_t;

using std::remove_const_t;
using std::remove_cv;
using std::remove_cv_t;
using std::remove_extent_t;
using std::remove_pointer_t;
using std::remove_reference;
using std::remove_reference_t;

using std::is_nothrow_constructible;
using std::is_nothrow_constructible_v;
using std::is_nothrow_default_constructible_v;
using std::is_nothrow_destructible;
using std::is_nothrow_destructible_v;
using std::is_trivially_destructible_v;

using std::is_copy_constructible;
using std::is_copy_constructible_v;
using std::is_nothrow_copy_constructible;
using std::is_nothrow_copy_constructible_v;
using std::is_trivially_copy_constructible;
using std::is_trivially_copy_constructible_v;
using std::is_trivially_copyable;
using std::is_trivially_copyable_v;

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

using std::alignment_of_v;

// <limits>
using std::numeric_limits;

// <new>
using std::max_align_t;

// <atomic>
using std::atomic;
using std::atomic_thread_fence;
using std::memory_order;

// <string>
using std::basic_string;
using std::char_traits;
using std::string;

// <fstream>
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

// <algorithm>
using std::copy;
using std::copy_n;
using std::find;
using std::find_if;
using std::max;
using std::min;
using std::transform;

// <memory>
using std::addressof;
using std::allocator;
using std::allocator_traits;
using std::default_delete;
using std::enable_shared_from_this;
using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;
using std::weak_ptr;

// <functional>
using std::bind;
using std::function;
using std::hash;
using std::less;

// <tuple>
using std::get;
using std::make_tuple;
using std::tuple;
using std::tuple_element;
using std::tuple_element_t;
using std::tuple_size;
using std::tuple_size_v;

// <utility>
using std::declval;
using std::forward;
using std::move;
using std::swap;

// <iterator>
using std::back_insert_iterator;
using std::back_inserter;
using std::begin;
using std::end;

// containers
using std::deque;
using std::map;
using std::set;
using std::unordered_map;
using std::unordered_set;
using std::vector;

// <mutex>
using std::lock_guard;
using std::mutex;
using std::unique_lock;

// <future>
using std::future;
using std::promise;

// <thread>
using std::thread;

// <chrono>
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

// <stdexcept>
using std::runtime_error;

// <system_error>
using std::error_category;
using std::error_code;

// <exception>
using std::exception;
using std::exception_ptr;
using std::rethrow_exception;

// ==== c++14 ====

// <utility>
using std::exchange;
using std::integer_sequence;
using std::make_integer_sequence;

template<std::size_t... Ints>
using index_sequence = std::integer_sequence<std::size_t, Ints...>;
template<std::size_t N>
using make_index_sequence = std::make_integer_sequence<std::size_t, N>;

// <memory>
using std::make_unique;

// ==== c++17 ====

// <type_traits>
using std::conjunction;
using std::conjunction_v;
using std::disjunction;
using std::disjunction_v;
using std::is_aggregate;
using std::is_aggregate_v;
using std::is_invocable;
using std::is_invocable_v;
using std::invoke_result;
using std::invoke_result_t;
using std::negation;
using std::negation_v;

// <cstddef>
using std::byte;

// <new>
using std::align_val_t;
using std::launder;

// <string_view>
using std::basic_string_view;
using std::string_view;

// <optional>
using std::nullopt;
using std::nullopt_t;
using std::optional;

// <variant>
using std::get_if;
using std::monostate;
using std::variant;
using std::visit;

// <functional>
using std::invoke;

// <tuple>
using std::apply;

// <charconv>
using std::from_chars;

// <memory>
using std::destroy_at;

// <shared_mutex>
using std::shared_mutex;

// <filesystem>
namespace filesystem
{
using std::filesystem::create_directories;
using std::filesystem::exists;
using std::filesystem::path;
using std::filesystem::remove;
} // namespace filesystem

// <memory_resource>
namespace pmr
{
using std::pmr::deque;
using std::pmr::get_default_resource;
using std::pmr::memory_resource;
using std::pmr::polymorphic_allocator;
using std::pmr::synchronized_pool_resource;
using std::pmr::vector;
} // namespace pmr

// ==== c++20 ====

// <type_traits>
using std::remove_cvref;
using std::remove_cvref_t;
using std::type_identity;
using std::type_identity_t;

// <concepts>
using std::constructible_from;
using std::destructible;
using std::integral;
using std::same_as;
using std::semiregular;

// <compare>
using std::strong_ordering;
using std::weak_ordering;

// <bit>
using std::bit_cast;
using std::bit_ceil;

// <format>
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

// <source_location>
using std::source_location;

// <ranges>
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
namespace views
{
using std::views::transform;
}

// <algorithm>
using std::lexicographical_compare_three_way;

// <memory>
using std::construct_at;

// <iterator>
using std::iter_value_t;

// <span>
using std::as_bytes;
using std::as_writable_bytes;
using std::dynamic_extent;
using std::span;

// ==== c++23 ====

// <functional>
#ifdef __cpp_lib_move_only_function
using std::move_only_function;
#endif

} // namespace cppstd
