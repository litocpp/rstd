#include <cerrno>
#include <cstdio>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

static volatile sig_atomic_t pipe_signals;
static void                  observe_pipe(int) {
    pipe_signals = 1;
}

auto broken_pipe(int mode) -> int {
    struct sigaction action {};
    action.sa_handler = mode == 1 ? SIG_IGN : mode == 2 ? observe_pipe : SIG_DFL;
    ::sigemptyset(&action.sa_mask);
    if (::sigaction(SIGPIPE, &action, nullptr) != 0) return 10;
    sigset_t mask, previous, pending;
    ::sigemptyset(&mask);
    ::sigaddset(&mask, SIGUSR1);
    auto blocked = mode == 3 || mode == 4;
    if (blocked) ::sigaddset(&mask, SIGPIPE);
    if (::pthread_sigmask(SIG_SETMASK, &mask, nullptr) != 0) return 11;
    if (mode == 4 && ::raise(SIGPIPE) != 0) return 12;
    int descriptors[2];
    if (::pipe(descriptors) != 0) return 13;
    if (mode == 5) {
        auto reader = ::fork();
        if (reader < 0) return 18;
        if (reader == 0) {
            ::close(descriptors[1]);
            char value;
            ::_exit(::read(descriptors[0], &value, 1) == 1 ? 0 : 1);
        }
        ::close(descriptors[0]);
        auto                     input = rstd::process::ChildStdin(descriptors[1]);
        rstd::array<u8, 1048576> data {};
        auto                     result = rstd::as<rstd::io::Write>(input).write(data.as_slice());
        int                      status;
        while (::waitpid(reader, &status, 0) < 0)
            if (errno != EINTR) return 19;
        if (! WIFEXITED(status) || WEXITSTATUS(status) || result.is_err() ||
            result.unwrap() == usize() || result.unwrap() >= usize(1048576))
            return 20;
        auto closed = rstd::as<rstd::io::Write>(input).write("x"_bytes);
        return closed.is_err() && closed.unwrap_err().raw_os_error() == Some(i32(EPIPE)) ? 0 : 21;
    }
    ::close(descriptors[0]);
    auto input  = rstd::process::ChildStdin(descriptors[1]);
    auto result = rstd::as<rstd::io::Write>(input).write("x"_bytes);
    if (result.is_ok() || result.unwrap_err().raw_os_error() != Some(i32(EPIPE))) return 14;
    struct sigaction after {};
    if (::sigaction(SIGPIPE, nullptr, &after) != 0 || after.sa_handler != action.sa_handler ||
        pipe_signals)
        return 15;
    if (::pthread_sigmask(SIG_SETMASK, nullptr, &previous) != 0 ||
        ::sigismember(&previous, SIGUSR1) != 1 || ::sigismember(&previous, SIGPIPE) != blocked)
        return 16;
    if (::sigpending(&pending) != 0 || ::sigismember(&pending, SIGPIPE) != (mode == 4)) return 17;
    return 0;
}

int main() {
    for (int mode = 0; mode < 6; ++mode) {
        auto pid = ::fork();
        if (pid < 0) return 1;
        if (pid == 0) ::_exit(broken_pipe(mode));
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0)
            if (errno != EINTR) return 2;
        if (! WIFEXITED(status) || WEXITSTATUS(status)) {
            std::fprintf(stderr, "ChildStdin mode=%d status=%d\n", mode, status);
            return 3;
        }
    }
    int descriptors[2];
    if (::pipe(descriptors) != 0) return 4;
    auto input   = rstd::process::ChildStdin(descriptors[1]);
    auto written = rstd::as<rstd::io::Write>(input).write("x"_bytes);
    char value   = 0;
    if (written.is_err() || written.unwrap() != usize(1) ||
        ::read(descriptors[0], &value, 1) != 1 || value != 'x')
        return 5;
    ::close(descriptors[0]);
    auto missing = rstd::process::ChildStdin();
    auto failed  = rstd::as<rstd::io::Write>(missing).write("x"_bytes);
    if (failed.is_ok() || failed.unwrap_err().raw_os_error() != Some(i32(EBADF))) return 6;
    std::puts("ChildStdin pipe checks=8");
    return 0;
}
