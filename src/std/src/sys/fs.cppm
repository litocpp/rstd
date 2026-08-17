module;
#include <rstd/macro.hpp>
export module rstd:sys.fs;
export import :sys.fs.contract;

#if RSTD_OS_UNIX
import :sys.fs.unix;
namespace rstd::sys::fs
{
namespace backend = unix;
}
#elif RSTD_OS_WINDOWS
import :sys.fs.windows;
namespace rstd::sys::fs
{
namespace backend = windows;
}
#else
import :sys.fs.unsupported;
namespace rstd::sys::fs
{
namespace backend = unsupported;
}
#endif

export namespace rstd::sys::fs
{

using backend::Directory;
using backend::canonicalize;
using backend::create_dir;
using backend::hard_link;
using backend::lock;
using backend::metadata;
using backend::open;
using backend::read;
using backend::read_at;
using backend::read_link;
using backend::remove_dir;
using backend::remove_file;
using backend::rename;
using backend::seek;
using backend::set_len;
using backend::set_permissions;
using backend::set_times;
using backend::soft_link;
using backend::sync_all;
using backend::sync_data;
using backend::write;
using backend::write_at;

} // namespace rstd::sys::fs
