export module rstd.core:assert;
export import :str.str;
export import :fmt;
export import :panicking;

namespace rstd
{

export struct assert_fmt {
/// workaround for gcc source_location link err
#ifdef __clang__
    [[noreturn]]
    inline assert_fmt(ref<str> expr_str, const source_location& loc = source_location::current()) {
        // panic_fmt(expr_str, loc);
        __builtin_abort();
    }
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str> expr_str, ref<str> msg,
                      const cppstd::source_location loc = cppstd::source_location::current()) {
        // panic_fmt(msg, loc);
        __builtin_abort();
    }
#else
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str>) {
        __builtin_abort();
    }
    [[gnu::always_inline]] [[noreturn]]
    inline assert_fmt(ref<str>, ref<str>) {
        __builtin_abort();
    }
#endif
};

} // namespace rstd