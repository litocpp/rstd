module;
#include <rstd/macro.hpp>
export module rstd:fs;
export import :io;
export import :path;
export import :sys.fd;
export import :sys.io.stdio;
export import :time;
#if RSTD_OS_UNIX
import :sys.libc.unix;
import :sys.pal.unix.time;
#endif

#if RSTD_OS_UNIX
namespace libc = rstd::sys::libc;
#endif

using rstd::ffi::CString;
using rstd::path::Path;
using rstd::sys::fd::BorrowedFd;
using rstd::sys::fd::OwnedFd;
using rstd::sys::fd::RawFd;
using namespace rstd::prelude;

namespace rstd::fs
{

using rstd::io::Error;
using rstd::io::ErrorKind;
using rstd::io::Result;
using rstd::io::SeekFrom;

export class File;
export class Metadata;

// ── FileType ──────────────────────────────────────────────────────────────
/// Classifies the kind of a filesystem entry (file, directory, symlink, ...).
export struct FileType {
    u32 mode_ { 0 };

    auto is_file() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFREG;
#else
        return false;
#endif
    }
    auto is_dir() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFDIR;
#else
        return false;
#endif
    }
    auto is_symlink() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFLNK;
#else
        return false;
#endif
    }
    // Unix-specific extensions.
    auto is_fifo() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFIFO;
#else
        return false;
#endif
    }
    auto is_block_device() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFBLK;
#else
        return false;
#endif
    }
    auto is_char_device() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFCHR;
#else
        return false;
#endif
    }
    auto is_socket() const noexcept -> bool {
#if RSTD_OS_UNIX
        return (mode_ & libc::S_IFMT) == libc::S_IFSOCK;
#else
        return false;
#endif
    }

    friend auto operator==(FileType a, FileType b) noexcept -> bool {
        return (a.mode_ & 0xF000u) == (b.mode_ & 0xF000u);
    }
};

// ── Permissions ───────────────────────────────────────────────────────────
/// Owner/group/other rwx permission bits. Mirrors `std::fs::Permissions`.
export class Permissions {
public:
    u32 mode_ { 0 };

    auto readonly() const noexcept -> bool {
#if RSTD_OS_UNIX
        // Rust definition: no write bits set anywhere.
        return (mode_ & 0222) == 0;
#else
        return false;
#endif
    }

    auto set_readonly(bool ro) noexcept -> void {
#if RSTD_OS_UNIX
        if (ro) mode_ &= ~u32(0222);
        else    mode_ |= 0222;
#endif
    }

    // Unix-specific:
    auto mode() const noexcept -> u32 { return mode_; }
    auto set_mode(u32 m) noexcept -> void { mode_ = m; }
    static auto from_mode(u32 m) noexcept -> Permissions { return Permissions { m }; }
};

// ── FileTimes ─────────────────────────────────────────────────────────────
/// Optional accessed + modified timestamps for `set_times`.
export struct FileTimes {
    Option<rstd::time::SystemTime> accessed_ {};
    Option<rstd::time::SystemTime> modified_ {};

    static auto make() noexcept -> FileTimes { return {}; }
    auto set_accessed(rstd::time::SystemTime t) noexcept -> FileTimes& {
        accessed_ = Some(t);
        return *this;
    }
    auto set_modified(rstd::time::SystemTime t) noexcept -> FileTimes& {
        modified_ = Some(t);
        return *this;
    }
};

// ── Metadata ──────────────────────────────────────────────────────────────
/// Information returned by `stat(2)` / `fstat(2)`. Mirrors `std::fs::Metadata`.
export class Metadata {
public:
#if RSTD_OS_UNIX
    libc::stat_t st_ {};
#endif

    auto file_type() const noexcept -> FileType {
#if RSTD_OS_UNIX
        return FileType { u32(st_.st_mode) };
#else
        return {};
#endif
    }
    auto is_dir() const noexcept -> bool { return file_type().is_dir(); }
    auto is_file() const noexcept -> bool { return file_type().is_file(); }
    auto is_symlink() const noexcept -> bool { return file_type().is_symlink(); }

    auto len() const noexcept -> u64 {
#if RSTD_OS_UNIX
        return u64(st_.st_size);
#else
        return 0;
#endif
    }

    auto permissions() const noexcept -> Permissions {
#if RSTD_OS_UNIX
        return Permissions { u32(st_.st_mode) & 0777u };
#else
        return {};
#endif
    }

