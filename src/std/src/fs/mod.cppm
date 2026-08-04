export module rstd:fs;
export import :fs.types;
export import :io;
export import :path;
export import :time;
import :os.fd;

using rstd::io::Error;
using rstd::io::ErrorKind;
using rstd::io::SeekFrom;
using rstd::os::fd::BorrowedFd;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using namespace rstd::prelude;

template<typename T>
using FsResult = rstd::io::Result<T>;

namespace rstd::fs
{

inline constexpr u32 PERMISSION_WRITE_MASK { rstd::uint32_t(0222) };

class MetadataFactory;

export class File;
export class Metadata;
export class ReadDir;

export class FileType {
    FileTypeKind m_kind { FileTypeKind::Unknown };

    explicit FileType(FileTypeKind kind) noexcept: m_kind(kind) {}
    friend class MetadataFactory;
    friend class ReadDir;

public:
    FileType() noexcept = default;

    auto is_file() const noexcept -> bool { return m_kind == FileTypeKind::File; }
    auto is_dir() const noexcept -> bool { return m_kind == FileTypeKind::Directory; }
    auto is_symlink() const noexcept -> bool { return m_kind == FileTypeKind::Symlink; }
    auto is_fifo() const noexcept -> bool { return m_kind == FileTypeKind::Fifo; }
    auto is_block_device() const noexcept -> bool { return m_kind == FileTypeKind::BlockDevice; }
    auto is_char_device() const noexcept -> bool { return m_kind == FileTypeKind::CharDevice; }
    auto is_socket() const noexcept -> bool { return m_kind == FileTypeKind::Socket; }

    friend auto operator==(FileType, FileType) noexcept -> bool = default;
};

export class Permissions {
    u32 m_mode {};

public:
    Permissions() noexcept = default;

    auto readonly() const noexcept -> bool { return (m_mode & PERMISSION_WRITE_MASK) == u32 {}; }

    void set_readonly(bool readonly) noexcept {
        if (readonly)
            m_mode &= ~PERMISSION_WRITE_MASK;
        else
            m_mode |= PERMISSION_WRITE_MASK;
    }

    auto        mode() const noexcept -> u32 { return m_mode; }
    void        set_mode(u32 mode) noexcept { m_mode = mode; }
    static auto from_mode(u32 mode) noexcept -> Permissions { return Permissions { mode }; }

private:
    explicit Permissions(u32 mode) noexcept: m_mode(mode) {}
};

export class FileTimes {
    Option<rstd::time::SystemTime> m_accessed {};
    Option<rstd::time::SystemTime> m_modified {};

    friend class File;

public:
    static auto make() noexcept -> FileTimes { return {}; }

    auto set_accessed(rstd::time::SystemTime time) noexcept -> FileTimes& {
        m_accessed = Some(time);
        return *this;
    }

    auto set_modified(rstd::time::SystemTime time) noexcept -> FileTimes& {
        m_modified = Some(time);
        return *this;
    }
};

export class Metadata {
    FileType                       m_file_type {};
    u64                            m_len {};
    Permissions                    m_permissions {};
    Option<rstd::time::SystemTime> m_accessed {};
    Option<rstd::time::SystemTime> m_modified {};
    Option<rstd::time::SystemTime> m_created {};
    u64                            m_dev {};
    u32                            m_rdev_major {};
    u32                            m_rdev_minor {};
    u64                            m_ino {};
    u32                            m_mode {};
    u64                            m_nlink {};
    u32                            m_uid {};
    u32                            m_gid {};

    friend class MetadataFactory;

