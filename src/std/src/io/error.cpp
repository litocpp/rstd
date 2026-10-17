module;
#include <rstd/macro.hpp>
module rstd.io.error;
#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
import rstd.sys.libc;
namespace libc = rstd::sys::libc;
#endif

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::io::error
{

auto native_error_code() noexcept -> RawOsError {
#if RSTD_OS_UNIX
    return RawOsError(static_cast<rstd::int32_t>(libc::get_errno()));
#elif RSTD_OS_WINDOWS
    return RawOsError(static_cast<rstd::int32_t>(libc::GetLastError()));
#else
    return RawOsError();
#endif
}

auto decode_error_kind(RawOsError error) noexcept -> rstd::io::error::ErrorKind {
    using Kind = rstd::io::error::ErrorKind;
#if RSTD_OS_UNIX
    auto native_error = static_cast<int>(error.to_primitive());
    switch (native_error) {
    case libc::ENOENT: return Kind { Kind::NotFound };
    case libc::EACCES:
    case libc::EPERM: return Kind { Kind::PermissionDenied };
    case libc::ECONNREFUSED: return Kind { Kind::ConnectionRefused };
    case libc::ECONNRESET: return Kind { Kind::ConnectionReset };
    case libc::EHOSTUNREACH: return Kind { Kind::HostUnreachable };
    case libc::ENETUNREACH: return Kind { Kind::NetworkUnreachable };
    case libc::ECONNABORTED: return Kind { Kind::ConnectionAborted };
    case libc::ENOTCONN: return Kind { Kind::NotConnected };
    case libc::EADDRINUSE: return Kind { Kind::AddrInUse };
    case libc::EADDRNOTAVAIL: return Kind { Kind::AddrNotAvailable };
    case libc::ENETDOWN: return Kind { Kind::NetworkDown };
    case libc::EPIPE: return Kind { Kind::BrokenPipe };
    case libc::EEXIST: return Kind { Kind::AlreadyExists };
    case libc::EAGAIN: return Kind { Kind::WouldBlock };
    case libc::ENOTDIR: return Kind { Kind::NotADirectory };
    case libc::EISDIR: return Kind { Kind::IsADirectory };
    case libc::ENOTEMPTY: return Kind { Kind::DirectoryNotEmpty };
    case libc::EROFS: return Kind { Kind::ReadOnlyFilesystem };
    case libc::ELOOP: return Kind { Kind::FilesystemLoop };
    case libc::EINVAL: return Kind { Kind::InvalidInput };
    case libc::ETIMEDOUT: return Kind { Kind::TimedOut };
    case libc::ENOSPC: return Kind { Kind::StorageFull };
    case libc::ESPIPE: return Kind { Kind::NotSeekable };
    case libc::EFBIG: return Kind { Kind::FileTooLarge };
    case libc::EBUSY: return Kind { Kind::ResourceBusy };
    case libc::ETXTBSY: return Kind { Kind::ExecutableFileBusy };
    case libc::EDEADLK: return Kind { Kind::Deadlock };
    case libc::EXDEV: return Kind { Kind::CrossesDevices };
    case libc::EMLINK: return Kind { Kind::TooManyLinks };
    case libc::ENAMETOOLONG: return Kind { Kind::InvalidFilename };
    case libc::E2BIG: return Kind { Kind::ArgumentListTooLong };
    case libc::EINTR: return Kind { Kind::Interrupted };
    case libc::ENOSYS:
    case libc::EOPNOTSUPP:
    case libc::EAFNOSUPPORT: return Kind { Kind::Unsupported };
    case libc::ENOMEM:
    case libc::ENOBUFS: return Kind { Kind::OutOfMemory };
    case libc::EINPROGRESS: return Kind { Kind::InProgress };
    default: break;
    }
    if (native_error == libc::EWOULDBLOCK) return Kind { Kind::WouldBlock };
    if (libc::HAS_ESTALE && native_error == libc::ESTALE) {
        return Kind { Kind::StaleNetworkFileHandle };
    }
    if (libc::HAS_EDQUOT && native_error == libc::EDQUOT) return Kind { Kind::QuotaExceeded };
#elif RSTD_OS_WINDOWS
    switch (static_cast<unsigned long>(error.to_primitive())) {
    case libc::ERROR_FILE_NOT_FOUND:
    case libc::ERROR_PATH_NOT_FOUND: return Kind { Kind::NotFound };
    case libc::ERROR_ACCESS_DENIED: return Kind { Kind::PermissionDenied };
    case libc::ERROR_CONNECTION_REFUSED: return Kind { Kind::ConnectionRefused };
    case libc::ERROR_CONNECTION_ABORTED: return Kind { Kind::ConnectionAborted };
    case libc::ERROR_NETNAME_DELETED: return Kind { Kind::ConnectionReset };
    case libc::ERROR_HOST_UNREACHABLE: return Kind { Kind::HostUnreachable };
    case libc::ERROR_NETWORK_UNREACHABLE: return Kind { Kind::NetworkUnreachable };
    case libc::ERROR_ADDRESS_ALREADY_ASSOCIATED: return Kind { Kind::AddrInUse };
    case libc::ERROR_BROKEN_PIPE:
    case libc::ERROR_NO_DATA: return Kind { Kind::BrokenPipe };
    case libc::ERROR_FILE_EXISTS:
    case libc::ERROR_ALREADY_EXISTS: return Kind { Kind::AlreadyExists };
    case libc::WAIT_TIMEOUT:
    case libc::ERROR_SEM_TIMEOUT: return Kind { Kind::TimedOut };
    case libc::ERROR_INVALID_PARAMETER:
    case libc::ERROR_INVALID_DATA: return Kind { Kind::InvalidInput };
    case libc::ERROR_DIR_NOT_EMPTY: return Kind { Kind::DirectoryNotEmpty };
    case libc::ERROR_DISK_FULL: return Kind { Kind::StorageFull };
    case libc::ERROR_SEEK: return Kind { Kind::NotSeekable };
    case libc::ERROR_NOT_READY:
    case libc::ERROR_BUSY: return Kind { Kind::ResourceBusy };
    case libc::ERROR_POSSIBLE_DEADLOCK: return Kind { Kind::Deadlock };
    case libc::ERROR_NOT_SAME_DEVICE: return Kind { Kind::CrossesDevices };
    case libc::ERROR_TOO_MANY_LINKS: return Kind { Kind::TooManyLinks };
    case libc::ERROR_FILENAME_EXCED_RANGE: return Kind { Kind::InvalidFilename };
    case libc::ERROR_NOT_ENOUGH_MEMORY:
    case libc::ERROR_OUTOFMEMORY: return Kind { Kind::OutOfMemory };
    case libc::ERROR_NOT_SUPPORTED:
    case libc::ERROR_CALL_NOT_IMPLEMENTED: return Kind { Kind::Unsupported };
    case libc::ERROR_LOCK_VIOLATION: return Kind { Kind::WouldBlock };
    case libc::ERROR_IO_PENDING: return Kind { Kind::InProgress };
    case libc::WSAEACCES: return Kind { Kind::PermissionDenied };
    case libc::WSAECONNREFUSED: return Kind { Kind::ConnectionRefused };
    case libc::WSAECONNRESET: return Kind { Kind::ConnectionReset };
    case libc::WSAEHOSTUNREACH: return Kind { Kind::HostUnreachable };
    case libc::WSAENETUNREACH: return Kind { Kind::NetworkUnreachable };
    case libc::WSAECONNABORTED: return Kind { Kind::ConnectionAborted };
    case libc::WSAENOTCONN: return Kind { Kind::NotConnected };
    case libc::WSAEADDRINUSE: return Kind { Kind::AddrInUse };
    case libc::WSAEADDRNOTAVAIL: return Kind { Kind::AddrNotAvailable };
    case libc::WSAENETDOWN: return Kind { Kind::NetworkDown };
    case libc::WSAEWOULDBLOCK: return Kind { Kind::WouldBlock };
    case libc::WSAETIMEDOUT: return Kind { Kind::TimedOut };
    case libc::WSAEINVAL: return Kind { Kind::InvalidInput };
    case libc::WSAENOBUFS: return Kind { Kind::OutOfMemory };
    case libc::WSAEALREADY:
    case libc::WSAEINPROGRESS: return Kind { Kind::InProgress };
    case libc::WSAEOPNOTSUPP: return Kind { Kind::Unsupported };
    default: break;
    }
#endif
    return Kind { Kind::Uncategorized };
}

auto ErrorKind::as_str() const noexcept -> ref<str> {
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

auto Error::last_os_error() noexcept -> Error {
    return from_raw_os_error(native_error_code());
}

auto Error::kind() const noexcept -> ErrorKind {
    switch (tag()) {
    case Tag::Os: return decode_error_kind(as_Os().code);
    case Tag::Kind: return as_Kind().kind;
    case Tag::Message: return as_Message().kind;
    }
    return ErrorKind { ErrorKind::Uncategorized };
}

} // namespace rstd::io::error

namespace rstd
{

// Write a decimal integer directly — avoids needing alloc for int Display.
void write_decimal(rstd::fmt::Formatter& f, i32 n) noexcept {
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

auto Impl<fmt::Display, io::error::ErrorKind>::fmt(fmt::Formatter& f) const -> bool {
    auto s = this->self().as_str();
    return f.write_raw(s.data(), s.size().to_primitive());
}

auto Impl<fmt::Debug, io::error::ErrorKind>::fmt(fmt::Formatter& f) const -> bool {
    auto s = this->self().as_str();
    return f.write_raw(s.data(), s.size().to_primitive());
}

auto Impl<fmt::Display, io::error::Error>::fmt(fmt::Formatter& f) const -> bool {
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

auto Impl<fmt::Debug, io::error::Error>::fmt(fmt::Formatter& f) const -> bool {
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

} // namespace rstd
