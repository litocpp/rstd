module;
#include <rstd/macro.hpp>
export module rstd:sys.fs.unsupported;
import :sys.fs.contract;
import :fs.types;
import :io;
import :os.fd;
import :path;
import :time;

#if ! RSTD_OS_UNIX && ! RSTD_OS_WINDOWS

namespace rstd::sys::fs::unsupported
{

using rstd::io::ErrorKind;
using rstd::io::SeekFrom;
using rstd::io::error::Error;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;

template<typename T>
using Result = rstd::io::Result<T>;

inline auto unsupported_error() noexcept -> Error {
    return Error::from_kind(ErrorKind { ErrorKind::Unsupported });
}

export auto open(ref<Path> path, OpenOptionsData const& options) -> Result<OwnedFd> {
    (void)path;
    (void)options;
    return Err(unsupported_error());
}

export auto read(RawFd fd, mut_ref<byte[]> buffer) -> Result<usize> {
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
}

export auto write(RawFd fd, slice<byte> buffer) -> Result<usize> {
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
}

export auto seek(RawFd fd, SeekFrom position) -> Result<u64> {
    (void)fd;
    (void)position;
    return Err(unsupported_error());
}

export auto sync_all(RawFd fd) -> Result<empty> {
    (void)fd;
    return Err(unsupported_error());
}

export auto sync_data(RawFd fd) -> Result<empty> {
    (void)fd;
    return Err(unsupported_error());
}

export auto set_len(RawFd fd, u64 size) -> Result<empty> {
    (void)fd;
    (void)size;
    return Err(unsupported_error());
}

export auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> Result<usize> {
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
}

export auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> Result<usize> {
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
}

export auto lock(RawFd fd, LockMode mode) -> Result<empty> {
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
}

export auto metadata(RawFd fd) -> Result<MetadataData> {
    (void)fd;
    return Err(unsupported_error());
}

export auto metadata(ref<Path> path, bool follow) -> Result<MetadataData> {
    (void)path;
    (void)follow;
    return Err(unsupported_error());
}

export auto set_permissions(RawFd fd, u32 mode) -> Result<empty> {
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
}

export auto set_permissions(ref<Path> path, u32 mode) -> Result<empty> {
    (void)path;
    (void)mode;
    return Err(unsupported_error());
}

export auto set_times(RawFd fd, FileTimesData times) -> Result<empty> {
    (void)fd;
    (void)times;
    return Err(unsupported_error());
}

export auto remove_file(ref<Path> path) -> Result<empty> {
    (void)path;
    return Err(unsupported_error());
}

export auto remove_dir(ref<Path> path) -> Result<empty> {
    (void)path;
    return Err(unsupported_error());
}

export auto rename(ref<Path> from, ref<Path> to) -> Result<empty> {
    (void)from;
    (void)to;
    return Err(unsupported_error());
}

export auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty> {
    (void)original;
    (void)link;
    return Err(unsupported_error());
}

export auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty> {
    (void)original;
    (void)link;
    return Err(unsupported_error());
}

export auto read_link(ref<Path> path) -> Result<PathBuf> {
    (void)path;
    return Err(unsupported_error());
}

export auto canonicalize(ref<Path> path) -> Result<PathBuf> {
    (void)path;
    return Err(unsupported_error());
}

export auto create_dir(ref<Path> path) -> Result<empty> {
    (void)path;
    return Err(unsupported_error());
}

export class Directory {
public:
    Directory() noexcept                               = default;
    Directory(Directory const&)                        = delete;
    auto operator=(Directory const&) -> Directory&     = delete;
    Directory(Directory&&) noexcept                    = default;
    auto operator=(Directory&&) noexcept -> Directory& = default;
    ~Directory()                                       = default;

    static auto open(ref<Path> path) -> Result<Directory> {
        (void)path;
        return Err(unsupported_error());
    }

    auto next() -> Option<Result<DirectoryEntryData>> { return None(); }
};

} // namespace rstd::sys::fs::unsupported

#endif
