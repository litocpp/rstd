module;
#include <rstd/macro.hpp>

#if RSTD_OS_UNIX
#    include <sys/wait.h>
#    include <unistd.h>
#    include <spawn.h>
#    include <signal.h>
#    include <fcntl.h>
#    include <errno.h>
#    include <stdlib.h>
#endif

module rstd;

using namespace rstd::prelude;

// ── ExitStatus::from_raw (Unix) ──────────────────────────────────────────
#if RSTD_OS_UNIX
auto rstd::process::ExitStatus::from_raw(i32 raw) noexcept -> ExitStatus {
    if (WIFEXITED(raw)) return ExitStatus::from_code(WEXITSTATUS(raw));
    if (WIFSIGNALED(raw)) return ExitStatus::from_signal(WTERMSIG(raw));
    return ExitStatus::from_code(-1);
}
#endif

// ── Pipe handle destructors ─────────────────────────────────────────────
namespace rstd::process
{

ChildStdin::~ChildStdin() {
#if RSTD_OS_UNIX
    if (fd >= 0) ::close(fd);
#endif
}
ChildStdout::~ChildStdout() {
#if RSTD_OS_UNIX
    if (fd >= 0) ::close(fd);
#endif
}
ChildStderr::~ChildStderr() {
#if RSTD_OS_UNIX
    if (fd >= 0) ::close(fd);
#endif
}

// ── Child ────────────────────────────────────────────────────────────────

Child::~Child() {}

auto Child::wait() -> io::Result<ExitStatus> {
#if RSTD_OS_UNIX
    // Drop stdin pipe so child sees EOF.
    stdin_pipe = {};

    int status = 0;
    while (true) {
        auto ret = ::waitpid(pid, &status, 0);
        if (ret == -1) {
            if (errno == EINTR) continue;
            return Err(io::error::Error::from_raw_os_error(errno));
        }
        break;
    }
    pid = -1;
    return Ok(ExitStatus::from_raw(status));
#else
    return Err(
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

auto Child::kill() -> io::Result<rstd::empty> {
#if RSTD_OS_UNIX
    if (pid <= 0) {
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    if (::kill(pid, SIGKILL) == -1) {
        return Err(io::error::Error::from_raw_os_error(errno));
    }
    return Ok(rstd::empty {});
#else
    return Err(
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

auto Child::wait_with_output() -> io::Result<Output> {
    // Drop stdin so child sees EOF.
    stdin_pipe = {};

    auto read_all = [](int fd) -> ::alloc::vec::Vec<u8> {
        ::alloc::vec::Vec<u8> buf;
#if RSTD_OS_UNIX
        if (fd >= 0) {
            u8 tmp[4096];
            while (true) {
                auto n = ::read(fd, tmp, sizeof(tmp));
                if (n <= 0) break;
                for (isize i = 0; i < n; i++) {
                    u8 b = tmp[i];
                    buf.push(rstd::move(b));
                }
            }
        }
#endif
        return buf;
    };

    int out_fd = stdout_pipe.is_some() ? (*stdout_pipe).fd : -1;
    int err_fd = stderr_pipe.is_some() ? (*stderr_pipe).fd : -1;

    auto out_buf = read_all(out_fd);
    auto err_buf = read_all(err_fd);
    stdout_pipe  = {};
    stderr_pipe  = {};

    auto status = wait();
    if (status.is_err()) return Err(status.unwrap_err());

    return Ok(Output { status.unwrap(), rstd::move(out_buf), rstd::move(err_buf) });
}

} // namespace rstd::process

// ── sys::process_impl::spawn ─────────────────────────────────────────────

namespace rstd::sys::process_impl
{

auto Spawn::spawn(rstd::process::Command& cmd)
    -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error> {
#if RSTD_OS_UNIX
    using namespace rstd::process;

    auto& prog     = cmd.program_;
    auto  prog_ptr = reinterpret_cast<char const*>(prog.to_bytes_with_nul().p);

    // argv: [program, args..., nullptr]
    auto argc     = cmd.args_.len() + 2;
    auto argv_buf = ::alloc::vec::Vec<char*>::with_capacity(argc);
    argv_buf.push(const_cast<char*>(prog_ptr));
    for (usize i = 0; i < cmd.args_.len(); i++) {
        auto ptr = reinterpret_cast<char const*>(cmd.args_.at(i).to_bytes_with_nul().p);
        argv_buf.push(const_cast<char*>(ptr));
    }
    argv_buf.push(nullptr);

    // File actions
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    int stdin_pipe[2]  = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };

    auto make_pipe = [](int fds[2]) -> bool {
        return ::pipe2(fds, O_CLOEXEC) == 0;
    };

    // stdin
    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        if (! make_pipe(stdin_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], 0);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    } else if (cmd.cfg_stdin_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    }

    // stdout
    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        if (! make_pipe(stdout_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], 1);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    } else if (cmd.cfg_stdout_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }

    // stderr
    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        if (! make_pipe(stderr_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], 2);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    } else if (cmd.cfg_stderr_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }

    {
        pid_t  child_pid = -1;
        char** envp      = environ;

        int err = ::posix_spawnp(&child_pid, prog_ptr, &actions, nullptr, argv_buf.begin(), envp);
        posix_spawn_file_actions_destroy(&actions);

        if (err != 0) {
            if (stdin_pipe[0] >= 0) {
                ::close(stdin_pipe[0]);
                ::close(stdin_pipe[1]);
            }
            if (stdout_pipe[0] >= 0) {
                ::close(stdout_pipe[0]);
                ::close(stdout_pipe[1]);
            }
            if (stderr_pipe[0] >= 0) {
                ::close(stderr_pipe[0]);
                ::close(stderr_pipe[1]);
            }
            return Err(rstd::io::error::Error::from_raw_os_error(err));
        }

        // Close child-side fds in parent
        if (stdin_pipe[0] >= 0) ::close(stdin_pipe[0]);
        if (stdout_pipe[1] >= 0) ::close(stdout_pipe[1]);
        if (stderr_pipe[1] >= 0) ::close(stderr_pipe[1]);

        Child child;
        child.pid = child_pid;
        if (stdin_pipe[1] >= 0) child.stdin_pipe = Some(ChildStdin(stdin_pipe[1]));
        if (stdout_pipe[0] >= 0) child.stdout_pipe = Some(ChildStdout(stdout_pipe[0]));
        if (stderr_pipe[0] >= 0) child.stderr_pipe = Some(ChildStderr(stderr_pipe[0]));
        return Ok(rstd::move(child));
    }

fail: {
    int e = errno;
    posix_spawn_file_actions_destroy(&actions);
    if (stdin_pipe[0] >= 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
    }
    if (stdout_pipe[0] >= 0) {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
    }
    if (stderr_pipe[0] >= 0) {
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
    }
    return Err(rstd::io::error::Error::from_raw_os_error(e));
}
#else
    return Err(rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::sys::process_impl
