module;
#include <rstd/macro.hpp>
export module rstd:os.linux.process;
export import :process.command;
#if RSTD_OS_LINUX
export import :os.linux.pidfd;

export namespace rstd::os::linux::process
{

struct CommandExt {
    /// Requests a race-free pidfd at spawn. Absence does not fail spawning.
    static auto create_pidfd(rstd::process::Command& command, bool value)
        -> rstd::process::Command& {
        command.create_pidfd_ = value;
        return command;
    }
};

struct ChildExt {
    static auto pidfd(const rstd::process::Child& child [[clang::lifetimebound]])
        -> io::Result<ref<PidFd>> {
        if (child.pidfd_.is_some())
            return Ok(ref<PidFd>::from_raw_parts(rstd::addressof(*child.pidfd_)));
        return Err(
            io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Other }));
    }
    /// Consumes Child on success; returns the original Child when no pidfd exists.
    static auto into_pidfd(rstd::process::Child child) -> Result<PidFd, rstd::process::Child> {
        if (child.pidfd_.is_none()) return Err(rstd::move(child));
        return Ok(child.pidfd_.take().unwrap());
    }
};

} // namespace rstd::os::linux::process
#endif
