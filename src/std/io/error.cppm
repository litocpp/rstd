module;
#include <rstd/enum.hpp>
export module rstd:io.error;
export import rstd.core;

using namespace rstd::literals;

namespace rstd::io::error
{

/// The platform-native OS error code type (e.g. errno on Unix, GetLastError on Windows).
export using RawOsError = i32;

/// A list specifying general categories of I/O error.
export struct ErrorKind {
    enum Entity
    {
        NotFound,
        PermissionDenied,
        ConnectionRefused,
        ConnectionReset,
        HostUnreachable,
        NetworkUnreachable,
        ConnectionAborted,
        NotConnected,
        AddrInUse,
        AddrNotAvailable,
        NetworkDown,
        BrokenPipe,
        AlreadyExists,
        WouldBlock,
        NotADirectory,
        IsADirectory,
        DirectoryNotEmpty,
        ReadOnlyFilesystem,
        FilesystemLoop,
        StaleNetworkFileHandle,
        InvalidInput,
        InvalidData,
        TimedOut,
        WriteZero,
        StorageFull,
        NotSeekable,
        QuotaExceeded,
        FileTooLarge,
        ResourceBusy,
        ExecutableFileBusy,
        Deadlock,
        CrossesDevices,
        TooManyLinks,
        InvalidFilename,
        ArgumentListTooLong,
        Interrupted,
        Unsupported,
        UnexpectedEof,
        OutOfMemory,
        InProgress,
        Other,
        Uncategorized,
    } code;

    using Self = ErrorKind;

    /// Returns a string description of this error kind.
    auto as_str() const noexcept -> ref<str> {
        switch (code) {
        case AddrInUse: return "address in use"_str;
        case AddrNotAvailable: return "address not available"_str;
        case AlreadyExists: return "entity already exists"_str;
        case ArgumentListTooLong: return "argument list too long"_str;
        case BrokenPipe: return "broken pipe"_str;
        case ConnectionAborted: return "connection aborted"_str;
        case ConnectionRefused: return "connection refused"_str;
        case ConnectionReset: return "connection reset"_str;
        case CrossesDevices: return "cross-device link or rename"_str;
        case Deadlock: return "deadlock"_str;
        case DirectoryNotEmpty: return "directory not empty"_str;
        case ExecutableFileBusy: return "executable file busy"_str;
        case FileTooLarge: return "file too large"_str;
        case FilesystemLoop: return "filesystem loop or indirection limit (e.g. symlink loop)"_str;
        case HostUnreachable: return "host unreachable"_str;
        case InProgress: return "in progress"_str;
        case Interrupted: return "operation interrupted"_str;
        case InvalidData: return "invalid data"_str;
        case InvalidFilename: return "invalid filename"_str;
        case InvalidInput: return "invalid input parameter"_str;
        case IsADirectory: return "is a directory"_str;
        case NetworkDown: return "network down"_str;
        case NetworkUnreachable: return "network unreachable"_str;
        case NotADirectory: return "not a directory"_str;
        case NotConnected: return "not connected"_str;
        case NotFound: return "entity not found"_str;
        case NotSeekable: return "seek on unseekable file"_str;
        case Other: return "other error"_str;
        case OutOfMemory: return "out of memory"_str;
        case PermissionDenied: return "permission denied"_str;
        case QuotaExceeded: return "quota exceeded"_str;
        case ReadOnlyFilesystem: return "read-only filesystem or storage medium"_str;
        case ResourceBusy: return "resource busy"_str;
        case StaleNetworkFileHandle: return "stale network file handle"_str;
        case StorageFull: return "no storage space"_str;
        case TimedOut: return "timed out"_str;
        case TooManyLinks: return "too many links"_str;
        case Uncategorized: return "uncategorized error"_str;
        case UnexpectedEof: return "unexpected end of file"_str;
        case Unsupported: return "unsupported"_str;
        case WouldBlock: return "operation would block"_str;
        case WriteZero: return "write zero"_str;
        }
        return ""_str;
    }

    friend bool operator==(ErrorKind a, ErrorKind b) noexcept { return a.code == b.code; }
};

} // namespace rstd::io::error

using namespace rstd::prelude;
using namespace rstd::io::error;

namespace rstd::io::error
{

/// The error type for I/O operations.
export struct Error {
    RSTD_ENUM_DEFAULT(Error,
                      (Kind, ErrorKind { ErrorKind::Uncategorized }),
                      (Os, (RawOsError code;)),
                      (Kind, (ErrorKind kind;)),
                      (Message, (ErrorKind kind; const char* message;)))

    /// Construct from a raw OS error code (errno / GetLastError).
    static auto from_raw_os_error(RawOsError code) noexcept -> Error { return Error::Os(code); }

    /// Construct from the current platform error code.
    static auto last_os_error() noexcept -> Error;

    /// Construct from an ErrorKind.
    static auto from_kind(ErrorKind k) noexcept -> Error { return Error::Kind(k); }

    /// Construct a static-message error (no allocation).
    static constexpr auto new_const(ErrorKind k, const char* msg) noexcept -> Error {
        return Error::Message(k, msg);
    }

