module rstd;
import :sys.fs.unsupported;
import :sys.fs.contract;
import :fs.types;
import rstd.io;
import rstd.os.fd;
import :path;
import :time;

using rstd::mem::MaybeUninit;

using rstd::io::ErrorKind;
using rstd::io::SeekFrom;
using rstd::io::error::Error;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;

template<typename T>
using IOResult = rstd::io::Result<T>;

namespace rstd::sys::fs::unsupported
{

inline auto unsupported_error() noexcept -> Error {
    return Error::from_kind(ErrorKind { ErrorKind::Unsupported });
}

auto open(ref<Path> path, OpenOptionsData const& options) -> IOResult<OwnedFd> {
    (void)path;
    (void)options;
    return Err(unsupported_error());
}

template<typename Byte>
auto read_impl(RawFd fd, mut_ref<Byte[]> buffer) -> IOResult<usize> {
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
}

auto read(RawFd fd, mut_ref<MaybeUninit<byte>[]> buffer) -> IOResult<usize> {
    return read_impl(fd, buffer);
}

auto read(RawFd fd, mut_ref<byte[]> buffer) -> IOResult<usize> {
    return read_impl(fd, buffer);
}

auto write(RawFd fd, slice<byte> buffer) -> IOResult<usize> {
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
}

auto seek(RawFd fd, SeekFrom position) -> IOResult<u64> {
    (void)fd;
    (void)position;
    return Err(unsupported_error());
}

auto sync_all(RawFd fd) -> IOResult<empty> {
    (void)fd;
    return Err(unsupported_error());
}

auto sync_data(RawFd fd) -> IOResult<empty> {
    (void)fd;
    return Err(unsupported_error());
}

auto set_len(RawFd fd, u64 size) -> IOResult<empty> {
    (void)fd;
    (void)size;
    return Err(unsupported_error());
}

template<typename Byte>
auto read_at_impl(RawFd fd, mut_ref<Byte[]> buffer, u64 offset) -> IOResult<usize> {
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
}

auto read_at(RawFd fd, mut_ref<MaybeUninit<byte>[]> buffer, u64 offset) -> IOResult<usize> {
    return read_at_impl(fd, buffer, offset);
}

auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> IOResult<usize> {
    return read_at_impl(fd, buffer, offset);
}

auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> IOResult<usize> {
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
}

auto lock(RawFd fd, LockMode mode) -> IOResult<empty> {
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
}

auto metadata(RawFd fd) -> IOResult<MetadataData> {
    (void)fd;
    return Err(unsupported_error());
}

auto metadata(ref<Path> path, bool follow) -> IOResult<MetadataData> {
    (void)path;
    (void)follow;
    return Err(unsupported_error());
}

auto set_permissions(RawFd fd, u32 mode) -> IOResult<empty> {
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
}

auto set_permissions(ref<Path> path, u32 mode) -> IOResult<empty> {
    (void)path;
    (void)mode;
    return Err(unsupported_error());
}

auto set_times(RawFd fd, FileTimesData times) -> IOResult<empty> {
    (void)fd;
    (void)times;
    return Err(unsupported_error());
}

auto remove_file(ref<Path> path) -> IOResult<empty> {
    (void)path;
    return Err(unsupported_error());
}

auto remove_dir(ref<Path> path) -> IOResult<empty> {
    (void)path;
    return Err(unsupported_error());
}

auto rename(ref<Path> from, ref<Path> to) -> IOResult<empty> {
    (void)from;
    (void)to;
    return Err(unsupported_error());
}

auto hard_link(ref<Path> original, ref<Path> link) -> IOResult<empty> {
    (void)original;
    (void)link;
    return Err(unsupported_error());
}

auto soft_link(ref<Path> original, ref<Path> link) -> IOResult<empty> {
    (void)original;
    (void)link;
    return Err(unsupported_error());
}

auto read_link(ref<Path> path) -> IOResult<PathBuf> {
    (void)path;
    return Err(unsupported_error());
}

auto canonicalize(ref<Path> path) -> IOResult<PathBuf> {
    (void)path;
    return Err(unsupported_error());
}

auto create_dir(ref<Path> path) -> IOResult<empty> {
    (void)path;
    return Err(unsupported_error());
}

auto Directory::open(ref<Path> path) -> IOResult<Directory> {
    (void)path;
    return Err(unsupported_error());
}

auto Directory::next() -> Option<IOResult<DirectoryEntryData>> {
    return None();
}

} // namespace rstd::sys::fs::unsupported
