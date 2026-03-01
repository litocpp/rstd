export module rstd.core:assert;
export import :str;
export import :fmt;
export import :panicking;

namespace rstd
{
#ifdef __GNUC__
export [[noreturn]]
void assert_fmt(ref<str> expr_str) {
    panic_fmt(cppstd::format("Assertion `{}` failed", expr_str));
}

export template<typename... T>
[[noreturn]]
void assert_fmt(ref<str> expr_str, rstd::format_string<T...> fmt, T&&... args) {
    panic_fmt(cppstd::format("Assertion `{}` failed: {}", expr_str, cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...))));
}
#else
export [[noreturn]]
void assert_fmt(ref<str> expr_str, const source_location& loc = source_location::current()) {
    panic_fmt(cppstd::format("Assertion `{}` failed", expr_str), loc);
}

export [[noreturn]]
template<typename... T>
void assert_fmt(ref<str> expr_str, rstd::format_string<T...> fmt, T&&... args,
                const source_location loc = source_location::current()) {
    panic_fmt(cppstd::format("Assertion `{}` failed: {}", expr_str, cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...)), loc);
}
#endif

} // namespace rstd