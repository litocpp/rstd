module;
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#  include <errno.h>
#endif
export module rstd:io.error;
export import :sys.io;
export import rstd.core;

namespace rstd::io::error
{

export using rstd::sys::io::RawOsError;

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

    auto as_str() const noexcept -> ref<str> {
        switch (code) {
        case AddrInUse:             return "address in use";
        case AddrNotAvailable:      return "address not available";
        case AlreadyExists:         return "entity already exists";
        case ArgumentListTooLong:   return "argument list too long";
        case BrokenPipe:            return "broken pipe";
        case ConnectionAborted:     return "connection aborted";
        case ConnectionRefused:     return "connection refused";
        case ConnectionReset:       return "connection reset";
        case CrossesDevices:        return "cross-device link or rename";
        case Deadlock:              return "deadlock";
        case DirectoryNotEmpty:     return "directory not empty";
        case ExecutableFileBusy:    return "executable file busy";
        case FileTooLarge:          return "file too large";
        case FilesystemLoop:        return "filesystem loop or indirection limit (e.g. symlink loop)";
        case HostUnreachable:       return "host unreachable";
        case InProgress:            return "in progress";
        case Interrupted:           return "operation interrupted";
        case InvalidData:           return "invalid data";
        case InvalidFilename:       return "invalid filename";
        case InvalidInput:          return "invalid input parameter";
        case IsADirectory:          return "is a directory";
        case NetworkDown:           return "network down";
        case NetworkUnreachable:    return "network unreachable";
        case NotADirectory:         return "not a directory";
        case NotConnected:          return "not connected";
        case NotFound:              return "entity not found";
        case NotSeekable:           return "seek on unseekable file";
        case Other:                 return "other error";
        case OutOfMemory:           return "out of memory";
        case PermissionDenied:      return "permission denied";
        case QuotaExceeded:         return "quota exceeded";
        case ReadOnlyFilesystem:    return "read-only filesystem or storage medium";
        case ResourceBusy:          return "resource busy";
        case StaleNetworkFileHandle:return "stale network file handle";
        case StorageFull:           return "no storage space";
        case TimedOut:              return "timed out";
        case TooManyLinks:          return "too many links";
        case Uncategorized:         return "uncategorized error";
        case UnexpectedEof:         return "unexpected end of file";
        case Unsupported:           return "unsupported";
        case WouldBlock:            return "operation would block";
        case WriteZero:             return "write zero";
        }
        return "";
    }

    friend bool operator==(ErrorKind a, ErrorKind b) noexcept { return a.code == b.code; }
};

namespace detail
{

#if RSTD_OS_UNIX
[[gnu::always_inline]] inline
auto decode_error_kind(RawOsError err) noexcept -> ErrorKind {
    using EK = ErrorKind;
    switch (err) {
    case ENOENT:       return EK { EK::NotFound };
    case EACCES:
    case EPERM:        return EK { EK::PermissionDenied };
    case ECONNREFUSED: return EK { EK::ConnectionRefused };
    case ECONNRESET:   return EK { EK::ConnectionReset };
    case EHOSTUNREACH: return EK { EK::HostUnreachable };
    case ENETUNREACH:  return EK { EK::NetworkUnreachable };
    case ECONNABORTED: return EK { EK::ConnectionAborted };
    case ENOTCONN:     return EK { EK::NotConnected };
    case EADDRINUSE:   return EK { EK::AddrInUse };
    case EADDRNOTAVAIL:return EK { EK::AddrNotAvailable };
    case ENETDOWN:     return EK { EK::NetworkDown };
    case EPIPE:        return EK { EK::BrokenPipe };
    case EEXIST:       return EK { EK::AlreadyExists };
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
    case EAGAIN:       return EK { EK::WouldBlock };
    case ENOTDIR:      return EK { EK::NotADirectory };
    case EISDIR:       return EK { EK::IsADirectory };
    case ENOTEMPTY:    return EK { EK::DirectoryNotEmpty };
    case EROFS:        return EK { EK::ReadOnlyFilesystem };
    case ELOOP:        return EK { EK::FilesystemLoop };
#ifdef ESTALE
    case ESTALE:       return EK { EK::StaleNetworkFileHandle };
#endif
    case EINVAL:       return EK { EK::InvalidInput };
    case ETIMEDOUT:    return EK { EK::TimedOut };
    case ENOSPC:       return EK { EK::StorageFull };
    case ESPIPE:       return EK { EK::NotSeekable };
#ifdef EDQUOT
    case EDQUOT:       return EK { EK::QuotaExceeded };
#endif
    case EFBIG:        return EK { EK::FileTooLarge };
    case EBUSY:        return EK { EK::ResourceBusy };
    case ETXTBSY:      return EK { EK::ExecutableFileBusy };
    case EDEADLK:      return EK { EK::Deadlock };
    case EXDEV:        return EK { EK::CrossesDevices };
    case EMLINK:       return EK { EK::TooManyLinks };
    case ENAMETOOLONG: return EK { EK::InvalidFilename };
    case E2BIG:        return EK { EK::ArgumentListTooLong };
    case EINTR:        return EK { EK::Interrupted };
    case ENOSYS:
    case EOPNOTSUPP:
    case EAFNOSUPPORT: return EK { EK::Unsupported };
    case ENOMEM:
    case ENOBUFS:      return EK { EK::OutOfMemory };
    case EINPROGRESS:  return EK { EK::InProgress };
    default:           return EK { EK::Uncategorized };
    }
}
#else
[[gnu::always_inline]] inline
auto decode_error_kind(RawOsError) noexcept -> ErrorKind {
    return ErrorKind { ErrorKind::Uncategorized };
}
#endif

} // namespace detail

