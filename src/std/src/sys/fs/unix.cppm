export module rstd:sys.fs.unix;
import :sys.fs.contract;
import :fs.types;
import rstd.io;
import rstd.os.fd;
import :path;
import rstd.sys.libc;
import :time;
import rstd.alloc;

using rstd::io::SeekFrom;
using rstd::mem::MaybeUninit;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

template<typename T>
using IOResult = rstd::io::Result<T>;

namespace rstd::sys::fs::unix
{

export auto open(ref<Path> path, OpenOptionsData const& options) -> IOResult<OwnedFd>;
export auto open_directory(ref<Path> path, const OpenOptionsData& options) -> IOResult<OwnedFd>;
export auto open_directory_for_traversal(ref<Path> path) -> IOResult<OwnedFd>;
export auto open_at(RawFd parent, ref<Path> path, const OpenOptionsData& options, bool directory)
    -> IOResult<OwnedFd>;
export auto metadata_at(RawFd parent, ref<Path> path, bool follow) -> IOResult<MetadataData>;
export auto create_dir_at(RawFd parent, ref<Path> path) -> IOResult<empty>;
export auto remove_at(RawFd parent, ref<Path> path, bool directory) -> IOResult<empty>;
export auto rename_at(RawFd parent, ref<Path> from, RawFd destination, ref<Path> to)
    -> IOResult<empty>;
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
    libc::DIR* m_handle { nullptr };

    explicit Directory(libc::DIR* handle) noexcept;

    void close() noexcept;

public:
    Directory() noexcept                           = default;
    Directory(Directory const&)                    = delete;
    auto operator=(Directory const&) -> Directory& = delete;
    Directory(Directory&& other) noexcept;
    auto operator=(Directory&& other) noexcept -> Directory&;
    ~Directory();

    static auto open(ref<Path> path) -> IOResult<Directory>;
    auto        next() -> Option<IOResult<DirectoryEntryData>>;
};

} // namespace rstd::sys::fs::unix
