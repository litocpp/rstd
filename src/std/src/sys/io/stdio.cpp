module;
#include <rstd/macro.hpp>
module rstd.sys.io.stdio;
import rstd.sys.libc;

using rstd::mem::MaybeUninit;

#if RSTD_OS_WINDOWS
using namespace rstd::sys::libc;
#endif

template<typename T>
using IOResult = rstd::io::Result<T>;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
namespace libc = rstd::sys::libc;

namespace rstd::sys::io::stdio
{

#if RSTD_OS_UNIX

/// Writes bytes to file descriptor `fd`, retrying on EINTR.
auto write_fd(int fd, slice<byte> buf) noexcept -> IOResult<usize> {
    while (true) {
        auto n = libc::write(fd, buf.as_raw_ptr(), buf.len().to_primitive());
        if (n >= 0) return Ok(usize(n));
        auto err = libc::get_errno();
        if (err == libc::EINTR) continue;
        return Err(Error::from_raw_os_error(rstd::io::error::RawOsError(
            static_cast<rstd::io::error::RawOsError::primitive_type>(err))));
    }
}

/// Reads bytes from file descriptor `fd`, retrying on EINTR.
template<typename Byte>
auto read_fd_impl(int fd, mut_ref<Byte[]> buf) noexcept -> IOResult<usize> {
    while (true) {
        auto n = libc::read(fd, buf.as_raw_ptr(), buf.len().to_primitive());
        if (n >= 0) return Ok(usize(n));
        auto err = libc::get_errno();
        if (err == libc::EINTR) continue;
        return Err(Error::from_raw_os_error(rstd::io::error::RawOsError(
            static_cast<rstd::io::error::RawOsError::primitive_type>(err))));
    }
}

#elif RSTD_OS_WINDOWS

auto write_fd(int fd, slice<byte> buf) noexcept -> IOResult<usize> {
    auto   raw = _get_osfhandle(fd);
    HANDLE h   = raw == -1 ? M_INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(raw);
    if (h == M_INVALID_HANDLE_VALUE || h == nullptr) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    DWORD written = 0;
    auto  length  = buf.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    if (! WriteFile(h, buf.as_raw_ptr(), static_cast<DWORD>(length), &written, nullptr)) {
        return Err(
            Error::from_raw_os_error(static_cast<rstd::io::error::RawOsError>(GetLastError())));
    }
    return Ok(usize(written));
}

template<typename Byte>
auto read_fd_impl(int fd, mut_ref<Byte[]> buf) noexcept -> IOResult<usize> {
    auto   raw = _get_osfhandle(fd);
    HANDLE h   = raw == -1 ? M_INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(raw);
    if (h == M_INVALID_HANDLE_VALUE || h == nullptr) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    DWORD read_bytes = 0;
    auto  length     = buf.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    if (! ReadFile(h, buf.as_raw_ptr(), static_cast<DWORD>(length), &read_bytes, nullptr)) {
        auto error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) return Ok(usize());
        return Err(Error::from_raw_os_error(static_cast<rstd::io::error::RawOsError>(error)));
    }
    return Ok(usize(read_bytes));
}

#else

auto write_fd(int, slice<byte>) noexcept -> IOResult<usize> {
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
}

template<typename Byte>
auto read_fd_impl(int, mut_ref<Byte[]>) noexcept -> IOResult<usize> {
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
}

#endif

auto read_fd(int fd, mut_ref<MaybeUninit<byte>[]> buf) noexcept -> IOResult<usize> {
    return read_fd_impl(fd, buf);
}
auto read_fd(int fd, mut_ref<byte[]> buf) noexcept -> IOResult<usize> {
    return read_fd_impl(fd, buf);
}

#if RSTD_OS_UNIX
auto is_terminal_fd(int fd) noexcept -> bool {
    return libc::isatty(fd) != 0;
}
#elif RSTD_OS_WINDOWS
auto is_terminal_fd(int fd) noexcept -> bool {
    HANDLE h;
    switch (fd) {
    case 0: h = GetStdHandle(M_STD_INPUT_HANDLE); break;
    case 1: h = GetStdHandle(M_STD_OUTPUT_HANDLE); break;
    case 2: h = GetStdHandle(M_STD_ERROR_HANDLE); break;
    default: return false;
    }
    if (h == M_INVALID_HANDLE_VALUE || h == nullptr) return false;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
}
#else
auto is_terminal_fd(int) noexcept -> bool {
    return false;
}
#endif

} // namespace rstd::sys::io::stdio
