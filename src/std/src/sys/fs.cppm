module;
#include <rstd/macro.hpp>
export module rstd:sys.fs;
import :fs.types;
import :io;
import :os.fd;
import :path;
import :sys.libc.unix;
import :time;
import rstd.alloc;

namespace rstd::sys::fs
{

using rstd::io::ErrorKind;
using rstd::io::SeekFrom;
using rstd::io::error::Error;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;
using rstd::ffi::CStr;
using rstd::ffi::OsString;
using ::alloc::ffi::CString;
using ::alloc::string::String;
using ::alloc::vec::Vec;

template<typename T>
using Result = rstd::io::Result<T>;

export struct OpenOptionsData {
    bool read;
    bool write;
    bool append;
    bool truncate;
    bool create;
    bool create_new;
    i32  custom_flags;
    u32  mode;
};

export struct FileTimesData {
    Option<rstd::time::SystemTime> const& accessed;
    Option<rstd::time::SystemTime> const& modified;
};

export struct MetadataData {
    rstd::fs::FileTypeKind         file_type { rstd::fs::FileTypeKind::Unknown };
    u64                            len {};
    u32                            permissions {};
    Option<rstd::time::SystemTime> accessed {};
    Option<rstd::time::SystemTime> modified {};
    Option<rstd::time::SystemTime> created {};
    u64                            dev {};
    u32                            rdev_major {};
    u32                            rdev_minor {};
    u64                            ino {};
    u32                            mode {};
    u64                            nlink {};
    u32                            uid {};
    u32                            gid {};
};

export struct DirectoryEntryData {
    Vec<u8>                name;
    rstd::fs::FileTypeKind file_type;
};

export enum class LockMode {
    Exclusive,
    Shared,
    TryExclusive,
    TryShared,
    Unlock,
};

inline auto unsupported_error() noexcept -> Error {
    return Error::from_kind(ErrorKind { ErrorKind::Unsupported });
}

auto path_cstring(ref<Path> path) -> Result<CString> {
    auto result = path.to_cstring();
    if (result.is_err()) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return Ok(rstd::move(result).unwrap_unchecked());
}

#if RSTD_OS_UNIX
namespace libc = rstd::sys::libc;
using rstd::sys::libc::DIR;

auto last_error() noexcept -> Error {
    return Error::from_raw_os_error(i32(libc::get_errno()));
}

auto file_type_from_mode(libc::mode_t mode) noexcept -> rstd::fs::FileTypeKind {
    switch (mode & libc::S_IFMT) {
    case libc::S_IFREG: return rstd::fs::FileTypeKind::File;
    case libc::S_IFDIR: return rstd::fs::FileTypeKind::Directory;
    case libc::S_IFLNK: return rstd::fs::FileTypeKind::Symlink;
    case libc::S_IFIFO: return rstd::fs::FileTypeKind::Fifo;
    case libc::S_IFBLK: return rstd::fs::FileTypeKind::BlockDevice;
    case libc::S_IFCHR: return rstd::fs::FileTypeKind::CharDevice;
    case libc::S_IFSOCK: return rstd::fs::FileTypeKind::Socket;
    default: return rstd::fs::FileTypeKind::Unknown;
    }
}

auto file_type_from_dirent(unsigned char type) noexcept -> rstd::fs::FileTypeKind {
    switch (type) {
    case libc::DT_REG: return rstd::fs::FileTypeKind::File;
    case libc::DT_DIR: return rstd::fs::FileTypeKind::Directory;
    case libc::DT_LNK: return rstd::fs::FileTypeKind::Symlink;
    case libc::DT_FIFO: return rstd::fs::FileTypeKind::Fifo;
    case libc::DT_BLK: return rstd::fs::FileTypeKind::BlockDevice;
    case libc::DT_CHR: return rstd::fs::FileTypeKind::CharDevice;
    case libc::DT_SOCK: return rstd::fs::FileTypeKind::Socket;
    default: return rstd::fs::FileTypeKind::Unknown;
    }
}

auto metadata_from_stat(libc::stat_t const& stat) -> MetadataData {
    auto mode = static_cast<libc::mode_t>(stat.st_mode);
    return MetadataData {
        .file_type   = file_type_from_mode(mode),
        .len         = u64(stat.st_size),
        .permissions = u32(mode & 0777u),
        .accessed    = rstd::time::SystemTime::from_unix_time(i64(stat.st_atim.tv_sec),
                                                              u32(stat.st_atim.tv_nsec)),
        .modified    = rstd::time::SystemTime::from_unix_time(i64(stat.st_mtim.tv_sec),
                                                              u32(stat.st_mtim.tv_nsec)),
        .created     = None(),
        .dev         = u64(stat.st_dev),
        .rdev_major  = u32(libc::major(stat.st_rdev)),
        .rdev_minor  = u32(libc::minor(stat.st_rdev)),
        .ino         = u64(stat.st_ino),
        .mode        = u32(mode),
        .nlink       = u64(stat.st_nlink),
        .uid         = u32(stat.st_uid),
        .gid         = u32(stat.st_gid),
    };
}

auto access_mode(OpenOptionsData const& options) -> Result<int> {
    if (options.read && ! options.write && ! options.append) return Ok(libc::O_RDONLY);
    if (! options.read && options.write && ! options.append) return Ok(libc::O_WRONLY);
    if (options.read && options.write && ! options.append) return Ok(libc::O_RDWR);
    if (! options.read && options.append) return Ok(libc::O_WRONLY | libc::O_APPEND);
    if (options.read && options.append) return Ok(libc::O_RDWR | libc::O_APPEND);
    return Err(Error::from_raw_os_error(i32(libc::EINVAL)));
}

auto creation_mode(OpenOptionsData const& options) -> Result<int> {
    if (! options.write && ! options.append &&
        (options.truncate || options.create || options.create_new)) {
        return Err(Error::from_raw_os_error(i32(libc::EINVAL)));
    }
    if (options.append && options.truncate && ! options.create_new) {
        return Err(Error::from_raw_os_error(i32(libc::EINVAL)));
    }
    if (options.create_new) return Ok(libc::O_CREAT | libc::O_EXCL);
    if (options.create && options.truncate) return Ok(libc::O_CREAT | libc::O_TRUNC);
    if (options.create) return Ok(libc::O_CREAT);
    if (options.truncate) return Ok(libc::O_TRUNC);
    return Ok(0);
}

constexpr auto off_t_max() noexcept -> rstd::uint64_t {
    static_assert(sizeof(libc::off_t) <= sizeof(rstd::uint64_t));
    constexpr auto bits = static_cast<rstd::uint32_t>(sizeof(libc::off_t) * 8);
    if constexpr (libc::off_t(-1) < libc::off_t(0)) {
        return static_cast<rstd::uint64_t>((rstd::uint128_t(1) << (bits - 1)) - 1);
    } else {
        return static_cast<rstd::uint64_t>(~rstd::uint128_t(0) >> (128 - bits));
    }
}

auto checked_off_t(u64 value) -> Result<libc::off_t> {
    auto native = value.to_primitive();
    if (native > off_t_max()) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    return Ok(static_cast<libc::off_t>(native));
}
#endif

export auto open(ref<Path> path, OpenOptionsData const& options) -> Result<OwnedFd> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto access = access_mode(options);
    if (access.is_err()) return Err(access.unwrap_err_unchecked());
    auto creation = creation_mode(options);
    if (creation.is_err()) return Err(creation.unwrap_err_unchecked());

