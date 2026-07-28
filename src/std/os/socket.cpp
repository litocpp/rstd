module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <winsock2.h>
#endif

module rstd;
import :os.socket;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#endif

namespace rstd::os::socket
{

void OwnedSocket::close_() noexcept {
    if (socket_ == INVALID_RAW_SOCKET) return;
#if RSTD_OS_UNIX
    rstd::sys::libc::close(socket_);
#elif RSTD_OS_WINDOWS
    ::closesocket(reinterpret_cast<SOCKET>(socket_));
#endif
    socket_ = INVALID_RAW_SOCKET;
}

OwnedSocket::~OwnedSocket() {
    close_();
}

} // namespace rstd::os::socket
