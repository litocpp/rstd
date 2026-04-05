module;
#include <rstd/macro.hpp>
export module rstd:process.command;
export import :process.exit_status;
export import :io;
export import rstd.alloc;

using ::alloc::string::String;
using ::alloc::vec::Vec;
using ::alloc::ffi::CString;
using namespace rstd::prelude;

export namespace rstd::process
{

struct EnvAction {
    CString key;
    Option<CString> value; // None = remove
};

// forwards
class Command;
struct Child;

/// A handle to a child process's standard input (write end of pipe).
///
/// Dropping this closes the pipe, causing the child to see EOF on its stdin.
struct ChildStdin {
    i32 fd { -1 };
    ~ChildStdin();
    ChildStdin(ChildStdin&& o) noexcept : fd(o.fd) { o.fd = -1; }
    ChildStdin& operator=(ChildStdin&&) = delete;
    ChildStdin() = default;
    explicit ChildStdin(i32 f) : fd(f) {}
};

/// A handle to a child process's standard output (read end of pipe).
struct ChildStdout {
    i32 fd { -1 };
    ~ChildStdout();
    ChildStdout(ChildStdout&& o) noexcept : fd(o.fd) { o.fd = -1; }
    ChildStdout& operator=(ChildStdout&&) = delete;
    ChildStdout() = default;
    explicit ChildStdout(i32 f) : fd(f) {}
};

/// A handle to a child process's standard error (read end of pipe).
struct ChildStderr {
    i32 fd { -1 };
    ~ChildStderr();
    ChildStderr(ChildStderr&& o) noexcept : fd(o.fd) { o.fd = -1; }
    ChildStderr& operator=(ChildStderr&&) = delete;
    ChildStderr() = default;
    explicit ChildStderr(i32 f) : fd(f) {}
};

} // namespace rstd::process

namespace rstd::sys::process_impl
{
auto spawn(rstd::process::Command& cmd)
    -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error>;
}

// ── io::Read / io::Write impls for child pipe handles ────────────────────
namespace rstd
{

template<>
struct Impl<io::Write, process::ChildStdin> : ImplBase<process::ChildStdin> {
    auto write(const u8* buf, usize len) -> io::Result<usize> {
        return sys::io::stdio::write_fd(this->self().fd, buf, len);
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Read, process::ChildStdout> : ImplBase<process::ChildStdout> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        return sys::io::stdio::read_fd(this->self().fd, buf, len);
    }
};

template<>
struct Impl<io::Read, process::ChildStderr> : ImplBase<process::ChildStderr> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        return sys::io::stdio::read_fd(this->self().fd, buf, len);
    }
};

} // namespace rstd

export namespace rstd::process
{

/// A child process handle.
struct Child {
    i32                  pid { -1 };
    Option<ChildStdin>   stdin_pipe;
    Option<ChildStdout>  stdout_pipe;
    Option<ChildStderr>  stderr_pipe;

    /// Returns the OS-assigned process ID.
    auto id() const noexcept -> u32 { return static_cast<u32>(pid); }

    /// Takes ownership of the child's stdin pipe handle.
    auto take_stdin() -> Option<ChildStdin> { return stdin_pipe.take(); }
    /// Takes ownership of the child's stdout pipe handle.
    auto take_stdout() -> Option<ChildStdout> { return stdout_pipe.take(); }
    /// Takes ownership of the child's stderr pipe handle.
    auto take_stderr() -> Option<ChildStderr> { return stderr_pipe.take(); }

    /// Waits for the child to exit and returns its status.
    auto wait() -> io::Result<ExitStatus>;

    /// Sends SIGKILL to the child process.
    auto kill() -> io::Result<rstd::empty>;

    /// Waits for the child and collects all remaining stdout/stderr.
    auto wait_with_output() -> io::Result<Output>;

    ~Child();
    Child(Child&& o) noexcept
        : pid(o.pid),
          stdin_pipe(o.stdin_pipe.take()),
          stdout_pipe(o.stdout_pipe.take()),
          stderr_pipe(o.stderr_pipe.take()) {
        o.pid = -1;
    }
    Child& operator=(Child&&) = delete;
    Child() = default;
};

/// A process builder, providing fine-grained control over how a new process
/// should be spawned. Analogous to Rust's `std::process::Command`.
class Command {
    CString          program_;
    Vec<CString>     args_ {};
    Vec<EnvAction>   env_actions_ {};
    Option<CString>  cwd_ {};
    Stdio            cfg_stdin_  { Stdio::inherit() };
    Stdio            cfg_stdout_ { Stdio::inherit() };
    Stdio            cfg_stderr_ { Stdio::inherit() };
    bool             env_clear_ { false };

    friend auto sys::process_impl::spawn(Command&)
        -> result::Result<Child, io::error::Error>;

    explicit Command(CString&& prog) : program_(rstd::move(prog)) {}

public:
    Command(Command&&) noexcept = default;
    Command& operator=(Command&&) noexcept = default;

    /// Creates a new `Command` for the given program.
    ///
    /// \param program  Path or name of the program to execute.
    static auto make(const char* program) -> Command {
        return Command(CString::from_raw_parts(program));
    }

    /// Adds an argument to pass to the program.
    auto arg(const char* a) -> Command& {
        args_.push(CString::from_raw_parts(a));
        return *this;
    }

    /// Sets an environment variable for the child process.
    auto env(const char* key, const char* value) -> Command& {
        env_actions_.push(EnvAction {
            CString::from_raw_parts(key),
            Some(CString::from_raw_parts(value))
        });
        return *this;
    }

    /// Removes an environment variable for the child process.
    auto env_remove(const char* key) -> Command& {
        env_actions_.push(EnvAction {
            CString::from_raw_parts(key),
            Option<CString> {}
        });
        return *this;
    }

    /// Clears all environment variables for the child process.
    auto env_clear() -> Command& {
        env_clear_ = true;
        return *this;
    }

    /// Sets the working directory for the child process.
    auto current_dir(const char* dir) -> Command& {
        cwd_ = Some(CString::from_raw_parts(dir));
        return *this;
    }

    /// Configures the child process's standard input.
    auto set_stdin(Stdio s) -> Command& { cfg_stdin_ = s; return *this; }
    /// Configures the child process's standard output.
    auto set_stdout(Stdio s) -> Command& { cfg_stdout_ = s; return *this; }
    /// Configures the child process's standard error.
    auto set_stderr(Stdio s) -> Command& { cfg_stderr_ = s; return *this; }

    /// Spawns the child process.
    auto spawn() -> io::Result<Child> {
        return sys::process_impl::spawn(*this);
    }

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
        auto child = spawn();
        if (child.is_err()) return Err(child.unwrap_err());
        return child.unwrap().wait_with_output();
    }
};

} // namespace rstd::process
