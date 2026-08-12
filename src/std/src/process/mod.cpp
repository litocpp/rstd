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
    return wait_with_output(OutputObserver {});
}

auto Child::wait_with_output(OutputObserver observer) -> io::Result<Output> {
    // Drop stdin so child sees EOF.
    stdin_pipe = {};

    auto read_all = [observer](int fd, OutputStream stream) -> ::alloc::vec::Vec<u8> {
        ::alloc::vec::Vec<u8> buf;
#if RSTD_OS_UNIX
        if (fd >= 0) {
            byte tmp[4096];
            while (true) {
                auto n = libc::read(fd, tmp, sizeof(tmp));
                if (n <= 0) break;
                auto chunk = slice<u8>::from_raw_parts(tmp, usize(static_cast<rstd::size_t>(n)));
                if (observer.notify != nullptr) observer.notify(observer.context, stream, chunk);
                buf.extend_from_slice(chunk);
            }
        }
#endif
        return buf;
    };

    int out_fd = stdout_pipe.is_some() ? (*stdout_pipe).fd : -1;
    int err_fd = stderr_pipe.is_some() ? (*stderr_pipe).fd : -1;

    auto stderr_reader = rstd::thread::spawn([err_fd, &read_all]() {
        return read_all(err_fd, OutputStream::Stderr);
    });
    if (stderr_reader.is_err()) {
        (void)kill();
        auto out_buf = read_all(out_fd, OutputStream::Stdout);
        auto err_buf = read_all(err_fd, OutputStream::Stderr);
        stdout_pipe  = {};
        stderr_pipe  = {};
        (void)wait();
        return Err(stderr_reader.unwrap_err());
    }

    auto out_buf = read_all(out_fd, OutputStream::Stdout);
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

struct SpawnContext {
    const char*          program {};
    char* const*         arguments {};
    char**               environment {};
    const char*          directory {};
    rstd::process::Stdio stdin_config {};
    rstd::process::Stdio stdout_config {};
    rstd::process::Stdio stderr_config {};
    int*                 stdin_pipe {};
    int*                 stdout_pipe {};
    int*                 stderr_pipe {};
    bool                 requires_fork {};
};

struct ChildSpawnError {
    int          error {};
    unsigned int footer {};
};

inline constexpr unsigned int CHILD_SPAWN_ERROR_FOOTER = 0x4e4f4558U;
static_assert(sizeof(ChildSpawnError) == 8);

auto close_pipe(int pipe[2]) -> void {
    if (pipe[0] >= 0) libc::close(pipe[0]);
    if (pipe[1] >= 0) libc::close(pipe[1]);
    pipe[0] = -1;
    pipe[1] = -1;
}

auto close_spawn_pipes(const SpawnContext& context) -> void {
    close_pipe(context.stdin_pipe);
    close_pipe(context.stdout_pipe);
    close_pipe(context.stderr_pipe);
}

auto add_pipe_actions(libc::posix_spawn_file_actions_t* actions, int source, int unused, int target)
    -> int {
    if (source != target) {
        auto error = libc::posix_spawn_file_actions_adddup2(actions, source, target);
        if (error != 0) return error;
        error = libc::posix_spawn_file_actions_addclose(actions, source);
        if (error != 0) return error;
    }
    if (unused != source && unused != target) {
        return libc::posix_spawn_file_actions_addclose(actions, unused);
    }
    return 0;
}

auto add_null_action(libc::posix_spawn_file_actions_t* actions, int target, int flags) -> int {
    return libc::posix_spawn_file_actions_addopen(actions, target, "/dev/null", flags, 0);
}

auto try_posix_spawn(const SpawnContext& context)
    -> rstd::result::Result<Option<libc::pid_t>, rstd::io::error::Error> {
    if (context.requires_fork || ! libc::posix_spawn_reports_exec_error()) return Ok(None());

    auto addchdir = libc::PosixSpawnAddChdir {};
    if (context.directory != nullptr) {
        addchdir = libc::get_posix_spawn_addchdir();
        if (addchdir == nullptr) return Ok(None());
    }

    libc::posix_spawn_file_actions_t actions;
    auto                             error = libc::posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
    }

    if (context.stdin_config.kind == rstd::process::Stdio::Piped_) {
        error = add_pipe_actions(&actions, context.stdin_pipe[0], context.stdin_pipe[1], 0);
    } else if (context.stdin_config.kind == rstd::process::Stdio::Null_) {
        error = add_null_action(&actions, 0, libc::O_RDONLY);
    }
    if (error == 0 && context.stdout_config.kind == rstd::process::Stdio::Piped_) {
        error = add_pipe_actions(&actions, context.stdout_pipe[1], context.stdout_pipe[0], 1);
    } else if (error == 0 && context.stdout_config.kind == rstd::process::Stdio::Null_) {
        error = add_null_action(&actions, 1, libc::O_WRONLY);
    }
    if (error == 0 && context.stderr_config.kind == rstd::process::Stdio::Piped_) {
        error = add_pipe_actions(&actions, context.stderr_pipe[1], context.stderr_pipe[0], 2);
    } else if (error == 0 && context.stderr_config.kind == rstd::process::Stdio::Null_) {
        error = add_null_action(&actions, 2, libc::O_WRONLY);
    }
    if (error == 0 && addchdir != nullptr) error = addchdir(&actions, context.directory);

    auto child_pid = libc::pid_t(-1);
    if (error == 0) {
        error = libc::posix_spawnp(
            &child_pid, context.program, &actions, nullptr, context.arguments, context.environment);
    }
    libc::posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
    }
    return Ok(Some(child_pid));
}

