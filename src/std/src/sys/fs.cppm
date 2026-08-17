module;
#include <rstd/macro.hpp>
export module rstd:sys.fs;
import :fs.types;
import :io;
import :os.fd;
import :path;
import :sys.libc;
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
using ::alloc::boxed::Box;
using ::alloc::ffi::CString;
using ::alloc::string::String;
using ::alloc::vec::Vec;
namespace libc = rstd::sys::libc;

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

#if RSTD_OS_WINDOWS
inline auto windows_error() noexcept -> Error {
    return Error::from_raw_os_error(i32(libc::GetLastError()));
}

auto path_wide(ref<Path> path) -> Result<Vec<wchar_t>> {
    auto os    = path.as_os_str();
    auto bytes = os.as_encoded_bytes();
    if (bytes.is_empty() || bytes.len().to_primitive() > 0x7fffffff) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto input    = reinterpret_cast<const char*>(bytes.as_raw_ptr());
    auto count    = static_cast<int>(bytes.len().to_primitive());
    auto required = libc::MultiByteToWideChar(
        libc::M_CP_UTF8, libc::M_MB_ERR_INVALID_CHARS, input, count, nullptr, 0);
    if (required <= 0) return Err(windows_error());

    auto result = Vec<wchar_t>::with_capacity(usize(static_cast<rstd::size_t>(required) + 1));
    result.resize(usize(static_cast<rstd::size_t>(required) + 1), wchar_t {});
    if (libc::MultiByteToWideChar(libc::M_CP_UTF8,
                                  libc::M_MB_ERR_INVALID_CHARS,
                                  input,
                                  count,
                                  result.as_mut_ptr(),
                                  required) != required) {
        return Err(windows_error());
    }
    result[usize(static_cast<rstd::size_t>(required))] = L'\0';
    return Ok(rstd::move(result));
}

auto utf8_from_wide(const wchar_t* data, rstd::size_t length) -> Result<Vec<u8>> {
    if (length > 0x7fffffff) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
    }
    if (length == 0) return Ok(Vec<u8>::make());
    auto count    = static_cast<int>(length);
    auto required = libc::WideCharToMultiByte(
        libc::M_CP_UTF8, libc::M_WC_ERR_INVALID_CHARS, data, count, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return Err(windows_error());

    auto result = Vec<u8>::with_capacity(usize(static_cast<rstd::size_t>(required)));
    result.resize(usize(static_cast<rstd::size_t>(required)), u8 {});
    if (libc::WideCharToMultiByte(libc::M_CP_UTF8,
                                  libc::M_WC_ERR_INVALID_CHARS,
                                  data,
                                  count,
                                  reinterpret_cast<char*>(result.as_mut_ptr().as_raw_ptr()),
                                  required,
                                  nullptr,
                                  nullptr) != required) {
        return Err(windows_error());
    }
    return Ok(rstd::move(result));
}

auto filetime_value(libc::FILETIME value) noexcept -> rstd::uint64_t {
    return (static_cast<rstd::uint64_t>(value.dwHighDateTime) << 32) |
           static_cast<rstd::uint64_t>(value.dwLowDateTime);
}

auto system_time(libc::FILETIME value) noexcept -> Option<rstd::time::SystemTime> {
    constexpr rstd::uint64_t WINDOWS_EPOCH = 116'444'736'000'000'000ULL;
    auto const               raw           = filetime_value(value);
    if (raw < WINDOWS_EPOCH) return None();
    auto const elapsed = raw - WINDOWS_EPOCH;
    auto const seconds = elapsed / 10'000'000ULL;
    if (seconds > static_cast<rstd::uint64_t>(0x7fffffffffffffffLL)) return None();
    return rstd::time::SystemTime::from_unix_time(
        i64(static_cast<rstd::int64_t>(seconds)),
        u32(static_cast<rstd::uint32_t>((elapsed % 10'000'000ULL) * 100ULL)));
}

auto filetime(rstd::time::SystemTime value) noexcept -> Option<libc::FILETIME> {
    constexpr rstd::int64_t  WINDOWS_EPOCH_SECONDS = 11'644'473'600LL;
    constexpr rstd::uint64_t MAX_SECONDS           = 0xffffffffffffffffULL / 10'000'000ULL;
    auto const               timestamp             = value.as_unix_time();
    auto const               seconds               = timestamp.seconds.to_primitive();
    if (seconds < -WINDOWS_EPOCH_SECONDS) return None();
    auto const shifted = static_cast<rstd::uint64_t>(seconds + WINDOWS_EPOCH_SECONDS);
    if (shifted > MAX_SECONDS) return None();
    auto const raw = shifted * 10'000'000ULL +
                     static_cast<rstd::uint64_t>(timestamp.nanoseconds.to_primitive() / 100);
    return Some(libc::FILETIME {
        .dwLowDateTime  = static_cast<libc::DWORD>(raw),
        .dwHighDateTime = static_cast<libc::DWORD>(raw >> 32),
    });
}