    auto flags    = access.unwrap_unchecked() | creation.unwrap_unchecked() |
                    static_cast<int>(options.custom_flags.to_primitive()) | libc::O_CLOEXEC;
    auto value    = rstd::move(path_value).unwrap_unchecked();
    auto raw_path = value.as_ptr();
    while (true) {
        auto fd =
            libc::open(raw_path, flags, static_cast<libc::mode_t>(options.mode.to_primitive()));
        if (fd >= 0) return Ok(OwnedFd::from_raw_fd(fd));
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
#else
    (void)path;
    (void)options;
    return Err(unsupported_error());
#endif
}

export auto read(RawFd fd, mut_ref<byte[]> buffer) -> Result<usize> {
#if RSTD_OS_UNIX
    while (true) {
        auto count = libc::read(fd, buffer.as_raw_ptr(), buffer.len().to_primitive());
        if (count >= 0) return Ok(usize(count));
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
#else
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
#endif
}

export auto write(RawFd fd, slice<byte> buffer) -> Result<usize> {
#if RSTD_OS_UNIX
    while (true) {
        auto count = libc::write(fd, buffer.as_raw_ptr(), buffer.len().to_primitive());
        if (count >= 0) return Ok(usize(count));
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
#else
    (void)fd;
    (void)buffer;
    return Err(unsupported_error());
#endif
}

export auto seek(RawFd fd, SeekFrom position) -> Result<u64> {
#if RSTD_OS_UNIX
    int         whence = libc::SEEK_SET;
    libc::off_t offset = 0;
    switch (position.which) {
    case SeekFrom::Which::Start:
        whence = libc::SEEK_SET;
        {
            auto converted = checked_off_t(position.start);
            if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
            offset = converted.unwrap_unchecked();
        }
        break;
    case SeekFrom::Which::End:
        whence = libc::SEEK_END;
        offset = static_cast<libc::off_t>(position.offset.to_primitive());
        break;
    case SeekFrom::Which::Current:
        whence = libc::SEEK_CUR;
        offset = static_cast<libc::off_t>(position.offset.to_primitive());
        break;
    }
    auto result = libc::lseek(fd, offset, whence);
    if (result < 0) return Err(last_error());
    return Ok(u64(result));
#else
    (void)fd;
    (void)position;
    return Err(unsupported_error());
#endif
}

export auto sync_all(RawFd fd) -> Result<empty> {
#if RSTD_OS_UNIX
    if (libc::fsync(fd) < 0) return Err(last_error());
    return Ok(empty {});
#else
    (void)fd;
    return Err(unsupported_error());
#endif
}

export auto sync_data(RawFd fd) -> Result<empty> {
#if RSTD_OS_UNIX
    if (libc::fdatasync(fd) < 0) return Err(last_error());
    return Ok(empty {});
#else
    (void)fd;
    return Err(unsupported_error());
#endif
}

export auto set_len(RawFd fd, u64 size) -> Result<empty> {
#if RSTD_OS_UNIX
    auto converted = checked_off_t(size);
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    while (libc::ftruncate(fd, converted.unwrap_unchecked()) < 0) {
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)size;
    return Err(unsupported_error());
#endif
}

export auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> Result<usize> {
#if RSTD_OS_UNIX
    auto converted = checked_off_t(offset);
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    while (true) {
        auto count = libc::pread(
            fd, buffer.as_raw_ptr(), buffer.len().to_primitive(), converted.unwrap_unchecked());
        if (count >= 0) return Ok(usize(count));
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
#else
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
#endif
}

export auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> Result<usize> {
#if RSTD_OS_UNIX
    auto converted = checked_off_t(offset);
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    while (true) {
        auto count = libc::pwrite(
            fd, buffer.as_raw_ptr(), buffer.len().to_primitive(), converted.unwrap_unchecked());
        if (count >= 0) return Ok(usize(count));
        if (libc::get_errno() != libc::EINTR) return Err(last_error());
    }
#else
    (void)fd;
    (void)buffer;
    (void)offset;
    return Err(unsupported_error());
#endif
}

export auto lock(RawFd fd, LockMode mode) -> Result<empty> {
#if RSTD_OS_UNIX
    int operation = libc::LOCK_EX;
    switch (mode) {
    case LockMode::Exclusive: operation = libc::LOCK_EX; break;
    case LockMode::Shared: operation = libc::LOCK_SH; break;
    case LockMode::TryExclusive: operation = libc::LOCK_EX | libc::LOCK_NB; break;
    case LockMode::TryShared: operation = libc::LOCK_SH | libc::LOCK_NB; break;
    case LockMode::Unlock: operation = libc::LOCK_UN; break;
    }
    while (libc::flock(fd, operation) < 0) {
        auto error = libc::get_errno();
        if (error == libc::EINTR && (mode == LockMode::Exclusive || mode == LockMode::Shared)) {
            continue;
        }
        if (error == libc::EWOULDBLOCK &&
            (mode == LockMode::TryExclusive || mode == LockMode::TryShared)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::WouldBlock }));
        }
        return Err(Error::from_raw_os_error(i32(error)));
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
#endif
}

export auto metadata(RawFd fd) -> Result<MetadataData> {
#if RSTD_OS_UNIX
    libc::stat_t value {};
    if (libc::fstat(fd, &value) < 0) return Err(last_error());
    return Ok(metadata_from_stat(value));
#else
    (void)fd;
    return Err(unsupported_error());
#endif
}

export auto metadata(ref<Path> path, bool follow) -> Result<MetadataData> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto         value    = rstd::move(path_value).unwrap_unchecked();
    auto         raw_path = value.as_ptr();
    libc::stat_t stat {};
    auto         result = follow ? libc::stat(raw_path, &stat) : libc::lstat(raw_path, &stat);
    if (result < 0) return Err(last_error());
    return Ok(metadata_from_stat(stat));
#else
    (void)path;
    (void)follow;
    return Err(unsupported_error());
#endif
}

export auto set_permissions(RawFd fd, u32 mode) -> Result<empty> {
#if RSTD_OS_UNIX
    if (libc::fchmod(fd, static_cast<libc::mode_t>(mode.to_primitive())) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)mode;
    return Err(unsupported_error());
#endif
}

export auto set_permissions(ref<Path> path, u32 mode) -> Result<empty> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value    = rstd::move(path_value).unwrap_unchecked();
    auto raw_path = value.as_ptr();
    if (libc::chmod(raw_path, static_cast<libc::mode_t>(mode.to_primitive())) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)path;
    (void)mode;
    return Err(unsupported_error());
#endif
}

