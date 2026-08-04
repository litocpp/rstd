module;
#include <rstd/macro.hpp>

module rstd;
import :sys.libc;
import :sys.pal;

using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

[[gnu::cold]] [[noreturn]]
void rstd::process::abort() {
    sys::pal::abort_internal();
}

[[gnu::cold]] [[noreturn]]
void rstd::process::exit(i32 code) {
    sys::pal::exit_internal(static_cast<int>(code.to_primitive()));
}

auto rstd::process::id() -> u32 {
    return sys::pal::getpid_internal();
}

// ── ExitStatus::from_raw (Unix) ──────────────────────────────────────────
#if RSTD_OS_UNIX
auto rstd::process::ExitStatus::from_raw(i32 raw) noexcept -> ExitStatus {
    auto native = static_cast<int>(raw.to_primitive());
    if (libc::wait_exited(native)) {
        return ExitStatus::from_code(i32(libc::wait_exitstatus(native)));
    }
    if (libc::wait_signaled(native)) {
        return ExitStatus::from_signal(i32(libc::wait_termsig(native)));
    }
    return ExitStatus::from_code(i32(-1));
}
#endif

// ── Pipe handle destructors ─────────────────────────────────────────────
namespace rstd::process
{

ChildStdin::~ChildStdin() {
#if RSTD_OS_UNIX
    if (fd >= 0) libc::close(fd);
#endif
}
ChildStdout::~ChildStdout() {
#if RSTD_OS_UNIX
    if (fd >= 0) libc::close(fd);
#endif
}
ChildStderr::~ChildStderr() {
#if RSTD_OS_UNIX
    if (fd >= 0) libc::close(fd);
#endif
}

// ── Child ────────────────────────────────────────────────────────────────

Child::~Child() {
}

auto Child::wait() -> io::Result<ExitStatus> {
#if RSTD_OS_UNIX
    if (status.is_some()) return Ok(status.take().unwrap());

    // Drop stdin pipe so child sees EOF.
    stdin_pipe = {};

    int status = 0;
    while (true) {
        auto ret = libc::waitpid(pid, &status, 0);
        if (ret == -1) {
            auto err = libc::get_errno();
            if (err == libc::EINTR) continue;
            return Err(io::error::Error::from_raw_os_error(i32(err)));
        }
        break;
    }
    pid = -1;
    return Ok(ExitStatus::from_raw(i32(status)));
#else
    return Err(
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

auto Child::try_wait() -> io::Result<Option<ExitStatus>> {
#if RSTD_OS_UNIX
    if (status.is_some()) return Ok(Some(*status));
    if (pid <= 0) {
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }

    int status_value = 0;
    while (true) {
        auto ret = libc::waitpid(pid, &status_value, libc::WNOHANG_);
        if (ret == 0) return Ok(None());
        if (ret == -1) {
            auto err = libc::get_errno();
            if (err == libc::EINTR) continue;
            return Err(io::error::Error::from_raw_os_error(i32(err)));
        }
        break;
    }
    pid         = -1;
    auto exited = ExitStatus::from_raw(i32(status_value));
    status      = Some(exited);
    return Ok(Some(exited));
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
    if (libc::kill(pid, libc::SIGKILL) == -1) {
        return Err(io::error::Error::from_raw_os_error(i32(libc::get_errno())));
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
            byte tmp[4096];
            while (true) {
                auto n = libc::read(fd, tmp, sizeof(tmp));
                if (n <= 0) break;
                buf.extend_from_slice(
                    slice<u8>::from_raw_parts(tmp, usize(static_cast<rstd::size_t>(n))));
            }
        }
#endif
        return buf;
    };

    int out_fd = stdout_pipe.is_some() ? (*stdout_pipe).fd : -1;
    int err_fd = stderr_pipe.is_some() ? (*stderr_pipe).fd : -1;

    auto stderr_reader = rstd::thread::spawn([err_fd, &read_all]() {
        return read_all(err_fd);
    });
    if (stderr_reader.is_err()) {
        (void)kill();
        auto out_buf = read_all(out_fd);
        auto err_buf = read_all(err_fd);
        stdout_pipe  = {};
        stderr_pipe  = {};
        (void)wait();
        return Err(stderr_reader.unwrap_err());
    }

    auto out_buf = read_all(out_fd);
    auto err_buf = rstd::move(stderr_reader).unwrap().join().unwrap();
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

#if RSTD_OS_UNIX
auto invalid_environment() -> rstd::io::error::Error {
    return rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::InvalidInput });
}

auto valid_environment_key(slice<u8> key) -> bool {
    if (key.is_empty()) return false;
    for (auto value : key) {
        if (value == u8() || value == u8('=')) return false;
    }
    return true;
}

auto valid_environment_value(slice<u8> value) -> bool {
    for (auto byte : value) {
        if (byte == u8()) return false;
    }
    return true;
}

auto environment_key_matches(const ::alloc::ffi::CString& entry, slice<u8> key) -> bool {
    auto bytes = entry.to_bytes();
    if (bytes.len() <= key.len() || bytes[key.len()] != u8('=')) return false;
    for (auto index = usize(); index < key.len(); ++index) {
        if (bytes[index] != key[index]) return false;
    }
    return true;
}

auto environment_entry(slice<u8> key, slice<u8> value)
    -> rstd::result::Result<::alloc::ffi::CString, rstd::io::error::Error> {
    auto bytes = ::alloc::vec::Vec<u8>::with_capacity(key.len() + value.len() + usize(1));
    bytes.extend_from_slice(key);
    bytes.push(u8('='));
    bytes.extend_from_slice(value);
    auto entry = ::alloc::ffi::CString::make(rstd::move(bytes));
    if (entry.is_err()) return Err(invalid_environment());
    return Ok(rstd::move(entry).unwrap());
}

auto child_environment(bool clear, const ::alloc::vec::Vec<rstd::process::EnvAction>& actions)
    -> rstd::result::Result<::alloc::vec::Vec<::alloc::ffi::CString>, rstd::io::error::Error> {
    auto result = ::alloc::vec::Vec<::alloc::ffi::CString>::make();
    if (! clear) {
        for (auto current = libc::environ; current != nullptr && *current != nullptr; ++current) {
            result.push(::alloc::ffi::CString::from_raw_parts(*current));
        }
    }

    for (const auto& action : actions) {
        if (! valid_environment_key(action.key.as_slice()) ||
            (action.value.is_some() && ! valid_environment_value(action.value->as_slice()))) {
            return Err(invalid_environment());
        }

        for (auto index = result.len(); index != usize();) {
            --index;
            if (environment_key_matches(result[index], action.key.as_slice())) {
                (void)result.remove(index);
            }
        }
        if (action.value.is_some()) {
            auto entry = environment_entry(action.key.as_slice(), action.value->as_slice());
            if (entry.is_err()) return Err(rstd::move(entry).unwrap_err());
            result.push(rstd::move(entry).unwrap());
        }
    }
    return Ok(rstd::move(result));
}
#endif

auto Spawn::spawn(rstd::process::Command& cmd)
    -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error> {
#if RSTD_OS_UNIX
    using namespace rstd::process;

    auto& prog     = cmd.program_;
    auto  prog_ptr = prog.as_ptr();

    // argv: [program, args..., nullptr]
    auto argc     = cmd.args_.len() + usize(2);
    auto argv_buf = ::alloc::vec::Vec<char*>::with_capacity(argc);
    argv_buf.push(const_cast<char*>(prog_ptr));
    for (rstd::size_t i = 0; i < cmd.args_.len().to_primitive(); ++i) {
        auto ptr = cmd.args_.at(usize(i)).as_ptr();
        argv_buf.push(const_cast<char*>(ptr));
    }
    argv_buf.push(nullptr);

    // File actions
    libc::posix_spawn_file_actions_t actions;
    libc::posix_spawn_file_actions_init(&actions);

    if (cmd.cwd_.is_some()) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
        auto error = libc::posix_spawn_file_actions_addchdir_np(&actions, cmd.cwd_->as_ptr());
        if (error != 0) {
            libc::posix_spawn_file_actions_destroy(&actions);
            return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
        }
#else
        libc::posix_spawn_file_actions_destroy(&actions);
        return Err(rstd::io::error::Error::from_kind(
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
    }

    int stdin_pipe[2]  = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };

    auto make_pipe = [](int fds[2]) -> bool {
        return libc::pipe2(fds, libc::O_CLOEXEC) == 0;
    };

    // stdin
    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        if (! make_pipe(stdin_pipe)) goto fail;
        libc::posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], 0);
        libc::posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
        libc::posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    } else if (cmd.cfg_stdin_.kind == Stdio::Null_) {
        libc::posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", libc::O_RDONLY, 0);
    }

    // stdout
    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        if (! make_pipe(stdout_pipe)) goto fail;
        libc::posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], 1);
        libc::posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
        libc::posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    } else if (cmd.cfg_stdout_.kind == Stdio::Null_) {
        libc::posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", libc::O_WRONLY, 0);
    }

    // stderr
    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        if (! make_pipe(stderr_pipe)) goto fail;
        libc::posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], 2);
        libc::posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        libc::posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    } else if (cmd.cfg_stderr_.kind == Stdio::Null_) {
        libc::posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", libc::O_WRONLY, 0);
    }

    {
        auto environment = child_environment(cmd.env_clear_, cmd.env_actions_);
        if (environment.is_err()) {
            libc::posix_spawn_file_actions_destroy(&actions);
            if (stdin_pipe[0] >= 0) {
                libc::close(stdin_pipe[0]);
                libc::close(stdin_pipe[1]);
            }
            if (stdout_pipe[0] >= 0) {
                libc::close(stdout_pipe[0]);
                libc::close(stdout_pipe[1]);
            }
            if (stderr_pipe[0] >= 0) {
                libc::close(stderr_pipe[0]);
                libc::close(stderr_pipe[1]);
            }
            return Err(rstd::move(environment).unwrap_err());
        }
        auto environment_values = rstd::move(environment).unwrap();
        auto environment_pointers =
            ::alloc::vec::Vec<char*>::with_capacity(environment_values.len() + usize(1));
        for (const auto& value : environment_values) {
            environment_pointers.push(const_cast<char*>(value.as_ptr()));
        }
        environment_pointers.push(nullptr);

        libc::pid_t child_pid = -1;

        int err = libc::posix_spawnp(&child_pid,
                                     prog_ptr,
                                     &actions,
                                     nullptr,
                                     argv_buf.begin(),
                                     environment_pointers.begin());
        libc::posix_spawn_file_actions_destroy(&actions);

        if (err != 0) {
            if (stdin_pipe[0] >= 0) {
                libc::close(stdin_pipe[0]);
                libc::close(stdin_pipe[1]);
            }
            if (stdout_pipe[0] >= 0) {
                libc::close(stdout_pipe[0]);
                libc::close(stdout_pipe[1]);
            }
            if (stderr_pipe[0] >= 0) {
                libc::close(stderr_pipe[0]);
                libc::close(stderr_pipe[1]);
            }
            return Err(rstd::io::error::Error::from_raw_os_error(i32(err)));
        }

        // Close child-side fds in parent
        if (stdin_pipe[0] >= 0) libc::close(stdin_pipe[0]);
        if (stdout_pipe[1] >= 0) libc::close(stdout_pipe[1]);
        if (stderr_pipe[1] >= 0) libc::close(stderr_pipe[1]);

        Child child;
        child.pid = child_pid;
        if (stdin_pipe[1] >= 0) child.stdin_pipe = Some(ChildStdin(stdin_pipe[1]));
        if (stdout_pipe[0] >= 0) child.stdout_pipe = Some(ChildStdout(stdout_pipe[0]));
        if (stderr_pipe[0] >= 0) child.stderr_pipe = Some(ChildStderr(stderr_pipe[0]));
        return Ok(rstd::move(child));
    }

fail: {
    int e = libc::get_errno();
    libc::posix_spawn_file_actions_destroy(&actions);
    if (stdin_pipe[0] >= 0) {
        libc::close(stdin_pipe[0]);
        libc::close(stdin_pipe[1]);
    }
    if (stdout_pipe[0] >= 0) {
        libc::close(stdout_pipe[0]);
        libc::close(stdout_pipe[1]);
    }
    if (stderr_pipe[0] >= 0) {
        libc::close(stderr_pipe[0]);
        libc::close(stderr_pipe[1]);
    }
    return Err(rstd::io::error::Error::from_raw_os_error(i32(e)));
}
#else
    return Err(rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::sys::process_impl