    auto modified() const -> Result<rstd::time::SystemTime> {
#if RSTD_OS_UNIX
        return Ok(rstd::time::SystemTime { rstd::sys::pal::unix::time::SystemTime {
            rstd::sys::pal::unix::time::Timespec {
                i64(st_.st_mtim.tv_sec), u32(st_.st_mtim.tv_nsec) } } });
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }
    auto accessed() const -> Result<rstd::time::SystemTime> {
#if RSTD_OS_UNIX
        return Ok(rstd::time::SystemTime { rstd::sys::pal::unix::time::SystemTime {
            rstd::sys::pal::unix::time::Timespec {
                i64(st_.st_atim.tv_sec), u32(st_.st_atim.tv_nsec) } } });
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }
    auto created() const -> Result<rstd::time::SystemTime> {
        // Linux's struct stat has no btime; statx() does, but mirroring Rust
        // we return Unsupported here. (Phase: future.)
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }

    // Unix-specific accessors:
    auto dev() const noexcept -> u64 {
#if RSTD_OS_UNIX
        return u64(st_.st_dev);
#else
        return 0;
#endif
    }
    auto ino() const noexcept -> u64 {
#if RSTD_OS_UNIX
        return u64(st_.st_ino);
#else
        return 0;
#endif
    }
    auto mode() const noexcept -> u32 {
#if RSTD_OS_UNIX
        return u32(st_.st_mode);
#else
        return 0;
#endif
    }
    auto nlink() const noexcept -> u64 {
#if RSTD_OS_UNIX
        return u64(st_.st_nlink);
#else
        return 0;
#endif
    }
    auto uid() const noexcept -> u32 {
#if RSTD_OS_UNIX
        return u32(st_.st_uid);
#else
        return 0;
#endif
    }
    auto gid() const noexcept -> u32 {
#if RSTD_OS_UNIX
        return u32(st_.st_gid);
#else
        return 0;
#endif
    }
    auto size() const noexcept -> u64 { return len(); }
};

/// Builder for opening files. Mirrors `std::fs::OpenOptions`.
export class OpenOptions {
public:
    bool read_ { false };
    bool write_ { false };
    bool append_ { false };
    bool truncate_ { false };
    bool create_ { false };
    bool create_new_ { false };
    i32  custom_flags_ { 0 };
    u32  mode_ { 0666 };

    static auto make() noexcept -> OpenOptions { return {}; }

    auto read(bool v) noexcept -> OpenOptions& {
        read_ = v;
        return *this;
    }
    auto write(bool v) noexcept -> OpenOptions& {
        write_ = v;
        return *this;
    }
    auto append(bool v) noexcept -> OpenOptions& {
        append_ = v;
        return *this;
    }
    auto truncate(bool v) noexcept -> OpenOptions& {
        truncate_ = v;
        return *this;
    }
    auto create(bool v) noexcept -> OpenOptions& {
        create_ = v;
        return *this;
    }
    auto create_new(bool v) noexcept -> OpenOptions& {
        create_new_ = v;
        return *this;
    }
    auto custom_flags(i32 f) noexcept -> OpenOptions& {
        custom_flags_ = f;
        return *this;
    }
    auto mode(u32 m) noexcept -> OpenOptions& {
        mode_ = m;
        return *this;
    }

    /// Open the file at `path` according to the configured options.
    auto open(ref<Path> path) const -> Result<File>;

#if RSTD_OS_UNIX
    /// Compute the platform open(2) flags from the options. Returns Err if
    /// the combination is invalid (mirrors Rust's get_access_mode/get_creation_mode).
    auto get_access_mode() const noexcept -> Result<i32> {
        if (read_ && !write_ && !append_) return Ok(i32 { libc::O_RDONLY });
        if (!read_ && write_ && !append_) return Ok(i32 { libc::O_WRONLY });
        if (read_ && write_ && !append_)  return Ok(i32 { libc::O_RDWR });
        if (!read_ && append_)            return Ok(i32 { libc::O_WRONLY | libc::O_APPEND });
        if (read_ && append_)             return Ok(i32 { libc::O_RDWR   | libc::O_APPEND });
        return Err(Error::from_raw_os_error(libc::EINVAL));
    }

    auto get_creation_mode() const noexcept -> Result<i32> {
        if (!write_ && !append_) {
            if (truncate_ || create_ || create_new_)
                return Err(Error::from_raw_os_error(libc::EINVAL));
        }
        if (append_) {
            if (truncate_ && !create_new_)
                return Err(Error::from_raw_os_error(libc::EINVAL));
        }
        if (create_new_) return Ok(i32 { libc::O_CREAT | libc::O_EXCL });
        if (create_ && truncate_) return Ok(i32 { libc::O_CREAT | libc::O_TRUNC });
        if (create_) return Ok(i32 { libc::O_CREAT });
        if (truncate_) return Ok(i32 { libc::O_TRUNC });
        return Ok(i32 { 0 });
    }
#endif
};

/// An open filesystem file. Mirrors `std::fs::File`.
export class File {
    OwnedFd fd_;

public:
    File() noexcept = default;
    explicit File(OwnedFd fd) noexcept : fd_(rstd::move(fd)) {}

