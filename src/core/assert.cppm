export module rstd.core:assert;
export import :str.str;
export import :fmt;
export import :panicking;

namespace rstd
{

export template<typename... T>
struct assert_fmt {
/// workaround for gcc source_location link err
#ifdef __clang__
    [[noreturn]]
    inline assert_fmt(ref<str> expr_str, const source_location& loc = source_location::current()) {
        panic_fmt(cppstd::format("Assertion `{}` failed", expr_str), loc);
    }
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str> expr_str, cppstd::format_string<T...> fmt, T&&... args,
                      const cppstd::source_location loc = cppstd::source_location::current()) {
        panic_fmt(cppstd::format("Assertion `{}` failed: {}",
                                 expr_str,
                                 cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...))),
                  loc);
    }
#else
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str>) {
        __builtin_abort();
    }
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str>, cppstd::format_string<T...>, T&&...) {
        __builtin_abort();
    }
#endif
};

template<typename... Ts>
assert_fmt(ref<str>, cppstd::format_string<Ts...>, Ts&&...) -> assert_fmt<Ts...>;

} // namespace rstd