export module rstd.core:fmt;
export import :meta;
export import :cppstd;
export import :trait;

namespace rstd::fmt
{

namespace detail
{
template<typename Tp, typename Context,
         typename Formatter = typename Context::template formatter_type<meta::remove_const_t<Tp>>,
         typename ParseContext = cppstd::basic_format_parse_context<typename Context::char_type>>
concept parsable_with = meta::semiregular<Formatter> && requires(Formatter f, ParseContext pc) {
    { f.parse(pc) } -> meta::same_as<typename ParseContext::iterator>;
};

template<typename Tp, typename Context,
         typename Formatter = typename Context::template formatter_type<meta::remove_const_t<Tp>>,
         typename ParseContext = cppstd::basic_format_parse_context<typename Context::char_type>>
concept formattable_with =
    meta::semiregular<Formatter> && requires(const Formatter cf, Tp&& t, Context fc) {
        { cf.format(t, fc) } -> meta::same_as<typename Context::iterator>;
    };

// An unspecified output iterator type used in the `formattable` concept.
template<typename CharT>
using Iter_for = cppstd::back_insert_iterator<cppstd::basic_string<CharT>>;

template<typename Tp, typename CharT,
         typename Context = cppstd::basic_format_context<Iter_for<CharT>, CharT>>
concept formattable_impl = parsable_with<Tp, Context> && formattable_with<Tp, Context>;
} // namespace detail

template<typename Tp, typename CharT = char>
concept formattable = detail::formattable_impl<meta::remove_reference_t<Tp>, CharT>;

export struct Display {
    template<typename Self, typename = void>
    struct Api {};
};

} // namespace rstd::fmt

namespace rstd
{
export template<typename T, typename A>
    requires meta::same_as<T, fmt::Display> && fmt::formattable<A, char>
struct Impl<T, A> {};

static_assert(Impled<int, fmt::Display>);

export void panic_raw(cppstd::string_view msg,
                      const source_location = cppstd::source_location::current());

export template<typename... T>
struct panic {
    panic(cppstd::format_string<T...>   fmt, T&&... args,
          const cppstd::source_location loc = cppstd::source_location::current()) {
        panic_raw(cppstd::vformat(fmt.get(), cppstd::make_format_args(args...)), loc);
    }
};

template<typename... Ts>
panic(cppstd::format_string<Ts...>, Ts&&...) -> panic<Ts...>;

} // namespace rstd