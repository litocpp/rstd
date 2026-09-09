module;
#include <rstd/macro.hpp>

export module rstd:process.command;
export import :process.exit_status;
export import :io;
export import :path;
export import rstd.alloc;
import :sys.io.stdio;

using ::alloc::ffi::CString;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;

export namespace rstd::process
{

struct EnvAction {
    OsString         key;
    Option<OsString> value;
};

// forwards
class Command;
struct Child;

/// A handle to a child process's standard input (write end of pipe).
///
/// Dropping this closes the pipe, causing the child to see EOF on its stdin.
struct ChildStdin {
    int fd { -1 };
    ~ChildStdin();
    ChildStdin(ChildStdin&& o) noexcept: fd(o.fd) { o.fd = -1; }
    ChildStdin& operator=(ChildStdin&&) = delete;
    ChildStdin()                        = default;
    explicit ChildStdin(int f): fd(f) {}
};

/// A handle to a child process's standard output (read end of pipe).
struct ChildStdout {
    int fd { -1 };
    ~ChildStdout();
    ChildStdout(ChildStdout&& o) noexcept: fd(o.fd) { o.fd = -1; }
    ChildStdout& operator=(ChildStdout&&) = delete;
    ChildStdout()                         = default;
    explicit ChildStdout(int f): fd(f) {}
};

/// A handle to a child process's standard error (read end of pipe).
struct ChildStderr {
    int fd { -1 };
    ~ChildStderr();
    ChildStderr(ChildStderr&& o) noexcept: fd(o.fd) { o.fd = -1; }
    ChildStderr& operator=(ChildStderr&&) = delete;
    ChildStderr()                         = default;
    explicit ChildStderr(int f): fd(f) {}
};

} // namespace rstd::process

namespace rstd::sys::process_impl
{
auto environment_keys_equal(ref<ffi::OsStr> left, ref<ffi::OsStr> right) -> bool;

struct Spawn {
    static auto spawn(rstd::process::Command& cmd)
        -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error>;
};
} // namespace rstd::sys::process_impl

// ── io::Read / io::Write impls for child pipe handles ────────────────────
namespace rstd
{

template<>
struct Impl<io::Write, process::ChildStdin> : ImplBase<process::ChildStdin> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        return sys::io::stdio::write_fd(this->self().fd, as_bytes(buf));
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Read, process::ChildStdout> : ImplBase<process::ChildStdout> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        return sys::io::stdio::read_fd(this->self().fd, as_bytes_mut(buf));
    }
};

template<>
struct Impl<io::Read, process::ChildStderr> : ImplBase<process::ChildStderr> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        return sys::io::stdio::read_fd(this->self().fd, as_bytes_mut(buf));
    }
};

} // namespace rstd

export namespace rstd::process
{

/// A child process handle.
struct Child {
    int pid { -1 };
#if RSTD_OS_WINDOWS
    void* process_handle { nullptr };
#endif
    Option<ChildStdin>  stdin_pipe;
    Option<ChildStdout> stdout_pipe;
    Option<ChildStderr> stderr_pipe;
    Option<ExitStatus>  status;

    /// Returns the OS-assigned process ID.
    auto id() const noexcept -> u32 { return u32(pid); }

    /// Takes ownership of the child's stdin pipe handle.
    auto take_stdin() -> Option<ChildStdin> { return stdin_pipe.take(); }
    /// Takes ownership of the child's stdout pipe handle.
    auto take_stdout() -> Option<ChildStdout> { return stdout_pipe.take(); }
    /// Takes ownership of the child's stderr pipe handle.
    auto take_stderr() -> Option<ChildStderr> { return stderr_pipe.take(); }

    /// Waits for the child to exit and returns its status.
    auto wait() -> io::Result<ExitStatus>;

    /// Checks whether the child has exited without blocking.
    auto try_wait() -> io::Result<Option<ExitStatus>>;

    /// Sends SIGKILL to the child process.
    auto kill() -> io::Result<rstd::empty>;

    /// Waits for the child and collects all remaining stdout/stderr.
    auto wait_with_output() -> io::Result<Output>;

    /// Waits for the child, collects stdout/stderr, and forwards chunks while they arrive.
    /// Stdout and stderr notifications may run concurrently.
    auto wait_with_output(OutputObserver observer) -> io::Result<Output>;

    ~Child();
    Child(Child&& o) noexcept
        : pid(o.pid),
#if RSTD_OS_WINDOWS
          process_handle(o.process_handle),
#endif
          stdin_pipe(o.stdin_pipe.take()),
          stdout_pipe(o.stdout_pipe.take()),
          stderr_pipe(o.stderr_pipe.take()),
          status(o.status.take()) {
        o.pid = -1;
#if RSTD_OS_WINDOWS
        o.process_handle = nullptr;
#endif
    }
    Child& operator=(Child&&) = delete;
    Child()                   = default;
};

/// A process builder, providing fine-grained control over how a new process
/// should be spawned. Analogous to Rust's `std::process::Command`.
class Command {
    OsString        program_;
    Vec<OsString>   args_ {};
    Vec<EnvAction>  env_actions_ {};
    Option<PathBuf> cwd_ {};
    Stdio           cfg_stdin_ { Stdio::inherit() };
    Stdio           cfg_stdout_ { Stdio::inherit() };
    Stdio           cfg_stderr_ { Stdio::inherit() };
    bool            env_clear_ { false };

