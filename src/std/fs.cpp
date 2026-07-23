module;

module rstd;
import :fs;
import :sys.fs;

using namespace rstd::prelude;
namespace sys_fs = rstd::sys::fs;

namespace rstd::fs
{

class MetadataFactory {
public:
    static auto make(sys_fs::MetadataData data) -> Metadata {
        Metadata value;
        value.m_file_type   = FileType { data.file_type };
        value.m_len         = data.len;
        value.m_permissions = Permissions::from_mode(data.permissions);
        value.m_accessed    = rstd::move(data.accessed);
        value.m_modified    = rstd::move(data.modified);
        value.m_created     = rstd::move(data.created);
        value.m_dev         = data.dev;
        value.m_rdev_major  = data.rdev_major;
        value.m_rdev_minor  = data.rdev_minor;
        value.m_ino         = data.ino;
        value.m_mode        = data.mode;
        value.m_nlink       = data.nlink;
        value.m_uid         = data.uid;
        value.m_gid         = data.gid;
        return value;
    }
};

auto metadata_result(io::Result<sys_fs::MetadataData> result) -> FsResult<Metadata> {
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(MetadataFactory::make(rstd::move(result).unwrap_unchecked()));
}

auto OpenOptions::open(ref<Path> path) const -> FsResult<File> {
    auto result = sys_fs::open(path,
                               sys_fs::OpenOptionsData {
                                   .read         = m_read,
                                   .write        = m_write,
                                   .append       = m_append,
                                   .truncate     = m_truncate,
                                   .create       = m_create,
                                   .create_new   = m_create_new,
                                   .custom_flags = m_custom_flags,
                                   .mode         = m_mode,
                               });
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(File { rstd::move(result).unwrap_unchecked() });
}

auto File::open(ref<Path> path) -> FsResult<File> {
    auto options = OpenOptions::make();
    options.read(true);
    return options.open(path);
}

auto File::create(ref<Path> path) -> FsResult<File> {
    auto options = OpenOptions::make();
    options.write(true).create(true).truncate(true);
    return options.open(path);
}

auto File::create_new(ref<Path> path) -> FsResult<File> {
    auto options = OpenOptions::make();
    options.read(true).write(true).create_new(true);
    return options.open(path);
}

auto File::read(mut_ref<u8[]> buffer) -> FsResult<usize> {
    return sys_fs::read(m_fd.as_raw_fd(), as_bytes_mut(buffer));
}

auto File::write(slice<u8> buffer) -> FsResult<usize> {
    return sys_fs::write(m_fd.as_raw_fd(), as_bytes(buffer));
}

auto File::flush() -> FsResult<empty> {
    return Ok(empty {});
}

auto File::seek(SeekFrom position) -> FsResult<u64> {
    return sys_fs::seek(m_fd.as_raw_fd(), position);
}

auto File::sync_all() -> FsResult<empty> {
    return sys_fs::sync_all(m_fd.as_raw_fd());
}

auto File::sync_data() -> FsResult<empty> {
    return sys_fs::sync_data(m_fd.as_raw_fd());
}

auto File::set_len(u64 size) -> FsResult<empty> {
    return sys_fs::set_len(m_fd.as_raw_fd(), size);
}

auto File::read_at(mut_ref<u8[]> buffer, u64 offset) const -> FsResult<usize> {
    return sys_fs::read_at(m_fd.as_raw_fd(), as_bytes_mut(buffer), offset);
}

auto File::write_at(slice<u8> buffer, u64 offset) const -> FsResult<usize> {
    return sys_fs::write_at(m_fd.as_raw_fd(), as_bytes(buffer), offset);
}

auto File::read_exact_at(mut_ref<u8[]> buffer, u64 offset) const -> FsResult<empty> {
    return io::read_exact_at(*this, buffer, offset);
}

auto File::write_all_at(slice<u8> buffer, u64 offset) const -> FsResult<empty> {
    return io::write_all_at(*this, buffer, offset);
}

auto File::lock() -> FsResult<empty> {
    return sys_fs::lock(m_fd.as_raw_fd(), sys_fs::LockMode::Exclusive);
}

auto File::lock_shared() -> FsResult<empty> {
    return sys_fs::lock(m_fd.as_raw_fd(), sys_fs::LockMode::Shared);
}

auto File::try_lock() -> FsResult<empty> {
    return sys_fs::lock(m_fd.as_raw_fd(), sys_fs::LockMode::TryExclusive);
}

auto File::try_lock_shared() -> FsResult<empty> {
    return sys_fs::lock(m_fd.as_raw_fd(), sys_fs::LockMode::TryShared);
}

auto File::unlock() -> FsResult<empty> {
    return sys_fs::lock(m_fd.as_raw_fd(), sys_fs::LockMode::Unlock);
}

auto File::metadata() const -> FsResult<Metadata> {
    return metadata_result(sys_fs::metadata(m_fd.as_raw_fd()));
}

auto File::set_permissions(Permissions permissions) -> FsResult<empty> {
    return sys_fs::set_permissions(m_fd.as_raw_fd(), permissions.mode());
}

auto File::set_times(FileTimes times) -> FsResult<empty> {
    return sys_fs::set_times(m_fd.as_raw_fd(),
                             sys_fs::FileTimesData { times.m_accessed, times.m_modified });
}

auto File::set_modified(rstd::time::SystemTime time) -> FsResult<empty> {
    return set_times(FileTimes::make().set_modified(time));
}

auto File::try_clone() const -> FsResult<File> {
    auto result = m_fd.try_clone();
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(File { rstd::move(result).unwrap_unchecked() });
}

auto read_to_end(File& file, Vec<u8>& output) -> FsResult<usize> {
    byte  chunk[4096];
    usize total {};
    while (true) {
        auto values = mut_ref<u8[]>::from_raw_parts(chunk, usize(4096));
        auto result = file.read(values);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        auto count = result.unwrap_unchecked();
        if (count == usize()) break;
        output.extend_from_slice(slice<u8>::from_raw_parts(chunk, count));
        total += count;
    }
    return Ok(total);
}

auto read(ref<Path> path) -> FsResult<Vec<u8>> {
    auto file_result = File::open(path);
    if (file_result.is_err()) return Err(rstd::move(file_result).unwrap_err_unchecked());
    auto file   = rstd::move(file_result).unwrap_unchecked();
    auto output = Vec<u8>::make();
    auto result = read_to_end(file, output);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(rstd::move(output));
}

auto read_to_string(ref<Path> path) -> FsResult<String> {
    auto result = read(path);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    auto bytes = rstd::move(result).unwrap_unchecked();
    if (rstd::str_::validate_utf8(bytes.as_slice()).is_err()) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
    }
    return Ok(String::from_utf8_unchecked(rstd::move(bytes)));
}

