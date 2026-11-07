module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX || RSTD_OS_MACOS
#include <rstd/test/gtest.hpp>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

export module rstd:async.readiness_tests;
#if RSTD_OS_LINUX || RSTD_OS_MACOS
import :sys.pal.poll;

using namespace rstd;
namespace pal = rstd::sys::pal::poll;

namespace
{

struct SocketPair {
    int read { -1 };
    int write { -1 };

    ~SocketPair() {
        if (read >= 0) ::close(read);
        if (write >= 0) ::close(write);
    }

    auto init() -> bool {
        int sockets[2] { -1, -1 };
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;
        read  = sockets[0];
        write = sockets[1];
        for (auto fd : sockets) {
            auto flags = ::fcntl(fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return false;
        }
        return true;
    }
};

auto key(rstd::uint32_t slot, rstd::uint32_t generation = 1) -> u64 {
    return u64((static_cast<rstd::uint64_t>(generation) << 32) | slot);
}

auto read_operation(int fd, u64 source, u64 operation, char& data) -> pal::Operation {
    return pal::Operation::read(
        pal::SourceKind::Socket, fd, source, operation, &data, usize(1), None(), u32());
}

TEST(RstdAsyncReadiness, FifoCancelAndReusedKeys) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.init());
    auto init = pal::Poller::init(pal::BackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(init.is_ok());
    auto poll   = rstd::move(init).unwrap_unchecked();
    auto source = key(0);
    char bytes[4] {};
    for (rstd::uint32_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(
            poll.poller.submit_operation(read_operation(sockets.read, source, key(i), bytes[i]))
                .is_ok());
    }
    EXPECT_TRUE(poll.poller.release_completion_source(source, sockets.read).is_err());
    ASSERT_TRUE(poll.poller.cancel_operation(key(1)).is_ok());
    auto canceled = poll.poller.wait(pal::WaitMode::Immediate, None());
    ASSERT_TRUE(canceled.is_ok());
    auto canceled_batch = rstd::move(canceled).unwrap_unchecked();
    ASSERT_EQ(canceled_batch.len(), usize(1));
    ASSERT_EQ(canceled_batch[usize()].kind(), pal::EventKind::Completion);
    EXPECT_EQ(canceled_batch[usize()].operation_key(), key(1));
    EXPECT_TRUE(canceled_batch[usize()].is_error());

    ASSERT_EQ(::send(sockets.write, "ac", 2, 0), 2);
    auto ready = poll.poller.wait(pal::WaitMode::Immediate, None());
    ASSERT_TRUE(ready.is_ok());
    auto batch = rstd::move(ready).unwrap_unchecked();
    ASSERT_EQ(batch.len(), usize(2));
    ASSERT_EQ(batch[usize()].kind(), pal::EventKind::Completion);
    ASSERT_EQ(batch[usize(1)].kind(), pal::EventKind::Completion);
    EXPECT_EQ(batch[usize()].operation_key(), key(0));
    EXPECT_EQ(batch[usize(1)].operation_key(), key(2));
    EXPECT_FALSE(batch[usize()].is_error());
    EXPECT_FALSE(batch[usize(1)].is_error());
    EXPECT_EQ(bytes[0], 'a');
    EXPECT_EQ(bytes[1], '\0');
    EXPECT_EQ(bytes[2], 'c');
    ASSERT_TRUE(poll.poller.release_completion_source(source, sockets.read).is_ok());

    auto next_source = key(0, 2);
    auto next_op     = key(1, 2);
    ASSERT_TRUE(
        poll.poller.submit_operation(read_operation(sockets.read, next_source, next_op, bytes[3]))
            .is_ok());
    ASSERT_TRUE(poll.poller.cancel_operation(key(1)).is_ok());
    ASSERT_TRUE(poll.poller.release_completion_source(source, sockets.read).is_ok());
    EXPECT_TRUE(poll.poller.release_completion_source(next_source, sockets.read).is_err());
    ASSERT_EQ(::send(sockets.write, "d", 1, 0), 1);
    auto next = poll.poller.wait(pal::WaitMode::Immediate, None());
    ASSERT_TRUE(next.is_ok());
    auto next_batch = rstd::move(next).unwrap_unchecked();
    ASSERT_EQ(next_batch.len(), usize(1));
    ASSERT_EQ(next_batch[usize()].kind(), pal::EventKind::Completion);
    EXPECT_EQ(next_batch[usize()].operation_key(), next_op);
    EXPECT_FALSE(next_batch[usize()].is_error());
    EXPECT_EQ(bytes[3], 'd');
    EXPECT_TRUE(poll.poller.release_completion_source(next_source, sockets.read).is_ok());
}

TEST(RstdAsyncReadiness, ShutdownCompletesAllPendingOperations) {
    SocketPair sockets;
    ASSERT_TRUE(sockets.init());
    auto init = pal::Poller::init(pal::BackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(init.is_ok());
    auto poll = rstd::move(init).unwrap_unchecked();
    char bytes[2] {};
    auto source = key(0);
    for (rstd::uint32_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(
            poll.poller.submit_operation(read_operation(sockets.read, source, key(i), bytes[i]))
                .is_ok());
    }
    EXPECT_TRUE(poll.poller.has_pending_operations());
    poll.poller.begin_shutdown();
    EXPECT_FALSE(poll.poller.has_pending_operations());
    auto ready = poll.poller.wait(pal::WaitMode::Immediate, None());
    ASSERT_TRUE(ready.is_ok());
    auto batch = rstd::move(ready).unwrap_unchecked();
    ASSERT_EQ(batch.len(), usize(2));
    for (rstd::uint32_t i = 0; i < 2; ++i) {
        ASSERT_EQ(batch[usize(i)].kind(), pal::EventKind::Completion);
        EXPECT_EQ(batch[usize(i)].operation_key(), key(i));
        EXPECT_TRUE(batch[usize(i)].is_error());
    }
    EXPECT_TRUE(poll.poller.release_completion_source(source, sockets.read).is_ok());
}

} // namespace
#endif
