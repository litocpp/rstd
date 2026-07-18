module;
#include <rstd/macro.hpp>
module rstd;
import :os.fd;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#endif

namespace rstd::os::fd
{

void OwnedFd::close_() noexcept {
    if (fd_ == INVALID_RAW_FD) return;
#if RSTD_OS_UNIX
    rstd::sys::libc::close(fd_);
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
        return Err(Error::from_raw_os_error(rstd::sys::libc::get_errno()));
    }
    return Ok(OwnedFd::from_raw_fd(new_fd));
#else
    return Err(
        Error::from_kind(rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::os::fd