[[noreturn]]
auto child_spawn_failure(int error_pipe) -> void {
    auto message = ChildSpawnError {
        .error  = libc::get_errno(),
        .footer = CHILD_SPAWN_ERROR_FOOTER,
    };
    while (libc::write(error_pipe, &message, sizeof(message)) == -1 &&
           libc::get_errno() == libc::EINTR) {
    }
    libc::_exit(127);
}

auto redirect_child_pipe(int source, int unused, int target, int error_pipe) -> void {
    if (source != target && libc::dup2(source, target) == -1) child_spawn_failure(error_pipe);
    if (source != target) libc::close(source);
    if (unused != source && unused != target) libc::close(unused);
}

auto redirect_child_null(int target, int flags, int error_pipe) -> void {
    auto descriptor = libc::open("/dev/null", flags);
    if (descriptor == -1) child_spawn_failure(error_pipe);
    if (descriptor != target && libc::dup2(descriptor, target) == -1) {
        child_spawn_failure(error_pipe);
    }
    if (descriptor != target) libc::close(descriptor);
}

auto wait_after_spawn_failure(libc::pid_t child_pid) -> void {
    int status {};
    while (libc::waitpid(child_pid, &status, 0) == -1 && libc::get_errno() == libc::EINTR) {
    }
}

