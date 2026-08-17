module;
#include <rstd/macro.hpp>

module rstd;
import :sys.libc;
import :sys.io.stdio;
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
#elif RSTD_OS_WINDOWS
    if (fd >= 0) libc::_close(fd);
#endif
}
ChildStdout::~ChildStdout() {
#if RSTD_OS_UNIX
    if (fd >= 0) libc::close(fd);
#elif RSTD_OS_WINDOWS
    if (fd >= 0) libc::_close(fd);
#endif
}
ChildStderr::~ChildStderr() {
#if RSTD_OS_UNIX
    if (fd >= 0) libc::close(fd);
#elif RSTD_OS_WINDOWS
    if (fd >= 0) libc::_close(fd);
#endif
}

// ── Child ────────────────────────────────────────────────────────────────

Child::~Child() {
#if RSTD_OS_WINDOWS
    if (process_handle != nullptr) {
        (void)libc::CloseHandle(static_cast<libc::HANDLE>(process_handle));
    }
#endif
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
#elif RSTD_OS_WINDOWS
    if (status.is_some()) return Ok(*status);
    if (process_handle == nullptr) {
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    stdin_pipe  = {};
    auto handle = static_cast<libc::HANDLE>(process_handle);
    if (libc::WaitForSingleObject(handle, libc::M_INFINITE) != libc::M_WAIT_OBJECT_0) {
        return Err(io::error::Error::from_raw_os_error(i32(libc::GetLastError())));
    }
    auto code = libc::DWORD {};
    if (! libc::GetExitCodeProcess(handle, &code)) {
        return Err(io::error::Error::from_raw_os_error(i32(libc::GetLastError())));
    }
    (void)libc::CloseHandle(handle);
    process_handle = nullptr;
    pid            = -1;
    auto exited    = ExitStatus::from_code(i32(static_cast<rstd::int32_t>(code)));
    status         = Some(exited);
    return Ok(exited);
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
#elif RSTD_OS_WINDOWS
    if (status.is_some()) return Ok(Some(*status));
    if (process_handle == nullptr) {
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    auto handle = static_cast<libc::HANDLE>(process_handle);
    auto code   = libc::DWORD {};
    if (! libc::GetExitCodeProcess(handle, &code)) {
        return Err(io::error::Error::from_raw_os_error(i32(libc::GetLastError())));
    }
    if (code == libc::M_STILL_ACTIVE) return Ok(None());
    (void)libc::CloseHandle(handle);
    process_handle = nullptr;
    pid            = -1;
    auto exited    = ExitStatus::from_code(i32(static_cast<rstd::int32_t>(code)));
    status         = Some(exited);
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
#elif RSTD_OS_WINDOWS
    if (process_handle == nullptr) {
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    if (! libc::TerminateProcess(static_cast<libc::HANDLE>(process_handle), 1)) {
        return Err(io::error::Error::from_raw_os_error(i32(libc::GetLastError())));
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
#elif RSTD_OS_WINDOWS
        if (fd >= 0) {
            byte tmp[4096];
            while (true) {
                auto chunk_buffer = mut_ref<u8[]>::from_raw_parts(
                    tmp, usize(static_cast<rstd::size_t>(sizeof(tmp))));
                auto read = rstd::sys::io::stdio::read_fd(fd, as_bytes_mut(chunk_buffer));
                if (read.is_err() || *read == usize()) break;
                auto chunk = slice<u8>::from_raw_parts(tmp, *read);
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

#if RSTD_OS_WINDOWS
auto windows_process_error() -> rstd::io::error::Error {
    return rstd::io::error::Error::from_raw_os_error(i32(libc::GetLastError()));
}

auto invalid_process_input() -> rstd::io::error::Error {
    return rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::InvalidInput });
}

auto wide_string(slice<u8> value)
    -> rstd::result::Result<::alloc::vec::Vec<wchar_t>, rstd::io::error::Error> {
    if (value.len().to_primitive() > 0x7fffffff) return Err(invalid_process_input());
    for (auto byte : value) {
        if (byte == u8()) return Err(invalid_process_input());
    }
    if (value.is_empty()) return Ok(::alloc::vec::Vec<wchar_t>::make());

    auto input    = reinterpret_cast<const char*>(value.as_raw_ptr());
    auto count    = static_cast<int>(value.len().to_primitive());
    auto required = libc::MultiByteToWideChar(
        libc::M_CP_UTF8, libc::M_MB_ERR_INVALID_CHARS, input, count, nullptr, 0);
    if (required <= 0) return Err(windows_process_error());
    auto result =
        ::alloc::vec::Vec<wchar_t>::with_capacity(usize(static_cast<rstd::size_t>(required)));
    result.resize(usize(static_cast<rstd::size_t>(required)), wchar_t {});
    if (libc::MultiByteToWideChar(libc::M_CP_UTF8,
                                  libc::M_MB_ERR_INVALID_CHARS,
                                  input,
                                  count,
                                  result.as_mut_ptr(),
                                  required) != required) {
        return Err(windows_process_error());
    }
    return Ok(rstd::move(result));
}

void append_wide_argument(::alloc::vec::Vec<wchar_t>& command, slice<wchar_t> argument) {
    auto quote = argument.is_empty();
    for (auto value : argument) {
        if (value == L' ' || value == L'\t' || value == L'"') {
            quote = true;
            break;
        }
    }
    if (! quote) {
        command.extend_from_slice(argument);
        return;
    }

    command.push(L'"');
    auto slashes = rstd::size_t {};
    for (auto value : argument) {
        if (value == L'\\') {
            ++slashes;
            continue;
        }
        if (value == L'"') {
            for (rstd::size_t index = 0; index < slashes * 2 + 1; ++index) command.push(L'\\');
            command.push(L'"');
        } else {
            for (rstd::size_t index = 0; index < slashes; ++index) command.push(L'\\');
            command.push(static_cast<wchar_t>(value));
        }
        slashes = 0;
    }
    for (rstd::size_t index = 0; index < slashes * 2; ++index) command.push(L'\\');
    command.push(L'"');
}

auto command_line(const ::alloc::ffi::CString&                    program,
                  const ::alloc::vec::Vec<::alloc::ffi::CString>& arguments)
    -> rstd::result::Result<::alloc::vec::Vec<wchar_t>, rstd::io::error::Error> {
    auto program_wide = wide_string(program.to_bytes());
    if (program_wide.is_err()) return Err(rstd::move(program_wide).unwrap_err());
    auto result = ::alloc::vec::Vec<wchar_t>::make();
    append_wide_argument(result, program_wide->as_slice());
    for (const auto& argument : arguments) {
        auto converted = wide_string(argument.to_bytes());
        if (converted.is_err()) return Err(rstd::move(converted).unwrap_err());
        result.push(L' ');
        append_wide_argument(result, converted->as_slice());
    }
    result.push(L'\0');
    return Ok(rstd::move(result));
}

struct WideEnvironmentEntry {
    ::alloc::vec::Vec<wchar_t> value;
    usize                      key_length {};
};

auto environment_key_length(slice<wchar_t> entry) -> usize {
    auto start = entry.is_empty() || entry[usize()] != L'=' ? usize() : usize(1);
    for (auto index = start; index < entry.len(); ++index) {
        if (entry[index] == L'=') return index;
    }
    return entry.len();
}

auto same_environment_key(const WideEnvironmentEntry& entry, slice<wchar_t> key) -> bool {
    if (entry.key_length != key.len()) return false;
    return libc::CompareStringOrdinal(entry.value.as_ptr(),
                                      static_cast<int>(entry.key_length.to_primitive()),
                                      key.as_raw_ptr(),
                                      static_cast<int>(key.len().to_primitive()),
                                      libc::M_TRUE) == libc::M_CSTR_EQUAL;
}

auto child_environment(bool clear, const ::alloc::vec::Vec<rstd::process::EnvAction>& actions)
    -> rstd::result::Result<::alloc::vec::Vec<wchar_t>, rstd::io::error::Error> {
    auto entries = ::alloc::vec::Vec<WideEnvironmentEntry>::make();
    if (! clear) {
        auto block = libc::GetEnvironmentStringsW();
        if (block == nullptr) return Err(windows_process_error());
        auto current = block;
        while (*current != L'\0') {
            auto length = rstd::size_t {};
            while (current[length] != L'\0') ++length;
            auto value = ::alloc::vec::Vec<wchar_t>::from(
                slice<wchar_t>::from_raw_parts(current, usize(length)));
            auto key_length = environment_key_length(value.as_slice());
            entries.push(WideEnvironmentEntry { rstd::move(value), key_length });
            current += length + 1;
        }
        (void)libc::FreeEnvironmentStringsW(block);
    }

    for (const auto& action : actions) {
        if (action.key.is_empty()) return Err(invalid_process_input());
        for (auto byte : action.key) {
            if (byte == u8() || byte == u8('=')) return Err(invalid_process_input());
        }
        auto key = wide_string(action.key.as_slice());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        for (auto index = entries.len(); index != usize();) {
            --index;
            if (same_environment_key(entries[index], key->as_slice())) (void)entries.remove(index);
        }
        if (action.value.is_none()) continue;
        auto value = wide_string(action.value->as_slice());
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        auto entry =
            ::alloc::vec::Vec<wchar_t>::with_capacity(key->len() + value->len() + usize(1));
        entry.extend_from_slice(key->as_slice());
        entry.push(L'=');
        entry.extend_from_slice(value->as_slice());
        entries.push(WideEnvironmentEntry { rstd::move(entry), key->len() });
    }

    auto result = ::alloc::vec::Vec<wchar_t>::make();
    for (const auto& entry : entries) {
        result.extend_from_slice(entry.value.as_slice());
        result.push(L'\0');
    }
    result.push(L'\0');
    if (entries.is_empty()) result.push(L'\0');
    return Ok(rstd::move(result));
}

struct WindowsPipe {
    libc::HANDLE child { libc::M_INVALID_HANDLE_VALUE };
    int          parent { -1 };
};

void close_windows_pipe(WindowsPipe& pipe) {
    if (pipe.child != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(pipe.child);
    if (pipe.parent >= 0) libc::_close(pipe.parent);
    pipe.child  = libc::M_INVALID_HANDLE_VALUE;
    pipe.parent = -1;
}

auto windows_pipe(bool child_reads, libc::SECURITY_ATTRIBUTES& security)
    -> rstd::result::Result<WindowsPipe, rstd::io::error::Error> {
    auto read_handle  = libc::M_INVALID_HANDLE_VALUE;
    auto write_handle = libc::M_INVALID_HANDLE_VALUE;
    if (! libc::CreatePipe(&read_handle, &write_handle, &security, 0)) {
        return Err(windows_process_error());
    }
    auto child  = child_reads ? read_handle : write_handle;
    auto parent = child_reads ? write_handle : read_handle;
    if (! libc::SetHandleInformation(parent, libc::M_HANDLE_FLAG_INHERIT, 0)) {
        auto error = windows_process_error();
        (void)libc::CloseHandle(read_handle);
        (void)libc::CloseHandle(write_handle);
        return Err(rstd::move(error));
    }
    auto flags = (child_reads ? libc::M_O_WRONLY : libc::M_O_RDONLY) | libc::M_O_BINARY;
    auto fd    = libc::_open_osfhandle(reinterpret_cast<rstd::intptr_t>(parent), flags);
    if (fd < 0) {
        auto error = invalid_process_input();
        (void)libc::CloseHandle(parent);
        (void)libc::CloseHandle(child);
        return Err(rstd::move(error));
    }
    return Ok(WindowsPipe { child, fd });
}

auto null_handle(bool input, libc::SECURITY_ATTRIBUTES& security)
    -> rstd::result::Result<libc::HANDLE, rstd::io::error::Error> {
    auto handle = libc::CreateFileW(L"NUL",
                                    input ? libc::M_GENERIC_READ : libc::M_GENERIC_WRITE,
                                    libc::M_FILE_SHARE_READ | libc::M_FILE_SHARE_WRITE,
                                    &security,
                                    libc::M_OPEN_EXISTING,
                                    libc::M_FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (handle == libc::M_INVALID_HANDLE_VALUE) return Err(windows_process_error());
    return Ok(handle);
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
#elif RSTD_OS_WINDOWS
    using namespace rstd::process;

    auto security                 = libc::SECURITY_ATTRIBUTES {};
    security.nLength              = sizeof(security);
    security.bInheritHandle       = libc::M_TRUE;
    security.lpSecurityDescriptor = nullptr;

    auto stdin_pipe  = WindowsPipe {};
    auto stdout_pipe = WindowsPipe {};
    auto stderr_pipe = WindowsPipe {};
    auto null_stdin  = libc::M_INVALID_HANDLE_VALUE;
    auto null_stdout = libc::M_INVALID_HANDLE_VALUE;
    auto null_stderr = libc::M_INVALID_HANDLE_VALUE;

    auto cleanup = [&] {
        close_windows_pipe(stdin_pipe);
        close_windows_pipe(stdout_pipe);
        close_windows_pipe(stderr_pipe);
        if (null_stdin != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stdin);
        if (null_stdout != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stdout);
        if (null_stderr != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stderr);
        null_stdin  = libc::M_INVALID_HANDLE_VALUE;
        null_stdout = libc::M_INVALID_HANDLE_VALUE;
        null_stderr = libc::M_INVALID_HANDLE_VALUE;
    };

    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        auto created = windows_pipe(true, security);
        if (created.is_err()) return Err(rstd::move(created).unwrap_err());
        stdin_pipe = rstd::move(created).unwrap();
    }
    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        auto created = windows_pipe(false, security);
        if (created.is_err()) {
            auto error = rstd::move(created).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        stdout_pipe = rstd::move(created).unwrap();
    }
    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        auto created = windows_pipe(false, security);
        if (created.is_err()) {
            auto error = rstd::move(created).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        stderr_pipe = rstd::move(created).unwrap();
    }

    auto startup    = libc::STARTUPINFOW {};
    startup.cb      = sizeof(startup);
    startup.dwFlags = libc::M_STARTF_USESTDHANDLES;

    if (cmd.cfg_stdin_.kind == Stdio::Piped_) {
        startup.hStdInput = stdin_pipe.child;
    } else if (cmd.cfg_stdin_.kind == Stdio::Null_) {
        auto opened = null_handle(true, security);
        if (opened.is_err()) {
            auto error = rstd::move(opened).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        null_stdin        = opened.unwrap();
        startup.hStdInput = null_stdin;
    } else {
        startup.hStdInput = libc::GetStdHandle(libc::M_STD_INPUT_HANDLE);
    }

    if (cmd.cfg_stdout_.kind == Stdio::Piped_) {
        startup.hStdOutput = stdout_pipe.child;
    } else if (cmd.cfg_stdout_.kind == Stdio::Null_) {
        auto opened = null_handle(false, security);
        if (opened.is_err()) {
            auto error = rstd::move(opened).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        null_stdout        = opened.unwrap();
        startup.hStdOutput = null_stdout;
    } else {
        startup.hStdOutput = libc::GetStdHandle(libc::M_STD_OUTPUT_HANDLE);
    }

    if (cmd.cfg_stderr_.kind == Stdio::Piped_) {
        startup.hStdError = stderr_pipe.child;
    } else if (cmd.cfg_stderr_.kind == Stdio::Null_) {
        auto opened = null_handle(false, security);
        if (opened.is_err()) {
            auto error = rstd::move(opened).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        null_stderr       = opened.unwrap();
        startup.hStdError = null_stderr;
    } else {
        startup.hStdError = libc::GetStdHandle(libc::M_STD_ERROR_HANDLE);
    }

    auto line = command_line(cmd.program_, cmd.args_);
    if (line.is_err()) {
        auto error = rstd::move(line).unwrap_err();
        cleanup();
        return Err(rstd::move(error));
    }
    auto environment = Option<::alloc::vec::Vec<wchar_t>> {};
    if (cmd.env_clear_ || ! cmd.env_actions_.is_empty()) {
        auto built = child_environment(cmd.env_clear_, cmd.env_actions_);
        if (built.is_err()) {
            auto error = rstd::move(built).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        environment = Some(rstd::move(built).unwrap());
    }

    auto directory = Option<::alloc::vec::Vec<wchar_t>> {};
    if (cmd.cwd_.is_some()) {
        auto converted = wide_string(cmd.cwd_->to_bytes());
        if (converted.is_err()) {
            auto error = rstd::move(converted).unwrap_err();
            cleanup();
            return Err(rstd::move(error));
        }
        converted->push(L'\0');
        directory = Some(rstd::move(converted).unwrap());
    }

    auto process_info        = libc::PROCESS_INFORMATION {};
    auto environment_pointer = environment.is_some()
                                   ? static_cast<void*>(environment->as_mut_ptr().as_raw_ptr())
                                   : nullptr;
    auto directory_pointer   = directory.is_some() ? directory->as_ptr().as_raw_ptr() : nullptr;
    if (! libc::CreateProcessW(nullptr,
                               line->as_mut_ptr(),
                               nullptr,
                               nullptr,
                               libc::M_TRUE,
                               libc::M_CREATE_UNICODE_ENVIRONMENT,
                               environment_pointer,
                               directory_pointer,
                               &startup,
                               &process_info)) {
        auto error = windows_process_error();
        cleanup();
        return Err(rstd::move(error));
    }

    if (stdin_pipe.child != libc::M_INVALID_HANDLE_VALUE) {
        (void)libc::CloseHandle(stdin_pipe.child);
        stdin_pipe.child = libc::M_INVALID_HANDLE_VALUE;
    }
    if (stdout_pipe.child != libc::M_INVALID_HANDLE_VALUE) {
        (void)libc::CloseHandle(stdout_pipe.child);
        stdout_pipe.child = libc::M_INVALID_HANDLE_VALUE;
    }
    if (stderr_pipe.child != libc::M_INVALID_HANDLE_VALUE) {
        (void)libc::CloseHandle(stderr_pipe.child);
        stderr_pipe.child = libc::M_INVALID_HANDLE_VALUE;
    }
    if (null_stdin != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stdin);
    if (null_stdout != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stdout);
    if (null_stderr != libc::M_INVALID_HANDLE_VALUE) (void)libc::CloseHandle(null_stderr);
    null_stdin  = libc::M_INVALID_HANDLE_VALUE;
    null_stdout = libc::M_INVALID_HANDLE_VALUE;
    null_stderr = libc::M_INVALID_HANDLE_VALUE;
    (void)libc::CloseHandle(process_info.hThread);

    auto child           = Child {};
    child.pid            = static_cast<int>(process_info.dwProcessId);
    child.process_handle = process_info.hProcess;
    if (stdin_pipe.parent >= 0) {
        child.stdin_pipe  = Some(ChildStdin(stdin_pipe.parent));
        stdin_pipe.parent = -1;
    }
    if (stdout_pipe.parent >= 0) {
        child.stdout_pipe  = Some(ChildStdout(stdout_pipe.parent));
        stdout_pipe.parent = -1;
    }
    if (stderr_pipe.parent >= 0) {
        child.stderr_pipe  = Some(ChildStderr(stderr_pipe.parent));
        stderr_pipe.parent = -1;
    }
    cleanup();
    return Ok(rstd::move(child));
#else
    return Err(rstd::io::error::Error::from_kind(
        rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported }));
#endif
}

} // namespace rstd::sys::process_impl
