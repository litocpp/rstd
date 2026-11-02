#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import rstd.sys.libc;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd;
using process::Command;
using process::Stdio;
using os::fd::OwnedFd;
namespace libc = rstd::sys::libc;

template<typename T>
concept PipeReader = requires(const T& endpoint) { endpoint.read(); };
template<typename T>
concept PipeWriter =
    requires(const T& endpoint, bytes::Bytes data) { endpoint.write(rstd::move(data)); };
static_assert(PipeReader<async::pipe::Receiver> && ! PipeWriter<async::pipe::Receiver>);
static_assert(PipeWriter<async::pipe::Sender> && ! PipeReader<async::pipe::Sender>);

#if RSTD_OS_UNIX
TEST(AsyncPipe, RejectsWrongDirectionBeforeChangingFlags) {
    int fds[2];
    ASSERT_EQ(libc::pipe2(fds, libc::O_CLOEXEC), 0);
    auto reader       = OwnedFd::from_raw_fd(fds[0]);
    auto writer       = OwnedFd::from_raw_fd(fds[1]);
    auto read_copy    = reader.try_clone().unwrap();
    auto read_fd      = read_copy.as_raw_fd();
    auto wrong_writer = async::pipe::Sender::from_owned_fd(rstd::move(read_copy));
    ASSERT_TRUE(wrong_writer.is_err());
    EXPECT_EQ(wrong_writer.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
    EXPECT_EQ(libc::fcntl(read_fd, libc::F_GETFD), -1);
    EXPECT_EQ(libc::get_errno(), libc::EBADF);
    EXPECT_EQ(libc::fcntl(fds[0], libc::F_GETFL) & libc::O_NONBLOCK, 0);

    auto write_copy   = writer.try_clone().unwrap();
    auto write_fd     = write_copy.as_raw_fd();
    auto wrong_reader = async::pipe::Receiver::from_owned_fd(rstd::move(write_copy));
    ASSERT_TRUE(wrong_reader.is_err());
    EXPECT_EQ(wrong_reader.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
    EXPECT_EQ(libc::fcntl(write_fd, libc::F_GETFD), -1);
    EXPECT_EQ(libc::get_errno(), libc::EBADF);
    EXPECT_EQ(libc::fcntl(fds[1], libc::F_GETFL) & libc::O_NONBLOCK, 0);
}

TEST(AsyncPipe, RejectsRegularFilesSocketsAndInvalidDescriptors) {
    char name[] = "/tmp/rstd-async-pipe-XXXXXX";
    int  fd     = libc::mkstemp(name);
    ASSERT_GE(fd, 0);
    (void)libc::unlink(name);
    auto file    = OwnedFd::from_raw_fd(fd);
    auto regular = async::pipe::Receiver::from_owned_fd(file.try_clone().unwrap());
    ASSERT_TRUE(regular.is_err());
    EXPECT_EQ(regular.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
    auto regular_writer = async::pipe::Sender::from_owned_fd(file.try_clone().unwrap());
    ASSERT_TRUE(regular_writer.is_err());
    EXPECT_EQ(regular_writer.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
    EXPECT_EQ(libc::fcntl(fd, libc::F_GETFL) & libc::O_NONBLOCK, 0);
    int sockets[2];
    ASSERT_EQ(libc::socketpair(libc::AF_UNIX, libc::SOCK_STREAM, 0, sockets), 0);
    auto first  = OwnedFd::from_raw_fd(sockets[0]);
    auto second = OwnedFd::from_raw_fd(sockets[1]);
    auto socket = async::pipe::Sender::from_owned_fd(rstd::move(first));
    ASSERT_TRUE(socket.is_err());
    EXPECT_EQ(socket.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
    auto invalid = async::pipe::Receiver::from_owned_fd(OwnedFd {});
    ASSERT_TRUE(invalid.is_err());
    EXPECT_EQ(invalid.unwrap_err().raw_os_error(), Some(i32(libc::EBADF)));
}

TEST(AsyncPipe, SetsNonblockingAndPreservesDescriptorFlags) {
    int fds[2];
    ASSERT_EQ(libc::pipe2(fds, libc::O_CLOEXEC), 0);
    auto reader = async::pipe::Receiver::from_owned_fd(OwnedFd::from_raw_fd(fds[0]));
    auto writer = async::pipe::Sender::from_owned_fd(OwnedFd::from_raw_fd(fds[1]));
    ASSERT_TRUE(reader.is_ok());
    ASSERT_TRUE(writer.is_ok());
    for (int fd : fds) {
        EXPECT_NE(libc::fcntl(fd, libc::F_GETFL) & libc::O_NONBLOCK, 0);
        EXPECT_NE(libc::fcntl(fd, libc::F_GETFD) & libc::FD_CLOEXEC, 0);
    }
}
#endif

#if RSTD_OS_LINUX
TEST(AsyncPipe, AcceptsReadWritePipeDescriptors) {
    int fds[2];
    ASSERT_EQ(libc::pipe2(fds, libc::O_CLOEXEC), 0);
    auto reader = OwnedFd::from_raw_fd(fds[0]);
    auto writer = OwnedFd::from_raw_fd(fds[1]);
    auto path   = ::alloc::ffi::CString::make(rstd::format("/proc/self/fd/{}", fds[0])).unwrap();
    int  fd     = libc::open(path.as_ptr(), libc::O_RDWR | libc::O_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto duplex = OwnedFd::from_raw_fd(fd);
    EXPECT_TRUE(async::pipe::Receiver::from_owned_fd(duplex.try_clone().unwrap()).is_ok());
    EXPECT_TRUE(async::pipe::Sender::from_owned_fd(rstd::move(duplex)).is_ok());
}
#endif

#if RSTD_OS_LINUX || RSTD_OS_MACOS
auto read_all(async::pipe::Receiver reader) -> async::coro<io::Result<Vec<u8>>> {
    Vec<u8> output;
    for (;;) {
        auto chunk = co_await reader.read(usize(113));
        if (chunk.is_err()) co_return Err(rstd::move(chunk).unwrap_err());
        if (chunk->is_empty()) co_return Ok(rstd::move(output));
        output.extend_from_slice(chunk->as_slice());
    }
}

auto reaped(i32 pid) -> bool {
    int status;
    return libc::waitpid(pid.to_primitive(), &status, libc::WNOHANG_) == -1 &&
           libc::get_errno() == libc::ECHILD;
}

auto run(async::coro<bool> task) -> bool {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result  = runtime.block_on(
        async::timeout(rstd::move(task).into_future(), time::Duration::from_secs(u64(5))));
    return result.is_ok() && *result;
}

auto command_configuration() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = Command::make("/bin/sh"_str);
    command.arg("-c"_str)
        .arg("printf '%s:%s:%s' \"$RSTD_ASYNC_VALUE\" \"$1\" \"$PWD\"; printf err >&2; exit 17"_str)
        .arg("sh"_str)
        .arg("argument"_str)
        .env_clear()
        .env("RSTD_ASYNC_VALUE"_str, "value"_str)
        .current_dir("/"_str)
        .set_stdout(Stdio::piped())
        .set_stderr(Stdio::piped());
    auto child = supervisor.handle().spawn(command).unwrap();
    if (child.take_stdin().is_some()) co_return false;
    auto output  = co_await read_all(child.take_stdout().unwrap());
    auto error   = co_await read_all(child.take_stderr().unwrap());
    auto status  = co_await child.wait();
    auto again   = co_await child.wait();
    auto stopped = co_await supervisor.shutdown();
    co_return output.is_ok() && output->as_slice() == "value:argument:/"_bytes && error.is_ok() &&
        error->as_slice() == "err"_bytes && status.is_ok() && status->code() == Some(i32(17)) &&
        again.is_ok() && again->code() == Some(i32(17)) && reaped(child.id()) && stopped.is_ok();
}

auto command_input(bool take_input, [[maybe_unused]] bool pidfd) -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = Command::make("/bin/cat"_str);
    command.set_stdin(Stdio::piped()).set_stdout(take_input ? Stdio::piped() : Stdio::null());
#if RSTD_OS_LINUX
    os::linux::process::CommandExt::create_pidfd(command, pidfd);
#endif
    auto child   = supervisor.handle().spawn(command).unwrap();
    auto pending = child.try_wait();
    if (pending.is_err() || pending->is_some()) co_return false;
    if (take_input) {
        auto input    = child.take_stdin().unwrap();
        auto output   = async::spawn(read_all(child.take_stdout().unwrap()));
        auto canceled = co_await async::timeout(child.wait().into_future(),
                                                time::Duration::from_millis(u64(5)));
        if (canceled.is_ok()) co_return false;
        Vec<u8> data;
        data.resize(usize(131072), u8(42));
        auto written = co_await input.write_all(bytes::Bytes::from_vec(rstd::move(data)));
        if (written.is_err() || *written != usize(131072)) co_return false;
        if ((co_await rstd::move(input).close()).is_err()) co_return false;
        auto received = co_await output;
        if (received.is_err() || received->is_err() || (*received)->len() != usize(131072))
            co_return false;
        for (auto value : (*received)->as_slice())
            if (value != u8(42)) co_return false;
    }
    auto status  = co_await child.wait();
    auto stopped = co_await supervisor.shutdown();
    co_return status.is_ok() && status->success() && child.take_stdin().is_none() &&
        reaped(child.id()) && stopped.is_ok();
}

auto command_failures() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto handle     = supervisor.handle();
    auto missing    = Command::make("/nonexistent/rstd-async-command"_str);
    auto failed     = handle.spawn(missing);
    if (failed.is_ok() || failed.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::NotFound })
        co_return false;
    supervisor.stop_accepting();
    auto rejected = handle.spawn(missing);
    if (rejected.is_ok() ||
        rejected.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::NotConnected })
        co_return false;
    co_return (co_await supervisor.shutdown()).is_ok();
}

auto command_drop() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = Command::make("/bin/cat"_str);
    command.set_stdin(Stdio::piped()).set_stdout(Stdio::null());
    i32                         pid;
    Option<async::pipe::Sender> input;
    {
        auto child = supervisor.handle().spawn(command).unwrap();
        pid        = child.id();
        input      = child.take_stdin();
    }
    auto stopped = co_await supervisor.shutdown();
    co_return stopped.is_ok() && reaped(pid);
}

TEST(AsyncCommand, PreservesConfigurationAndCapturesStdoutAndStderr) {
    EXPECT_TRUE(run(command_configuration()));
}
TEST(AsyncCommand, WaitClosesUntakenStdin) {
    EXPECT_TRUE(run(command_input(false, false)));
}
TEST(AsyncCommand, TakenStdinSurvivesCanceledWaitAndSupportsBackpressure) {
    EXPECT_TRUE(run(command_input(true, false)));
}
#if RSTD_OS_LINUX
TEST(AsyncCommand, PreservesPidfdWaitOwnership) {
    EXPECT_TRUE(run(command_input(false, true)));
}
#endif
TEST(AsyncCommand, SpawnFailureReleasesReservationAndStoppedHandleRejectsCreation) {
    EXPECT_TRUE(run(command_failures()));
}
TEST(AsyncCommand, DroppedChildRemainsOwnedUntilShutdownReapsIt) {
    EXPECT_TRUE(run(command_drop()));
}
#endif

int main() {
    return rstd::test::run_registered().to_primitive();
}