    static auto time_result(Option<rstd::time::SystemTime> const& value)
        -> FsResult<rstd::time::SystemTime> {
        if (value.is_none()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }
        auto copied = *value;
        return Ok(rstd::move(copied));
    }

public:
    auto file_type() const noexcept -> FileType { return m_file_type; }
    auto is_dir() const noexcept -> bool { return m_file_type.is_dir(); }
    auto is_file() const noexcept -> bool { return m_file_type.is_file(); }
    auto is_symlink() const noexcept -> bool { return m_file_type.is_symlink(); }
    auto len() const noexcept -> u64 { return m_len; }
    auto permissions() const noexcept -> Permissions { return m_permissions; }
    auto modified() const -> FsResult<rstd::time::SystemTime> { return time_result(m_modified); }
    auto accessed() const -> FsResult<rstd::time::SystemTime> { return time_result(m_accessed); }
    auto created() const -> FsResult<rstd::time::SystemTime> { return time_result(m_created); }
    auto dev() const noexcept -> u64 { return m_dev; }
    auto rdev_major() const noexcept -> u32 { return m_rdev_major; }
    auto rdev_minor() const noexcept -> u32 { return m_rdev_minor; }
    auto ino() const noexcept -> u64 { return m_ino; }
    auto mode() const noexcept -> u32 { return m_mode; }
    auto nlink() const noexcept -> u64 { return m_nlink; }
    auto uid() const noexcept -> u32 { return m_uid; }
    auto gid() const noexcept -> u32 { return m_gid; }
    auto size() const noexcept -> u64 { return m_len; }
};

export class OpenOptions {
    bool m_read { false };
    bool m_write { false };
    bool m_append { false };
    bool m_truncate { false };
    bool m_create { false };
    bool m_create_new { false };
    i32  m_custom_flags {};
    u32  m_mode { rstd::uint32_t(0666) };

public:
    static auto make() noexcept -> OpenOptions { return {}; }

    auto read(bool value) noexcept -> OpenOptions& {
        m_read = value;
        return *this;
    }
    auto write(bool value) noexcept -> OpenOptions& {
        m_write = value;
        return *this;
    }
    auto append(bool value) noexcept -> OpenOptions& {
        m_append = value;
        return *this;
    }
    auto truncate(bool value) noexcept -> OpenOptions& {
        m_truncate = value;
        return *this;
    }
    auto create(bool value) noexcept -> OpenOptions& {
        m_create = value;
        return *this;
    }
    auto create_new(bool value) noexcept -> OpenOptions& {
        m_create_new = value;
        return *this;
    }
    auto custom_flags(i32 flags) noexcept -> OpenOptions& {
        m_custom_flags = flags;
        return *this;
    }
    auto mode(u32 mode) noexcept -> OpenOptions& {
        m_mode = mode;
        return *this;
    }

    auto open(ref<Path> path) const -> FsResult<File>;
};

export class File {
    OwnedFd m_fd;

public:
    File() noexcept = default;
    explicit File(OwnedFd fd) noexcept: m_fd(rstd::move(fd)) {}

    static auto open(ref<Path> path) -> FsResult<File>;
    static auto create(ref<Path> path) -> FsResult<File>;
    static auto create_new(ref<Path> path) -> FsResult<File>;
    static auto options() noexcept -> OpenOptions { return OpenOptions::make(); }

    auto read(mut_ref<u8[]> buffer) -> FsResult<usize>;
    auto write(slice<u8> buffer) -> FsResult<usize>;
    auto write_all(slice<u8> buffer) -> FsResult<empty>;
    auto flush() -> FsResult<empty>;
    auto seek(SeekFrom position) -> FsResult<u64>;
    auto sync_all() -> FsResult<empty>;
    auto sync_data() -> FsResult<empty>;
    auto set_len(u64 size) -> FsResult<empty>;
    auto read_at(mut_ref<u8[]> buffer, u64 offset) const -> FsResult<usize>;
    auto write_at(slice<u8> buffer, u64 offset) const -> FsResult<usize>;
    auto read_exact_at(mut_ref<u8[]> buffer, u64 offset) const -> FsResult<empty>;
    auto write_all_at(slice<u8> buffer, u64 offset) const -> FsResult<empty>;
    auto lock() -> FsResult<empty>;
    auto lock_shared() -> FsResult<empty>;
    auto try_lock() -> FsResult<empty>;
    auto try_lock_shared() -> FsResult<empty>;
    auto unlock() -> FsResult<empty>;
    auto metadata() const -> FsResult<Metadata>;
    auto set_permissions(Permissions permissions) -> FsResult<empty>;
    auto set_times(FileTimes times) -> FsResult<empty>;
    auto set_modified(rstd::time::SystemTime time) -> FsResult<empty>;
    auto try_clone() const -> FsResult<File>;

    auto as_raw_fd() const noexcept -> RawFd { return m_fd.as_raw_fd(); }
    auto as_fd() const noexcept [[clang::lifetimebound]] -> BorrowedFd { return m_fd.as_fd(); }
    auto into_raw_fd() && noexcept -> RawFd { return rstd::move(m_fd).into_raw_fd(); }

