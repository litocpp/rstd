module;
#include <rstd/macro.hpp>
module rstd;
import :sys.fs.windows;
import :sys.fs.contract;
import :fs.types;
import :io;
import :os.fd;
import :path;
import :sys.os_str.windows;
import :sys.libc;
import :time;
import rstd.alloc;

namespace rstd::sys::fs::windows
{

using rstd::io::ErrorKind;
using rstd::io::SeekFrom;
using rstd::io::error::Error;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;
using rstd::path::Path;
using rstd::path::PathBuf;
using rstd::ffi::OsString;
using ::alloc::vec::Vec;
namespace libc = rstd::sys::libc;

template<typename T>
using Result = rstd::io::Result<T>;

inline auto windows_error() noexcept -> Error {
    return Error::from_raw_os_error(i32(libc::GetLastError()));
}

auto path_wide(ref<Path> path) -> Result<Vec<wchar_t>> {
    if (path.as_os_str().is_empty())
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    auto converted = rstd::sys::os_str::windows::to_wide(path.as_os_str());
    if (converted.is_err()) return Err(rstd::move(converted).unwrap_err_unchecked());
    auto result = rstd::move(converted).unwrap_unchecked();
    auto length = result.len() - usize(1);
    auto slash  = [](wchar_t value) noexcept {
        return value == L'\\' || value == L'/';
    };
    auto drive_absolute = length >= usize(3) &&
                          ((result[usize()] >= L'A' && result[usize()] <= L'Z') ||
                           (result[usize()] >= L'a' && result[usize()] <= L'z')) &&
                          result[usize(1)] == L':' && slash(result[usize(2)]);
    auto unc_absolute   = length >= usize(3) && slash(result[usize()]) && slash(result[usize(1)]) &&
                          result[usize(2)] != L'?';
    if ((drive_absolute || unc_absolute) && length >= usize(248)) {
        auto extended = Vec<wchar_t>::with_capacity(length + usize(9));
        extended.push(L'\\');
        extended.push(L'\\');
        extended.push(L'?');
        extended.push(L'\\');
        auto start = usize {};
        if (unc_absolute) {
            extended.push(L'U');
            extended.push(L'N');
            extended.push(L'C');
            extended.push(L'\\');
            start = usize(2);
        }
        for (auto index = start; index < length; ++index) {
            extended.push(result[index] == L'/' ? L'\\' : result[index]);
        }
        extended.push(L'\0');
        return Ok(rstd::move(extended));
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
            auto native =
                rstd::sys::os_str::windows::from_wide(wide.as_ptr() + start, usize(length - start));
            auto encoded = rstd::move(native).into_encoded_bytes();
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

struct NativeOpenOptions {
    libc::DWORD access;
    libc::DWORD creation;
    libc::DWORD disposition;
    libc::DWORD flags;
};

auto parse_open_options(OpenOptionsData const& options) -> Result<NativeOpenOptions> {
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
    if (options.append) {
        access |= libc::M_FILE_GENERIC_WRITE & ~libc::M_FILE_WRITE_DATA;
    } else if (options.write) {
        access |= libc::M_GENERIC_WRITE;
    }

    libc::DWORD creation    = libc::M_OPEN_EXISTING;
    libc::DWORD disposition = libc::M_FILE_OPEN;
    if (options.create_new) {
        creation    = libc::M_CREATE_NEW;
        disposition = libc::M_FILE_CREATE;
    } else if (options.create && options.truncate) {
        creation    = libc::M_CREATE_ALWAYS;
        disposition = libc::M_FILE_OVERWRITE_IF;
    } else if (options.create) {
        creation    = libc::M_OPEN_ALWAYS;
        disposition = libc::M_FILE_OPEN_IF;
    } else if (options.truncate) {
        creation    = libc::M_TRUNCATE_EXISTING;
        disposition = libc::M_FILE_OVERWRITE;
    }

    auto flags = libc::M_FILE_ATTRIBUTE_NORMAL |
                 static_cast<libc::DWORD>(options.custom_flags.to_primitive());
    if (options.create_new) flags |= libc::M_FILE_FLAG_OPEN_REPARSE_POINT;
    return Ok(NativeOpenOptions { access, creation, disposition, flags });
}

auto open(ref<Path> path, OpenOptionsData const& options) -> Result<OwnedFd> {
    auto native = rstd_try(parse_open_options(options));
    auto handle = open_native(path, native.access, native.creation, native.flags);
    if (handle.is_err()) return Err(rstd::move(handle).unwrap_err_unchecked());
    return Ok(OwnedFd::from_raw_fd(static_cast<RawFd>(handle.unwrap_unchecked())));
}

auto open_directory_native(ref<Path> path, libc::DWORD access, libc::DWORD flags)
    -> Result<OwnedFd> {
    auto                             raw    = rstd_try(open_native(
        path, access, libc::M_OPEN_EXISTING, flags | libc::M_FILE_FLAG_BACKUP_SEMANTICS));
    auto                             handle = OwnedFd::from_raw_fd(raw);
    libc::BY_HANDLE_FILE_INFORMATION info {};
    if (! libc::GetFileInformationByHandle(raw, &info)) return Err(windows_error());
    if ((info.dwFileAttributes & libc::M_FILE_ATTRIBUTE_DIRECTORY) == 0)
        return Err(Error::from_kind(ErrorKind { ErrorKind::NotADirectory }));
    return Ok(rstd::move(handle));
}

auto open_directory(ref<Path> path, OpenOptionsData const& options) -> Result<OwnedFd> {
    auto native = rstd_try(parse_open_options(options));
    // CreateFileW cannot constrain creation/truncation to directories.
    if (options.create || options.create_new || options.truncate)
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return open_directory_native(path, native.access, native.flags);
}

auto open_directory_for_traversal(ref<Path> path) -> Result<OwnedFd> {
    return open_directory_native(path, libc::M_FILE_TRAVERSE, libc::M_FILE_ATTRIBUTE_NORMAL);
}

auto relative_path_wide(ref<Path> path) -> Result<Vec<wchar_t>> {
    auto wide = rstd_try(rstd::sys::os_str::windows::to_wide(path.as_os_str(), false));
    if (wide.is_empty() || wide.len() > usize(0xffff / sizeof(wchar_t)))
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return Ok(rstd::move(wide));
}

auto nt_error(long status) noexcept -> Error {
    return Error::from_raw_os_error(i32(libc::RtlNtStatusToDosError(status)));
}

auto open_relative(RawFd       dir,
                   ref<Path>   path,
                   libc::DWORD access,
                   libc::DWORD disposition,
                   libc::DWORD create_options) -> Result<OwnedFd> {
    auto wide   = rstd_try(relative_path_wide(path));
    auto length = static_cast<unsigned short>(wide.len().to_primitive() * sizeof(wchar_t));
    libc::UNICODE_STRING    name { length, length, wide.as_mut_ptr() };
    libc::OBJECT_ATTRIBUTES attributes {};
    attributes.Length        = sizeof(attributes);
    attributes.RootDirectory = dir;
    attributes.ObjectName    = &name;
    libc::IO_STATUS_BLOCK io_status {};
    libc::HANDLE          handle {};
    auto status = libc::NtCreateFile(&handle,
                                     access | libc::M_SYNCHRONIZE,
                                     &attributes,
                                     &io_status,
                                     nullptr,
                                     libc::M_FILE_ATTRIBUTE_NORMAL,
                                     libc::M_FILE_SHARE_READ | libc::M_FILE_SHARE_WRITE |
                                         libc::M_FILE_SHARE_DELETE,
                                     disposition,
                                     create_options | libc::M_FILE_SYNCHRONOUS_IO_NONALERT,
                                     nullptr,
                                     0);
    if (status < 0) return Err(nt_error(status));
    return Ok(OwnedFd::from_raw_fd(handle));
}

auto open_at(RawFd dir, ref<Path> path, OpenOptionsData const& options, bool directory)
    -> Result<OwnedFd> {
    if (! directory && path.is_absolute()) return open(path, options);
    auto native    = rstd_try(parse_open_options(options));
    auto supported = libc::M_FILE_ATTRIBUTE_NORMAL | libc::M_FILE_FLAG_OPEN_REPARSE_POINT |
                     libc::M_FILE_FLAG_BACKUP_SEMANTICS;
    if ((native.flags & ~supported) != 0)
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    auto flags = directory ? libc::M_FILE_DIRECTORY_FILE : libc::M_FILE_NON_DIRECTORY_FILE;
    if ((native.flags & libc::M_FILE_FLAG_OPEN_REPARSE_POINT) != 0)
        flags |= libc::M_FILE_OPEN_REPARSE_POINT;
    if ((native.flags & libc::M_FILE_FLAG_BACKUP_SEMANTICS) != 0)
        flags |= libc::M_FILE_OPEN_FOR_BACKUP_INTENT;
    return open_relative(dir, path, native.access, native.disposition, flags);
}

auto metadata_at(RawFd dir, ref<Path> path, bool follow) -> Result<MetadataData> {
    auto handle = rstd_try(open_relative(dir,
                                         path,
                                         libc::M_FILE_READ_ATTRIBUTES,
                                         libc::M_FILE_OPEN,
                                         follow ? 0 : libc::M_FILE_OPEN_REPARSE_POINT));
    libc::BY_HANDLE_FILE_INFORMATION info {};
    if (! libc::GetFileInformationByHandle(handle.as_raw_fd(), &info)) return Err(windows_error());
    return Ok(metadata_from_file_info(info, ! follow));
}

auto create_dir_at(RawFd dir, ref<Path> path) -> Result<empty> {
    auto handle = rstd_try(open_relative(dir,
                                         path,
                                         libc::M_GENERIC_READ | libc::M_GENERIC_WRITE,
                                         libc::M_FILE_CREATE,
                                         libc::M_FILE_DIRECTORY_FILE));
    return Ok(empty {});
}

auto remove_at(RawFd dir, ref<Path> path, bool directory) -> Result<empty> {
    auto type   = directory ? libc::M_FILE_DIRECTORY_FILE : libc::M_FILE_NON_DIRECTORY_FILE;
    auto handle = rstd_try(open_relative(
        dir, path, libc::M_DELETE, libc::M_FILE_OPEN, type | libc::M_FILE_OPEN_REPARSE_POINT));
    libc::FILE_DISPOSITION_INFO_EX info { libc::M_FILE_DISPOSITION_FLAG_DELETE |
                                          libc::M_FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                          libc::M_FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE };
    if (libc::SetFileInformationByHandle(
            handle.as_raw_fd(), libc::M_FILE_DISPOSITION_INFO_EX_CLASS, &info, sizeof(info)))
        return Ok(empty {});
    auto error = libc::GetLastError();
    if (error != libc::ERROR_INVALID_PARAMETER && error != libc::ERROR_NOT_SUPPORTED &&
        error != libc::M_ERROR_INVALID_FUNCTION)
        return Err(Error::from_raw_os_error(i32(error)));
    libc::FILE_DISPOSITION_INFO fallback { libc::M_TRUE };
    if (! libc::SetFileInformationByHandle(
            handle.as_raw_fd(), libc::M_FILE_DISPOSITION_INFO_CLASS, &fallback, sizeof(fallback)))
        return Err(windows_error());
    return Ok(empty {});
}

auto rename_at(RawFd from_dir, ref<Path> from, RawFd to_dir, ref<Path> to) -> Result<empty> {
    auto name   = rstd_try(relative_path_wide(to));
    auto handle = rstd_try(open_relative(
        from_dir, from, libc::M_DELETE, libc::M_FILE_OPEN, libc::M_FILE_OPEN_REPARSE_POINT));
    using Info  = libc::FILE_RENAME_INFO;
    auto bytes  = name.len().to_primitive() * sizeof(wchar_t);
    auto size   = sizeof(Info) + bytes;
    auto layout = rstd::alloc::Layout::from_size_align_unchecked(usize(size), usize(alignof(Info)));
    auto memory = ::alloc::alloc(layout);
    if (memory == nullptr) return Err(Error::from_kind(ErrorKind { ErrorKind::OutOfMemory }));
    auto info             = new (memory) Info {};
    info->ReplaceIfExists = true;
    info->RootDirectory   = to_dir;
    info->FileNameLength  = static_cast<libc::DWORD>(bytes);
    // Copy the variable-length tail without indexing beyond the SDK's one-element array.
    rstd::mem::memcpy(reinterpret_cast<byte*>(memory) + __builtin_offsetof(Info, FileName),
                      name.as_ptr(),
                      usize(bytes));
    libc::IO_STATUS_BLOCK io_status {};
    auto                  status = libc::NtSetInformationFile(handle.as_raw_fd(),
                                                              &io_status,
                                                              info,
                                                              static_cast<libc::DWORD>(size),
                                                              libc::M_FILE_RENAME_INFORMATION);
    ::alloc::dealloc(memory, layout);
    if (status < 0) return Err(nt_error(status));
    return Ok(empty {});
}

auto read(RawFd fd, mut_ref<byte[]> buffer) -> Result<usize> {
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
}

auto write(RawFd fd, slice<byte> buffer) -> Result<usize> {
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
}

auto seek(RawFd fd, SeekFrom position) -> Result<u64> {
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
}

auto sync_all(RawFd fd) -> Result<empty> {
    if (! libc::FlushFileBuffers(static_cast<libc::HANDLE>(fd))) return Err(windows_error());
    return Ok(empty {});
}

auto sync_data(RawFd fd) -> Result<empty> {
    return sync_all(fd);
}

auto set_len(RawFd fd, u64 size) -> Result<empty> {
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
}

auto read_at(RawFd fd, mut_ref<byte[]> buffer, u64 offset) -> Result<usize> {
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
}

auto write_at(RawFd fd, slice<byte> buffer, u64 offset) -> Result<usize> {
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
}

auto lock(RawFd fd, LockMode mode) -> Result<empty> {
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
}

auto metadata(RawFd fd) -> Result<MetadataData> {
    auto info = libc::BY_HANDLE_FILE_INFORMATION {};
    if (! libc::GetFileInformationByHandle(static_cast<libc::HANDLE>(fd), &info)) {
        return Err(windows_error());
    }
    return Ok(metadata_from_file_info(info));
}

auto metadata(ref<Path> path, bool follow) -> Result<MetadataData> {
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
}

auto set_permissions(RawFd fd, u32 mode) -> Result<empty> {
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
}

auto set_permissions(ref<Path> path, u32 mode) -> Result<empty> {
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
}

auto set_times(RawFd fd, FileTimesData times) -> Result<empty> {
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
}

auto remove_file(ref<Path> path) -> Result<empty> {
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::DeleteFileW(value.as_ptr())) return Err(windows_error());
    return Ok(empty {});
}

auto remove_dir(ref<Path> path) -> Result<empty> {
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::RemoveDirectoryW(value.as_ptr())) return Err(windows_error());
    return Ok(empty {});
}

auto rename(ref<Path> from, ref<Path> to) -> Result<empty> {
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
}

auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty> {
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
}

auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty> {
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
}

auto read_link(ref<Path> path) -> Result<PathBuf> {
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
}

auto canonicalize(ref<Path> path) -> Result<PathBuf> {
    auto handle = open_native(path,
                              libc::M_FILE_READ_ATTRIBUTES,
                              libc::M_OPEN_EXISTING,
                              libc::M_FILE_FLAG_BACKUP_SEMANTICS);
    if (handle.is_err()) return Err(rstd::move(handle).unwrap_err_unchecked());
    auto native = handle.unwrap_unchecked();
    auto result = path_from_handle(native);
    (void)libc::CloseHandle(native);
    return result;
}

auto create_dir(ref<Path> path) -> Result<empty> {
    auto wide = path_wide(path);
    if (wide.is_err()) return Err(rstd::move(wide).unwrap_err_unchecked());
    auto value = rstd::move(wide).unwrap_unchecked();
    if (! libc::CreateDirectoryW(value.as_ptr(), nullptr)) return Err(windows_error());
    return Ok(empty {});
}

void Directory::close() noexcept {
    if (m_handle != libc::M_INVALID_HANDLE_VALUE) (void)libc::FindClose(m_handle);
    m_handle = libc::M_INVALID_HANDLE_VALUE;
}

Directory::Directory(Directory&& other) noexcept
    : m_handle(other.m_handle), m_entry(other.m_entry), m_pending(other.m_pending) {
    other.m_handle = libc::M_INVALID_HANDLE_VALUE;
}

auto Directory::operator=(Directory&& other) noexcept -> Directory& {
    if (this != &other) {
        close();
        m_handle       = other.m_handle;
        m_entry        = other.m_entry;
        m_pending      = other.m_pending;
        other.m_handle = libc::M_INVALID_HANDLE_VALUE;
    }
    return *this;
}

Directory::~Directory() {
    close();
}

auto Directory::open(ref<Path> path) -> Result<Directory> {
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

    auto result     = Directory {};
    result.m_handle = libc::FindFirstFileW(pattern.as_ptr(), &result.m_entry);
    if (result.m_handle == libc::M_INVALID_HANDLE_VALUE) return Err(windows_error());
    return Ok(rstd::move(result));
}

auto Directory::next() -> Option<Result<DirectoryEntryData>> {
    if (m_handle == libc::M_INVALID_HANDLE_VALUE) return None();
    while (true) {
        if (m_pending) {
            m_pending = false;
        } else if (! libc::FindNextFileW(m_handle, &m_entry)) {
            auto error = libc::GetLastError();
            if (error == libc::M_ERROR_NO_MORE_FILES) return None();
            return Some(Result<DirectoryEntryData>(Err(Error::from_raw_os_error(i32(error)))));
        }

        auto* name = m_entry.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }
        auto length = rstd::size_t {};
        while (name[length] != L'\0') ++length;
        auto converted = rstd::sys::os_str::windows::from_wide(name, usize(length));
        return Some(Result<DirectoryEntryData>(Ok(DirectoryEntryData {
            .name      = rstd::move(converted).into_encoded_bytes(),
            .file_type = file_type_from_attributes(m_entry.dwFileAttributes),
        })));
    }
}

} // namespace rstd::sys::fs::windows
