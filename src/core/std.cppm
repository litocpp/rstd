module;
#include <string>
#include <format>
#include <iterator>
#include <source_location>
#include <tuple>
#include <functional>
#include <memory>
#include <utility>

#define ALWAYS_INLINE [[gnu::always_inline]]
export module rstd.core:std;

export namespace rstd::cppstd
{

// format
template<typename T>
using basic_format_parse_context = std::basic_format_parse_context<T>;
template<class OutputIt, class CharT>
using basic_format_context = std::basic_format_context<OutputIt, CharT>;

template<class CharT, class... Args>
using basic_format_string = std::basic_format_string<CharT, Args...>;

template<class... Args>
using format_string = std::basic_format_string<char, std::type_identity_t<Args>...>;

ALWAYS_INLINE inline std::string vformat(std::string_view fmt, std::format_args args) {
    return std::vformat(fmt, args);
}

template<class Context = std::format_context, class... Args>
ALWAYS_INLINE auto make_format_args(Args&... args) {
    return std::make_format_args<Context>(args...);
}

// string
template<class CharT, class Traits = std::char_traits<CharT>,
         class Allocator = std::allocator<CharT>>
using basic_string = std::basic_string<CharT, Traits, Allocator>;

using string = std::basic_string<char>;

template<typename CharT, typename Traits = std::char_traits<CharT>>
using basic_string_view = std::basic_string_view<CharT, Traits>;

using string_view = std::basic_string_view<char>;

// iterator
template<typename Container>
using back_insert_iterator = std::back_insert_iterator<Container>;

// source_location
using source_location = std::source_location;

// tuple
template<class... Types>
using tuple = std::tuple<Types...>;

template<std::size_t I, typename T>
using tuple_element = std::tuple_element<I, T>;
template<std::size_t I, typename T>
using tuple_element_t = std::tuple_element_t<I, T>;

template<typename T>
using tuple_size = std::tuple_size<T>;

template<class T, T... Ints>
using integer_sequence = std::integer_sequence<T, Ints...>;
template<class T, T N>
using make_integer_sequence = std::make_integer_sequence<T, N>;

template<std::size_t... Ints>
using index_sequence = std::integer_sequence<std::size_t, Ints...>;
template<std::size_t N>
using make_index_sequence = std::make_integer_sequence<std::size_t, N>;

template<std::size_t I, class... Types>
constexpr const typename std::tuple_element<I, std::tuple<Types...>>::type&
get(const std::tuple<Types...>& t) noexcept {
    return std::get<I>(t);
}

template<class F, class Tuple>
ALWAYS_INLINE constexpr decltype(auto) apply(F&& f, Tuple&& t) {
    return std::apply(std::forward<F>(f), std::forward<Tuple>(t));
}

template<class F, class... Args>
ALWAYS_INLINE constexpr std::invoke_result_t<F, Args...>
invoke(F&& f, Args&&... args) noexcept(std::is_nothrow_invocable_v<F, Args...>) {
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace rstd::cppstd

export namespace rstd
{
template<typename _Tp>
ALWAYS_INLINE constexpr _Tp&& forward(typename std::remove_reference<_Tp>::type& __t) noexcept {
    return std::forward<_Tp>(__t);
}
template<typename _Tp>
ALWAYS_INLINE constexpr _Tp&& forward(typename std::remove_reference<_Tp>::type&& __t) noexcept {
    return std::forward<_Tp>(__t);
}

template<typename _Tp>
ALWAYS_INLINE constexpr typename std::remove_reference<_Tp>::type&& move(_Tp&& __t) noexcept {
    return std::move<_Tp>(std::forward<_Tp>(__t));
}

template<class T>
ALWAYS_INLINE inline constexpr T* addressof(T& arg) noexcept {
    return std::addressof<T>(arg);
}
template<class T>
const T* addressof(const T&&) = delete;

// memory
template<class T, class... Args>
ALWAYS_INLINE constexpr T* construct_at(T* location, Args&&... args) {
    return std::construct_at(location, std::forward<Args>(args)...);
}
template<class T>
ALWAYS_INLINE inline constexpr void destroy_at(T* p) {
    std::destroy_at(p);
}
} // namespace rstd