    /// Open `path` read-only.
    static auto open(ref<Path> path) -> Result<File> {
        OpenOptions o;
        o.read(true);
        return o.open(path);
    }

    /// Open `path` for writing, creating it (truncating if it exists).
    static auto create(ref<Path> path) -> Result<File> {
        OpenOptions o;
        o.write(true).create(true).truncate(true);
        return o.open(path);
    }

    /// Open `path` for writing, failing if it already exists.
    static auto create_new(ref<Path> path) -> Result<File> {
        OpenOptions o;
        o.read(true).write(true).create_new(true);
        return o.open(path);
    }

    /// Returns a new `OpenOptions` builder.
    static auto options() noexcept -> OpenOptions { return OpenOptions::make(); }

    /// Reads up to `len` bytes into `buf`. Retries on EINTR.
    auto read(u8* buf, usize len) -> Result<usize> {
#if RSTD_OS_UNIX
        return rstd::sys::io::stdio::read_fd(fd_.as_raw_fd(), buf, len);
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// Writes up to `len` bytes from `buf`. Retries on EINTR.
    auto write(const u8* buf, usize len) -> Result<usize> {
#if RSTD_OS_UNIX
        return rstd::sys::io::stdio::write_fd(fd_.as_raw_fd(), buf, len);
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// No-op for files; the kernel buffers writes.
    auto flush() -> Result<empty> { return Ok(empty {}); }

    /// Seeks within the file.
    auto seek(SeekFrom pos) -> Result<u64> {
#if RSTD_OS_UNIX
        int        whence;
        libc::off_t off;
        switch (pos.which) {
        case SeekFrom::Which::Start:
            whence = libc::SEEK_SET; off = libc::off_t(u64(pos.offset)); break;
        case SeekFrom::Which::End:
            whence = libc::SEEK_END; off = libc::off_t(pos.offset); break;
        case SeekFrom::Which::Current:
            whence = libc::SEEK_CUR; off = libc::off_t(pos.offset); break;
        }
        auto r = libc::lseek(fd_.as_raw_fd(), off, whence);
        if (r < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(u64(r));
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// fsync(2) — flushes file data and metadata to disk.
    auto sync_all() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::fsync(fd_.as_raw_fd()) < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(empty {});
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// fdatasync(2) — flushes file data (skipping non-essential metadata).
    auto sync_data() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::fdatasync(fd_.as_raw_fd()) < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(empty {});
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// ftruncate(2) — truncate or extend the file to `size` bytes.
    auto set_len(u64 size) -> Result<empty> {
#if RSTD_OS_UNIX
        while (libc::ftruncate(fd_.as_raw_fd(), libc::off_t(size)) < 0) {
            if (libc::get_errno() == libc::EINTR) continue;
            return Err(Error::from_raw_os_error(libc::get_errno()));
        }
        return Ok(empty {});
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// pread(2) — positional read at absolute `offset`. EINTR-retried.
    auto read_at(u8* buf, usize len, u64 offset) -> Result<usize> {
#if RSTD_OS_UNIX
        while (true) {
            libc::ssize_t n = libc::pread(fd_.as_raw_fd(), buf, len, libc::off_t(offset));
            if (n >= 0) return Ok(usize(n));
            if (libc::get_errno() == libc::EINTR) continue;
            return Err(Error::from_raw_os_error(libc::get_errno()));
        }
#else
        (void)buf; (void)len; (void)offset;
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// pwrite(2) — positional write at absolute `offset`. EINTR-retried.
    auto write_at(const u8* buf, usize len, u64 offset) -> Result<usize> {
#if RSTD_OS_UNIX
        while (true) {
            libc::ssize_t n = libc::pwrite(fd_.as_raw_fd(), buf, len, libc::off_t(offset));
            if (n >= 0) return Ok(usize(n));
            if (libc::get_errno() == libc::EINTR) continue;
            return Err(Error::from_raw_os_error(libc::get_errno()));
        }
#else
        (void)buf; (void)len; (void)offset;
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// pread loop: read exactly `len` bytes from `offset`.
    auto read_exact_at(u8* buf, usize len, u64 offset) -> Result<empty> {
        while (len > 0) {
            auto r = read_at(buf, len, offset);
            if (r.is_err()) return Err(r.unwrap_err_unchecked());
            usize n = r.unwrap_unchecked();
            if (n == 0)
                return Err(Error::from_kind(ErrorKind { ErrorKind::UnexpectedEof }));
            buf += n;
            len -= n;
            offset += n;
        }
        return Ok(empty {});
    }

    /// pwrite loop: write all `len` bytes starting at `offset`.
    auto write_all_at(const u8* buf, usize len, u64 offset) -> Result<empty> {
        while (len > 0) {
            auto r = write_at(buf, len, offset);
            if (r.is_err()) return Err(r.unwrap_err_unchecked());
            usize n = r.unwrap_unchecked();
            if (n == 0)
                return Err(Error::from_kind(ErrorKind { ErrorKind::WriteZero }));
            buf += n;
            len -= n;
            offset += n;
        }
        return Ok(empty {});
    }

    /// flock(2) LOCK_EX (exclusive, blocking).
    auto lock() -> Result<empty> {
#if RSTD_OS_UNIX
        while (true) {
            if (libc::flock(fd_.as_raw_fd(), libc::LOCK_EX) == 0) return Ok(empty {});
            if (libc::get_errno() == libc::EINTR) continue;
            return Err(Error::from_raw_os_error(libc::get_errno()));
        }
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// flock(2) LOCK_SH (shared, blocking).
    auto lock_shared() -> Result<empty> {
#if RSTD_OS_UNIX
        while (true) {
            if (libc::flock(fd_.as_raw_fd(), libc::LOCK_SH) == 0) return Ok(empty {});
            if (libc::get_errno() == libc::EINTR) continue;
            return Err(Error::from_raw_os_error(libc::get_errno()));
        }
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// flock(2) libc::LOCK_EX | libc::LOCK_NB. Returns WouldBlock if another process holds the lock.
    auto try_lock() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::flock(fd_.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) == 0) return Ok(empty {});
        if (libc::get_errno() == libc::EWOULDBLOCK)
            return Err(Error::from_kind(ErrorKind { ErrorKind::WouldBlock }));
        return Err(Error::from_raw_os_error(libc::get_errno()));
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// flock(2) libc::LOCK_SH | libc::LOCK_NB.
    auto try_lock_shared() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::flock(fd_.as_raw_fd(), libc::LOCK_SH | libc::LOCK_NB) == 0) return Ok(empty {});
        if (libc::get_errno() == libc::EWOULDBLOCK)
            return Err(Error::from_kind(ErrorKind { ErrorKind::WouldBlock }));
        return Err(Error::from_raw_os_error(libc::get_errno()));
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// flock(2) LOCK_UN.
    auto unlock() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::flock(fd_.as_raw_fd(), libc::LOCK_UN) == 0) return Ok(empty {});
        return Err(Error::from_raw_os_error(libc::get_errno()));
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// fstat(2) — returns metadata for this file.
    auto metadata() const -> Result<Metadata> {
#if RSTD_OS_UNIX
        Metadata m;
        if (libc::fstat(fd_.as_raw_fd(), &m.st_) < 0)
            return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(rstd::move(m));
#else
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// fchmod(2) — change the file's permission bits.
    auto set_permissions(Permissions perm) -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::fchmod(fd_.as_raw_fd(), libc::mode_t(perm.mode_)) < 0)
            return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(empty {});
#else
        (void)perm;
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// futimens(2) — set accessed/modified times. libc::UTIME_OMIT for unset fields.
    auto set_times(FileTimes times) -> Result<empty> {
#if RSTD_OS_UNIX
        libc::timespec_t ts[2];
        auto fill = [&](Option<rstd::time::SystemTime> const& opt, libc::timespec_t& out) {
            if (opt.is_some()) {
                auto const& spec = (*opt).inner.t;
                out.tv_sec  = libc::time_t(spec.tv_sec);
                out.tv_nsec = long(spec.tv_nsec);
            } else {
                out.tv_sec  = 0;
                out.tv_nsec = libc::UTIME_OMIT;
            }
        };
        fill(times.accessed_, ts[0]);
        fill(times.modified_, ts[1]);
        if (libc::futimens(fd_.as_raw_fd(), ts) < 0)
            return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(empty {});
#else
        (void)times;
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    }

    /// Convenience wrapper: set only the modified time.
    auto set_modified(rstd::time::SystemTime t) -> Result<empty> {
        return set_times(FileTimes::make().set_modified(t));
    }

    /// Returns a new `File` referring to the same underlying file. Uses dup(2).
    auto try_clone() const -> Result<File> {
        auto cloned = fd_.try_clone();
        if (cloned.is_err()) return Err(cloned.unwrap_err_unchecked());
        return Ok(File { rstd::move(cloned).unwrap_unchecked() });
    }

    auto as_raw_fd() const noexcept -> RawFd { return fd_.as_raw_fd(); }
    auto as_fd() const noexcept -> BorrowedFd { return fd_.as_fd(); }
    auto into_raw_fd() noexcept -> RawFd { return fd_.into_raw_fd(); }

    static auto from_raw_fd(RawFd fd) noexcept -> File {
        return File { OwnedFd::from_raw_fd(fd) };
    }
};

inline auto OpenOptions::open(ref<Path> path) const -> Result<File> {
#if RSTD_OS_UNIX
    auto cs_res = path.to_cstring();
    if (cs_res.is_err()) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto cs = rstd::move(cs_res).unwrap_unchecked();

    auto access = get_access_mode();
    if (access.is_err()) return Err(access.unwrap_err_unchecked());
    auto creation = get_creation_mode();
    if (creation.is_err()) return Err(creation.unwrap_err_unchecked());

    int flags = access.unwrap_unchecked() | creation.unwrap_unchecked() |
                custom_flags_ | libc::O_CLOEXEC;
    auto path_ptr = reinterpret_cast<const char*>(cs.to_bytes_with_nul().p);

    while (true) {
        int fd = libc::open(path_ptr, flags, libc::mode_t(mode_));
        if (fd >= 0) return Ok(File { OwnedFd { fd } });
        if (libc::get_errno() == libc::EINTR) continue;
        return Err(Error::from_raw_os_error(libc::get_errno()));
    }
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

// ── Free functions ────────────────────────────────────────────────────────

namespace detail
{

#if RSTD_OS_UNIX
/// Helper: Path → null-terminated char buffer + length, or InvalidInput on interior NUL.
inline auto path_cstring(ref<Path> path) -> Result<CString> {
    auto r = path.to_cstring();
    if (r.is_err()) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return Ok(rstd::move(r).unwrap_unchecked());
}
#endif

inline auto read_to_end(File& f, Vec<u8>& out) -> Result<usize> {
    u8    chunk[4096];
    usize total = 0;
    while (true) {
        auto r = f.read(chunk, sizeof(chunk));
        if (r.is_err()) return Err(r.unwrap_err_unchecked());
        usize n = r.unwrap_unchecked();
        if (n == 0) break;
        for (usize i = 0; i < n; i++) out.push(u8(chunk[i]));
        total += n;
    }
    return Ok(total);
}

} // namespace detail

/// Reads the entire contents of `path` into a new `Vec<u8>`.
export inline auto read(ref<Path> path) -> Result<Vec<u8>> {
    auto fres = File::open(path);
    if (fres.is_err()) return Err(fres.unwrap_err_unchecked());
    auto f   = rstd::move(fres).unwrap_unchecked();
    auto out = Vec<u8>::make();
    auto r   = detail::read_to_end(f, out);
    if (r.is_err()) return Err(r.unwrap_err_unchecked());
    return Ok(rstd::move(out));
}

/// Reads the entire contents of `path` into a new UTF-8 `String`.
/// Returns InvalidData on invalid UTF-8.
export inline auto read_to_string(ref<Path> path) -> Result<String> {
    auto v = read(path);
    if (v.is_err()) return Err(v.unwrap_err_unchecked());
    auto bytes = rstd::move(v).unwrap_unchecked();
    if (!rstd::char_::is_valid_utf8(bytes.as_slice().p, bytes.len())) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
    }
    return Ok(String::from_utf8_unchecked(rstd::move(bytes)));
}

/// Writes `contents` to `path`, creating or truncating the file as needed.
export inline auto write(ref<Path> path, slice<u8> contents) -> Result<empty> {
    auto fres = File::create(path);
    if (fres.is_err()) return Err(fres.unwrap_err_unchecked());
    auto  f   = rstd::move(fres).unwrap_unchecked();
    usize off = 0;
    while (off < contents.len()) {
        auto r = f.write(contents.p + off, contents.len() - off);
        if (r.is_err()) return Err(r.unwrap_err_unchecked());
        usize n = r.unwrap_unchecked();
        if (n == 0)
            return Err(Error::from_kind(ErrorKind { ErrorKind::WriteZero }));
        off += n;
    }
    return Ok(empty {});
}

#if RSTD_OS_UNIX
namespace detail
{
inline auto stat_path(ref<Path> path, bool follow) -> Result<Metadata> {
    auto cs = path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto    csv = rstd::move(cs).unwrap_unchecked();
    Metadata m;
    int r = follow
                ? libc::stat(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p),  &m.st_)
                : libc::lstat(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p), &m.st_);
    if (r < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
    return Ok(rstd::move(m));
}
} // namespace detail
#endif

/// stat(2) — metadata for `path` (follows symlinks).
export inline auto metadata(ref<Path> path) -> Result<Metadata> {
#if RSTD_OS_UNIX
    return detail::stat_path(path, true);
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// lstat(2) — metadata for `path` without following symlinks.
export inline auto symlink_metadata(ref<Path> path) -> Result<Metadata> {
#if RSTD_OS_UNIX
    return detail::stat_path(path, false);
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// Returns Ok(true) if `path` exists. Maps NotFound to Ok(false).
export inline auto exists(ref<Path> path) -> Result<bool> {
    auto m = metadata(path);
    if (m.is_ok()) return Ok(true);
    auto e = rstd::move(m).unwrap_err_unchecked();
    if (e.kind().code == ErrorKind::NotFound) return Ok(false);
    return Err(rstd::move(e));
}

#if RSTD_OS_UNIX
namespace detail
{
inline auto run_path1(ref<Path> path, int (*fn)(const char*)) -> Result<empty> {
    auto cs = path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto csv = rstd::move(cs).unwrap_unchecked();
    if (fn(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p)) < 0)
        return Err(Error::from_raw_os_error(libc::get_errno()));
    return Ok(empty {});
}
inline auto run_path2(ref<Path> from, ref<Path> to, int (*fn)(const char*, const char*))
    -> Result<empty> {
    auto a = path_cstring(from);
    if (a.is_err()) return Err(a.unwrap_err_unchecked());
    auto av = rstd::move(a).unwrap_unchecked();
    auto b  = path_cstring(to);
    if (b.is_err()) return Err(b.unwrap_err_unchecked());
    auto bv = rstd::move(b).unwrap_unchecked();
    if (fn(reinterpret_cast<const char*>(av.to_bytes_with_nul().p),
           reinterpret_cast<const char*>(bv.to_bytes_with_nul().p)) < 0)
        return Err(Error::from_raw_os_error(libc::get_errno()));
    return Ok(empty {});
}
} // namespace detail
#endif

/// unlink(2).
export inline auto remove_file(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    return detail::run_path1(path, libc::unlink);
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// rmdir(2). Fails if the directory is non-empty.
export inline auto remove_dir(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    return detail::run_path1(path, libc::rmdir);
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// rename(2) — move/rename `from` to `to`.
export inline auto rename(ref<Path> from, ref<Path> to) -> Result<empty> {
#if RSTD_OS_UNIX
    return detail::run_path2(from, to, libc::rename);
#else
    (void)from; (void)to;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// link(2) — create a hard link to `original` at `link`.
export inline auto hard_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return detail::run_path2(original, link, libc::link);
#else
    (void)original; (void)link;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// symlink(2) — create `link` pointing at `original`.
export inline auto soft_link(ref<Path> original, ref<Path> link) -> Result<empty> {
#if RSTD_OS_UNIX
    return detail::run_path2(original, link, libc::symlink);
#else
    (void)original; (void)link;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// readlink(2) — read the target of a symlink at `path`.
export inline auto read_link(ref<Path> path) -> Result<rstd::path::PathBuf> {
#if RSTD_OS_UNIX
    auto cs = detail::path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto csv = rstd::move(cs).unwrap_unchecked();

    usize cap = 256;
    while (true) {
        Vec<u8> v = Vec<u8>::with_capacity(cap);
        for (usize i = 0; i < cap; i++) v.push(u8(0));
        auto n = libc::readlink(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p),
                            reinterpret_cast<char*>(v.as_mut_slice().as_raw_ptr()),
                            cap);
        if (n < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
        if (usize(n) < cap) {
            // Truncate to the actual length.
            while (v.len() > usize(n)) (void)v.pop();
            return Ok(rstd::path::PathBuf::from(
                ::alloc::string::String::from_utf8_unchecked(rstd::move(v))));
        }
        cap *= 2;
    }
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// realpath(3) — fully resolve a path to its absolute, symlink-free form.
export inline auto canonicalize(ref<Path> path) -> Result<rstd::path::PathBuf> {
#if RSTD_OS_UNIX
    auto cs = detail::path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto  csv = rstd::move(cs).unwrap_unchecked();
    char* raw = libc::realpath(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p), nullptr);
    if (!raw) return Err(Error::from_raw_os_error(libc::get_errno()));
    usize   len = 0;
    while (raw[len] != 0) ++len;
    Vec<u8> v   = Vec<u8>::with_capacity(len);
    for (usize i = 0; i < len; i++) v.push(u8(raw[i]));
    libc::free(raw);
    return Ok(rstd::path::PathBuf::from(
        ::alloc::string::String::from_utf8_unchecked(rstd::move(v))));
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// mkdir(2) with mode 0o777 (subject to umask).
export inline auto create_dir(ref<Path> path) -> Result<empty> {
#if RSTD_OS_UNIX
    auto cs = detail::path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto csv = rstd::move(cs).unwrap_unchecked();
    if (libc::mkdir(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p), 0777) < 0)
        return Err(Error::from_raw_os_error(libc::get_errno()));
    return Ok(empty {});
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// Recursively create `path` and any missing parent directories.
/// Mirrors Rust's `DirBuilder::create_dir_all`.
export inline auto create_dir_all(ref<Path> path) -> Result<empty> {
    if (path.is_empty()) return Ok(empty {});
    auto m = metadata(path);
    if (m.is_ok()) {
        if (rstd::move(m).unwrap_unchecked().is_dir()) return Ok(empty {});
        return Err(Error::from_kind(ErrorKind { ErrorKind::AlreadyExists }));
    }
    auto parent = path.parent();
    if (parent.is_some()) {
        auto pres = create_dir_all(*parent);
        if (pres.is_err()) return Err(pres.unwrap_err_unchecked());
    }
    auto r = create_dir(path);
    if (r.is_err()) {
        // Tolerate races where another thread/process created the dir.
        auto e = r.unwrap_err_unchecked();
        if (e.kind().code == ErrorKind::AlreadyExists) return Ok(empty {});
        return Err(rstd::move(e));
    }
    return Ok(empty {});
}

/// chmod(2).
export inline auto set_permissions(ref<Path> path, Permissions perm) -> Result<empty> {
#if RSTD_OS_UNIX
    auto cs = detail::path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto csv = rstd::move(cs).unwrap_unchecked();
    if (libc::chmod(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p),
                libc::mode_t(perm.mode_)) < 0)
        return Err(Error::from_raw_os_error(libc::get_errno()));
    return Ok(empty {});
#else
    (void)path; (void)perm;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// Read `from` and write its contents to `to`. Returns the number of bytes copied.
/// First cut: simple read/write loop. Phase-4-fast (later) can swap in
/// `copy_file_range` on Linux.
export inline auto copy(ref<Path> from, ref<Path> to) -> Result<u64> {
    auto fres = File::open(from);
    if (fres.is_err()) return Err(fres.unwrap_err_unchecked());
    auto src = rstd::move(fres).unwrap_unchecked();

    auto src_meta = src.metadata();
    if (src_meta.is_err()) return Err(src_meta.unwrap_err_unchecked());
    auto perm = rstd::move(src_meta).unwrap_unchecked().permissions();

    auto dres = OpenOptions::make()
                    .write(true)
                    .create(true)
                    .truncate(true)
                    .mode(perm.mode())
                    .open(to);
    if (dres.is_err()) return Err(dres.unwrap_err_unchecked());
    auto dst = rstd::move(dres).unwrap_unchecked();

    u8  chunk[8192];
    u64 total = 0;
    while (true) {
        auto r = src.read(chunk, sizeof(chunk));
        if (r.is_err()) return Err(r.unwrap_err_unchecked());
        usize n = r.unwrap_unchecked();
        if (n == 0) break;
        usize off = 0;
        while (off < n) {
            auto w = dst.write(chunk + off, n - off);
            if (w.is_err()) return Err(w.unwrap_err_unchecked());
            usize m = w.unwrap_unchecked();
            if (m == 0)
                return Err(Error::from_kind(ErrorKind { ErrorKind::WriteZero }));
            off += m;
        }
        total += n;
    }
    return Ok(total);
}

// ── DirEntry / ReadDir ────────────────────────────────────────────────────

/// One entry yielded by `ReadDir`. Holds an owned name + parent path snapshot.
export class DirEntry {
public:
    rstd::path::PathBuf parent_;
    rstd::ffi::OsString name_;
    u32                 type_ { 0 }; // S_IFMT bits or 0 if unknown

    /// Full path: parent_ + "/" + name_.
    auto path() const -> rstd::path::PathBuf {
        return parent_.join(rstd::ref<rstd::path::Path>(name_.as_os_str()));
    }

    /// File name component (no parent).
    auto file_name() const -> rstd::ffi::OsString {
        // Best-effort clone: rebuild from bytes.
        return rstd::ffi::OsString::from(name_.as_os_str());
    }

    /// File type. Falls back to lstat if dirent didn't carry it.
    auto file_type() const -> Result<FileType> {
        if (type_ != 0) return Ok(FileType { type_ });
        auto p = path();
        return symlink_metadata(rstd::ref<rstd::path::Path>(p.as_path()))
            .map([](Metadata m) { return m.file_type(); });
    }

    auto metadata() const -> Result<Metadata> {
        auto p = path();
        return symlink_metadata(rstd::ref<rstd::path::Path>(p.as_path()));
    }
};

/// Iterates over directory entries. Move-only; `closedir` on drop.
export class ReadDir {
public:
#if RSTD_OS_UNIX
    libc::DIR*              dir_ { nullptr };
    rstd::path::PathBuf parent_;

    ReadDir() noexcept = default;
    ReadDir(libc::DIR* d, rstd::path::PathBuf parent) noexcept
        : dir_(d), parent_(rstd::move(parent)) {}
    ReadDir(ReadDir const&)        = delete;
    auto operator=(ReadDir const&) = delete;
    ReadDir(ReadDir&& other) noexcept : dir_(other.dir_), parent_(rstd::move(other.parent_)) {
        other.dir_ = nullptr;
    }
    auto operator=(ReadDir&& other) noexcept -> ReadDir& {
        if (this != &other) {
            close_();
            dir_       = other.dir_;
            parent_    = rstd::move(other.parent_);
            other.dir_ = nullptr;
        }
        return *this;
    }
    ~ReadDir() { close_(); }

    /// Returns the next entry, skipping "." and "..". `None` at end of stream.
    auto next() -> Option<Result<DirEntry>> {
        if (!dir_) return None();
        while (true) {
            libc::get_errno()              = 0;
            libc::dirent* d = libc::readdir(dir_);
            if (!d) {
                if (libc::get_errno() == 0) return None();
                return Some(Result<DirEntry>(Err(Error::from_raw_os_error(libc::get_errno()))));
            }
            const char* nm = d->d_name;
            if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;

            DirEntry e;
            e.parent_ = rstd::path::PathBuf::from(
                rstd::ffi::OsString::from(parent_.as_path().as_os_str()));
            e.name_   = rstd::ffi::OsString::from(rstd::ref<rstd::str>(nm));
            switch (d->d_type) {
            case libc::DT_REG:  e.type_ = libc::S_IFREG;  break;
            case libc::DT_DIR:  e.type_ = libc::S_IFDIR;  break;
            case libc::DT_LNK:  e.type_ = libc::S_IFLNK;  break;
            case libc::DT_FIFO: e.type_ = libc::S_IFIFO;  break;
            case libc::DT_BLK:  e.type_ = libc::S_IFBLK;  break;
            case libc::DT_CHR:  e.type_ = libc::S_IFCHR;  break;
            case libc::DT_SOCK: e.type_ = libc::S_IFSOCK; break;
            default:              e.type_ = 0;                break;
            }
            return Some(Result<DirEntry>(Ok(rstd::move(e))));
        }
    }

private:
    void close_() noexcept {
        if (dir_) {
            libc::closedir(dir_);
            dir_ = nullptr;
        }
    }
#endif
};

/// opendir(3) — open a directory for iteration.
export inline auto read_dir(ref<Path> path) -> Result<ReadDir> {
#if RSTD_OS_UNIX
    auto cs = detail::path_cstring(path);
    if (cs.is_err()) return Err(cs.unwrap_err_unchecked());
    auto    csv = rstd::move(cs).unwrap_unchecked();
    libc::DIR*  d   = libc::opendir(reinterpret_cast<const char*>(csv.to_bytes_with_nul().p));
    if (!d) return Err(Error::from_raw_os_error(libc::get_errno()));
    // Copy path bytes for parent_.
    Vec<u8> bytes = Vec<u8>::with_capacity(path.len());
    for (usize i = 0; i < path.len(); i++) bytes.push(u8(path.data()[i]));
    auto parent_pb = rstd::path::PathBuf::from(
        ::alloc::string::String::from_utf8_unchecked(rstd::move(bytes)));
    return Ok(ReadDir { d, rstd::move(parent_pb) });
#else
    (void)path;
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}

/// Recursively remove `path` (a directory) and everything inside it.
/// Symlinks at the leaf are unlinked, not followed.
export inline auto remove_dir_all(ref<Path> path) -> Result<empty> {
    auto m = symlink_metadata(path);
    if (m.is_err()) return Err(m.unwrap_err_unchecked());
    auto meta = rstd::move(m).unwrap_unchecked();
    if (!meta.is_dir()) {
        // Rust's behaviour: treat symlinks-to-dirs as files for removal.
        return remove_file(path);
    }
    auto rd_res = read_dir(path);
    if (rd_res.is_err()) return Err(rd_res.unwrap_err_unchecked());
    auto rd = rstd::move(rd_res).unwrap_unchecked();
    while (true) {
        auto opt = rd.next();
        if (opt.is_none()) break;
        auto er = rstd::move(opt).unwrap_unchecked();
        if (er.is_err()) return Err(er.unwrap_err_unchecked());
        auto ent  = rstd::move(er).unwrap_unchecked();
        auto p    = ent.path();
        auto ftr  = ent.file_type();
        if (ftr.is_err()) return Err(ftr.unwrap_err_unchecked());
        auto ft = ftr.unwrap_unchecked();
        if (ft.is_dir()) {
            auto r = remove_dir_all(rstd::ref<rstd::path::Path>(p.as_path()));
            if (r.is_err()) return Err(r.unwrap_err_unchecked());
        } else {
            auto r = remove_file(rstd::ref<rstd::path::Path>(p.as_path()));
            if (r.is_err()) return Err(r.unwrap_err_unchecked());
        }
    }
    return remove_dir(path);
}

} // namespace rstd::fs

// ── Trait impls for File (must live in namespace rstd) ────────────────────
namespace rstd
{

template<>
struct Impl<io::Read, fs::File> : ImplBase<fs::File> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        return this->self().read(buf, len);
    }
};

template<>
struct Impl<io::Write, fs::File> : ImplBase<fs::File> {
    auto write(const u8* buf, usize len) -> io::Result<usize> {
        return this->self().write(buf, len);
    }
    auto flush() -> io::Result<empty> { return this->self().flush(); }
};

template<>
struct Impl<io::Seek, fs::File> : ImplBase<fs::File> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return this->self().seek(pos); }
};

} // namespace rstd
