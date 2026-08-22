export module rstd.core:panicking;
import :num.types;
export import :fmt;
export import :panic;
export import :str.str;

namespace rstd
{

/// Triggers a panic with a formatted message and source location.
/// \param args The formatted message arguments.
/// \param loc The source location of the panic.
export [[noreturn]]
void panic_fmt(fmt::Arguments args, source_location loc);

/// Triggers a non-unwinding panic for noexcept or FFI contexts.
/// \param args The formatted message arguments.
/// \param loc The source location of the panic.
export [[noreturn]]
void panic_fmt_nounwind(fmt::Arguments args, source_location loc);

/// Triggers a panic with an already constructed message and source location.
export [[noreturn]]
void panic_message(ref<str> message, source_location loc);

export template<rstd::size_t N>
[[noreturn]]
inline void panic_message(const char (&message)[N], source_location loc) {
    panic_fmt({ message, N - 1, nullptr, 0 }, loc);
}

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
            panic_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) }, loc.val);
        } else {
            panic_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 }, loc.val);
        }
    }
};

template<typename... Ts>
panic(fmt::format_string<Ts...>, Ts&&...) -> panic<Ts...>;
template<rstd::size_t N>
panic(const char (&)[N], panic_::SrcLoc = {}) -> panic<>;
panic(ref<str>, panic_::SrcLoc = {}) -> panic<>;

// Overload for runtime ref<str> (no compile-time format checking).
// Used when the message is already a pre-built string (e.g. unwrap_failed).
template<>
struct panic<> {
    template<rstd::size_t N>
    [[gnu::always_inline]] [[noreturn]]
    inline panic(const char (&msg)[N], panic_::SrcLoc loc = {}) {
        panic_message(msg, loc.val);
    }

    [[gnu::always_inline]] [[noreturn]]
    inline panic(ref<str> msg, panic_::SrcLoc loc = {}) {
        panic_message(msg, loc.val);
    }
};

} // namespace rstd
