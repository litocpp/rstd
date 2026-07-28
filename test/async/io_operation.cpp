#include <gtest/gtest.h>
#include <atomic>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

import rstd;

using namespace rstd;
using namespace rstd::literals;
using namespace rstd::prelude;

namespace
{

struct NativeSocketPair {
    os::fd::RawFd first { os::fd::INVALID_RAW_FD };
    os::fd::RawFd second { os::fd::INVALID_RAW_FD };

    NativeSocketPair() = default;
    NativeSocketPair(os::fd::RawFd first, os::fd::RawFd second): first(first), second(second) {}
    NativeSocketPair(const NativeSocketPair&)                    = delete;
    auto operator=(const NativeSocketPair&) -> NativeSocketPair& = delete;
    NativeSocketPair(NativeSocketPair&& other) noexcept
        : first(rstd::exchange(other.first, os::fd::INVALID_RAW_FD)),
          second(rstd::exchange(other.second, os::fd::INVALID_RAW_FD)) {}

    ~NativeSocketPair() {
#if defined(_WIN32)
        if (first != os::fd::INVALID_RAW_FD) closesocket(reinterpret_cast<SOCKET>(first));
        if (second != os::fd::INVALID_RAW_FD) closesocket(reinterpret_cast<SOCKET>(second));
#else
        if (first != os::fd::INVALID_RAW_FD) ::close(first);
        if (second != os::fd::INVALID_RAW_FD) ::close(second);
#endif
    }
};

#if defined(_WIN32)
auto socket_fd(SOCKET socket) noexcept -> os::fd::RawFd {
    return reinterpret_cast<os::fd::RawFd>(socket);
}
#endif

auto make_socket_pair() -> Option<NativeSocketPair> {
#if defined(_WIN32)
    auto data = WSADATA {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return None<NativeSocketPair>();

    auto listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listener == INVALID_SOCKET) return None<NativeSocketPair>();

    auto address            = sockaddr_in {};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 1) != 0) {
        closesocket(listener);
        return None<NativeSocketPair>();
    }

    auto address_len = int(sizeof(address));
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_len) != 0) {
        closesocket(listener);
        return None<NativeSocketPair>();
    }

    auto client = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (client == INVALID_SOCKET ||
        ::connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (client != INVALID_SOCKET) closesocket(client);
        closesocket(listener);
        return None<NativeSocketPair>();
    }

    auto accepted = ::accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (accepted == INVALID_SOCKET) {
        closesocket(client);
        return None<NativeSocketPair>();
    }
    return Some(NativeSocketPair { socket_fd(client), socket_fd(accepted) });
#else
    int sockets[2] = { -1, -1 };
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) != 0) {
        return None<NativeSocketPair>();
    }
    return Some(NativeSocketPair { sockets[0], sockets[1] });
#endif
}

auto completion_round_trip(os::fd::RawFd writer, os::fd::RawFd reader)
    -> async::coro<io::Result<bytes::Bytes>> {
    auto write_source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(writer));
    auto write = co_await async::IoOperation::write(write_source,
                                                    bytes::Bytes::copy_from_slice("iocp"_bytes));
    if (write.is_err()) co_return Err(rstd::move(write).unwrap_err_unchecked());
    if (rstd::move(write).unwrap_unchecked().transferred() != usize(4)) {
        co_return Err(
            io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::WriteZero }));
    }

    auto read_source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(reader));
    auto read        = co_await async::IoOperation::read(read_source, usize(4));
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());
    co_return Ok(rstd::move(read).unwrap_unchecked().into_data());
}

TEST(RstdAsyncIoOperation, SocketReadWriteCompletesThroughBackend) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto runtime = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto result = runtime.block_on(completion_round_trip(sockets.first, sockets.second));

    ASSERT_TRUE(result.is_ok());
    auto data = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(data.len(), usize(4));
    EXPECT_EQ(data[usize()], u8('i'));
    EXPECT_EQ(data[usize(3)], u8('p'));
}

auto read_without_io(os::fd::RawFd fd) -> async::coro<io::Result<async::IoCompletion>> {
    auto source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(fd));
    co_return co_await async::IoOperation::read(source, usize(1));
}

TEST(RstdAsyncIoOperation, RuntimeWithoutIoRejectsCompletion) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();

    auto result = runtime.block_on(read_without_io(sockets.first));

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::Unsupported });
}

TEST(RstdAsyncIoOperation, TimedOutReadCancelsAndLeavesSocketUsable) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto source  = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(sockets.first));

    auto timed = runtime.block_on(async::timeout(async::IoOperation::read(source, usize(1)),
                                                 time::Duration::from_millis(u64(2))));

    EXPECT_TRUE(timed.is_err());
    auto result = runtime.block_on(completion_round_trip(sockets.first, sockets.second));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().len(), usize(4));
}

auto pending_read(os::fd::RawFd fd, std::atomic<bool>& entered)
    -> async::coro<io::Result<async::IoCompletion>> {
    entered.store(true, std::memory_order_release);
    auto source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(fd));
    co_return co_await async::IoOperation::read(source, usize(1));
}

TEST(RstdAsyncIoOperation, RuntimeShutdownDrainsPendingRead) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto entered = std::atomic<bool> { false };

    auto joined = [&] {
        auto runtime = async::RuntimeBuilder::multi_thread()
                           .worker_threads(usize(1))
                           .enable_io()
                           .build()
                           .unwrap();
        auto handle  = runtime.spawn(pending_read(sockets.first, entered));
        while (! entered.load(std::memory_order_acquire)) hint::spin_loop();
        thread::sleep(time::Duration::from_millis(u64(2)));
        return handle;
    }();

    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err_unchecked().is_aborted());
}

#if defined(_WIN32)
auto wait_for_socket_readiness(os::fd::RawFd fd) -> async::coro<io::Result<async::ReadyEvent>> {
    auto registration = async::Registration::register_fd(fd).unwrap();
    co_return co_await async::ReadinessFuture {
        registration,
        async::Interest::readable(),
    };
}

TEST(RstdAsyncIoOperation, IocpDoesNotAdvertiseReadiness) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto runtime = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto result = runtime.block_on(wait_for_socket_readiness(sockets.first));

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::Unsupported });
}

auto file_completion_round_trip(os::handle::BorrowedHandle handle)
    -> async::coro<io::Result<bytes::Bytes>> {
    auto source = async::CompletionSource::file(handle);
    auto write =
        co_await async::IoOperation::write(source, bytes::Bytes::copy_from_slice("file"_bytes));
    if (write.is_err()) co_return Err(rstd::move(write).unwrap_err_unchecked());
    auto read = co_await async::IoOperation::read(source, usize(4));
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());
    co_return Ok(rstd::move(read).unwrap_unchecked().into_data());
}

TEST(RstdAsyncIoOperation, IocpFileReadWriteCompletes) {
    wchar_t directory[MAX_PATH] {};
    wchar_t path[MAX_PATH] {};
    ASSERT_NE(GetTempPathW(MAX_PATH, directory), DWORD(0));
    ASSERT_NE(GetTempFileNameW(directory, L"rst", 0, path), UINT(0));
    auto handle =
        CreateFileW(path,
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED,
                    nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    auto owned_handle = os::handle::OwnedHandle::from_raw_handle(handle);
    auto runtime      = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto result = runtime.block_on(file_completion_round_trip(owned_handle.as_handle()));

    ASSERT_TRUE(result.is_ok());
    auto data = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(data.len(), usize(4));
    EXPECT_EQ(data[usize()], u8('f'));
    EXPECT_EQ(data[usize(3)], u8('e'));
}
#endif

} // namespace