auto write(ref<Path> path, slice<u8> contents) -> FsResult<empty> {
    auto file_result = File::create(path);
    if (file_result.is_err()) return Err(rstd::move(file_result).unwrap_err_unchecked());
    auto file = rstd::move(file_result).unwrap_unchecked();
    return io::write_all(file, contents);
}

auto metadata(ref<Path> path) -> FsResult<Metadata> {
    return metadata_result(sys_fs::metadata(path, true));
}

auto symlink_metadata(ref<Path> path) -> FsResult<Metadata> {
    return metadata_result(sys_fs::metadata(path, false));
}

auto exists(ref<Path> path) -> FsResult<bool> {
    auto result = metadata(path);
    if (result.is_ok()) return Ok(true);
    auto error = rstd::move(result).unwrap_err_unchecked();
    if (error.kind() == ErrorKind { ErrorKind::NotFound }) return Ok(false);
    return Err(rstd::move(error));
}

auto remove_file(ref<Path> path) -> FsResult<empty> {
    return sys_fs::remove_file(path);
}

auto remove_dir(ref<Path> path) -> FsResult<empty> {
    return sys_fs::remove_dir(path);
}

auto rename(ref<Path> from, ref<Path> to) -> FsResult<empty> {
    return sys_fs::rename(from, to);
}

auto hard_link(ref<Path> original, ref<Path> link) -> FsResult<empty> {
    return sys_fs::hard_link(original, link);
}

auto soft_link(ref<Path> original, ref<Path> link) -> FsResult<empty> {
    return sys_fs::soft_link(original, link);
}

auto read_link(ref<Path> path) -> FsResult<rstd::path::PathBuf> {
    return sys_fs::read_link(path);
}

auto canonicalize(ref<Path> path) -> FsResult<rstd::path::PathBuf> {
    return sys_fs::canonicalize(path);
}

auto create_dir(ref<Path> path) -> FsResult<empty> {
    return sys_fs::create_dir(path);
}

auto create_dir_all(ref<Path> path) -> FsResult<empty> {
    if (path.is_empty()) return Ok(empty {});
    auto current = metadata(path);
    if (current.is_ok()) {
        if (rstd::move(current).unwrap_unchecked().is_dir()) return Ok(empty {});
        return Err(Error::from_kind(ErrorKind { ErrorKind::AlreadyExists }));
    }
    auto parent = path.parent();
    if (parent.is_some()) {
        auto result = create_dir_all(*parent);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    }
    auto result = create_dir(path);
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err_unchecked();
        if (error.kind() == ErrorKind { ErrorKind::AlreadyExists }) return Ok(empty {});
        return Err(rstd::move(error));
    }
    return Ok(empty {});
}

auto set_permissions(ref<Path> path, Permissions permissions) -> FsResult<empty> {
    return sys_fs::set_permissions(path, permissions.mode());
}

