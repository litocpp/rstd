export module rstd.core:panicking;
export import :fmt;
export import :panic;

namespace rstd
{

export [[noreturn]]
void panic_fmt(ref<str> msg, const source_location loc = source_location::current());

export template<typename... T>
struct panic {
    [[gnu::always_inline]] [[noreturn]]
    inline panic(cppstd::format_string<T...>   fmt, T&&... args,
                 const cppstd::source_location loc = cppstd::source_location::current()) {
        panic_fmt(cppstd::vformat(fmt.get(), cppstd::make_format_args(args...)), loc);
    }
};

template<typename... Ts>
panic(cppstd::format_string<Ts...>, Ts&&...) -> panic<Ts...>;
} // namespace rstd