module;
#include <rstd/macro.hpp>
export module rstd.tests.std_fd_module;
import rstd.core;
import rstd.os.fd;
#if RSTD_OS_UNIX
import rstd.sys.libc;
#endif

export auto std_fd_module() -> bool {
    using namespace rstd;
    using namespace rstd::os::fd;
    OwnedFd empty;
    if (empty.is_open()) return false;
#if RSTD_OS_UNIX
    namespace libc = rstd::sys::libc;
    auto raw       = libc::open("/dev/null", libc::O_RDONLY);
    if (raw < 0) return false;
    {
        auto owned    = FromRawFd::Api<OwnedFd>::from_raw_fd(raw);
        auto borrowed = as<AsFd>(owned).as_fd();
        auto clone    = borrowed.try_clone_to_owned();
        if (clone.is_err() || clone->as_raw_fd() == raw) return false;
        auto transferred = as<IntoRawFd>(owned).into_raw_fd();
        auto adopted     = OwnedFd::from_raw_fd(transferred);
        if (owned.is_open() || as<AsRawFd>(adopted).as_raw_fd() != raw) return false;
    }
    return libc::fcntl(raw, libc::F_GETFD) == -1;
#else
    return true;
#endif
}
