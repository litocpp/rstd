export module rstd.os.linux.net;
export import rstd.os.socket;
export import rstd.io.error;

namespace rstd::os::linux::net
{
export auto set_mark(socket::BorrowedSocket socket, u32 value) -> io::Result<empty>;
export auto set_transparent(socket::BorrowedSocket socket, bool enabled) -> io::Result<empty>;
export auto set_recv_original_destination(socket::BorrowedSocket socket, bool enabled)
    -> io::Result<empty>;
} // namespace rstd::os::linux::net
