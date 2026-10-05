module;
#include <rstd/macro.hpp>
export module rstd:os.linux.pidfd;
export import :os.fd;
export import :process.exit_status;
export import :io.error;

#if RSTD_OS_LINUX
export namespace rstd::os::linux::process
{

/// An owned process descriptor. Destruction closes it without killing or reaping.
class PidFd {
    fd::OwnedFd descriptor_;
    explicit PidFd(fd::OwnedFd descriptor): descriptor_(rstd::move(descriptor)) {}

public:
    PidFd(PidFd&&) noexcept                    = default;
    auto operator=(PidFd&&) noexcept -> PidFd& = default;
    auto as_raw_fd() const noexcept -> fd::RawFd { return descriptor_.as_raw_fd(); }
    auto as_fd() const noexcept [[clang::lifetimebound]] -> fd::BorrowedFd {
        return descriptor_.as_fd();
    }
    auto into_raw_fd() && noexcept -> fd::RawFd { return rstd::move(descriptor_).into_raw_fd(); }
    static auto from_raw_fd(fd::RawFd descriptor) -> PidFd {
        return PidFd(fd::OwnedFd::from_raw_fd(descriptor));
    }
    static auto from_owned_fd(fd::OwnedFd descriptor) -> PidFd {
        return PidFd(rstd::move(descriptor));
    }
    auto into_owned_fd() && -> fd::OwnedFd { return rstd::move(descriptor_); }
    auto kill() const -> io::Result<empty>;
    /// Reaps without closing stdin. Repeated waits can fail on older kernels.
    auto wait() const -> io::Result<rstd::process::ExitStatus>;
    auto try_wait() const -> io::Result<Option<rstd::process::ExitStatus>>;
};

} // namespace rstd::os::linux::process

namespace rstd
{

template<>
struct Impl<os::fd::IntoRawFd, os::linux::process::PidFd> : ImplBase<os::linux::process::PidFd> {
    auto into_raw_fd() noexcept -> os::fd::RawFd { return rstd::move(this->self()).into_raw_fd(); }
};

template<>
struct Impl<os::fd::FromRawFd, os::linux::process::PidFd> {
    static auto from_raw_fd(os::fd::RawFd fd) -> os::linux::process::PidFd {
        return os::linux::process::PidFd::from_raw_fd(fd);
    }
};

} // namespace rstd
#endif