auto copy(ref<Path> from, ref<Path> to) -> FsResult<u64> {
    auto source_result = File::open(from);
    if (source_result.is_err()) return Err(rstd::move(source_result).unwrap_err_unchecked());
    auto source = rstd::move(source_result).unwrap_unchecked();

    auto metadata = source.metadata();
    if (metadata.is_err()) return Err(rstd::move(metadata).unwrap_err_unchecked());
    auto permissions = rstd::move(metadata).unwrap_unchecked().permissions();

    auto destination_result = OpenOptions::make()
                                  .write(true)
                                  .create(true)
                                  .truncate(true)
                                  .mode(permissions.mode())
                                  .open(to);
    if (destination_result.is_err()) {
        return Err(rstd::move(destination_result).unwrap_err_unchecked());
    }
    auto destination = rstd::move(destination_result).unwrap_unchecked();

    byte chunk[8192];
    u64 total {};
    while (true) {
        auto values      = mut_ref<u8[]>::from_raw_parts(chunk, usize(8192));
        auto read_result = source.read(values);
        if (read_result.is_err()) return Err(rstd::move(read_result).unwrap_err_unchecked());
        auto count = read_result.unwrap_unchecked();
        if (count == usize()) break;

        auto written = io::write_all(destination, slice<u8>::from_raw_parts(chunk, count));
        if (written.is_err()) return Err(rstd::move(written).unwrap_err_unchecked());
        total += u64(count.to_primitive());
    }
    return Ok(total);
}

auto DirEntry::path() const -> rstd::path::PathBuf {
    return m_parent.join(rstd::ref<rstd::path::Path>(m_name.as_os_str()));
}

auto DirEntry::file_name() const -> rstd::ffi::OsString {
    return rstd::ffi::OsString::from(m_name.as_os_str());
}

auto DirEntry::file_type() const -> FsResult<FileType> {
    if (m_file_type != FileType {}) return Ok(m_file_type);
    auto full_path = path();
    auto result    = symlink_metadata(rstd::ref<rstd::path::Path>(full_path.as_path()));
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(rstd::move(result).unwrap_unchecked().file_type());
}

auto DirEntry::metadata() const -> FsResult<Metadata> {
    auto full_path = path();
    return symlink_metadata(rstd::ref<rstd::path::Path>(full_path.as_path()));
}

ReadDir::ReadDir(ReadDir&& other) noexcept
    : m_handle(other.m_handle), m_parent(rstd::move(other.m_parent)) {
    other.m_handle = nullptr;
}

auto ReadDir::operator=(ReadDir&& other) noexcept -> ReadDir& {
    if (this != &other) {
        close();
        m_handle       = other.m_handle;
        m_parent       = rstd::move(other.m_parent);
        other.m_handle = nullptr;
    }
    return *this;
}

ReadDir::~ReadDir() {
    close();
}

void ReadDir::close() noexcept {
    sys_fs::close_directory(m_handle);
    m_handle = nullptr;
}

auto ReadDir::next() -> Option<FsResult<DirEntry>> {
    auto result = sys_fs::read_directory(m_handle);
    if (result.is_none()) return None();
    auto entry_result = rstd::move(result).unwrap_unchecked();
    if (entry_result.is_err()) {
        return Some(FsResult<DirEntry>(Err(rstd::move(entry_result).unwrap_err_unchecked())));
    }
    auto entry = rstd::move(entry_result).unwrap_unchecked();
    auto parent =
        rstd::path::PathBuf::from(rstd::ffi::OsString::from(m_parent.as_path().as_os_str()));
    auto name = rstd::ffi::OsString::from_encoded_bytes_unchecked(rstd::move(entry.name));
    return Some(FsResult<DirEntry>(
        Ok(DirEntry { rstd::move(parent), rstd::move(name), FileType { entry.file_type } })));
}

auto read_dir(ref<Path> path) -> FsResult<ReadDir> {
    auto result = sys_fs::open_directory(path);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());

    auto parent = rstd::path::PathBuf::from(rstd::ffi::OsString::from(path.as_os_str()));
    return Ok(ReadDir { rstd::move(result).unwrap_unchecked(), rstd::move(parent) });
}

auto remove_dir_all(ref<Path> path) -> FsResult<empty> {
    auto metadata_result = symlink_metadata(path);
    if (metadata_result.is_err()) {
        return Err(rstd::move(metadata_result).unwrap_err_unchecked());
    }
    if (! rstd::move(metadata_result).unwrap_unchecked().is_dir()) return remove_file(path);

    auto directory_result = read_dir(path);
    if (directory_result.is_err()) {
        return Err(rstd::move(directory_result).unwrap_err_unchecked());
    }
    auto directory = rstd::move(directory_result).unwrap_unchecked();
    while (true) {
        auto next = directory.next();
        if (next.is_none()) break;
        auto entry_result = rstd::move(next).unwrap_unchecked();
        if (entry_result.is_err()) return Err(rstd::move(entry_result).unwrap_err_unchecked());
        auto entry       = rstd::move(entry_result).unwrap_unchecked();
        auto child       = entry.path();
        auto type_result = entry.file_type();
        if (type_result.is_err()) return Err(rstd::move(type_result).unwrap_err_unchecked());

        FsResult<empty> result = type_result.unwrap_unchecked().is_dir()
                                     ? remove_dir_all(rstd::ref<rstd::path::Path>(child.as_path()))
                                     : remove_file(rstd::ref<rstd::path::Path>(child.as_path()));
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    }
    return remove_dir(path);
}

} // namespace rstd::fs
