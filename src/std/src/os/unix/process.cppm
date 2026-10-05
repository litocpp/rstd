module;
#include <rstd/macro.hpp>
export module rstd:os.unix.process;
export import :process.command;

#if RSTD_OS_UNIX
export namespace rstd::os::unix::process
{

struct CommandExt {
    /// Zero makes the child the leader of a new process group.
    static auto process_group(rstd::process::Command& command, i32 pgroup)
        -> rstd::process::Command& {
        command.process_group_ = Some(pgroup);
        return command;
    }
};

struct ChildExt {
    static auto send_signal(const rstd::process::Child& child, i32 signal) -> io::Result<empty> {
        return child.send_signal(signal, false);
    }
    /// For the PID fallback, the child must be its process group's leader.
    static auto send_process_group_signal(const rstd::process::Child& child, i32 signal)
        -> io::Result<empty> {
        return child.send_signal(signal, true);
    }
    static auto kill_process_group(rstd::process::Child& child) -> io::Result<empty>;
};

} // namespace rstd::os::unix::process
#endif
