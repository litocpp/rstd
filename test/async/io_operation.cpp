#include <rstd/test/gtest.hpp>
#include <atomic>
#include <cstdlib>

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

struct ForwardWakerState {
    Option<task::Waker> target {};
    std::atomic<int>    wakes { 0 };
};

extern const task::RawWakerVTable FORWARD_WAKER_VTABLE;

auto forward_waker_clone(voidp data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(data, rstd::addressof(FORWARD_WAKER_VTABLE));
}

void forward_waker_wake_by_ref(voidp data) {
    auto& state = *static_cast<ForwardWakerState*>(data);
    state.wakes.fetch_add(1, std::memory_order_relaxed);
    state.target->wake_by_ref();
}

void forward_waker_wake(voidp data) {
    forward_waker_wake_by_ref(data);
}

void forward_waker_drop(voidp) {
}

const task::RawWakerVTable FORWARD_WAKER_VTABLE {
    &forward_waker_clone,
    &forward_waker_wake,
    &forward_waker_wake_by_ref,
    &forward_waker_drop,
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

auto native_send(os::fd::RawFd fd, const char* data, int len) -> int {
#if defined(_WIN32)
    return ::send(reinterpret_cast<SOCKET>(fd), data, len, 0);
#else
    return static_cast<int>(::send(fd, data, static_cast<size_t>(len), MSG_NOSIGNAL));
#endif
}

struct PollIoWithReplacementWaker {
    async::IoOperation operation;
    os::fd::RawFd      writer;
    ForwardWakerState* first;
    ForwardWakerState* second;
    bool               started { false };

    using Output = io::Result<async::IoCompletion>;

    auto poll(mut_ref<PollIoWithReplacementWaker> self, task::Context& cx) -> task::Poll<Output> {
        auto& wrapper = *self;
        if (! wrapper.started) {
            wrapper.first->target  = Some(cx.waker().clone());
            wrapper.second->target = Some(cx.waker().clone());
            auto first_waker       = task::Waker::from_raw(task::RawWaker::from_raw_parts(
                wrapper.first, rstd::addressof(FORWARD_WAKER_VTABLE)));
            auto second_waker      = task::Waker::from_raw(task::RawWaker::from_raw_parts(
                wrapper.second, rstd::addressof(FORWARD_WAKER_VTABLE)));
            auto first_cx          = task::Context { first_waker };
            auto second_cx         = task::Context { second_waker };
            if (future::poll(wrapper.operation, first_cx).is_ready() ||
                future::poll(wrapper.operation, second_cx).is_ready()) {
                rstd::panic { "pending IO operation completed before test write" };
            }
            wrapper.started = true;
            if (native_send(wrapper.writer, "w", 1) != 1) {
                return task::Poll<Output>::Ready(Err(io::error::Error::last_os_error()));
            }
            return task::Poll<Output>::Pending();
        }

        auto result = future::poll(wrapper.operation, cx);
        if (result.is_ready()) {
            wrapper.first->target  = None<task::Waker>();
            wrapper.second->target = None<task::Waker>();
        }
        return result;
    }
};

static_assert(Impled<PollIoWithReplacementWaker, future::Future<io::Result<async::IoCompletion>>>);

auto external_then_read(async::AnyExecutor           executor,
                        os::fd::RawFd                reader,
                        std::atomic<rstd::uint64_t>& external_thread,
                        std::atomic<rstd::uint64_t>& completion_thread)
    -> async::coro<io::Result<async::IoCompletion>> {
    if (! co_await executor) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::NotConnected }));
    }
    external_thread.store(thread::current().id().as_u64().get().to_primitive(),
                          std::memory_order_release);
    auto source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(reader));
    auto result = co_await async::IoOperation::read(source, usize(1));
    completion_thread.store(thread::current().id().as_u64().get().to_primitive(),
                            std::memory_order_release);
    co_return rstd::move(result);
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

auto cancel_read_then_release_source(os::fd::RawFd fd) -> async::coro<io::Result<empty>> {
    auto source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(fd));
    (void)co_await async::timeout(async::IoOperation::read(source, usize(1)),
                                  time::Duration::from_millis(u64(1)));
    co_return co_await rstd::move(source).release();
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

TEST(RstdAsyncIoOperation, FuturePollUsesOneSubmissionAndLatestWaker) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto source  = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(sockets.first));
    auto first   = ForwardWakerState {};
    auto second  = ForwardWakerState {};
    auto runtime = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto result = runtime.block_on(PollIoWithReplacementWaker {
        async::IoOperation::read(source, usize(1)),
        sockets.second,
        &first,
        &second,
    });

    ASSERT_TRUE(result.is_ok());
    auto completion = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(completion.transferred(), usize(1));
    EXPECT_EQ(completion.data()[usize()], u8('w'));
    EXPECT_EQ(first.wakes.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(second.wakes.load(std::memory_order_relaxed), 1);
}

