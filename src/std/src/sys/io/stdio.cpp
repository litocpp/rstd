module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#include <winternl.h>
#endif
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

#if RSTD_OS_WINDOWS
auto validate_async_pipe(os::handle::BorrowedHandle handle, bool writing) noexcept
    -> IOResult<empty> {
    auto  native = static_cast<HANDLE>(handle.as_raw_handle());
    DWORD flags {};
    if (GetFileType(native) != FILE_TYPE_PIPE ||
        ! GetNamedPipeInfo(native, &flags, nullptr, nullptr, nullptr) ||
        (flags & PIPE_TYPE_MESSAGE) != 0)
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    IO_STATUS_BLOCK status {};
    struct Access {
        ACCESS_MASK value;
    } access {};
    struct Mode {
        ULONG value;
    } mode {};
    auto query = [&](void* out, ULONG length, int kind) {
        return NtQueryInformationFile(native, &status, out, length, kind);
    };
    // The user-mode SDK omits these FILE_INFORMATION_CLASS names.
    constexpr int file_access_information = 8;
    constexpr int file_mode_information   = 16;
    auto          result                  = query(&access, sizeof(access), file_access_information);
    if (result >= 0) result = query(&mode, sizeof(mode), file_mode_information);
    if (result < 0) return Err(Error::from_raw_os_error(i32(RtlNtStatusToDosError(result))));
    // FileModeInformation reports the synchronous flags even for unnamed pipes.
    constexpr ULONG synchronous_io = 0x10 | 0x20;
    if ((access.value & (writing ? FILE_WRITE_DATA : FILE_READ_DATA)) == 0 ||
        (mode.value & synchronous_io) != 0)
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return Ok(empty {});
}
#endif

auto write_pipe_fd(int fd, slice<byte> buf) noexcept -> IOResult<usize> {
#if RSTD_OS_UNIX
    libc::sigset_t blocked {}, previous {}, pending {};
    libc::sigemptyset(&blocked);
    libc::sigaddset(&blocked, libc::signal_pipe);
    auto mask_error = libc::pthread_sigmask(libc::signal_block, &blocked, &previous);
    if (mask_error) return Err(Error::from_raw_os_error(i32(mask_error)));
    if (libc::sigpending(&pending) < 0) {
        auto error = libc::get_errno();
        (void)libc::pthread_sigmask(libc::signal_setmask, &previous, nullptr);
        return Err(Error::from_raw_os_error(i32(error)));
    }
    auto already_pending = libc::sigismember(&pending, libc::signal_pipe) == 1;
    auto result          = write_fd(fd, buf);
    if (! already_pending && libc::sigpending(&pending) == 0 &&
        libc::sigismember(&pending, libc::signal_pipe) == 1) {
        int received = 0;
        (void)libc::sigwait(&blocked, &received);
    }
    mask_error = libc::pthread_sigmask(libc::signal_setmask, &previous, nullptr);
    if (mask_error) return Err(Error::from_raw_os_error(i32(mask_error)));
    return result;
#else
    return write_fd(fd, buf);
#endif
}

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
