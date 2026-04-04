export module rstd.core:panicking;
export import :fmt;
export import :panic;

namespace rstd
{

// ── Core panic entry points ────────────────────────────────────────────────

// Formatted panic: constructs PanicInfo{can_unwind=true} and calls rstd_panic_impl.
export [[noreturn]]
void panic_fmt(fmt::Arguments args, panic_::Location loc);

// Variant for noexcept / extern-C contexts (drop impls, FFI boundaries).
// Sets PanicInfo::can_unwind = false so the handler knows it must abort.
export [[noreturn]]
void panic_fmt_nounwind(fmt::Arguments args, panic_::Location loc);

// ── panic<Args...> ─────────────────────────────────────────────────────────
// Compile-time-checked panic.  Usage:
//   rstd::panic("something went wrong");
//   rstd::panic("expected {}, got {}", expected, actual);
//
// The first argument must be a string *literal*.  The consteval constructor of
// format_string validates placeholder count vs argument count at compile time.
// SrcLoc (a consteval wrapper) captures the caller location on both Clang and GCC.
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