    static auto from_raw_fd(RawFd fd) noexcept -> File { return File { OwnedFd::from_raw_fd(fd) }; }
};

export auto read(ref<Path> path) -> FsResult<Vec<u8>>;
export auto read_to_string(ref<Path> path) -> FsResult<String>;
export auto write(ref<Path> path, slice<u8> contents) -> FsResult<empty>;
export auto write_atomic(ref<Path> path, slice<u8> contents) -> FsResult<empty>;
export auto metadata(ref<Path> path) -> FsResult<Metadata>;
export auto symlink_metadata(ref<Path> path) -> FsResult<Metadata>;
export auto exists(ref<Path> path) -> FsResult<bool>;
export auto remove_file(ref<Path> path) -> FsResult<empty>;
export auto remove_dir(ref<Path> path) -> FsResult<empty>;
export auto rename(ref<Path> from, ref<Path> to) -> FsResult<empty>;
export auto hard_link(ref<Path> original, ref<Path> link) -> FsResult<empty>;
export auto soft_link(ref<Path> original, ref<Path> link) -> FsResult<empty>;
export auto read_link(ref<Path> path) -> FsResult<rstd::path::PathBuf>;
export auto canonicalize(ref<Path> path) -> FsResult<rstd::path::PathBuf>;
export auto create_dir(ref<Path> path) -> FsResult<empty>;
export auto create_dir_all(ref<Path> path) -> FsResult<empty>;
export auto set_permissions(ref<Path> path, Permissions permissions) -> FsResult<empty>;
export auto copy(ref<Path> from, ref<Path> to) -> FsResult<u64>;

export class DirEntry {
    rstd::path::PathBuf m_parent;
    rstd::ffi::OsString m_name;
    FileType            m_file_type;

    DirEntry(rstd::path::PathBuf parent, rstd::ffi::OsString name, FileType type)
        : m_parent(rstd::move(parent)), m_name(rstd::move(name)), m_file_type(type) {}
    friend class ReadDir;

public:
    auto path() const -> rstd::path::PathBuf;
    auto file_name() const -> rstd::ffi::OsString;
    auto file_type() const -> FsResult<FileType>;
    auto metadata() const -> FsResult<Metadata>;
};

export class ReadDir {
    void*               m_handle { nullptr };
    rstd::path::PathBuf m_parent;

    ReadDir(void* handle, rstd::path::PathBuf parent) noexcept
        : m_handle(handle), m_parent(rstd::move(parent)) {}
    void        close() noexcept;
    friend auto read_dir(ref<Path> path) -> FsResult<ReadDir>;

public:
    ReadDir() noexcept             = default;
    ReadDir(ReadDir const&)        = delete;
    auto operator=(ReadDir const&) = delete;
    ReadDir(ReadDir&& other) noexcept;
    auto operator=(ReadDir&& other) noexcept -> ReadDir&;
    ~ReadDir();

    auto next() -> Option<FsResult<DirEntry>>;
};

export auto read_dir(ref<Path> path) -> FsResult<ReadDir>;
export auto remove_dir_all(ref<Path> path) -> FsResult<empty>;

} // namespace rstd::fs

namespace rstd
{

template<>
struct Impl<os::fd::IntoRawFd, fs::File> : ImplBase<fs::File> {
    auto into_raw_fd() noexcept -> os::fd::RawFd { return rstd::move(this->self()).into_raw_fd(); }
};

template<>
struct Impl<os::fd::FromRawFd, fs::File> {
    static auto from_raw_fd(os::fd::RawFd fd) noexcept -> fs::File {
        return fs::File::from_raw_fd(fd);
    }
};

template<>
struct Impl<io::Read, fs::File> : ImplBase<fs::File> {
    auto read(mut_ref<u8[]> buffer) -> io::Result<usize> { return this->self().read(buffer); }
};

template<>
struct Impl<io::Write, fs::File> : ImplBase<fs::File> {
    auto write(slice<u8> buffer) -> io::Result<usize> { return this->self().write(buffer); }
    auto flush() -> io::Result<empty> { return this->self().flush(); }
};

template<>
struct Impl<io::Seek, fs::File> : ImplBase<fs::File> {
    auto seek(io::SeekFrom position) -> io::Result<u64> { return this->self().seek(position); }
};

template<>
struct Impl<io::ReadAt, fs::File> : ImplBase<fs::File> {
    auto read_at(mut_ref<u8[]> buffer, u64 offset) const -> io::Result<usize> {
        return this->self().read_at(buffer, offset);
    }
};

template<>
struct Impl<io::WriteAt, fs::File> : ImplBase<fs::File> {
    auto write_at(slice<u8> buffer, u64 offset) const -> io::Result<usize> {
        return this->self().write_at(buffer, offset);
    }
};

} // namespace rstd
