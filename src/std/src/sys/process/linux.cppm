module;
#include <rstd/macro.hpp>

export module rstd.sys.process.linux;

#if RSTD_OS_LINUX
export namespace rstd::sys::process
{

auto pidfd_channel(int descriptors[2]) -> int;
auto send_current_pidfd(int socket) -> int;
auto receive_pidfd(int socket, int& fd) -> int;
auto pidfd_send_signal(int fd, int signal, bool group) -> int;

struct PidFdWait {
    bool available {};
    bool signaled {};
    int  status {};
    int  error {};
};

auto pidfd_wait(int fd, bool nonblocking) -> PidFdWait;

} // namespace rstd::sys::process
#endif
