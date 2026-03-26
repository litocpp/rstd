export module rstd.core:panicking;
export import :fmt;
export import :panic;

namespace rstd
{

export [[noreturn]]
void panic_fmt(fmt::Arguments args, const source_location loc = source_location::current());

export template<typename... Args>
struct panic {
#ifdef __clang__
    [[gnu::always_inline]] [[noreturn]]
    inline panic(ref<str>, Args&&... args, const source_location loc = source_location::current()) {
        // panic_fmt(args, loc);
        __builtin_abort();
    }
#else
    [[noreturn]]
    inline panic(ref<str>) {
        __builtin_abort();
    }
#endif
};

template<typename... Ts>
panic(ref<str>, Ts&&...) -> panic<Ts...>;
} // namespace rstd