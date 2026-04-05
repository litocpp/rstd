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

// forward
class Command;
struct Child;

} // namespace rstd::process

namespace rstd::sys::process_impl
{
auto spawn(rstd::process::Command& cmd)
    -> rstd::result::Result<rstd::process::Child, rstd::io::error::Error>;
}

export namespace rstd::process
{

/// A child process handle.
struct Child {
    i32  pid       { -1 };
    i32  stdin_fd  { -1 };
    i32  stdout_fd { -1 };
    i32  stderr_fd { -1 };

    /// Returns the OS-assigned process ID.
    auto id() const noexcept -> u32 { return static_cast<u32>(pid); }

    /// Waits for the child to exit and returns its status.
    auto wait() -> io::Result<ExitStatus>;

    /// Sends SIGKILL to the child process.
    auto kill() -> io::Result<rstd::empty>;

    ~Child();
    Child(Child&& o) noexcept
        : pid(o.pid), stdin_fd(o.stdin_fd), stdout_fd(o.stdout_fd), stderr_fd(o.stderr_fd) {
        o.pid = -1; o.stdin_fd = -1; o.stdout_fd = -1; o.stderr_fd = -1;
    }
    Child& operator=(Child&&) = delete;
    Child() = default;

private:
    void close_fds();
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
    auto output() -> io::Result<Output>;
};

} // namespace rstd::process
