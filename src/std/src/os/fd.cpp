module;
#include <rstd/macro.hpp>
module rstd;
import :os.fd;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#elif RSTD_OS_WINDOWS
import :sys.libc.windows;
#endif

namespace rstd::os::fd
{

void OwnedFd::close_() noexcept {
    if (fd_ == INVALID_RAW_FD) return;
#if RSTD_OS_UNIX
    rstd::sys::libc::close(fd_);
#elif RSTD_OS_WINDOWS
    (void)rstd::sys::libc::CloseHandle(static_cast<rstd::sys::libc::HANDLE>(fd_));
#endif
    fd_ = INVALID_RAW_FD;
}

OwnedFd::~OwnedFd() {
    close_();
}

auto BorrowedFd::try_clone_to_owned() const -> Result<OwnedFd> {
#if RSTD_OS_UNIX
    auto new_fd = rstd::sys::libc::fcntl(fd_, rstd::sys::libc::F_DUPFD_CLOEXEC, 3);
    if (new_fd < 0) {
        return Err(Error::from_raw_os_error(i32(rstd::sys::libc::get_errno())));
    }
    return Ok(OwnedFd::from_raw_fd(new_fd));
#elif RSTD_OS_WINDOWS
    auto duplicate = rstd::sys::libc::M_INVALID_HANDLE_VALUE;
    auto process   = rstd::sys::libc::GetCurrentProcess();
    if (! rstd::sys::libc::DuplicateHandle(process,
                                           static_cast<rstd::sys::libc::HANDLE>(fd_),
                                           process,
                                           &duplicate,
                                           0,
                                           rstd::sys::libc::M_FALSE,
                                           rstd::sys::libc::DUPLICATE_SAME_ACCESS)) {
        return Err(Error::from_raw_os_error(i32(rstd::sys::libc::GetLastError())));
    }
    return Ok(OwnedFd::from_raw_fd(static_cast<RawFd>(duplicate)));
#else
    return Err(
        Error::from_kind(rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::os::fd
