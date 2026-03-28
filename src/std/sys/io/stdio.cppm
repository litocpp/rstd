module;
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#  include <unistd.h>
#  include <errno.h>
#endif
#if RSTD_OS_WINDOWS
#  include <windows.h>
#endif
export module rstd:sys.io.stdio;
export import :io.error;
export import rstd.core;

namespace rstd::sys::io::stdio
{

using rstd::io::Result;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

#if RSTD_OS_UNIX

/// Write `len` bytes from `buf` to file descriptor `fd`.
/// Automatically retries on EINTR.  Returns bytes written.
export auto write_fd(int fd, const u8* buf, usize len) noexcept -> Result<usize> {
    while (true) {
        auto n = ::write(fd, buf, len);
        if (n >= 0) return Ok(usize(n));
        auto err = errno;
        if (err == EINTR) continue;
        return Err(Error::from_raw_os_error(err));
    }
}

/// Read up to `len` bytes from file descriptor `fd` into `buf`.
/// Automatically retries on EINTR.  Returns bytes read (0 = EOF).
export auto read_fd(int fd, u8* buf, usize len) noexcept -> Result<usize> {
    while (true) {
        auto n = ::read(fd, buf, len);
        if (n >= 0) return Ok(usize(n));
        auto err = errno;
        if (err == EINTR) continue;
        return Err(Error::from_raw_os_error(err));
    }
}

#elif RSTD_OS_WINDOWS

export auto write_fd(int fd, const u8* buf, usize len) noexcept -> Result<usize> {
    HANDLE h;
    switch (fd) {
    case 1: h = GetStdHandle(STD_OUTPUT_HANDLE); break;
    case 2: h = GetStdHandle(STD_ERROR_HANDLE);  break;
    default:
        return Err(Error::from_kind(
            ErrorKind { ErrorKind::InvalidInput }));
    }
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        return Err(Error::from_raw_os_error(
            static_cast<rstd::io::error::RawOsError>(GetLastError())));
    }
    DWORD written = 0;
    if (!WriteFile(h, buf, static_cast<DWORD>(len), &written, nullptr)) {
        return Err(Error::from_raw_os_error(
            static_cast<rstd::io::error::RawOsError>(GetLastError())));
    }
    return Ok(usize(written));
}

export auto read_fd(int fd, u8* buf, usize len) noexcept -> Result<usize> {
    if (fd != 0) {
        return Err(Error::from_kind(
            ErrorKind { ErrorKind::InvalidInput }));
    }
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        return Err(Error::from_raw_os_error(
            static_cast<rstd::io::error::RawOsError>(GetLastError())));
    }
    DWORD read_bytes = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(len), &read_bytes, nullptr)) {
        return Err(Error::from_raw_os_error(
            static_cast<rstd::io::error::RawOsError>(GetLastError())));
    }
    return Ok(usize(read_bytes));
}

#else

export auto write_fd(int, const u8*, usize) noexcept -> Result<usize> {
    return Err(Error::from_kind(
        ErrorKind { ErrorKind::Unsupported }));
}

export auto read_fd(int, u8*, usize) noexcept -> Result<usize> {
    return Err(Error::from_kind(
        ErrorKind { ErrorKind::Unsupported }));
}

#endif

} // namespace rstd::sys::io::stdio
