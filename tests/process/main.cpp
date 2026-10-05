#include <rstd/test/gtest.hpp>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>

import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::process::Command;
using rstd::process::Stdio;
namespace up = rstd::os::unix::process;
namespace lp = rstd::os::linux::process;

auto pidfd_supported() -> bool {
#ifdef SYS_pidfd_open
    auto fd = static_cast<int>(::syscall(SYS_pidfd_open, ::getpid(), 0));
    if (fd < 0) return false;
    ::close(fd);
    return true;
#else
    return false;
#endif
}

// Tests clean up explicitly even when an assertion fails.
struct Reap {
    int pid;
    ~Reap() {
        int  status;
        auto result = ::waitpid(pid, &status, WNOHANG);
        if (result == 0) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
};

auto waiting_command() -> Command {
    auto command = Command::make("/bin/cat"_str);
    command.set_stdin(Stdio::piped()).set_stdout(Stdio::null());
    return command;
}

TEST(Process, UnixProcessGroupSpawnPaths) {
    for (bool fork : rstd::array<bool, 2> { false, true }) {
        auto command = waiting_command();
        if (fork) {
            command = Command::make("cat"_str);
            command.env("PATH"_str, "/bin:/usr/bin"_str)
                .set_stdin(Stdio::piped())
                .set_stdout(Stdio::null());
        }
        up::CommandExt::process_group(command, i32());
        auto child   = command.spawn().unwrap();
        auto pid     = static_cast<int>(child.id().to_primitive());
        auto cleanup = Reap { pid };
        EXPECT_EQ(::getpgid(pid), pid);
        ASSERT_TRUE(up::ChildExt::send_process_group_signal(child, i32(SIGTERM)).is_ok());
        EXPECT_EQ(child.wait().unwrap().signal().unwrap(), i32(SIGTERM));
        EXPECT_EQ(child.id().to_primitive(), static_cast<unsigned int>(pid));
        EXPECT_TRUE(up::ChildExt::kill_process_group(child).is_ok());
    }
}

TEST(Process, UnixExistingGroupAndIndividualSignal) {
    auto command = waiting_command();
    up::CommandExt::process_group(command, i32(::getpgrp()));
    auto child   = command.spawn().unwrap();
    auto cleanup = Reap { static_cast<int>(child.id().to_primitive()) };
    EXPECT_EQ(::getpgid(cleanup.pid), ::getpgrp());
    ASSERT_TRUE(up::ChildExt::send_signal(child, i32(SIGTERM)).is_ok());
    EXPECT_EQ(child.wait().unwrap().signal().unwrap(), i32(SIGTERM));
}

TEST(Process, PidFdOptInAndCachedWait) {
    auto command = waiting_command();
    lp::CommandExt::create_pidfd(command, true);
    auto child    = command.spawn().unwrap();
    auto cleanup  = Reap { static_cast<int>(child.id().to_primitive()) };
    auto borrowed = lp::ChildExt::pidfd(child);
    if (borrowed.is_err() && ! pidfd_supported()) GTEST_SKIP() << "pidfd unavailable";
    ASSERT_TRUE(borrowed.is_ok());
    auto fd = borrowed.unwrap()->as_raw_fd();
    EXPECT_TRUE((::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    EXPECT_TRUE(child.try_wait().unwrap().is_none());
    EXPECT_TRUE(child.stdin_pipe->as_fd().as_raw_fd() >= 0);
    ASSERT_TRUE(child.kill().is_ok());
    auto status = child.wait().unwrap();
    EXPECT_EQ(status.signal().unwrap(), i32(SIGKILL));
    EXPECT_EQ(child.wait().unwrap().signal().unwrap(), i32(SIGKILL));
    EXPECT_EQ(child.try_wait().unwrap()->signal().unwrap(), i32(SIGKILL));
    EXPECT_TRUE(child.kill().is_ok());
    EXPECT_TRUE(child.stdin_pipe.is_none());
}

TEST(Process, PidFdOwnershipTransferAndMissingHandle) {
    auto command = waiting_command();
    auto child   = command.spawn().unwrap();
    auto cleanup = Reap { static_cast<int>(child.id().to_primitive()) };
    EXPECT_TRUE(lp::ChildExt::pidfd(child).is_err());
    auto missing = lp::ChildExt::into_pidfd(rstd::move(child));
    ASSERT_TRUE(missing.is_err());
    auto recovered = rstd::move(missing).unwrap_err();
    EXPECT_TRUE(recovered.kill().is_ok());
    EXPECT_TRUE(recovered.wait().is_ok());

    lp::CommandExt::create_pidfd(command, true);
    auto with_fd    = command.spawn().unwrap();
    auto cleanup_fd = Reap { static_cast<int>(with_fd.id().to_primitive()) };
    if (lp::ChildExt::pidfd(with_fd).is_err() && ! pidfd_supported())
        GTEST_SKIP() << "pidfd unavailable";
    ASSERT_TRUE(lp::ChildExt::pidfd(with_fd).is_ok());
    // Consuming Child closes stdin; the standalone pidfd only owns the process descriptor.
    auto          descriptor = lp::ChildExt::into_pidfd(rstd::move(with_fd)).unwrap();
    struct pollfd pending { descriptor.as_fd().as_raw_fd(), POLLIN, 0 };
    ASSERT_EQ(::poll(&pending, 1, 5000), 1);
    EXPECT_TRUE(descriptor.wait().unwrap().success());
    auto repeated = descriptor.try_wait();
    if (repeated.is_ok()) EXPECT_TRUE(repeated.unwrap().unwrap().success());

    lp::CommandExt::create_pidfd(command, false);
    auto disabled         = command.spawn().unwrap();
    auto cleanup_disabled = Reap { static_cast<int>(disabled.id().to_primitive()) };
    EXPECT_TRUE(lp::ChildExt::pidfd(disabled).is_err());
    EXPECT_TRUE(disabled.kill().is_ok());
    EXPECT_TRUE(disabled.wait().is_ok());
}

TEST(Process, PidFdKillAndGroupSignal) {
    auto command = waiting_command();
    lp::CommandExt::create_pidfd(command, true);
    up::CommandExt::process_group(command, i32());
    auto child    = command.spawn().unwrap();
    auto cleanup  = Reap { static_cast<int>(child.id().to_primitive()) };
    auto borrowed = lp::ChildExt::pidfd(child);
    if (borrowed.is_err() && ! pidfd_supported()) GTEST_SKIP() << "pidfd unavailable";
    ASSERT_TRUE(borrowed.is_ok());
    auto group = up::ChildExt::kill_process_group(child);
    if (group.is_err()) {
        auto error = group.unwrap_err().raw_os_error().unwrap().to_primitive();
        EXPECT_EQ(error, EINVAL);
        EXPECT_TRUE(borrowed.unwrap()->kill().is_ok());
    }
    EXPECT_EQ(child.wait().unwrap().signal().unwrap(), i32(SIGKILL));
}

TEST(Process, DropDoesNotKillOrReap) {
    auto command = waiting_command();
    lp::CommandExt::create_pidfd(command, true);
    Option<rstd::process::ChildStdin> input;
    int                               pid;
    {
        auto child = command.spawn().unwrap();
        pid        = static_cast<int>(child.id().to_primitive());
        input      = child.take_stdin();
    }
    auto cleanup = Reap { pid };
    int  status;
    EXPECT_EQ(::waitpid(pid, &status, WNOHANG), 0);
    input = None();
    EXPECT_EQ(::waitpid(pid, &status, 0), pid);
    EXPECT_TRUE(WIFEXITED(status));
}

TEST(Process, PidFdSpawnFailure) {
    auto command = Command::make("/nonexistent/rstd-process-test"_str);
    lp::CommandExt::create_pidfd(command, true);
    EXPECT_TRUE(command.spawn().is_err());
}

int main() {
    ::alarm(20);
    return rstd::test::run_registered().to_primitive();
}
