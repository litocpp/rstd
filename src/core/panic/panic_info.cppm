export module rstd.core:panic_info;
export import :fmt;
export import :str.str;

namespace rstd::panic_
{

// Cross-ABI-safe location type.
// Matches Rust's core::panic::Location: file/line/col only, no function_name.
// POD struct — safe to pass across extern "C" boundaries.
export struct Location {
    const char* _file;
    u32         _line;
    u32         _col;

    constexpr auto file_name() const noexcept -> const char* { return _file; }
    constexpr auto line()      const noexcept -> u32         { return _line; }
    constexpr auto column()    const noexcept -> u32         { return _col; }

    static constexpr auto from(cppstd::source_location sl) noexcept -> Location {
        return { sl.file_name(), u32(sl.line()), u32(sl.column()) };
    }
};

export struct PanicInfo {
    fmt::Arguments message;
    Location       location;
    bool           can_unwind         = true;
    bool           force_no_backtrace = false;
};

// Consteval wrapper for source_location.
//
// Placing source_location::current() inside a *consteval* constructor ensures the
// call-site location is captured correctly on both Clang and GCC, even when the
// outer function is itself a function template.  Use as a trailing default parameter:
//
//   [[noreturn]] void foo(int x, SrcLoc loc = {});
//
// The caller writes just `foo(42)` and gets the right location automatically.
export struct SrcLoc {
    cppstd::source_location val;
    consteval SrcLoc(cppstd::source_location v =
                     cppstd::source_location::current()) noexcept
        : val(v) {}
};

} // namespace rstd::panic_
