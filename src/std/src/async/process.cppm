export module rstd:async.process;
export import :process.command;
export import :async.pipe;
export import :async.coro_future;
export import :async.oneshot;
import :sync;

using namespace rstd::prelude;

namespace rstd::async
{
using ChildResult = rstd::io::Result<rstd::process::ExitStatus>;
struct ChildState;
struct ProcessState;

export class ProcessHandle;
export class Child {
    sync::Arc<ChildState> state_;
    explicit Child(sync::Arc<ChildState> state);
    friend class ProcessHandle;
    friend class SpawnReservation;
    static auto wait_finished_owned(sync::Arc<ChildState> child) -> coro<rstd::io::Result<empty>>;
    static auto wait_owned(sync::Arc<ChildState> child) -> coro<ChildResult>;
    static auto cleanup_owned(sync::Arc<ChildState> child) -> coro<ChildResult>;
    static auto kill_owned(sync::Arc<ChildState> child) -> coro<rstd::io::Result<empty>>;

public:
    Child(const Child&) = delete;
    Child(Child&&) noexcept;
    auto operator=(Child&&) noexcept -> Child& = delete;
    ~Child();
    auto id() const -> i32;
    auto take_stdin() -> Option<pipe::Sender>;
    auto take_stdout() -> Option<pipe::Receiver>;
    auto take_stderr() -> Option<pipe::Receiver>;
    // Closes stdin unless taken by the caller. try_wait() leaves stdin open.
    // Canceling a wait does not release the supervisor's reap ownership.
    auto wait() const -> coro<ChildResult>;
    // Observes the end of supervision, not successful exit; does not consume wait errors.
    auto wait_finished() const -> coro<rstd::io::Result<empty>>;
    auto try_wait() const -> rstd::io::Result<Option<rstd::process::ExitStatus>>;
    // Requests termination; kill() also waits for the child to be reaped.
    auto start_kill() const -> rstd::io::Result<empty>;
    auto kill() const -> coro<rstd::io::Result<empty>>;
    auto terminate() const -> rstd::io::Result<empty>;
    auto cleanup() const -> coro<ChildResult>;
};

export class SpawnReservation {
    sync::Arc<ProcessState> state_;
    bool                    active_ { true };
    explicit SpawnReservation(sync::Arc<ProcessState> state);
    friend class ProcessHandle;
    auto adopt_native(rstd::process::Child& native) -> rstd::io::Result<Child>;

public:
    SpawnReservation(const SpawnReservation&) = delete;
    SpawnReservation(SpawnReservation&& other) noexcept;
    ~SpawnReservation();
    // Transfers the sole wait/reap right only on success. The caller must own this child PID.
    auto adopt(i32 pid) -> rstd::io::Result<Child>;
};

export class ProcessHandle {
    sync::Arc<ProcessState> state_;
    explicit ProcessHandle(sync::Arc<ProcessState> state);
    friend class ProcessSupervisor;

public:
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle(ProcessHandle&&) noexcept;
    auto operator=(ProcessHandle&&) noexcept -> ProcessHandle&;
    ~ProcessHandle();
    auto clone() const -> ProcessHandle;
    auto accepting() const -> bool;
    // Reserve before fork so shutdown cannot finish between creation and adoption.
    auto prepare() const -> rstd::io::Result<SpawnReservation>;
    // Reuses Command configuration; reservation and reap ownership stay inside this entry point.
    auto spawn(rstd::process::Command& command) const -> rstd::io::Result<Child>;
    // The caller retains a native child's wait/reap right when this returns an error.
    auto adopt(i32 pid) const -> rstd::io::Result<Child>;
};

export class ProcessSupervisor {
    sync::Arc<ProcessState> state_;
    explicit ProcessSupervisor(sync::Arc<ProcessState> state);
    static auto wait_shutdown_owned(sync::Arc<ProcessState> state) -> coro<rstd::io::Result<empty>>;
    static auto shutdown_owned(sync::Arc<ProcessState> state) -> coro<rstd::io::Result<empty>>;

public:
    ProcessSupervisor(const ProcessSupervisor&) = delete;
    ProcessSupervisor(ProcessSupervisor&&) noexcept;
    auto operator=(ProcessSupervisor&&) noexcept -> ProcessSupervisor& = delete;
    ~ProcessSupervisor();
    // Requires an I/O-enabled runtime. Unix subscribes to child signals before spawning.
    static auto make() -> rstd::io::Result<ProcessSupervisor>;
    auto        handle() const -> ProcessHandle;
    void        stop_accepting() const;
    void        start_shutdown() const;
    // Requires the owning runtime to keep running. Each wait owns its state and may be canceled.
    auto shutdown() const -> coro<rstd::io::Result<empty>>;
    auto wait_shutdown() const -> coro<rstd::io::Result<empty>>;
    auto take_shutdown_result() -> Option<rstd::io::Result<empty>>;
};
} // namespace rstd::async