auto file_type_from_attributes(libc::DWORD attributes, bool reparse_is_symlink = true) noexcept
    -> rstd::fs::FileTypeKind {
    if (reparse_is_symlink && (attributes & libc::M_FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return rstd::fs::FileTypeKind::Symlink;
    }
    if ((attributes & libc::M_FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return rstd::fs::FileTypeKind::Directory;
    }
    return rstd::fs::FileTypeKind::File;
}

auto metadata_from_file_info(const libc::BY_HANDLE_FILE_INFORMATION& info,
                             bool reparse_is_symlink = true) noexcept -> MetadataData {
    auto const size     = (static_cast<rstd::uint64_t>(info.nFileSizeHigh) << 32) |
                          static_cast<rstd::uint64_t>(info.nFileSizeLow);
    auto const index    = (static_cast<rstd::uint64_t>(info.nFileIndexHigh) << 32) |
                          static_cast<rstd::uint64_t>(info.nFileIndexLow);
    auto const writable = (info.dwFileAttributes & libc::M_FILE_ATTRIBUTE_READONLY) == 0;
    auto const mode     = writable ? 0666u : 0444u;
    return MetadataData {
        .file_type   = file_type_from_attributes(info.dwFileAttributes, reparse_is_symlink),
        .len         = u64(size),
        .permissions = u32(mode),
        .accessed    = system_time(info.ftLastAccessTime),
        .modified    = system_time(info.ftLastWriteTime),
        .created     = system_time(info.ftCreationTime),
        .dev         = u64(info.dwVolumeSerialNumber),
        .ino         = u64(index),
        .mode        = u32(mode),
        .nlink       = u64(info.nNumberOfLinks),
    };
}

auto open_native(ref<Path> path, libc::DWORD access, libc::DWORD creation, libc::DWORD flags)
    -> Result<libc::HANDLE> {
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value  = rstd::move(wide).unwrap_unchecked();
    auto handle = libc::CreateFileW(value.as_ptr(),
                                    access,
                                    libc::M_FILE_SHARE_READ | libc::M_FILE_SHARE_WRITE |
                                        libc::M_FILE_SHARE_DELETE,
                                    nullptr,
                                    creation,
                                    flags,
                                    nullptr);
    if (handle == libc::M_INVALID_HANDLE_VALUE) return Err(windows_error());
    return Ok(handle);
}

auto path_from_handle(libc::HANDLE handle) -> Result<PathBuf> {
    auto wide = Vec<wchar_t>::with_capacity(usize(512));
    wide.resize(usize(512), wchar_t {});
    while (true) {
        auto count =
            libc::GetFinalPathNameByHandleW(handle,
                                            wide.as_mut_ptr(),
                                            static_cast<libc::DWORD>(wide.len().to_primitive()),
                                            libc::M_FILE_NAME_NORMALIZED | libc::M_VOLUME_NAME_DOS);
        if (count == 0) return Err(windows_error());
        if (count < wide.len().to_primitive()) {
            auto length = static_cast<rstd::size_t>(count);
            auto start  = rstd::size_t {};
            auto unc    = false;
            if (length >= 8 && wide[usize()] == L'\\' && wide[usize(1)] == L'\\' &&
                wide[usize(2)] == L'?' && wide[usize(3)] == L'\\' && wide[usize(4)] == L'U' &&
                wide[usize(5)] == L'N' && wide[usize(6)] == L'C' && wide[usize(7)] == L'\\') {
                start = 8;
                unc   = true;
            } else if (length >= 4 && wide[usize()] == L'\\' && wide[usize(1)] == L'\\' &&
                       wide[usize(2)] == L'?' && wide[usize(3)] == L'\\') {
                start = 4;
            }
            auto bytes = utf8_from_wide(wide.as_ptr() + start, length - start);
            if (bytes.is_err()) return Err(rstd::move(bytes).unwrap_err_unchecked());
            auto encoded = rstd::move(bytes).unwrap_unchecked();
            if (unc) {
                auto prefixed = Vec<u8>::with_capacity(encoded.len() + usize(2));
                prefixed.push(u8('\\'));
                prefixed.push(u8('\\'));
                prefixed.extend_from_slice(encoded.as_slice());
                encoded = rstd::move(prefixed);
            }
            return Ok(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(encoded))));
        }
        wide.resize(usize(static_cast<rstd::size_t>(count) + 1), wchar_t {});
    }
}