export auto set_times(RawFd fd, FileTimesData times) -> Result<empty> {
#if RSTD_OS_UNIX
    libc::timespec_t values[2];
    auto fill = [](Option<rstd::time::SystemTime> const& source, libc::timespec_t& target) {
        if (source.is_some()) {
            auto value     = (*source).as_unix_time();
            target.tv_sec  = static_cast<libc::time_t>(value.seconds.to_primitive());
            target.tv_nsec = static_cast<long>(value.nanoseconds.to_primitive());
        } else {
            target.tv_sec  = 0;
            target.tv_nsec = libc::UTIME_OMIT;
        }
    };
    fill(times.accessed, values[0]);
    fill(times.modified, values[1]);
    if (libc::futimens(fd, values) < 0) return Err(last_error());
    return Ok(empty {});
#else
    (void)fd;
    (void)times;
    return Err(unsupported_error());
#endif
}

auto run_path(ref<Path> path, int (*operation)(const char*)) -> Result<empty> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value = rstd::move(path_value).unwrap_unchecked();
    if (operation(value.as_ptr()) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)path;
    (void)operation;
    return Err(unsupported_error());
#endif
}

auto run_paths(ref<Path> first, ref<Path> second, int (*operation)(const char*, const char*))
    -> Result<empty> {
#if RSTD_OS_UNIX
    auto first_value = path_cstring(first);
    if (first_value.is_err()) return Err(first_value.unwrap_err_unchecked());
    auto second_value = path_cstring(second);
    if (second_value.is_err()) return Err(second_value.unwrap_err_unchecked());
    auto first_path  = rstd::move(first_value).unwrap_unchecked();
    auto second_path = rstd::move(second_value).unwrap_unchecked();
    if (operation(first_path.as_ptr(), second_path.as_ptr()) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)first;
    (void)second;
    (void)operation;
    return Err(unsupported_error());
#endif
}