    friend sys::process_impl::Spawn;

    explicit Command(OsString prog): program_(rstd::move(prog)) {}

public:
    Command(Command&&) noexcept            = default;
    Command& operator=(Command&&) noexcept = default;

    /// Creates a new `Command` for the given program.
    ///
    /// \param program  Path or name of the program to execute.
    static auto make(ref<OsStr> program) -> Command { return Command(program.to_os_string()); }

    /// Adds an argument to pass to the program.
    auto arg(ref<OsStr> value) -> Command& {
        args_.push(value.to_os_string());
        return *this;
    }

    /// Sets an environment variable for the child process.
    auto env(ref<OsStr> key, ref<OsStr> value) -> Command& {
        for (auto& action : env_actions_) {
            if (sys::process_impl::environment_keys_equal(action.key.as_os_str(), key)) {
                action.value = Some(value.to_os_string());
                return *this;
            }
        }
        env_actions_.push(EnvAction { key.to_os_string(), Some(value.to_os_string()) });
        return *this;
    }

    /// Removes an environment variable for the child process.
    auto env_remove(ref<OsStr> key) -> Command& {
        for (auto index = usize(); index < env_actions_.len(); ++index) {
            if (sys::process_impl::environment_keys_equal(env_actions_[index].key.as_os_str(),
                                                          key)) {
                if (env_clear_)
                    (void)env_actions_.remove(index);
                else
                    env_actions_[index].value = None();
                return *this;
            }
        }
        if (! env_clear_) env_actions_.push(EnvAction { key.to_os_string(), None() });
        return *this;
    }

    /// Clears all environment variables for the child process.
    auto env_clear() -> Command& {
        env_clear_ = true;
        env_actions_.clear();
        return *this;
    }

    /// Sets the working directory for the child process.
    auto current_dir(ref<Path> dir) -> Command& {
        cwd_ = Some(PathBuf::from(dir.as_os_str().to_os_string()));
        return *this;
    }

    auto get_program() const noexcept [[clang::lifetimebound]] -> ref<OsStr> {
        return program_.as_os_str();
    }
    auto get_args() const noexcept [[clang::lifetimebound]] -> slice<OsString> {
        return args_.as_slice();
    }
    auto get_envs() const noexcept [[clang::lifetimebound]] -> slice<EnvAction> {
        return env_actions_.as_slice();
    }
    auto get_current_dir() const noexcept [[clang::lifetimebound]] -> Option<ref<Path>> {
        return cwd_.is_some() ? Some(cwd_->as_path()) : None();
    }
    auto args(slice<OsString> values) -> Command& {
        for (const auto& value : values) arg(value.as_os_str());
        return *this;
    }
    auto envs(slice<tuple<OsString, OsString>> values) -> Command& {
        for (const auto& value : values)
            env(value.template get<0>().as_os_str(), value.template get<1>().as_os_str());
        return *this;
    }

    /// Configures the child process's standard input.
    auto set_stdin(Stdio s) -> Command& {
        cfg_stdin_ = s;
        return *this;
    }
    /// Configures the child process's standard output.
    auto set_stdout(Stdio s) -> Command& {
        cfg_stdout_ = s;
        return *this;
    }
    /// Configures the child process's standard error.
    auto set_stderr(Stdio s) -> Command& {
        cfg_stderr_ = s;
        return *this;
    }

    /// Spawns the child process.
    auto spawn() -> io::Result<Child> { return sys::process_impl::Spawn::spawn(*this); }

    /// Executes the command and waits for it to finish, returning the exit status.
    auto status() -> io::Result<ExitStatus> {
        auto child = spawn();
        if (child.is_err()) return Err(child.unwrap_err());
        return child.unwrap().wait();
    }

    /// Executes the command, waits for it, and collects stdout/stderr.
    auto output() -> io::Result<Output> {
        cfg_stdout_ = Stdio::piped();
        cfg_stderr_ = Stdio::piped();
        auto child  = spawn();
        if (child.is_err()) return Err(child.unwrap_err());
        return child.unwrap().wait_with_output();
    }

    /// Executes the command, collecting output while forwarding chunks to an observer.
    /// Stdout and stderr notifications may run concurrently.
    auto output(OutputObserver observer) -> io::Result<Output> {
        cfg_stdout_ = Stdio::piped();
        cfg_stderr_ = Stdio::piped();
        auto child  = spawn();
        if (child.is_err()) return Err(child.unwrap_err());
        return child.unwrap().wait_with_output(observer);
    }
};

} // namespace rstd::process
