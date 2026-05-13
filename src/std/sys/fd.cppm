module;
#include <rstd/macro.hpp>
export module rstd:sys.fd;
export import :io.error;
export import rstd.core;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#endif

namespace rstd::sys::fd
{

using rstd::io::Result;
using rstd::io::error::Error;
namespace libc = rstd::sys::libc;

#if RSTD_OS_UNIX
export using RawFd = int;
export inline constexpr RawFd INVALID_RAW_FD = -1;
#else
export using RawFd = void*;
export inline constexpr RawFd INVALID_RAW_FD = (void*)(-1);
#endif

/// Non-owning view of a file descriptor.
export struct BorrowedFd {
    RawFd fd { INVALID_RAW_FD };

    constexpr auto as_raw_fd() const noexcept -> RawFd { return fd; }

    static constexpr auto borrow_raw(RawFd fd) noexcept -> BorrowedFd {
        return BorrowedFd { fd };
    }
};

/// RAII-owning file descriptor; closes on destruction.
export class OwnedFd {
public:
    OwnedFd() noexcept = default;
    explicit OwnedFd(RawFd fd) noexcept : fd_(fd) {}
    OwnedFd(OwnedFd const&)            = delete;
    auto operator=(OwnedFd const&)     = delete;
    OwnedFd(OwnedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = INVALID_RAW_FD; }
    auto operator=(OwnedFd&& other) noexcept -> OwnedFd& {
        if (this != &other) {
            close_();
            fd_       = other.fd_;
            other.fd_ = INVALID_RAW_FD;
        }
        return *this;
    }
    ~OwnedFd() { close_(); }

    auto as_raw_fd() const noexcept -> RawFd { return fd_; }
    auto as_fd() const noexcept -> BorrowedFd { return BorrowedFd::borrow_raw(fd_); }

    /// Consumes self and returns the raw fd; caller takes ownership.
    auto into_raw_fd() noexcept -> RawFd {
        auto r = fd_;
        fd_    = INVALID_RAW_FD;
        return r;
    }

    /// Adopts an existing raw fd into a new OwnedFd.
    static auto from_raw_fd(RawFd fd) noexcept -> OwnedFd { return OwnedFd { fd }; }

    /// Duplicates the fd with O_CLOEXEC set on the duplicate.
    auto try_clone() const -> Result<OwnedFd> {
#if RSTD_OS_UNIX
        int new_fd = libc::fcntl(fd_, libc::F_DUPFD_CLOEXEC, 0);
        if (new_fd < 0) return Err(Error::from_raw_os_error(libc::get_errno()));
        return Ok(OwnedFd { new_fd });
#else
        return Err(Error::from_kind(rstd::io::error::ErrorKind {
            rstd::io::error::ErrorKind::Unsupported }));
#endif
    }

    auto is_open() const noexcept -> bool { return fd_ != INVALID_RAW_FD; }

private:
    void close_() noexcept {
        if (fd_ != INVALID_RAW_FD) {
#if RSTD_OS_UNIX
            // Per Rust convention: don't retry close() on EINTR. On Linux the fd
            // is already closed even if EINTR is returned, and a retry could
            // close an unrelated fd that has since been recycled.
            libc::close(fd_);
#endif
            fd_ = INVALID_RAW_FD;
        }
    }

    RawFd fd_ { INVALID_RAW_FD };
};

} // namespace rstd::sys::fd
