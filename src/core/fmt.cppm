module;
#include <format>
#include "rstd/trait_macro.hpp"
export module rstd.core:fmt;
export import :trait;

namespace rstd::fmt
{

namespace detail
{
template<typename Tp, typename Context,
         typename Formatter    = typename Context::template formatter_type<std::remove_const_t<Tp>>,
         typename ParseContext = std::basic_format_parse_context<typename Context::char_type>>
concept parsable_with = std::semiregular<Formatter> && requires(Formatter f, ParseContext pc) {
    { f.parse(pc) } -> std::same_as<typename ParseContext::iterator>;
};

template<typename Tp, typename Context,
         typename Formatter    = typename Context::template formatter_type<std::remove_const_t<Tp>>,
         typename ParseContext = std::basic_format_parse_context<typename Context::char_type>>
concept formattable_with =
    std::semiregular<Formatter> && requires(const Formatter cf, Tp&& t, Context fc) {
        { cf.format(t, fc) } -> std::same_as<typename Context::iterator>;
    };

// An unspecified output iterator type used in the `formattable` concept.
template<typename CharT>
using Iter_for = std::back_insert_iterator<std::basic_string<CharT>>;

template<typename Tp, typename CharT,
         typename Context = std::basic_format_context<Iter_for<CharT>, CharT>>
concept formattable_impl = parsable_with<Tp, Context> && formattable_with<Tp, Context>;
} // namespace detail

template<typename Tp, typename CharT = char>
concept formattable = detail::formattable_impl<std::remove_reference_t<Tp>, CharT>;

export template<typename Self>
struct Display {};

} // namespace rstd::fmt

namespace rstd
{
export template<template<typename> class T, typename A>
    requires std::same_as<T<A>, fmt::Display<A>> && fmt::formattable<A, char>
struct Impl<T, A> {};

static_assert(Implemented<int, fmt::Display>);

export void panic_raw(std::string_view msg, const source_location = source_location::current());

export template<typename... T>
struct panic {
    panic(std::format_string<T...> fmt, T&&... args,
          const source_location    loc = source_location::current()) {
        panic_raw(std::vformat(fmt.get(), std::make_format_args(args...)), loc);
    }
};

template<typename... Ts>
panic(std::format_string<Ts...>, Ts&&...) -> panic<Ts...>;

} // namespace rstd