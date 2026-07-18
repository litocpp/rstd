module;
#include <rstd/macro.hpp>
export module rstd:os.fd;
import :io.error;
import rstd.core;

namespace rstd::os::fd
{

using rstd::io::Result;
using rstd::io::error::Error;
#if RSTD_OS_UNIX
export using RawFd                           = int;
export inline constexpr RawFd INVALID_RAW_FD = -1;
#else
export using RawFd                       = void*;
export inline RawFd const INVALID_RAW_FD = (void*)(-1);
#endif

/// Non-owning view of a file descriptor.
export class BorrowedFd;

/// RAII-owning file descriptor; closes on destruction.
export class OwnedFd {
public:
    OwnedFd() noexcept             = default;
    OwnedFd(OwnedFd const&)        = delete;
    auto operator=(OwnedFd const&) = delete;
    OwnedFd(OwnedFd&& other) noexcept: fd_(other.fd_) { other.fd_ = INVALID_RAW_FD; }
    auto operator=(OwnedFd&& other) noexcept -> OwnedFd& {
        if (this != &other) {
            close_();
            fd_       = other.fd_;
            other.fd_ = INVALID_RAW_FD;
        }
        return *this;
    }
    ~OwnedFd();

    auto as_raw_fd() const noexcept -> RawFd { return fd_; }
    auto as_fd() const noexcept [[clang::lifetimebound]] -> BorrowedFd;

    /// Consumes self and returns the raw fd; caller takes ownership.
    auto into_raw_fd() && noexcept -> RawFd {
        auto r = fd_;
        fd_    = INVALID_RAW_FD;
        return r;
    }

    /// Adopts an open fd whose ownership is transferred exclusively to the result.
    static auto from_raw_fd(RawFd fd) noexcept -> OwnedFd {
        if (fd == INVALID_RAW_FD) panic { "invalid raw fd" };
        return OwnedFd { fd };
    }

    /// Duplicates the fd with O_CLOEXEC set on the duplicate.
    auto try_clone() const -> Result<OwnedFd>;

    auto is_open() const noexcept -> bool { return fd_ != INVALID_RAW_FD; }

private:
    explicit OwnedFd(RawFd fd) noexcept: fd_(fd) {}

    void close_() noexcept;

    RawFd fd_ { INVALID_RAW_FD };
};

/// Non-owning view of a file descriptor.
export class BorrowedFd {
    RawFd fd_;

    explicit BorrowedFd(RawFd fd) noexcept: fd_(fd) {}

public:
    BorrowedFd() = delete;

    auto as_raw_fd() const noexcept -> RawFd { return fd_; }
    auto as_fd() const noexcept [[clang::lifetimebound]] -> BorrowedFd { return *this; }

    /// Borrows an fd that remains open for the complete lifetime of the returned value.
    static auto borrow_raw(RawFd fd) noexcept -> BorrowedFd {
        if (fd == INVALID_RAW_FD) panic { "invalid raw fd" };
        return BorrowedFd { fd };
    }

    auto try_clone_to_owned() const -> Result<OwnedFd>;
};

inline auto OwnedFd::as_fd() const noexcept [[clang::lifetimebound]] -> BorrowedFd {
    return BorrowedFd::borrow_raw(fd_);
}

inline auto OwnedFd::try_clone() const -> Result<OwnedFd> {
    return as_fd().try_clone_to_owned();
}

export struct AsRawFd {
    template<typename Self, typename = void>
    struct Api {
        using Trait = AsRawFd;
        auto as_raw_fd() const noexcept -> RawFd { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::as_raw_fd>;
};

export struct AsFd {
    template<typename Self, typename = void>
    struct Api {
        using Trait = AsFd;
        auto as_fd() const noexcept [[clang::lifetimebound]] -> BorrowedFd {
            return trait_call<0>(this);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::as_fd>;
};

export struct IntoRawFd {
    template<typename Self, typename = void>
    struct Api {
        using Trait = IntoRawFd;
        auto into_raw_fd() noexcept -> RawFd { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::into_raw_fd>;
};

export struct FromRawFd {
    template<typename Self, typename = void>
    struct Api {
        using Trait = FromRawFd;
        static auto from_raw_fd(RawFd fd) noexcept -> Self { return trait_static_call<0, Api>(fd); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::from_raw_fd>;
};

} // namespace rstd::os::fd

namespace rstd
{

template<>
struct Impl<os::fd::IntoRawFd, os::fd::OwnedFd> : ImplBase<os::fd::OwnedFd> {
    auto into_raw_fd() noexcept -> os::fd::RawFd { return rstd::move(this->self()).into_raw_fd(); }
};

template<>
struct Impl<os::fd::FromRawFd, os::fd::OwnedFd> {
    static auto from_raw_fd(os::fd::RawFd fd) noexcept -> os::fd::OwnedFd {
        return os::fd::OwnedFd::from_raw_fd(fd);
    }
};

template<>
struct Impl<os::fd::AsRawFd, os::fd::RawFd> : ImplBase<os::fd::RawFd> {
    auto as_raw_fd() const noexcept -> os::fd::RawFd { return this->self(); }
};

template<>
struct Impl<os::fd::IntoRawFd, os::fd::RawFd> : ImplBase<os::fd::RawFd> {
    auto into_raw_fd() noexcept -> os::fd::RawFd { return this->self(); }
};

template<>
struct Impl<os::fd::FromRawFd, os::fd::RawFd> {
    static auto from_raw_fd(os::fd::RawFd fd) noexcept -> os::fd::RawFd { return fd; }
};

} // namespace rstd