/// The error type for I/O operations.
export struct Error {
    enum class Tag : u8 { Os, Kind, Message };

private:
    Tag        tag_     { Tag::Kind };
    RawOsError os_code_ { 0 };
    ErrorKind  kind_    { ErrorKind::Uncategorized };
    const char* msg_    { nullptr };

public:
    /// Construct from a raw OS error code (errno / GetLastError).
    static auto from_raw_os_error(RawOsError code) noexcept -> Error {
        Error e;
        e.tag_     = Tag::Os;
        e.os_code_ = code;
        return e;
    }

    /// Construct from an ErrorKind.
    static auto from_kind(ErrorKind k) noexcept -> Error {
        Error e;
        e.tag_   = Tag::Kind;
        e.kind_  = k;
        return e;
    }

    /// Construct a static-message error (no allocation).
    static constexpr auto new_const(ErrorKind k, const char* msg) noexcept -> Error {
        Error e;
        e.tag_  = Tag::Message;
        e.kind_ = k;
        e.msg_  = msg;
        return e;
    }

    /// Returns the ErrorKind for this error.
    auto kind() const noexcept -> ErrorKind {
        switch (tag_) {
        case Tag::Os:      return detail::decode_error_kind(os_code_);
        case Tag::Kind:    return kind_;
        case Tag::Message: return kind_;
        }
        return ErrorKind { ErrorKind::Uncategorized };
    }

    /// Returns the raw OS error code if this error originated from the OS.
    auto raw_os_error() const noexcept -> Option<RawOsError> {
        if (tag_ == Tag::Os) return Some(RawOsError(os_code_));
        return None();
    }

    /// Returns the static message if present (Tag::Message only).
    auto static_message() const noexcept -> const char* {
        if (tag_ == Tag::Message) return msg_;
        return nullptr;
    }

    auto tag() const noexcept -> Tag { return tag_; }

    friend bool operator==(const Error& a, const Error& b) noexcept {
        return a.kind() == b.kind();
    }
};

/// Static error constants mirroring Rust's io::Error constants.
export inline constexpr Error Error_INVALID_UTF8 =
    Error::new_const(ErrorKind { ErrorKind::InvalidData }, "stream did not contain valid UTF-8");

export inline constexpr Error Error_READ_EXACT_EOF =
    Error::new_const(ErrorKind { ErrorKind::UnexpectedEof }, "failed to fill whole buffer");

export inline constexpr Error Error_WRITE_ALL_EOF =
    Error::new_const(ErrorKind { ErrorKind::WriteZero }, "failed to write whole buffer");

} // namespace rstd::io::error

// ── fmt::Display and fmt::Debug impls ─────────────────────────────────────
namespace rstd::io::error::detail
{
// Write a decimal integer directly — avoids needing alloc for int Display.
inline void write_decimal(fmt::Formatter& f, i32 n) noexcept {
    char buf[12];
    char* end = buf + sizeof(buf);
    char* p   = end;
    u32   v   = (n < 0) ? u32(i64(0) - i64(n)) : u32(n);
    if (v == 0) { f.write_raw((const u8*)"0", 1); return; }
    while (v > 0) { *--p = char('0' + v % 10); v /= 10; }
    if (n < 0) *--p = '-';
    f.write_raw((const u8*)p, usize(end - p));
}
} // namespace rstd::io::error::detail

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
        using io::error::detail::write_decimal;
        auto& e = this->self();
        switch (e.tag()) {
        case Tag::Os: {
            // "entity not found (os error 2)"
            auto k = e.kind();
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
        using io::error::detail::write_decimal;
        auto& e = this->self();
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
export template<typename T>
using Result = result::Result<T, error::Error>;

} // namespace rstd::io
