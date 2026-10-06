module;
#include <rstd/macro.hpp>
export module rstd:sys.fs;
export import :sys.fs.contract;
import :io;
import :os.fd;
import :path;

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
#if RSTD_OS_UNIX
using backend::open_directory;
using backend::open_directory_for_traversal;
using backend::open_at;
using backend::metadata_at;
using backend::create_dir_at;
using backend::remove_at;
using backend::rename_at;
#else
// Native directory-relative operations are not implemented on these backends yet.
inline auto open_directory(ref<path::Path>, const OpenOptionsData&)
    -> rstd::io::Result<os::fd::OwnedFd> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto open_directory_for_traversal(ref<path::Path>) -> rstd::io::Result<os::fd::OwnedFd> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto open_at(os::fd::RawFd, ref<path::Path>, const OpenOptionsData&, bool)
    -> rstd::io::Result<os::fd::OwnedFd> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto metadata_at(os::fd::RawFd, ref<path::Path>, bool) -> rstd::io::Result<MetadataData> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto create_dir_at(os::fd::RawFd, ref<path::Path>) -> rstd::io::Result<empty> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto remove_at(os::fd::RawFd, ref<path::Path>, bool) -> rstd::io::Result<empty> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
inline auto rename_at(os::fd::RawFd, ref<path::Path>, os::fd::RawFd, ref<path::Path>)
    -> rstd::io::Result<empty> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
#endif
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
