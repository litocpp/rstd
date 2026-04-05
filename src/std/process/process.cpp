module;
#include <rstd/macro.hpp>

#if RSTD_OS_UNIX
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
extern char** environ;
#endif

module rstd;

using namespace rstd::prelude;

// ── ExitStatus::from_raw (Unix) ──────────────────────────────────────────
#if RSTD_OS_UNIX
auto rstd::process::ExitStatus::from_raw(i32 raw) noexcept -> ExitStatus {
    if (WIFEXITED(raw)) {
        return ExitStatus::from_code(WEXITSTATUS(raw));
    } else if (WIFSIGNALED(raw)) {
        return ExitStatus::from_signal(WTERMSIG(raw));
    }
    return ExitStatus::from_code(-1);
}
#endif

// ── Child ────────────────────────────────────────────────────────────────

void rstd::process::Child::close_fds() {
#if RSTD_OS_UNIX
    if (stdin_fd  >= 0) { ::close(stdin_fd);  stdin_fd  = -1; }
    if (stdout_fd >= 0) { ::close(stdout_fd); stdout_fd = -1; }
    if (stderr_fd >= 0) { ::close(stderr_fd); stderr_fd = -1; }
#endif
}

rstd::process::Child::~Child() {
    close_fds();
    // If the child is still alive, we don't reap it here (becomes zombie).
    // Users should call wait() or kill()+wait().
}

auto rstd::process::Child::wait() -> io::Result<process::ExitStatus> {
#if RSTD_OS_UNIX
    // Close our end of stdin pipe so the child sees EOF.
    if (stdin_fd >= 0) { ::close(stdin_fd); stdin_fd = -1; }

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
    close_fds();
    return Ok(process::ExitStatus::from_raw(status));
#else
    return Err(io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

auto rstd::process::Child::kill() -> io::Result<rstd::empty> {
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
    return Err(io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

// ── Command::output ──────────────────────────────────────────────────────

auto rstd::process::Command::output() -> io::Result<process::Output> {
    // Force piped stdout/stderr to capture output
    cfg_stdout_ = Stdio::piped();
    cfg_stderr_ = Stdio::piped();

    auto child_res = spawn();
    if (child_res.is_err()) return Err(child_res.unwrap_err());
    auto child = child_res.unwrap();

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
            ::close(fd);
        }
#endif
        return buf;
    };

    auto out_buf = read_all(child.stdout_fd);
    child.stdout_fd = -1;
    auto err_buf = read_all(child.stderr_fd);
    child.stderr_fd = -1;

    auto status = child.wait();
    if (status.is_err()) return Err(status.unwrap_err());

    return Ok(process::Output {
        status.unwrap(),
        rstd::move(out_buf),
        rstd::move(err_buf)
    });
}

// ── sys::process_impl::spawn (Unix posix_spawn) ─────────────────────────

namespace rstd::sys::process_impl
{

auto spawn(rstd::process::Command& cmd)
    -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error>
{
#if RSTD_OS_UNIX
    using namespace rstd::process;

    // Build argv: [program, args..., nullptr]
    auto& prog = cmd.program_;
    auto prog_ptr = reinterpret_cast<char const*>(prog.to_bytes_with_nul().p);

    // argv array
    auto argc = cmd.args_.len() + 2; // program + args + nullptr
    auto argv_buf = ::alloc::vec::Vec<char*>::with_capacity(argc);
    argv_buf.push(const_cast<char*>(prog_ptr));
    for (usize i = 0; i < cmd.args_.len(); i++) {
        auto ptr = reinterpret_cast<char const*>(cmd.args_.at(i).to_bytes_with_nul().p);
        argv_buf.push(const_cast<char*>(ptr));
    }
    argv_buf.push(nullptr);

    // File actions for stdio redirection
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    int stdin_pipe[2]  = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };

    auto make_pipe = [](int fds[2]) -> bool {
        return ::pipe2(fds, O_CLOEXEC) == 0;
    };

    // Setup stdin
    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        if (! make_pipe(stdin_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], 0);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    } else if (cmd.cfg_stdin_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    }

    // Setup stdout
    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        if (! make_pipe(stdout_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], 1);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    } else if (cmd.cfg_stdout_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }

    // Setup stderr
    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        if (! make_pipe(stderr_pipe)) goto fail;
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], 2);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    } else if (cmd.cfg_stderr_.kind == Stdio::Null_) {
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }

    // cwd via posix_spawn_file_actions_addchdir_np (glibc 2.29+)
    // For portability, skip cwd for now (TODO: fork+exec fallback for cwd)

    {
        pid_t pid = -1;
        char** envp = environ;
        // TODO: handle env_actions_ and env_clear_

        int err = ::posix_spawnp(&pid, prog_ptr, &actions, nullptr,
                                 argv_buf.begin(), envp);
        posix_spawn_file_actions_destroy(&actions);

        if (err != 0) {
            // Close all pipe fds on error
            if (stdin_pipe[0]  >= 0) { ::close(stdin_pipe[0]); ::close(stdin_pipe[1]); }
            if (stdout_pipe[0] >= 0) { ::close(stdout_pipe[0]); ::close(stdout_pipe[1]); }
            if (stderr_pipe[0] >= 0) { ::close(stderr_pipe[0]); ::close(stderr_pipe[1]); }
            return Err(rstd::io::error::Error::from_raw_os_error(err));
        }

        // Close child-side pipe ends in parent
        if (stdin_pipe[0]  >= 0) ::close(stdin_pipe[0]);
        if (stdout_pipe[1] >= 0) ::close(stdout_pipe[1]);
        if (stderr_pipe[1] >= 0) ::close(stderr_pipe[1]);

        Child child;
        child.pid       = pid;
        child.stdin_fd  = stdin_pipe[1];  // parent writes to child stdin
        child.stdout_fd = stdout_pipe[0]; // parent reads child stdout
        child.stderr_fd = stderr_pipe[0]; // parent reads child stderr
        return Ok(rstd::move(child));
    }

fail:
    {
        int e = errno;
        posix_spawn_file_actions_destroy(&actions);
        if (stdin_pipe[0]  >= 0) { ::close(stdin_pipe[0]); ::close(stdin_pipe[1]); }
        if (stdout_pipe[0] >= 0) { ::close(stdout_pipe[0]); ::close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { ::close(stderr_pipe[0]); ::close(stderr_pipe[1]); }
        return Err(rstd::io::error::Error::from_raw_os_error(e));
    }
#else
    return Err(rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::sys::process_impl
