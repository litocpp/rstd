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
using rstd::path::Path;
using namespace rstd::prelude;

export namespace rstd::process
{

struct EnvAction {
    Vec<u8>         key;
    Option<Vec<u8>> value;
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
    int                 pid { -1 };
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
          stdin_pipe(o.stdin_pipe.take()),
          stdout_pipe(o.stdout_pipe.take()),
          stderr_pipe(o.stderr_pipe.take()),
          status(o.status.take()) {
        o.pid = -1;
    }
    Child& operator=(Child&&) = delete;
    Child()                   = default;
};

/// A process builder, providing fine-grained control over how a new process
/// should be spawned. Analogous to Rust's `std::process::Command`.
class Command {
    CString         program_;
    Vec<CString>    args_ {};
    Vec<EnvAction>  env_actions_ {};
    Option<CString> cwd_ {};
    Stdio           cfg_stdin_ { Stdio::inherit() };
    Stdio           cfg_stdout_ { Stdio::inherit() };
    Stdio           cfg_stderr_ { Stdio::inherit() };
    bool            env_clear_ { false };

    friend sys::process_impl::Spawn;

    explicit Command(CString&& prog): program_(rstd::move(prog)) {}

    static auto cstring(ref<OsStr> value) -> CString {
        return CString::make(Vec<u8>::from(value.as_encoded_bytes())).unwrap();
    }

public:
    Command(Command&&) noexcept            = default;
    Command& operator=(Command&&) noexcept = default;

    /// Creates a new `Command` for the given program.
    ///
    /// \param program  Path or name of the program to execute.
    static auto make(ref<OsStr> program) -> Command { return Command(cstring(program)); }

    /// Adds an argument to pass to the program.
    auto arg(ref<OsStr> value) -> Command& {
        args_.push(cstring(value));
        return *this;
    }

    /// Sets an environment variable for the child process.
    auto env(ref<OsStr> key, ref<OsStr> value) -> Command& {
        env_actions_.push(EnvAction { Vec<u8>::from(key.as_encoded_bytes()),
                                      Some(Vec<u8>::from(value.as_encoded_bytes())) });
        return *this;
    }

    /// Removes an environment variable for the child process.
    auto env_remove(ref<OsStr> key) -> Command& {
        env_actions_.push(EnvAction { Vec<u8>::from(key.as_encoded_bytes()), Option<Vec<u8>> {} });
        return *this;
    }

    /// Clears all environment variables for the child process.
    auto env_clear() -> Command& {
        env_clear_ = true;
        return *this;
    }

    /// Sets the working directory for the child process.
    auto current_dir(ref<Path> dir) -> Command& {
        cwd_ = Some(dir.to_cstring().unwrap());
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
