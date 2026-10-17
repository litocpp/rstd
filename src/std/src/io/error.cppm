module;
#include <rstd/enum.hpp>
export module rstd.io.error;
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
    auto as_str() const noexcept -> ref<str>;

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

namespace rstd
{

template<>
struct Impl<fmt::Display, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool;
};

template<>
struct Impl<fmt::Debug, io::error::ErrorKind> : ImplBase<io::error::ErrorKind> {
    auto fmt(fmt::Formatter& f) const -> bool;
};

template<>
struct Impl<fmt::Display, io::error::Error> : ImplBase<io::error::Error> {
    auto fmt(fmt::Formatter& f) const -> bool;
};

template<>
struct Impl<fmt::Debug, io::error::Error> : ImplBase<io::error::Error> {
    auto fmt(fmt::Formatter& f) const -> bool;
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
