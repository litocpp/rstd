export module rstd.core:assert;
export import :str;
export import :fmt;

namespace rstd
{
export void assert_raw(ref<str> expr_str, ref<str> message,
                       const source_location = source_location::current());

export template<typename... T>
void assert_fmt(ref<str> expr_str, rstd::format_string<T...> fmt = "", T&&... args,
                const source_location loc = source_location::current()) {
    assert_raw(expr_str, cppstd::vformat(fmt.get(), rstd::fmt::make_format_args(args...)), loc);
}
} // namespace rstd