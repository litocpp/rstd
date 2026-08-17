export module rstd:sys.fs.windows;
import :sys.fs.contract;
import :fs.types;
import :io;
import :os.fd;
import :path;
import :sys.libc;
import :time;
import rstd.alloc;

namespace rstd::sys::fs::windows
{

using rstd::io::SeekFrom;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

template<typename T>
using Result = rstd::io::Result<T>;

export auto open(ref<Path> path, OpenOptionsData const& options) -> Result<OwnedFd>;
export auto read(RawFd fd, mut_ref<byte[]> buffer) -> Result<usize>;
export auto write(RawFd fd, slice<byte> buffer) -> Result<usize>;
export auto seek(RawFd fd, SeekFrom position) -> Result<u64>;
export auto sync_all(RawFd fd) -> Result<empty>;
export auto sync_data(RawFd fd) -> Result<empty>;
export auto set_len(RawFd fd, u64 size) -> Result<empty>;
export auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> Result<usize>;
export auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> Result<usize>;
export auto lock(RawFd fd, LockMode mode) -> Result<empty>;
export auto metadata(RawFd fd) -> Result<MetadataData>;
export auto metadata(ref<Path> path, bool follow) -> Result<MetadataData>;
export auto set_permissions(RawFd fd, u32 mode) -> Result<empty>;
export auto set_permissions(ref<Path> path, u32 mode) -> Result<empty>;
export auto set_times(RawFd fd, FileTimesData times) -> Result<empty>;
export auto remove_file(ref<Path> path) -> Result<empty>;
export auto remove_dir(ref<Path> path) -> Result<empty>;
export auto rename(ref<Path> from, ref<Path> to) -> Result<empty>;
export auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty>;
export auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty>;
export auto read_link(ref<Path> path) -> Result<PathBuf>;
export auto canonicalize(ref<Path> path) -> Result<PathBuf>;
export auto create_dir(ref<Path> path) -> Result<empty>;

export class Directory {
    libc::HANDLE           m_handle { libc::M_INVALID_HANDLE_VALUE };
    libc::WIN32_FIND_DATAW m_entry {};
    bool                   m_pending { true };

    void close() noexcept;

public:
    Directory() noexcept                           = default;
    Directory(Directory const&)                    = delete;
    auto operator=(Directory const&) -> Directory& = delete;
    Directory(Directory&& other) noexcept;
    auto operator=(Directory&& other) noexcept -> Directory&;
    ~Directory();

    static auto open(ref<Path> path) -> Result<Directory>;
    auto        next() -> Option<Result<DirectoryEntryData>>;
};

} // namespace rstd::sys::fs::windows