export auto remove_file(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_path(path, libc::unlink);
#else
    return run_path(path, nullptr);
#endif
}

export auto remove_dir(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_path(path, libc::rmdir);
#else
    return run_path(path, nullptr);
#endif
}

export auto rename(ref<Path> from, ref<Path> to) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(from, to, libc::rename);
#else
    return run_paths(from, to, nullptr);
#endif
}

export auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(original, link, libc::link);
#else
    return run_paths(original, link, nullptr);
#endif
}

export auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(original, link, libc::symlink);
#else
    return run_paths(original, link, nullptr);
#endif
}

export auto read_link(ref<Path> path) -> Result<PathBuf> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value    = rstd::move(path_value).unwrap_unchecked();
    auto raw_path = value.as_ptr();

    rstd::size_t capacity = 256;
    while (true) {
        auto bytes = Vec<u8>::with_capacity(usize(capacity));
        bytes.resize(usize(capacity), u8 {});
        auto raw   = as_bytes_mut(bytes.as_mut_slice().as_mut_ref());
        auto count = libc::readlink(raw_path, reinterpret_cast<char*>(raw.as_raw_ptr()), capacity);
        if (count < 0) return Err(last_error());
        auto length = static_cast<rstd::size_t>(count);
        if (length < capacity) {
            while (bytes.len().to_primitive() > length) (void)bytes.pop();
            return Ok(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(bytes))));
        }
        capacity *= 2;
    }
