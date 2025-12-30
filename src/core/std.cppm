module;
// c
#include <cstdint>
#include <cstddef>
#include <source_location>
#include <cstdlib>
#include <cstdio>

#include <string_view>
#include <string>
#include <format>
#include <source_location>

#include <iterator>
#include <tuple>
#include <functional>
#include <memory>
#include <utility>

#define ALWAYS_INLINE [[gnu::always_inline]]
export module rstd.core:std;

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

template<std::size_t... Ints>
using index_sequence = std::integer_sequence<std::size_t, Ints...>;
template<std::size_t N>
using make_index_sequence = std::make_integer_sequence<std::size_t, N>;

using std::abort;
using std::apply;
using std::get;
using std::invoke;

using std::forward;
using std::move;
using std::addressof;
using std::construct_at;
using std::destroy_at;


// c stdio
using std::fwrite;
using std::fflush;

using ::stderr;
using ::stdout;

} // namespace rstd::cppstd

export namespace rstd
{

using std::forward;
using std::move;
using std::addressof;
using std::construct_at;
using std::destroy_at;

using ::stderr;
using ::stdout;

} // namespace rstd