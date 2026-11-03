#include <rstd/macro.hpp>
#if RSTD_OS_LINUX || RSTD_OS_MACOS
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
import rstd;
import rstd.sys.libc;

using namespace rstd::prelude;
using namespace rstd::literals;

auto check(bool value, const char* name) -> bool {
    ::fprintf(stderr, "%s %s\n", value ? "PASS" : "FAIL", name);
    return value;
}
auto receive(rstd::async::pipe::Receiver& reader, usize target) -> rstd::async::coro<bool> {
    usize total {};
    for (;;) {
        auto bytes = co_await reader.read(usize(113));
        if (bytes.is_err()) co_return false;
        if (bytes->is_empty()) break;
        for (auto byte : bytes->as_slice())
            if (byte != u8(42)) co_return false;
        total += bytes->len();
    }
    co_return total == target;
}
auto produce(rstd::async::pipe::Sender& writer, usize target) -> rstd::async::coro<bool> {
    Vec<u8> data;
    data.resize(target, u8(42));
    auto result = co_await writer.write_all(rstd::bytes::Bytes::from_vec(rstd::move(data)));
    auto closed = co_await rstd::move(writer).close();
    co_return result.is_ok() && *result == target&& closed.is_ok();
}
auto run() -> rstd::async::coro<bool> {
    using namespace rstd;
    int fds[2];
    if (sys::libc::pipe2(fds, O_CLOEXEC) < 0) co_return false;
    auto reader =
        async::pipe::Receiver::from_owned_fd(os::fd::OwnedFd::from_raw_fd(fds[0])).unwrap();
    auto writer = async::pipe::Sender::from_owned_fd(os::fd::OwnedFd::from_raw_fd(fds[1])).unwrap();
    auto received = async::spawn(receive(reader, usize(131072)));
    if (! check(co_await produce(writer, usize(131072)),
                "pipe write_all handles partial writes and backpressure"))
        co_return false;
    auto read = co_await received;
    if (! check(read.is_ok() && *read, "pipe drains fragmented data and EOF")) co_return false;
    if (! check((co_await rstd::move(reader).close()).is_ok(),
                "pipe close waits for deregistration"))
        co_return false;
    if (! check(::fcntl(fds[0], F_GETFD) < 0 && errno == EBADF,
                "pipe close releases its owned descriptor"))
        co_return false;

    if (sys::libc::pipe2(fds, O_CLOEXEC) < 0) co_return false;
    auto broken = async::pipe::Sender::from_owned_fd(os::fd::OwnedFd::from_raw_fd(fds[1])).unwrap();
    ::close(fds[0]);
    auto failed = co_await broken.write(bytes::Bytes::from_vec(Vec<u8>::from("x"_bytes)));
    if (! check(failed.is_err() && failed.unwrap_err().raw_os_error() == Some(i32(EPIPE)),
                "pipe EPIPE does not terminate its host"))
        co_return false;
    (void)co_await rstd::move(broken).close();

    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto handle     = supervisor.handle();
    int  pid        = ::fork();
    if (pid == 0) {
        for (;;) ::pause();
    }
    if (pid < 0) co_return false;
    auto child     = handle.adopt(i32(pid)).unwrap();
    auto duplicate = handle.adopt(i32(pid));
    if (! check(duplicate.is_err(), "native child has exactly one reap owner")) co_return false;
    auto running = child.try_wait();
    if (! check(running.is_ok() && running->is_none(), "try_wait reports a running child"))
        co_return false;
    auto timed =
        co_await async::timeout(child.wait().into_future(), time::Duration::from_millis(u64(10)));
    if (! check(timed.is_err(), "canceling an exit wait leaves the child owned")) co_return false;
    child.start_kill().unwrap();
    auto status = co_await child.wait();
    if (! check(status.is_ok() && status->signal() == Some(i32(SIGKILL)),
                "terminated child is asynchronously reaped"))
        co_return false;
    auto repeat = co_await child.wait();
    if (! check(repeat.is_ok() && repeat->signal() == Some(i32(SIGKILL)),
                "completed child wait is repeatable"))
        co_return false;
    auto cached = child.try_wait();
    if (! check(cached.is_ok() && cached->is_some() && (*cached)->signal() == Some(i32(SIGKILL)),
                "try_wait returns the cached exit status"))
        co_return false;
    if (! check((co_await child.kill()).is_ok(), "kill is harmless after reaping")) co_return false;

    int unrelated = ::fork();
    if (unrelated == 0) ::_exit(19);
    if (unrelated < 0) co_return false;
    int early = ::fork();
    if (early == 0) ::_exit(17);
    if (early < 0) co_return false;
    auto early_child = handle.adopt(i32(early)).unwrap();
    auto first       = async::spawn(early_child.wait_finished());
    auto second      = co_await early_child.wait();
    auto joined      = co_await first;
    if (! check(second.is_ok() && second->code() == Some(i32(17)) && joined.is_ok() &&
                    joined->is_ok(),
                "early exit wakes independent waiters"))
        co_return false;
    int raw {};
    if (! check(::waitpid(unrelated, &raw, 0) == unrelated && WIFEXITED(raw) &&
                    WEXITSTATUS(raw) == 19,
                "supervisor never reaps another owner's child"))
        co_return false;

    int dropped = ::fork();
    if (dropped == 0) {
        for (;;) ::pause();
    }
    if (dropped < 0) co_return false;
    {
        auto owned = handle.adopt(i32(dropped)).unwrap();
    }
    auto reservation = handle.prepare().unwrap();
    auto shutdown    = async::spawn(supervisor.shutdown());
    co_await async::sleep(time::Duration::from_millis(u64(1)));
    if (! check(! shutdown.is_finished(), "shutdown waits for a reserved in-flight spawn"))
        co_return false;
    if (! check(handle.prepare().is_err(), "stopping supervisor rejects new spawn reservations"))
        co_return false;
    int late = ::fork();
    if (late == 0) {
        for (;;) ::pause();
    }
    if (late < 0) co_return false;
    auto late_child  = reservation.adopt(i32(late)).unwrap();
    auto late_status = co_await late_child.wait();
    if (! check(late_status.is_ok() && late_status->signal() == Some(i32(SIGKILL)),
                "reserved child adopted during shutdown is killed and reaped"))
        co_return false;
    auto stopped = co_await shutdown;
    if (! check(stopped.is_ok() && stopped->is_ok(),
                "shutdown drains dropped and running children"))
        co_return false;
    if (! check(::waitpid(dropped, &raw, WNOHANG) < 0 && errno == ECHILD,
                "dropped child leaves no zombie"))
        co_return false;

    auto killer   = async::ProcessSupervisor::make().unwrap();
    auto reserved = killer.handle().prepare().unwrap();
    int  killed   = ::fork();
    if (killed == 0) {
        for (;;) ::pause();
    }
    if (killed < 0) co_return false;
    auto victim = reserved.adopt(i32(killed)).unwrap();
    if (! check((co_await victim.kill()).is_ok(), "kill waits for a live child to be reaped"))
        co_return false;
    if (! check(::waitpid(killed, &raw, WNOHANG) < 0 && errno == ECHILD, "kill leaves no zombie"))
        co_return false;
    if (! check((co_await killer.shutdown()).is_ok(), "supervisor drains after kill"))
        co_return false;
    co_return true;
}
int main() {
    ::alarm(30);
    auto runtime = rstd::async::RuntimeBuilder::current_thread()
                       .max_blocking_threads(usize(1))
                       .enable_all()
                       .build()
                       .unwrap();
    return runtime.block_on(run()) ? 0 : 1;
}
#else
int main() {
    return 0;
}
#endif
