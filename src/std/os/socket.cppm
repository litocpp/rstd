module;
#include <rstd/macro.hpp>

export module rstd:os.socket;
export import :io.error;
export import :os.fd;
export import rstd.core;

namespace rstd::os::socket
{

using rstd::io::Result;
using rstd::io::error::Error;

export using RawSocket                           = fd::RawFd;
export inline const RawSocket INVALID_RAW_SOCKET = fd::INVALID_RAW_FD;

export class BorrowedSocket;

export class OwnedSocket {
public:
    OwnedSocket() noexcept             = default;
    OwnedSocket(const OwnedSocket&)    = delete;
    auto operator=(const OwnedSocket&) = delete;
    OwnedSocket(OwnedSocket&& other) noexcept: socket_(other.socket_) {
        other.socket_ = INVALID_RAW_SOCKET;
    }
    auto operator=(OwnedSocket&& other) noexcept -> OwnedSocket& {
        if (this != &other) {
            close_();
            socket_       = other.socket_;
            other.socket_ = INVALID_RAW_SOCKET;
        }
        return *this;
    }
    ~OwnedSocket();

    auto as_raw_socket() const noexcept -> RawSocket { return socket_; }
    auto as_socket() const noexcept [[clang::lifetimebound]] -> BorrowedSocket;

    auto into_raw_socket() && noexcept -> RawSocket {
        auto socket = socket_;
        socket_     = INVALID_RAW_SOCKET;
        return socket;
    }

    static auto from_raw_socket(RawSocket socket) noexcept -> OwnedSocket {
        if (socket == INVALID_RAW_SOCKET) panic { "invalid raw socket" };
        return OwnedSocket { socket };
    }

    auto is_open() const noexcept -> bool { return socket_ != INVALID_RAW_SOCKET; }

private:
    explicit OwnedSocket(RawSocket socket) noexcept: socket_(socket) {}

    void close_() noexcept;

    RawSocket socket_ { INVALID_RAW_SOCKET };
};

export class BorrowedSocket {
    RawSocket socket_;

    explicit BorrowedSocket(RawSocket socket) noexcept: socket_(socket) {}

public:
    BorrowedSocket() = delete;

    auto as_raw_socket() const noexcept -> RawSocket { return socket_; }
    auto as_socket() const noexcept [[clang::lifetimebound]] -> BorrowedSocket { return *this; }

    static auto borrow_raw(RawSocket socket) noexcept -> BorrowedSocket {
        if (socket == INVALID_RAW_SOCKET) panic { "invalid raw socket" };
        return BorrowedSocket { socket };
    }
};

inline auto OwnedSocket::as_socket() const noexcept [[clang::lifetimebound]] -> BorrowedSocket {
    return BorrowedSocket::borrow_raw(socket_);
}

} // namespace rstd::os::socket
