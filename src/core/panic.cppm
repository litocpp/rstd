export module rstd.core:panic;
export import :fmt;

namespace rstd
{

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