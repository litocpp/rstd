module;
#include <rstd/macro.hpp>

export module rstd:os.handle;
export import :os.fd;
export import rstd.core;

#if RSTD_OS_WINDOWS
namespace rstd::os::handle
{

export using RawHandle                           = fd::RawFd;
export inline const RawHandle INVALID_RAW_HANDLE = fd::INVALID_RAW_FD;

export class BorrowedHandle;

export class OwnedHandle {
public:
    OwnedHandle() noexcept             = default;
    OwnedHandle(const OwnedHandle&)    = delete;
    auto operator=(const OwnedHandle&) = delete;
    OwnedHandle(OwnedHandle&& other) noexcept: handle_(other.handle_) {
        other.handle_ = INVALID_RAW_HANDLE;
    }
    auto operator=(OwnedHandle&& other) noexcept -> OwnedHandle& {
        if (this != &other) {
            close_();
            handle_       = other.handle_;
            other.handle_ = INVALID_RAW_HANDLE;
        }
        return *this;
    }
    ~OwnedHandle();

    auto as_raw_handle() const noexcept -> RawHandle { return handle_; }
    auto as_handle() const noexcept [[clang::lifetimebound]] -> BorrowedHandle;

    auto into_raw_handle() && noexcept -> RawHandle {
        auto handle = handle_;
        handle_     = INVALID_RAW_HANDLE;
        return handle;
    }

    static auto from_raw_handle(RawHandle handle) noexcept -> OwnedHandle {
        if (handle == INVALID_RAW_HANDLE) panic { "invalid raw handle" };
        return OwnedHandle { handle };
    }

    auto is_open() const noexcept -> bool { return handle_ != INVALID_RAW_HANDLE; }

private:
    explicit OwnedHandle(RawHandle handle) noexcept: handle_(handle) {}

    void close_() noexcept;

    RawHandle handle_ { INVALID_RAW_HANDLE };
};

export class BorrowedHandle {
    RawHandle handle_;

    explicit BorrowedHandle(RawHandle handle) noexcept: handle_(handle) {}

public:
    BorrowedHandle() = delete;

    auto as_raw_handle() const noexcept -> RawHandle { return handle_; }
    auto as_handle() const noexcept [[clang::lifetimebound]] -> BorrowedHandle { return *this; }

    static auto borrow_raw(RawHandle handle) noexcept -> BorrowedHandle {
        if (handle == INVALID_RAW_HANDLE) panic { "invalid raw handle" };
        return BorrowedHandle { handle };
    }
};

inline auto OwnedHandle::as_handle() const noexcept [[clang::lifetimebound]] -> BorrowedHandle {
    return BorrowedHandle::borrow_raw(handle_);
}

} // namespace rstd::os::handle
#endif
