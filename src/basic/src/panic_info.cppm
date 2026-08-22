export module rstd.basic:panic_info;
export import :prelude;

namespace rstd::panic_
{
// Cross-ABI-safe location type.
// Extends Rust's core::panic::Location (file/line/col) with function_name.
// POD struct — safe to pass across extern "C" boundaries.
/// A cross-ABI-safe source location, analogous to Rust's `core::panic::Location`.
export struct Location {
    const char* _file;
    const char* _function;
    uint32_t    _line;
    uint32_t    _col;

    /// Returns the file name where this location was captured.
    constexpr auto file_name() const noexcept -> const char* { return _file; }
    /// Returns the compiler-defined function name.
    constexpr auto function_name() const noexcept -> const char* { return _function; }
    /// Returns the line number.
    constexpr auto line() const noexcept -> uint32_t { return _line; }
    /// Returns the column number.
    constexpr auto column() const noexcept -> uint32_t { return _col; }

    /// Creates a `Location` from a `source_location`.
    static constexpr auto from(rstd::source_location sl) noexcept -> Location {
        return { sl.file_name(),
                 sl.function_name(),
                 static_cast<uint32_t>(sl.line()),
                 static_cast<uint32_t>(sl.column()) };
    }
};

/// Function pointer type for writing panic message bytes.
export using WriteFn = bool (*)(void*, uint8_t const*, size_t);

/// Carries information about a panic, analogous to Rust's `core::panic::PanicInfo`.
export struct PanicInfo {
    /// Opaque pointer to the panic payload.
    void const* data;
    /// Formats the panic payload by writing bytes through `write`.
    bool (*fmt)(void const* data, void* ctx, WriteFn write);
    /// The source location where the panic originated.
    Location location;
    /// Whether the panic is allowed to unwind the stack.
    bool can_unwind = true;
    /// Whether to suppress backtrace generation.
    bool force_no_backtrace = false;
};

/// Consteval wrapper for `source_location` to capture the caller's location as a default parameter.
export struct SrcLoc {
    /// The captured source location value.
    rstd::source_location val;
    /// Implicitly captures the caller's source location at compile time.
    consteval SrcLoc(rstd::source_location value = rstd::source_location::current()) noexcept
        : val(value) {}
};

} // namespace rstd::panic_
