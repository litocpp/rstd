module;
#include <rstd/macro.hpp>
export module rstd:process.exit_status;
export import rstd.alloc;

using ::alloc::vec::Vec;

export namespace rstd::process
{

/// Describes the result of a process after it has terminated.
struct ExitStatus {
private:
    i32  code_   { 0 };
    bool exited_ { true };

public:
    constexpr ExitStatus() = default;

    /// Construct from a raw exit code (process exited normally).
    static constexpr auto from_code(i32 code) noexcept -> ExitStatus {
        return { code, true };
    }

    /// Construct from a signal number (process was killed).
    static constexpr auto from_signal(i32 sig) noexcept -> ExitStatus {
        return { sig, false };
    }

#if RSTD_OS_UNIX
    /// Construct from a raw waitpid status on Unix.
    static auto from_raw(i32 raw) noexcept -> ExitStatus;
#endif

    /// Was termination successful? Returns `true` if exit code was 0.
    constexpr auto success() const noexcept -> bool {
        return exited_ && code_ == 0;
    }

    /// Returns the exit code if the process exited normally.
    constexpr auto code() const noexcept -> Option<i32> {
        if (exited_) { i32 c = code_; return Some(rstd::move(c)); }
        return None();
    }

    /// Returns the signal number if the process was killed by a signal (Unix).
    constexpr auto signal() const noexcept -> Option<i32> {
        if (! exited_) { i32 c = code_; return Some(rstd::move(c)); }
        return None();
    }

private:
    constexpr ExitStatus(i32 code, bool exited) : code_(code), exited_(exited) {}
};

/// The collected output of a finished process.
struct Output {
    ExitStatus         status;
    Vec<u8> stdout_buf;
    Vec<u8> stderr_buf;
};

/// Describes how to configure a child process's standard I/O stream.
struct Stdio {
    enum Kind : u8 { Inherit_, Null_, Piped_ };
    Kind kind;

    /// The child inherits the corresponding stream from the parent.
    static constexpr auto inherit() noexcept -> Stdio { return { Inherit_ }; }
    /// The child's stream is connected to `/dev/null`.
    static constexpr auto null() noexcept -> Stdio { return { Null_ }; }
    /// A new pipe is created between parent and child.
    static constexpr auto piped() noexcept -> Stdio { return { Piped_ }; }
};

} // namespace rstd::process
