export module rstd.basic:panic_info;
export import :prelude;

namespace rstd::panic_
{
#ifdef __clang__
using sl_type = rstd::source_location;
#else
struct source_location {
    static consteval source_location current() noexcept { return {}; }

    constexpr u32         line() const noexcept { return 0u; }
    constexpr u32         column() const noexcept { return 0u; }
    constexpr const char* file_name() const noexcept { return ""; }
    constexpr const char* function_name() const noexcept { return ""; }
};

using sl_type = source_location;
#endif
// Cross-ABI-safe location type.
// Matches Rust's core::panic::Location: file/line/col only, no function_name.
// POD struct — safe to pass across extern "C" boundaries.
export struct Location {
    const char* _file;
    u32         _line;
    u32         _col;

    constexpr auto file_name() const noexcept -> const char* { return _file; }
    constexpr auto line() const noexcept -> u32 { return _line; }
    constexpr auto column() const noexcept -> u32 { return _col; }

    static constexpr auto from(sl_type sl) noexcept -> Location {
        return { sl.file_name(), u32(sl.line()), u32(sl.column()) };
    }
};

export using WriteFn = bool (*)(void*, u8 const*, usize);

export struct PanicInfo {
    void const* data;
    bool (*fmt)(void const* data, void* ctx, WriteFn write);
    Location location;
    bool     can_unwind         = true;
    bool     force_no_backtrace = false;
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
    sl_type val;
    consteval SrcLoc(sl_type v = sl_type::current()) noexcept: val(v) {}
};

} // namespace rstd::panic_
