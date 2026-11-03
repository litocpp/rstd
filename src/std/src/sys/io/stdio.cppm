module;
#include <rstd/macro.hpp>
export module rstd.sys.io.stdio;
export import rstd.io.error;
export import rstd.core;
#if RSTD_OS_WINDOWS
import rstd.os.handle;
#endif

using rstd::mem::MaybeUninit;
namespace rstd::sys::io::stdio
{
export auto write_fd(int fd, slice<byte> buf) noexcept -> rstd::io::Result<usize>;
export auto write_pipe_fd(int fd, slice<byte> buf) noexcept -> rstd::io::Result<usize>;
export auto read_fd(int fd, mut_ref<MaybeUninit<byte>[]> buf) noexcept -> rstd::io::Result<usize>;
export auto read_fd(int fd, mut_ref<byte[]> buf) noexcept -> rstd::io::Result<usize>;
#if RSTD_OS_WINDOWS
export auto validate_async_pipe(os::handle::BorrowedHandle handle, bool writing) noexcept
    -> rstd::io::Result<empty>;
#endif
export auto is_terminal_fd(int fd) noexcept -> bool;
} // namespace rstd::sys::io::stdio
