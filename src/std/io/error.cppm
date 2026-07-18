module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
export module rstd:io.error;
export import rstd.core;

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
        case AddrInUse: return "address in use";
        case AddrNotAvailable: return "address not available";
        case AlreadyExists: return "entity already exists";
        case ArgumentListTooLong: return "argument list too long";
        case BrokenPipe: return "broken pipe";
        case ConnectionAborted: return "connection aborted";
        case ConnectionRefused: return "connection refused";
        case ConnectionReset: return "connection reset";
        case CrossesDevices: return "cross-device link or rename";
        case Deadlock: return "deadlock";
        case DirectoryNotEmpty: return "directory not empty";
        case ExecutableFileBusy: return "executable file busy";
        case FileTooLarge: return "file too large";
        case FilesystemLoop: return "filesystem loop or indirection limit (e.g. symlink loop)";
        case HostUnreachable: return "host unreachable";
        case InProgress: return "in progress";
        case Interrupted: return "operation interrupted";
        case InvalidData: return "invalid data";
        case InvalidFilename: return "invalid filename";
        case InvalidInput: return "invalid input parameter";
        case IsADirectory: return "is a directory";
        case NetworkDown: return "network down";
        case NetworkUnreachable: return "network unreachable";
        case NotADirectory: return "not a directory";
        case NotConnected: return "not connected";
        case NotFound: return "entity not found";
        case NotSeekable: return "seek on unseekable file";
        case Other: return "other error";
        case OutOfMemory: return "out of memory";
        case PermissionDenied: return "permission denied";
        case QuotaExceeded: return "quota exceeded";
        case ReadOnlyFilesystem: return "read-only filesystem or storage medium";
        case ResourceBusy: return "resource busy";
        case StaleNetworkFileHandle: return "stale network file handle";
        case StorageFull: return "no storage space";
        case TimedOut: return "timed out";
        case TooManyLinks: return "too many links";
        case Uncategorized: return "uncategorized error";
        case UnexpectedEof: return "unexpected end of file";
        case Unsupported: return "unsupported";
        case WouldBlock: return "operation would block";
        case WriteZero: return "write zero";
        }
        return "";
    }

    friend bool operator==(ErrorKind a, ErrorKind b) noexcept { return a.code == b.code; }
};

} // namespace rstd::io::error

using namespace rstd::prelude;
using namespace rstd::io::error;

namespace rstd::io::error
{

#define RSTD_IO_ERROR_VARIANTS(V) \
    V(Os, (RawOsError code;))     \
    V(Kind, (ErrorKind kind;))    \
    V(Message, (ErrorKind kind; const char* message;))

/// The error type for I/O operations.
export struct Error {
private:
    RSTD_ENUM_SELF(Error)
    RSTD_ENUM_VARIANT_COUNT(RSTD_IO_ERROR_VARIANTS)
    RSTD_ENUM_INDEX_TYPE()
    RSTD_ENUM_PAYLOAD_TYPES(RSTD_IO_ERROR_VARIANTS)

public:
    RSTD_ENUM_TAG_TYPE(RSTD_IO_ERROR_VARIANTS)

private:
    RSTD_ENUM_DEFAULT_STORAGE(RSTD_IO_ERROR_VARIANTS)
    RSTD_ENUM_STORAGE(Error)
    RSTD_ENUM_ACCESSORS(RSTD_IO_ERROR_VARIANTS)

public:
    constexpr Error() noexcept: RSTD_ENUM_INIT(Kind, ErrorKind { ErrorKind::Uncategorized }) {}

    /// Construct from a raw OS error code (errno / GetLastError).
    static auto from_raw_os_error(RawOsError code) noexcept -> Error {
        return Error(RSTD_ENUM_IN_PLACE(Os), code);
    }

    /// Construct from the current platform error code.
    static auto last_os_error() noexcept -> Error;

    /// Construct from an ErrorKind.
    static auto from_kind(ErrorKind k) noexcept -> Error {
        return Error(RSTD_ENUM_IN_PLACE(Kind), k);
    }

    /// Construct a static-message error (no allocation).
    static constexpr auto new_const(ErrorKind k, const char* msg) noexcept -> Error {
        return Error(RSTD_ENUM_IN_PLACE(Message), k, msg);
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

    /// Returns the internal tag indicating the error representation.
    RSTD_ENUM_OBSERVERS()

    friend bool operator==(const Error& a, const Error& b) noexcept { return a.kind() == b.kind(); }
};

#undef RSTD_IO_ERROR_VARIANTS

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
    char  buf[12];
    char* end = buf + sizeof(buf);
    char* p   = end;
    u32   v   = (n < 0) ? u32(i64(0) - i64(n)) : u32(n);
    if (v == 0) {
        f.write_raw((const u8*)"0", 1);
        return;
    }
    while (v > 0) {
        *--p = char('0' + v % 10);
        v /= 10;
    }
    if (n < 0) *--p = '-';
    f.write_raw((const u8*)p, usize(end - p));
}
namespace rstd
{

template<>
struct Impl<fmt::Display, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self().as_str();
        return f.write_raw(s.data(), s.size());
    }
};

template<>
struct Impl<fmt::Debug, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self().as_str();
        return f.write_raw(s.data(), s.size());
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
            f.write_raw(kind_str.data(), kind_str.size());
            const char prefix[] = " (os error ";
            f.write_raw((const u8*)prefix, sizeof(prefix) - 1);
            write_decimal(f, e.raw_os_error().unwrap_unchecked());
            return f.write_raw((const u8*)")", 1);
        }
        case Tag::Kind: {
            auto k = e.kind();
            auto s = k.as_str();
            return f.write_raw(s.data(), s.size());
        }
        case Tag::Message: {
            auto msg = e.static_message();
            return f.write_raw((const u8*)msg, rstd::strlen(msg));
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
            f.write_raw((const u8*)prefix, sizeof(prefix) - 1);
            write_decimal(f, e.raw_os_error().unwrap_unchecked());
            return f.write_raw((const u8*)")", 1);
        }
        case Tag::Kind: {
            const char prefix[] = "Kind(";
            f.write_raw((const u8*)prefix, sizeof(prefix) - 1);
            auto k = e.kind();
            as<fmt::Display>(k).fmt(f);
            return f.write_raw((const u8*)")", 1);
        }
        case Tag::Message: {
            const char prefix[] = "Error { kind: ";
            f.write_raw((const u8*)prefix, sizeof(prefix) - 1);
            auto k = e.kind();
            as<fmt::Display>(k).fmt(f);
            const char sep[] = ", message: \"";
            f.write_raw((const u8*)sep, sizeof(sep) - 1);
            auto msg = e.static_message();
            f.write_raw((const u8*)msg, rstd::strlen(msg));
            return f.write_raw((const u8*)"\" }", 3);
        }
        }
        return false;
    }
};

} // namespace rstd

namespace rstd::io
{
/// A specialized Result type for I/O operations.
/// \tparam T The success value type.
export template<typename T>
using Result = result::Result<T, error::Error>;

} // namespace rstd::io