TEST(RstdAsyncIoOperation, IoAwaitReturnsFromExternalExecutorToRuntimeOwner) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    ASSERT_EQ(native_send(sockets.second, "e", 1), 1);

    auto context  = async::LocalExecutorContext::make();
    auto executor = async::AnyExecutor::from_executor(context.executor());
    auto runtime =
        async::RuntimeBuilder::multi_thread().worker_threads(usize(1)).enable_io().build().unwrap();
    auto external_thread   = std::atomic<rstd::uint64_t> { 0 };
    auto completion_thread = std::atomic<rstd::uint64_t> { 0 };
    auto caller_thread     = thread::current().id().as_u64().get().to_primitive();
    auto joined            = runtime.spawn(
        external_then_read(executor.clone(), sockets.first, external_thread, completion_thread));

    for (int attempt = 0; attempt < 1000 && external_thread.load(std::memory_order_acquire) == 0;
         ++attempt) {
        (void)context.run_ready();
        if (external_thread.load(std::memory_order_acquire) == 0) {
            thread::sleep(time::Duration::from_millis(u64(1)));
        }
    }
    ASSERT_EQ(external_thread.load(std::memory_order_acquire), caller_thread);

    auto joined_result = runtime.block_on(rstd::move(joined));
    ASSERT_TRUE(joined_result.is_ok());
    auto result = rstd::move(joined_result).unwrap_unchecked();
    ASSERT_TRUE(result.is_ok());
    auto completion = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(completion.transferred(), usize(1));
    EXPECT_EQ(completion.data()[usize()], u8('e'));
    EXPECT_NE(completion_thread.load(std::memory_order_acquire), caller_thread);
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
    auto released = runtime.block_on(rstd::move(source).release());
    ASSERT_TRUE(released.is_ok());
    auto result = runtime.block_on(completion_round_trip(sockets.first, sockets.second));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().len(), usize(4));
}

TEST(RstdAsyncIoOperation, SourceReleaseWaitsForCanceledOperationTerminal) {
    auto pair = make_socket_pair();
    ASSERT_TRUE(pair.is_some());
    auto sockets = rstd::move(pair).unwrap_unchecked();
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();

    auto released = runtime.block_on(cancel_read_then_release_source(sockets.first));

    ASSERT_TRUE(released.is_ok());
    auto result = runtime.block_on(completion_round_trip(sockets.second, sockets.first));
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

#if defined(__linux__)
auto file_completion_round_trip(os::fd::BorrowedFd fd) -> async::coro<io::Result<bytes::Bytes>> {
    auto source = async::CompletionSource::file(fd);
    auto write =
        co_await async::IoOperation::write(source, bytes::Bytes::copy_from_slice("file"_bytes));
    if (write.is_err()) co_return Err(rstd::move(write).unwrap_err_unchecked());
    if (rstd::move(write).unwrap_unchecked().transferred() != usize(4)) {
        co_return Err(
            io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::WriteZero }));
    }
    if (::lseek(fd.as_raw_fd(), 0, SEEK_SET) < 0) {
        co_return Err(io::error::Error::last_os_error());
    }

    auto read = co_await async::IoOperation::read(source, usize(4));
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());
    co_return Ok(rstd::move(read).unwrap_unchecked().into_data());
}

TEST(RstdAsyncIoOperation, LinuxFileReadWriteCompletesThroughIoUring) {
    char path[] = "/tmp/rstd-io-uring-XXXXXX";
    auto raw_fd = ::mkstemp(path);
    ASSERT_GE(raw_fd, 0);
    ASSERT_EQ(::unlink(path), 0);
    auto fd      = os::fd::OwnedFd::from_raw_fd(raw_fd);
    auto runtime = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto result = runtime.block_on(file_completion_round_trip(fd.as_fd()));
    if (result.is_err() && result.unwrap_err_unchecked().kind() ==
                               io::error::ErrorKind { io::error::ErrorKind::Unsupported }) {
        GTEST_SKIP() << "io_uring is unavailable";
    }

    ASSERT_TRUE(result.is_ok());
    auto data = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(data.len(), usize(4));
    EXPECT_EQ(data[usize()], u8('f'));
    EXPECT_EQ(data[usize(1)], u8('i'));
    EXPECT_EQ(data[usize(2)], u8('l'));
    EXPECT_EQ(data[usize(3)], u8('e'));
}
#endif

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