    /// Returns the ErrorKind for this error.
    auto kind() const noexcept -> ErrorKind;

    /// Returns the raw OS error code if this error originated from the OS.
    auto raw_os_error() const noexcept -> Option<RawOsError> {
        if (is_Os()) return Some(RawOsError(as_Os().code));
        return None();
    }

    /// Returns the static message if present (Tag::Message only).
    auto static_message() const noexcept -> const char* {
        if (is_Message()) return as_Message().message;
        return nullptr;
    }

    friend bool operator==(const Error& a, const Error& b) noexcept { return a.kind() == b.kind(); }
};

/// Constant error for invalid UTF-8 data in a stream.
export inline constexpr Error Error_INVALID_UTF8 =
    Error::new_const(ErrorKind { ErrorKind::InvalidData }, "stream did not contain valid UTF-8");

/// Constant error returned when `read_exact` encounters an unexpected EOF.
export inline constexpr Error Error_READ_EXACT_EOF =
    Error::new_const(ErrorKind { ErrorKind::UnexpectedEof }, "failed to fill whole buffer");

/// Constant error returned when `write_all` fails to write all bytes.
export inline constexpr Error Error_WRITE_ALL_EOF =
    Error::new_const(ErrorKind { ErrorKind::WriteZero }, "failed to write whole buffer");

} // namespace rstd::io::error

// ── fmt::Display and fmt::Debug impls ─────────────────────────────────────
using namespace rstd::prelude;

// Write a decimal integer directly — avoids needing alloc for int Display.
inline void write_decimal(rstd::fmt::Formatter& f, i32 n) noexcept {
    char           buf[12];
    char*          end = buf + sizeof(buf);
    char*          p   = end;
    auto const     raw = n.to_primitive();
    rstd::uint32_t v   = raw < 0 ? static_cast<rstd::uint32_t>(-static_cast<rstd::int64_t>(raw))
                                 : static_cast<rstd::uint32_t>(raw);
    if (v == 0) {
        f.write_raw("0", 1);
        return;
    }
    while (v > 0) {
        *--p = char('0' + v % 10);
        v /= 10;
    }
    if (raw < 0) *--p = '-';
    f.write_raw(p, static_cast<rstd::size_t>(end - p));
}
namespace rstd
{

template<>
struct Impl<fmt::Display, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self().as_str();
        return f.write_raw(s.data(), s.size().to_primitive());
    }
};

template<>
struct Impl<fmt::Debug, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self().as_str();
        return f.write_raw(s.data(), s.size().to_primitive());
    }
};

template<>
struct Impl<fmt::Display, io::error::Error> : ImplBase<io::error::Error> {
    auto fmt(fmt::Formatter& f) const -> bool {
        using Tag = io::error::Error::Tag;
        auto& e   = this->self();
        switch (e.tag()) {
        case Tag::Os: {
            // "entity not found (os error 2)"
            auto k        = e.kind();
            auto kind_str = k.as_str();
            f.write_raw(kind_str.data(), kind_str.size().to_primitive());
            const char prefix[] = " (os error ";
            f.write_raw(prefix, sizeof(prefix) - 1);
            write_decimal(f, e.raw_os_error().unwrap_unchecked());
            return f.write_raw(")", 1);
        }
        case Tag::Kind: {
            auto k = e.kind();
            auto s = k.as_str();
            return f.write_raw(s.data(), s.size().to_primitive());
        }
        case Tag::Message: {
            auto msg = e.static_message();
            return f.write_raw(msg, rstd::strlen(msg));
        }
        }
        return false;
    }
};

template<>
struct Impl<fmt::Debug, io::error::Error> : ImplBase<io::error::Error> {
    auto fmt(fmt::Formatter& f) const -> bool {
        using Tag = io::error::Error::Tag;
        auto& e   = this->self();
        switch (e.tag()) {
        case Tag::Os: {
            const char prefix[] = "Os(";
            f.write_raw(prefix, sizeof(prefix) - 1);
            write_decimal(f, e.raw_os_error().unwrap_unchecked());
            return f.write_raw(")", 1);
        }
        case Tag::Kind: {
            const char prefix[] = "Kind(";
            f.write_raw(prefix, sizeof(prefix) - 1);
            auto k = e.kind();
            as<fmt::Display>(k).fmt(f);
            return f.write_raw(")", 1);
        }
        case Tag::Message: {
            const char prefix[] = "Error { kind: ";
            f.write_raw(prefix, sizeof(prefix) - 1);
            auto k = e.kind();
            as<fmt::Display>(k).fmt(f);
            const char sep[] = ", message: \"";
            f.write_raw(sep, sizeof(sep) - 1);
            auto msg = e.static_message();
            f.write_raw(msg, rstd::strlen(msg));
            return f.write_raw("\" }", 3);
        }
        }
        return false;
    }
};

template<>
struct Impl<error::Error, io::error::Error> : DefaultInImpl<error::Error, io::error::Error> {};

} // namespace rstd

namespace rstd::io
{
/// A specialized Result type for I/O operations.
/// \tparam T The success value type.
export template<typename T>
using Result = result::Result<T, error::Error>;

} // namespace rstd::io
