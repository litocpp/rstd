export module rstd.core:panicking;
export import :fmt;
export import :panic;
export import :str.str;

namespace rstd
{

/// Triggers a panic with a formatted message and source location.
/// \param args The formatted message arguments.
/// \param loc The source location of the panic.
export [[noreturn]]
void panic_fmt(fmt::Arguments args, panic_::Location loc);

/// Triggers a non-unwinding panic for noexcept or FFI contexts.
/// \param args The formatted message arguments.
/// \param loc The source location of the panic.
export [[noreturn]]
void panic_fmt_nounwind(fmt::Arguments args, panic_::Location loc);

/// Compile-time-checked panic with format string support.
///
/// The first argument must be a string literal. Placeholder count is validated
/// at compile time. Source location is captured automatically.
/// \tparam Args The types of the format arguments.
export template<typename... Args>
struct panic {
    [[gnu::always_inline]] [[noreturn]]
    inline panic(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        if constexpr (sizeof...(Args) > 0) {
            fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
            panic_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                      panic_::Location::from(loc.val));
        } else {
            panic_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 },
                      panic_::Location::from(loc.val));
        }
    }
};

template<typename... Ts>
panic(fmt::format_string<Ts...>, Ts&&...) -> panic<Ts...>;
panic(ref<str>, panic_::SrcLoc = {}) -> panic<>;

// Overload for runtime ref<str> (no compile-time format checking).
// Used when the message is already a pre-built string (e.g. unwrap_failed).
template<>
struct panic<> {
    [[gnu::always_inline]] [[noreturn]]
    inline panic(ref<str> msg, panic_::SrcLoc loc = {}) {
        // Wrap the str bytes as a single Display argument.
        fmt::Argument         arg     = fmt::Argument::make(msg);
        static constexpr char fmt_s[] = "{}";
        panic_fmt({ (const u8*)fmt_s, 2, &arg, 1 }, panic_::Location::from(loc.val));
    }
};

} // namespace rstd
