export module rstd.core:assert;
export import :str.str;
export import :fmt;
export import :panicking;

namespace rstd
{
/// workaround for gcc source_location link err
#ifdef __clang__
export [[noreturn]]
void assert_fmt(ref<str> expr_str, const source_location& loc = source_location::current()) {
    panic_fmt(cppstd::format("Assertion `{}` failed", expr_str), loc);
}

export template<typename... T>
[[noreturn]]
void assert_fmt(ref<str> expr_str, rstd::format_string<T...> fmt, T&&... args,
                const source_location loc = source_location::current()) {
    panic_fmt(cppstd::format("Assertion `{}` failed: {}",
                             expr_str,
                             cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...))),
              loc);
}
#else
export [[noreturn]]
void assert_fmt(ref<str> expr_str) {
    panic_fmt(cppstd::format("Assertion `{}` failed", expr_str));
}

export template<typename... T>
[[noreturn]]
void assert_fmt(ref<str> expr_str, rstd::format_string<T...> fmt, T&&... args) {
    panic_fmt(cppstd::format("Assertion `{}` failed: {}",
                             expr_str,
                             cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...))));
}
#endif

} // namespace rstd