#else
    (void)path;
    return Err(unsupported_error());
#endif
}

export auto canonicalize(ref<Path> path) -> Result<PathBuf> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value = rstd::move(path_value).unwrap_unchecked();
    auto raw   = libc::realpath(value.as_ptr(), nullptr);
    if (! raw) return Err(last_error());
    auto bytes = Vec<u8>::from(CStr::from_ptr(raw).to_bytes());
    libc::free(raw);
    return Ok(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(bytes))));
#else
    (void)path;
    return Err(unsupported_error());
#endif
}

export auto create_dir(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value = rstd::move(path_value).unwrap_unchecked();
    if (libc::mkdir(value.as_ptr(), 0777) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
#else
    (void)path;
    return Err(unsupported_error());
#endif
}

export auto open_directory(ref<Path> path) -> Result<void*> {
#if RSTD_OS_UNIX
    auto path_value = path_cstring(path);
    if (path_value.is_err()) return Err(path_value.unwrap_err_unchecked());
    auto value     = rstd::move(path_value).unwrap_unchecked();
    auto directory = libc::opendir(value.as_ptr());
    if (! directory) return Err(last_error());
    return Ok(static_cast<void*>(directory));
#else
    (void)path;
    return Err(unsupported_error());
#endif
}

export auto read_directory(void* handle) -> Option<Result<DirectoryEntryData>> {
#if RSTD_OS_UNIX
    if (! handle) return None();
    auto directory = static_cast<DIR*>(handle);
    while (true) {
        libc::get_errno() = 0;
        auto entry        = libc::readdir(directory);
        if (! entry) {
            if (libc::get_errno() == 0) return None();
            return Some(Result<DirectoryEntryData>(Err(last_error())));
        }
        auto name = entry->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;

        auto bytes = Vec<u8>::from(CStr::from_ptr(name).to_bytes());
        return Some(Result<DirectoryEntryData>(Ok(DirectoryEntryData {
            .name      = rstd::move(bytes),
            .file_type = file_type_from_dirent(entry->d_type),
        })));
    }
#else
    (void)handle;
    return None();
#endif
}

export void close_directory(void* handle) noexcept {
#if RSTD_OS_UNIX
    if (handle) (void)libc::closedir(static_cast<DIR*>(handle));
#else
    (void)handle;
#endif
}

} // namespace rstd::sys::fs
