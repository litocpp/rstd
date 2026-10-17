module;
#include <rstd/macro.hpp>
export module rstd.tests.std_socket_module;
import rstd.os.socket;
#if RSTD_OS_UNIX
import rstd.sys.libc;
#endif

export auto std_socket_module() -> bool {
    using namespace rstd::os::socket;
    OwnedSocket empty;
    if (empty.is_open()) return false;
#if RSTD_OS_UNIX
    namespace libc = rstd::sys::libc;
    auto raw       = libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0);
    if (raw < 0) return false;
    {
        auto owned = OwnedSocket::from_raw_socket(raw);
        if (owned.as_socket().as_raw_socket() != raw) return false;
        auto moved       = rstd::move(owned);
        auto transferred = rstd::move(moved).into_raw_socket();
        auto adopted     = OwnedSocket::from_raw_socket(transferred);
        if (owned.is_open() || moved.is_open() || adopted.as_raw_socket() != raw) return false;
    }
    return libc::fcntl(raw, libc::F_GETFD) == -1;
#else
    return true;
#endif
}