struct WindowsDirectory {
    libc::HANDLE           handle { libc::M_INVALID_HANDLE_VALUE };
    libc::WIN32_FIND_DATAW entry {};
    bool                   pending { true };
};
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
#elif RSTD_OS_WINDOWS
    if (! options.read && ! options.write && ! options.append) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    if (! options.write && ! options.append &&
        (options.truncate || options.create || options.create_new)) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    if (options.append && options.truncate && ! options.create_new) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }

    auto access = libc::DWORD {};
    if (options.read) access |= libc::M_GENERIC_READ;
    if (options.write) access |= libc::M_GENERIC_WRITE;
    if (options.append) access |= libc::M_FILE_APPEND_DATA;

    auto creation = libc::M_OPEN_EXISTING;
    if (options.create_new) {
        creation = libc::M_CREATE_NEW;
    } else if (options.create && options.truncate) {
        creation = libc::M_CREATE_ALWAYS;
    } else if (options.create) {
        creation = libc::M_OPEN_ALWAYS;
    } else if (options.truncate) {
        creation = libc::M_TRUNCATE_EXISTING;
    }

    auto flags  = libc::M_FILE_ATTRIBUTE_NORMAL |
                  static_cast<libc::DWORD>(options.custom_flags.to_primitive());
    auto handle = open_native(path, access, creation, flags);
    if (handle.is_err()) return Err(rstd::move(handle).unwrap_err_unchecked());
    return Ok(OwnedFd::from_raw_fd(static_cast<RawFd>(handle.unwrap_unchecked())));
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
#elif RSTD_OS_WINDOWS
    auto count  = libc::DWORD {};
    auto length = buffer.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    if (! libc::ReadFile(static_cast<libc::HANDLE>(fd),
                         buffer.as_raw_ptr(),
                         static_cast<libc::DWORD>(length),
                         &count,
                         nullptr)) {
        auto error = libc::GetLastError();
        if (error == libc::M_ERROR_HANDLE_EOF) return Ok(usize());
        return Err(Error::from_raw_os_error(i32(error)));
    }
    return Ok(usize(count));
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
#elif RSTD_OS_WINDOWS
    auto count  = libc::DWORD {};
    auto length = buffer.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    if (! libc::WriteFile(static_cast<libc::HANDLE>(fd),
                          buffer.as_raw_ptr(),
                          static_cast<libc::DWORD>(length),
                          &count,
                          nullptr)) {
        return Err(windows_error());
    }
    return Ok(usize(count));
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
#elif RSTD_OS_WINDOWS
    auto whence = libc::M_FILE_BEGIN;
    auto offset = libc::LARGE_INTEGER {};
    switch (position.which) {
    case SeekFrom::Which::Start:
        if (position.start.to_primitive() > 0x7fffffffffffffffULL) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        offset.QuadPart = static_cast<decltype(offset.QuadPart)>(position.start.to_primitive());
        break;
    case SeekFrom::Which::End:
        whence          = libc::M_FILE_END;
        offset.QuadPart = position.offset.to_primitive();
        break;
    case SeekFrom::Which::Current:
        whence          = libc::M_FILE_CURRENT;
        offset.QuadPart = position.offset.to_primitive();
        break;
    }
    auto result = libc::LARGE_INTEGER {};
    if (! libc::SetFilePointerEx(static_cast<libc::HANDLE>(fd), offset, &result, whence)) {
        return Err(windows_error());
    }
    if (result.QuadPart < 0) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    return Ok(u64(static_cast<rstd::uint64_t>(result.QuadPart)));
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
#elif RSTD_OS_WINDOWS
    if (! libc::FlushFileBuffers(static_cast<libc::HANDLE>(fd))) return Err(windows_error());
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
#elif RSTD_OS_WINDOWS
    return sync_all(fd);
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
#elif RSTD_OS_WINDOWS
    if (size.to_primitive() > 0x7fffffffffffffffULL) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto handle  = static_cast<libc::HANDLE>(fd);
    auto zero    = libc::LARGE_INTEGER {};
    auto current = libc::LARGE_INTEGER {};
    if (! libc::SetFilePointerEx(handle, zero, &current, libc::M_FILE_CURRENT)) {
        return Err(windows_error());
    }
    auto target     = libc::LARGE_INTEGER {};
    target.QuadPart = static_cast<decltype(target.QuadPart)>(size.to_primitive());
    if (! libc::SetFilePointerEx(handle, target, nullptr, libc::M_FILE_BEGIN) ||
        ! libc::SetEndOfFile(handle)) {
        auto error = windows_error();
        (void)libc::SetFilePointerEx(handle, current, nullptr, libc::M_FILE_BEGIN);
        return Err(rstd::move(error));
    }
    if (! libc::SetFilePointerEx(handle, current, nullptr, libc::M_FILE_BEGIN)) {
        return Err(windows_error());
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
#elif RSTD_OS_WINDOWS
    auto       overlapped = libc::OVERLAPPED {};
    auto const raw_offset = offset.to_primitive();
    overlapped.Offset     = static_cast<libc::DWORD>(raw_offset);
    overlapped.OffsetHigh = static_cast<libc::DWORD>(raw_offset >> 32);
    auto count            = libc::DWORD {};
    auto length           = buffer.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    auto handle = static_cast<libc::HANDLE>(fd);
    if (! libc::ReadFile(
            handle, buffer.as_raw_ptr(), static_cast<libc::DWORD>(length), &count, &overlapped)) {
        auto error = libc::GetLastError();
        if (error == libc::M_ERROR_HANDLE_EOF) return Ok(usize());
        if (error != libc::ERROR_IO_PENDING ||
            ! libc::GetOverlappedResult(handle, &overlapped, &count, libc::M_TRUE)) {
            return Err(Error::from_raw_os_error(
                i32(error == libc::ERROR_IO_PENDING ? libc::GetLastError() : error)));
        }
    }
    return Ok(usize(count));
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
#elif RSTD_OS_WINDOWS
    auto       overlapped = libc::OVERLAPPED {};
    auto const raw_offset = offset.to_primitive();
    overlapped.Offset     = static_cast<libc::DWORD>(raw_offset);
    overlapped.OffsetHigh = static_cast<libc::DWORD>(raw_offset >> 32);
    auto count            = libc::DWORD {};
    auto length           = buffer.len().to_primitive();
    if (length > 0x7ffff000u) length = 0x7ffff000u;
    auto handle = static_cast<libc::HANDLE>(fd);
    if (! libc::WriteFile(
            handle, buffer.as_raw_ptr(), static_cast<libc::DWORD>(length), &count, &overlapped)) {
        auto error = libc::GetLastError();
        if (error != libc::ERROR_IO_PENDING ||
            ! libc::GetOverlappedResult(handle, &overlapped, &count, libc::M_TRUE)) {
            return Err(Error::from_raw_os_error(
                i32(error == libc::ERROR_IO_PENDING ? libc::GetLastError() : error)));
        }
    }
    return Ok(usize(count));
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
#elif RSTD_OS_WINDOWS
    auto overlapped = libc::OVERLAPPED {};
    if (mode == LockMode::Unlock) {
        if (! libc::UnlockFileEx(
                static_cast<libc::HANDLE>(fd), 0, 0xffffffffu, 0xffffffffu, &overlapped)) {
            return Err(Error::from_raw_os_error(i32(libc::GetLastError())));
        }
        return Ok(empty {});
    }

    auto flags = libc::DWORD {};
    if (mode == LockMode::Exclusive || mode == LockMode::TryExclusive) {
        flags |= libc::LOCKFILE_EXCLUSIVE_LOCK;
    }
    if (mode == LockMode::TryExclusive || mode == LockMode::TryShared) {
        flags |= libc::LOCKFILE_FAIL_IMMEDIATELY;
    }
    if (! libc::LockFileEx(
            static_cast<libc::HANDLE>(fd), flags, 0, 0xffffffffu, 0xffffffffu, &overlapped)) {
        auto error = libc::GetLastError();
        if (error == libc::ERROR_LOCK_VIOLATION &&
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
#elif RSTD_OS_WINDOWS
    auto info = libc::BY_HANDLE_FILE_INFORMATION {};
    if (! libc::GetFileInformationByHandle(static_cast<libc::HANDLE>(fd), &info)) {
        return Err(windows_error());
    }
    return Ok(metadata_from_file_info(info));
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
#elif RSTD_OS_WINDOWS
    auto flags = libc::M_FILE_FLAG_BACKUP_SEMANTICS;
    if (! follow) flags |= libc::M_FILE_FLAG_OPEN_REPARSE_POINT;
    auto handle = open_native(path, libc::M_FILE_READ_ATTRIBUTES, libc::M_OPEN_EXISTING, flags);
    if (handle.is_err()) return Err(rstd::move(handle).unwrap_err_unchecked());
    auto native = handle.unwrap_unchecked();
    auto info   = libc::BY_HANDLE_FILE_INFORMATION {};
    if (! libc::GetFileInformationByHandle(native, &info)) {
        auto error = windows_error();
        (void)libc::CloseHandle(native);
        return Err(rstd::move(error));
    }
    (void)libc::CloseHandle(native);
    return Ok(metadata_from_file_info(info, ! follow));
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
#elif RSTD_OS_WINDOWS
    auto info   = libc::FILE_BASIC_INFO {};
    auto handle = static_cast<libc::HANDLE>(fd);
    if (! libc::GetFileInformationByHandleEx(
            handle, libc::M_FILE_BASIC_INFO_CLASS, &info, sizeof(info))) {
        return Err(windows_error());
    }
    if ((mode.to_primitive() & 0222u) == 0)
        info.FileAttributes |= libc::M_FILE_ATTRIBUTE_READONLY;
    else
        info.FileAttributes &= ~libc::M_FILE_ATTRIBUTE_READONLY;
    if (! libc::SetFileInformationByHandle(
            handle, libc::M_FILE_BASIC_INFO_CLASS, &info, sizeof(info))) {
        return Err(windows_error());
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
#elif RSTD_OS_WINDOWS
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value      = rstd::move(wide).unwrap_unchecked();
    auto attributes = libc::GetFileAttributesW(value.as_ptr());
    if (attributes == libc::M_INVALID_FILE_ATTRIBUTES) return Err(windows_error());
    if ((mode.to_primitive() & 0222u) == 0)
        attributes |= libc::M_FILE_ATTRIBUTE_READONLY;
    else
        attributes &= ~libc::M_FILE_ATTRIBUTE_READONLY;
    if (! libc::SetFileAttributesW(value.as_ptr(), attributes)) return Err(windows_error());
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
#elif RSTD_OS_WINDOWS
    auto accessed = Option<libc::FILETIME> {};
    auto modified = Option<libc::FILETIME> {};
    if (times.accessed.is_some()) {
        accessed = filetime(*times.accessed);
        if (accessed.is_none()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
    }
    if (times.modified.is_some()) {
        modified = filetime(*times.modified);
        if (modified.is_none()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
    }
    auto* accessed_ptr = accessed.is_some() ? rstd::addressof(*accessed) : nullptr;
    auto* modified_ptr = modified.is_some() ? rstd::addressof(*modified) : nullptr;
    if (! libc::SetFileTime(static_cast<libc::HANDLE>(fd), nullptr, accessed_ptr, modified_ptr)) {
        return Err(windows_error());
    }
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
#elif RSTD_OS_WINDOWS
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::DeleteFileW(value.as_ptr())) return Err(windows_error());
    return Ok(empty {});
#else
    return run_path(path, nullptr);
#endif
}

export auto remove_dir(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_path(path, libc::rmdir);
#elif RSTD_OS_WINDOWS
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::RemoveDirectoryW(value.as_ptr())) return Err(windows_error());
    return Ok(empty {});
#else
    return run_path(path, nullptr);
#endif
}

export auto rename(ref<Path> from, ref<Path> to) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(from, to, libc::rename);
#elif RSTD_OS_WINDOWS
    auto from_wide = path_wide(from);
    if (from_wide.is_err()) return Err(rstd::move(from_wide).unwrap_err_unchecked());
    auto to_wide = path_wide(to);
    if (to_wide.is_err()) return Err(rstd::move(to_wide).unwrap_err_unchecked());
    auto from_value = rstd::move(from_wide).unwrap_unchecked();
    auto to_value   = rstd::move(to_wide).unwrap_unchecked();
    if (! libc::MoveFileExW(
            from_value.as_ptr(), to_value.as_ptr(), libc::M_MOVEFILE_REPLACE_EXISTING)) {
        return Err(windows_error());
    }
    return Ok(empty {});
#else
    return run_paths(from, to, nullptr);
#endif
}

export auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(original, link, libc::link);
#elif RSTD_OS_WINDOWS
    auto original_wide = path_wide(original);
    if (original_wide.is_err()) return Err(rstd::move(original_wide).unwrap_err_unchecked());
    auto link_wide = path_wide(link);
    if (link_wide.is_err()) return Err(rstd::move(link_wide).unwrap_err_unchecked());
    auto original_value = rstd::move(original_wide).unwrap_unchecked();
    auto link_value     = rstd::move(link_wide).unwrap_unchecked();
    if (! libc::CreateHardLinkW(link_value.as_ptr(), original_value.as_ptr(), nullptr)) {
        return Err(windows_error());
    }
    return Ok(empty {});
#else
    return run_paths(original, link, nullptr);
#endif
}

export auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return run_paths(original, link, libc::symlink);
#elif RSTD_OS_WINDOWS
    auto original_wide = path_wide(original);
    if (original_wide.is_err()) return Err(rstd::move(original_wide).unwrap_err_unchecked());
    auto link_wide = path_wide(link);
    if (link_wide.is_err()) return Err(rstd::move(link_wide).unwrap_err_unchecked());
    auto original_value = rstd::move(original_wide).unwrap_unchecked();
    auto link_value     = rstd::move(link_wide).unwrap_unchecked();
    auto attributes     = libc::GetFileAttributesW(original_value.as_ptr());
    auto flags          = libc::M_SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (attributes != libc::M_INVALID_FILE_ATTRIBUTES &&
        (attributes & libc::M_FILE_ATTRIBUTE_DIRECTORY) != 0) {
        flags |= libc::M_SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
    if (! libc::CreateSymbolicLinkW(link_value.as_ptr(), original_value.as_ptr(), flags)) {
        return Err(windows_error());
    }
    return Ok(empty {});
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
#elif RSTD_OS_WINDOWS
    auto handle = open_native(path,
                              libc::M_FILE_READ_ATTRIBUTES,
                              libc::M_OPEN_EXISTING,
                              libc::M_FILE_FLAG_BACKUP_SEMANTICS);
    if (handle.is_err()) return Err(rstd::move(handle).unwrap_err_unchecked());
    auto native = handle.unwrap_unchecked();
    auto result = path_from_handle(native);
    (void)libc::CloseHandle(native);
    return result;
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
#elif RSTD_OS_WINDOWS
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::CreateDirectoryW(value.as_ptr(), nullptr)) return Err(windows_error());
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
#elif RSTD_OS_WINDOWS
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto pattern = rstd::move(wide).unwrap_unchecked();
    (void)pattern.pop();
    if (! pattern.is_empty()) {
        auto last = pattern[pattern.len() - usize(1)];
        if (last != L'\\' && last != L'/') pattern.push(L'\\');
    }
    pattern.push(L'*');
    pattern.push(L'\0');

    auto state    = Box<WindowsDirectory>::make();
    state->handle = libc::FindFirstFileW(pattern.as_ptr(), &state->entry);
    if (state->handle == libc::M_INVALID_HANDLE_VALUE) return Err(windows_error());
    auto raw = rstd::move(state).into_raw().as_raw_ptr();
    return Ok(static_cast<void*>(raw));
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
#elif RSTD_OS_WINDOWS
    if (! handle) return None();
    auto& state = *static_cast<WindowsDirectory*>(handle);
    while (true) {
        if (state.pending) {
            state.pending = false;
        } else if (! libc::FindNextFileW(state.handle, &state.entry)) {
            auto error = libc::GetLastError();
            if (error == libc::M_ERROR_NO_MORE_FILES) return None();
            return Some(Result<DirectoryEntryData>(Err(Error::from_raw_os_error(i32(error)))));
        }

        auto* name = state.entry.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }
        auto length = rstd::size_t {};
        while (name[length] != L'\0') ++length;
        auto converted = utf8_from_wide(name, length);
        if (converted.is_err()) {
            return Some(
                Result<DirectoryEntryData>(Err(rstd::move(converted).unwrap_err_unchecked())));
        }
        return Some(Result<DirectoryEntryData>(Ok(DirectoryEntryData {
            .name      = rstd::move(converted).unwrap_unchecked(),
            .file_type = file_type_from_attributes(state.entry.dwFileAttributes),
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
#elif RSTD_OS_WINDOWS
    if (! handle) return;
    auto* state = static_cast<WindowsDirectory*>(handle);
    if (state->handle != libc::M_INVALID_HANDLE_VALUE) (void)libc::FindClose(state->handle);
    auto pointer = mut_ptr<WindowsDirectory>::from_raw_parts(state);
    auto owned   = Box<WindowsDirectory>::from_raw(pointer);
    (void)owned;
#else
    (void)handle;
#endif
}

} // namespace rstd::sys::fs