auto fork_exec(const SpawnContext& context)
    -> rstd::result::Result<libc::pid_t, rstd::io::error::Error> {
    int error_pipe[2] = { -1, -1 };
    if (libc::pipe2(error_pipe, libc::O_CLOEXEC) == -1) {
        return Err(rstd::io::error::Error::from_raw_os_error(i32(libc::get_errno())));
    }

    auto child_pid = libc::fork();
    if (child_pid == -1) {
        auto error = libc::get_errno();
        libc::close(error_pipe[0]);
        libc::close(error_pipe[1]);
        return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
    }

    if (child_pid == 0) {
        libc::close(error_pipe[0]);
        if (context.directory != nullptr && libc::chdir(context.directory) == -1) {
            child_spawn_failure(error_pipe[1]);
        }

        if (context.stdin_config.kind == rstd::process::Stdio::Piped_) {
            redirect_child_pipe(context.stdin_pipe[0], context.stdin_pipe[1], 0, error_pipe[1]);
        } else if (context.stdin_config.kind == rstd::process::Stdio::Null_) {
            redirect_child_null(0, libc::O_RDONLY, error_pipe[1]);
        }
        if (context.stdout_config.kind == rstd::process::Stdio::Piped_) {
            redirect_child_pipe(context.stdout_pipe[1], context.stdout_pipe[0], 1, error_pipe[1]);
        } else if (context.stdout_config.kind == rstd::process::Stdio::Null_) {
            redirect_child_null(1, libc::O_WRONLY, error_pipe[1]);
        }
        if (context.stderr_config.kind == rstd::process::Stdio::Piped_) {
            redirect_child_pipe(context.stderr_pipe[1], context.stderr_pipe[0], 2, error_pipe[1]);
        } else if (context.stderr_config.kind == rstd::process::Stdio::Null_) {
            redirect_child_null(2, libc::O_WRONLY, error_pipe[1]);
        }

        libc::environ = context.environment;
        libc::execvp(context.program, context.arguments);
        child_spawn_failure(error_pipe[1]);
    }

    libc::close(error_pipe[1]);
    auto message  = ChildSpawnError {};
    auto received = decltype(libc::read(error_pipe[0], &message, sizeof(message))) {};
    do {
        received = libc::read(error_pipe[0], &message, sizeof(message));
    } while (received == -1 && libc::get_errno() == libc::EINTR);
    libc::close(error_pipe[0]);

    if (received == 0) return Ok(child_pid);
    if (received == -1) {
        auto error = libc::get_errno();
        libc::kill(child_pid, libc::SIGKILL);
        wait_after_spawn_failure(child_pid);
        return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
    }
    wait_after_spawn_failure(child_pid);
    if (received != static_cast<decltype(received)>(sizeof(message)) ||
        message.footer != CHILD_SPAWN_ERROR_FOOTER) {
        return Err(rstd::io::error::Error::from_raw_os_error(i32(libc::EIO)));
    }
    return Err(rstd::io::error::Error::from_raw_os_error(i32(message.error)));
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

    int stdin_pipe[2]  = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };

    auto make_pipe = [](int fds[2]) -> bool {
        return libc::pipe2(fds, libc::O_CLOEXEC) == 0;
    };

    // stdin
    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        if (! make_pipe(stdin_pipe)) {
            auto error = libc::get_errno();
            close_pipe(stdin_pipe);
            close_pipe(stdout_pipe);
            close_pipe(stderr_pipe);
            return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
        }
    }

    // stdout
    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        if (! make_pipe(stdout_pipe)) {
            auto error = libc::get_errno();
            close_pipe(stdin_pipe);
            close_pipe(stdout_pipe);
            close_pipe(stderr_pipe);
            return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
        }
    }

    // stderr
    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        if (! make_pipe(stderr_pipe)) {
            auto error = libc::get_errno();
            close_pipe(stdin_pipe);
            close_pipe(stdout_pipe);
            close_pipe(stderr_pipe);
            return Err(rstd::io::error::Error::from_raw_os_error(i32(error)));
        }
    }

    auto environment = child_environment(cmd.env_clear_, cmd.env_actions_);
    if (environment.is_err()) {
        auto context = SpawnContext {
            .stdin_pipe  = stdin_pipe,
            .stdout_pipe = stdout_pipe,
            .stderr_pipe = stderr_pipe,
        };
        close_spawn_pipes(context);
        return Err(rstd::move(environment).unwrap_err());
    }
    auto environment_values = rstd::move(environment).unwrap();
    auto environment_pointers =
        ::alloc::vec::Vec<char*>::with_capacity(environment_values.len() + usize(1));
    for (const auto& value : environment_values) {
        environment_pointers.push(const_cast<char*>(value.as_ptr()));
    }
    environment_pointers.push(nullptr);

    auto path_changed = cmd.env_clear_;
    for (const auto& action : cmd.env_actions_) {
        if (action.key.len() == usize(4) && action.key[usize {}] == u8('P') &&
            action.key[usize(1)] == u8('A') && action.key[usize(2)] == u8('T') &&
            action.key[usize(3)] == u8('H')) {
            path_changed = true;
            break;
        }
    }
    auto program_is_path = false;
    for (const auto byte : prog.to_bytes()) {
        if (byte == u8('/')) {
            program_is_path = true;
            break;
        }
    }

    auto context = SpawnContext {
        .program       = prog_ptr,
        .arguments     = argv_buf.begin(),
        .environment   = environment_pointers.begin(),
        .directory     = cmd.cwd_.is_some() ? cmd.cwd_->as_ptr() : nullptr,
        .stdin_config  = cmd.cfg_stdin_,
        .stdout_config = cmd.cfg_stdout_,
        .stderr_config = cmd.cfg_stderr_,
        .stdin_pipe    = stdin_pipe,
        .stdout_pipe   = stdout_pipe,
        .stderr_pipe   = stderr_pipe,
        .requires_fork = path_changed && ! program_is_path,
    };

    auto spawned = try_posix_spawn(context);
    if (spawned.is_err()) {
        close_spawn_pipes(context);
        return Err(rstd::move(spawned).unwrap_err());
    }
    auto child_pid = libc::pid_t(-1);
    if (spawned->is_some()) {
        child_pid = spawned->take().unwrap();
    } else {
        auto forked = fork_exec(context);
        if (forked.is_err()) {
            close_spawn_pipes(context);
            return Err(rstd::move(forked).unwrap_err());
        }
        child_pid = forked.unwrap();
    }

    if (stdin_pipe[0] >= 0) libc::close(stdin_pipe[0]);
    if (stdout_pipe[1] >= 0) libc::close(stdout_pipe[1]);
    if (stderr_pipe[1] >= 0) libc::close(stderr_pipe[1]);

    Child child;
    child.pid = child_pid;
    if (stdin_pipe[1] >= 0) child.stdin_pipe = Some(ChildStdin(stdin_pipe[1]));
    if (stdout_pipe[0] >= 0) child.stdout_pipe = Some(ChildStdout(stdout_pipe[0]));
    if (stderr_pipe[0] >= 0) child.stderr_pipe = Some(ChildStderr(stderr_pipe[0]));
    return Ok(rstd::move(child));
#else
    return Err(rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::sys::process_impl
