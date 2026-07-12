#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct PipePair {
    sys::fd::OwnedFd reader;
    sys::fd::OwnedFd writer;
};

auto make_pipe() -> Option<PipePair> {
    int fds[2] = { -1, -1 };
    if (sys::libc::pipe2(fds, sys::libc::O_NONBLOCK | sys::libc::O_CLOEXEC) != 0) {
        return None();
    }
    return Some(PipePair {
        sys::fd::OwnedFd::from_raw_fd(fds[0]),
        sys::fd::OwnedFd::from_raw_fd(fds[1]),
    });
}

auto write_byte(sys::fd::RawFd fd) -> bool {
    const u8 value = 1;
    return sys::libc::write(fd, rstd::addressof(value), 1) == 1;
}

auto wait_readable(async::Registration& registration, std::atomic<bool>& entered)
    -> async::coro<io::Result<async::ReadyEvent>> {
    entered.store(true, std::memory_order_release);
    co_return co_await async::ReadinessFuture { registration, async::Interest::readable() };
}

auto wait_readable_on_owner(async::Registration& registration,
                            std::atomic<u64>&    before,
                            std::atomic<u64>&    after) -> async::coro<bool> {
    before.store(thread::current().id().as_u64().get(), std::memory_order_release);
    auto ready = co_await async::ReadinessFuture { registration, async::Interest::readable() };
    after.store(thread::current().id().as_u64().get(), std::memory_order_release);
    co_return ready.is_ok() && rstd::move(ready).unwrap_unchecked().is_readable();
}

auto wait_owned_registration(async::Registration registration, std::atomic<bool>& entered)
    -> async::coro<void> {
    entered.store(true, std::memory_order_release);
    (void)co_await async::ReadinessFuture { registration, async::Interest::readable() };
}

TEST(RstdAsyncPoll, PipeReadinessCompletesThroughCurrentWorker) {
    auto pipe = make_pipe();
    ASSERT_TRUE(pipe.is_some());
    auto fds          = rstd::move(pipe).unwrap_unchecked();
    auto registration = async::Registration::register_fd(fds.reader.as_raw_fd()).unwrap();
    auto entered      = std::atomic<bool> { false };
    auto writer_fd    = fds.writer.as_raw_fd();
    auto writer       = thread::spawn([writer_fd, &entered] {
                      while (! entered.load(std::memory_order_acquire)) {
                          hint::spin_loop();
                      }
                      thread::sleep(time::Duration::from_millis(1));
                      return write_byte(writer_fd);
                        }).unwrap();
    auto runtime      = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();

    auto ready = runtime.block_on(wait_readable(registration, entered));
    ASSERT_TRUE(ready.is_ok());
    EXPECT_TRUE(rstd::move(ready).unwrap_unchecked().is_readable());
    EXPECT_TRUE(rstd::move(writer).join().unwrap());
}

TEST(RstdAsyncPoll, ReadinessWithoutIoReturnsUnsupported) {
    auto pipe = make_pipe();
    ASSERT_TRUE(pipe.is_some());
    auto fds          = rstd::move(pipe).unwrap_unchecked();
    auto registration = async::Registration::register_fd(fds.reader.as_raw_fd()).unwrap();
    auto entered      = std::atomic<bool> { false };
    auto runtime      = async::RuntimeBuilder::current_thread().build().unwrap();

    auto ready = runtime.block_on(wait_readable(registration, entered));
    ASSERT_TRUE(ready.is_err());
    EXPECT_EQ(rstd::move(ready).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::Unsupported });
}

TEST(RstdAsyncPoll, WorkerOwnedReadinessReturnsToEachOwnerThread) {
    auto first_pipe  = make_pipe();
    auto second_pipe = make_pipe();
    ASSERT_TRUE(first_pipe.is_some());
    ASSERT_TRUE(second_pipe.is_some());
    auto first               = rstd::move(first_pipe).unwrap_unchecked();
    auto second              = rstd::move(second_pipe).unwrap_unchecked();
    auto first_registration  = async::Registration::register_fd(first.reader.as_raw_fd()).unwrap();
    auto second_registration = async::Registration::register_fd(second.reader.as_raw_fd()).unwrap();
    auto first_before        = std::atomic<u64> { 0 };
    auto first_after         = std::atomic<u64> { 0 };
    auto second_before       = std::atomic<u64> { 0 };
    auto second_after        = std::atomic<u64> { 0 };
    auto runtime =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_io().build().unwrap();

    auto first_join =
        runtime.spawn(wait_readable_on_owner(first_registration, first_before, first_after));
    auto second_join =
        runtime.spawn(wait_readable_on_owner(second_registration, second_before, second_after));
    ASSERT_TRUE(write_byte(first.writer.as_raw_fd()));
    ASSERT_TRUE(write_byte(second.writer.as_raw_fd()));

    auto first_result  = runtime.block_on(rstd::move(first_join));
    auto second_result = runtime.block_on(rstd::move(second_join));
    ASSERT_TRUE(first_result.is_ok());
    ASSERT_TRUE(second_result.is_ok());
    EXPECT_TRUE(rstd::move(first_result).unwrap());
    EXPECT_TRUE(rstd::move(second_result).unwrap());
    EXPECT_EQ(first_before.load(std::memory_order_acquire),
              first_after.load(std::memory_order_acquire));
    EXPECT_EQ(second_before.load(std::memory_order_acquire),
              second_after.load(std::memory_order_acquire));
    EXPECT_NE(first_before.load(std::memory_order_acquire),
              second_before.load(std::memory_order_acquire));
}

TEST(RstdAsyncPoll, ShutdownCancelsReadinessDuringTaskTransition) {
    auto entered = std::atomic<bool> { false };
    auto joined  = [&entered] {
        auto pipe = make_pipe();
        if (pipe.is_none()) {
            rstd::panic { "pipe2 failed" };
        }
        auto fds          = rstd::move(pipe).unwrap_unchecked();
        auto registration = async::Registration::register_fd(fds.reader.as_raw_fd()).unwrap();
        auto runtime =
            async::RuntimeBuilder::multi_thread().worker_threads(1).enable_io().build().unwrap();
        auto handle = runtime.spawn(wait_owned_registration(rstd::move(registration), entered));
        while (! entered.load(std::memory_order_acquire)) {
            hint::spin_loop();
        }
        return handle;
    }();

    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
}

} // namespace
