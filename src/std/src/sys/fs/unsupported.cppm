export module rstd:sys.fs.unsupported;
import :sys.fs.contract;
import :fs.types;
import rstd.io;
import rstd.os.fd;
import :path;
import :time;

using rstd::mem::MaybeUninit;

using rstd::io::SeekFrom;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;

template<typename T>
using IOResult = rstd::io::Result<T>;

namespace rstd::sys::fs::unsupported
{

export auto open(ref<Path> path, OpenOptionsData const& options) -> IOResult<OwnedFd>;
export auto read(RawFd fd, mut_ref<MaybeUninit<byte>[]> buffer) -> IOResult<usize>;
export auto read(RawFd fd, mut_ref<byte[]> buffer) -> IOResult<usize>;
export auto write(RawFd fd, slice<byte> buffer) -> IOResult<usize>;
export auto seek(RawFd fd, SeekFrom position) -> IOResult<u64>;
export auto sync_all(RawFd fd) -> IOResult<empty>;
export auto sync_data(RawFd fd) -> IOResult<empty>;
export auto set_len(RawFd fd, u64 size) -> IOResult<empty>;
export auto read_at(RawFd fd, mut_ref<MaybeUninit<byte>[]> buffer, u64 offset) -> IOResult<usize>;
export auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> IOResult<usize>;
export auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> IOResult<usize>;
export auto lock(RawFd fd, LockMode mode) -> IOResult<empty>;
export auto metadata(RawFd fd) -> IOResult<MetadataData>;
export auto metadata(ref<Path> path, bool follow) -> IOResult<MetadataData>;
export auto set_permissions(RawFd fd, u32 mode) -> IOResult<empty>;
export auto set_permissions(ref<Path> path, u32 mode) -> IOResult<empty>;
export auto set_times(RawFd fd, FileTimesData times) -> IOResult<empty>;
export auto remove_file(ref<Path> path) -> IOResult<empty>;
export auto remove_dir(ref<Path> path) -> IOResult<empty>;
export auto rename(ref<Path> from, ref<Path> to) -> IOResult<empty>;
export auto hard_link(ref<Path> original, ref<Path> link) -> IOResult<empty>;
export auto soft_link(ref<Path> original, ref<Path> link) -> IOResult<empty>;
export auto read_link(ref<Path> path) -> IOResult<PathBuf>;
export auto canonicalize(ref<Path> path) -> IOResult<PathBuf>;
export auto create_dir(ref<Path> path) -> IOResult<empty>;

export class Directory {
public:
    Directory() noexcept                               = default;
    Directory(Directory const&)                        = delete;
    auto operator=(Directory const&) -> Directory&     = delete;
    Directory(Directory&&) noexcept                    = default;
    auto operator=(Directory&&) noexcept -> Directory& = default;
    ~Directory()                                       = default;

    static auto open(ref<Path> path) -> IOResult<Directory>;
    auto        next() -> Option<IOResult<DirectoryEntryData>>;
};

} // namespace rstd::sys::fs::unsupported
