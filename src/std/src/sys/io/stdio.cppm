module;
#include <rstd/macro.hpp>
export module rstd.sys.io.stdio;
export import rstd.io.error;
export import rstd.core;
namespace rstd::sys::io::stdio
{
export auto write_fd(int fd, slice<byte> buf) noexcept -> rstd::io::Result<usize>;
export auto read_fd(int fd, mut_ref<byte[]> buf) noexcept -> rstd::io::Result<usize>;
export auto is_terminal_fd(int fd) noexcept -> bool;
} // namespace rstd::sys::io::stdio
