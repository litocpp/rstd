module;
#include <rstd/macro.hpp>
module rstd;
#if RSTD_OS_LINUX
import :sys.process.linux;
import :sys.libc;

using namespace rstd::prelude;

auto pidfd_status(int fd, bool nonblocking) -> rstd::io::Result<Option<rstd::process::ExitStatus>> {
    auto result = rstd::sys::process::pidfd_wait(fd, nonblocking);
    if (result.error) return Err(rstd::io::error::Error::from_raw_os_error(i32(result.error)));
    if (! result.available) return Ok(None());
    return Ok(Some(result.signaled ? rstd::process::ExitStatus::from_signal(i32(result.status))
                                   : rstd::process::ExitStatus::from_code(i32(result.status))));
}

auto rstd::os::linux::process::PidFd::kill() const -> io::Result<empty> {
    auto error = sys::process::pidfd_send_signal(as_raw_fd(), sys::libc::SIGKILL, false);
    if (error) return Err(io::error::Error::from_raw_os_error(i32(error)));
    return Ok(empty {});
}

auto rstd::os::linux::process::PidFd::wait() const -> io::Result<rstd::process::ExitStatus> {
    auto result = pidfd_status(as_raw_fd(), false);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    return Ok(result.unwrap().unwrap());
}

auto rstd::os::linux::process::PidFd::try_wait() const
    -> io::Result<Option<rstd::process::ExitStatus>> {
    return pidfd_status(as_raw_fd(), true);
}
#endif
