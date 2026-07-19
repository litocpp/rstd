module;
#include <rstd/macro.hpp>
export module rstd:sys.io;
import :io.error;
import :sys.libc;

namespace libc = rstd::sys::libc;

export namespace rstd::sys::io
{

using RawOsError = i32;

auto last_os_error() noexcept -> RawOsError {
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
    case libc::ERROR_IO_PENDING: return Kind { Kind::InProgress };
    default: break;
    }
#endif
    return Kind { Kind::Uncategorized };
}

} // namespace rstd::sys::io
