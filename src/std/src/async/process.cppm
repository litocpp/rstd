module;
#include <rstd/macro.hpp>
export module rstd:async.process;
export import :process.command;
export import :async.pipe;
export import :async.coro_future;
export import :async.oneshot;
import :async.signal;
import :async.notify;
import :async.select;
import :async.spawn;
import :async.runtime_core;
import rstd.sys.libc;
import :sync;
#if RSTD_OS_WINDOWS
import rstd.sys.process.windows;
#endif
#if RSTD_OS_UNIX
import :os.unix.process;
#endif

using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

namespace rstd::async
{
using ChildResult = io::Result<rstd::process::ExitStatus>;
struct ChildWaiter {
    usize                  id;
    oneshot::Sender<empty> result;
};
struct ChildFields {
    rstd::process::Child              child;
    Option<pipe::Sender>              stdin_pipe;
    Option<pipe::Receiver>            stdout_pipe, stderr_pipe;
    Option<rstd::process::ExitStatus> status;
    Option<io::Error>                 error;
    bool                              finished {}, waiting {};
    usize                             next_waiter { 1 };
    Vec<ChildWaiter>                  waiters;
    explicit ChildFields(rstd::process::Child child): child(rstd::move(child)) {}
};
struct ChildState {
    i32                      pid;
    sync::Mutex<ChildFields> fields;
#if RSTD_OS_WINDOWS
    ::alloc::boxed::Box<NotifyHandle> exit_notice;
    Option<sys::process::ProcessWait> exit_wait;
    ChildState(rstd::process::Child              child,
               ::alloc::boxed::Box<NotifyHandle> notice,
               sys::process::ProcessWait         wait)
        : pid(i32(child.id().to_primitive())),
          fields(ChildFields { rstd::move(child) }),
          exit_notice(rstd::move(notice)),
          exit_wait(Some(rstd::move(wait))) {}
    static void notify_exit(void* context) noexcept {
        (void)static_cast<NotifyHandle*>(context)->notify();
    }
#else
    explicit ChildState(rstd::process::Child child)
        : pid(i32(child.id().to_primitive())), fields(ChildFields { rstd::move(child) }) {}
#endif
    auto terminate() const -> io::Result<empty> {
        auto state = fields.lock().unwrap();
        if (state->finished) return Ok(empty {});
        auto result = state->child.kill();
#if RSTD_OS_UNIX
        if (result.is_err() &&
            result.as_ref().unwrap_err().raw_os_error() == Some(i32(libc::ESRCH)))
            return Ok(empty {});
#endif
        return result;
    }
    auto poll_exit() const -> bool {
        Vec<ChildWaiter> waiters;
        {
            auto state = fields.lock().unwrap();
            if (! state->finished) {
                auto result = state->child.try_wait();
                if (result.is_err())
                    state->error = Some(rstd::move(result).unwrap_err());
                else if (result->is_none())
                    return false;
                else
                    state->status = *result;
                state->finished = true;
            }
            waiters        = rstd::move(state->waiters);
            state->waiters = Vec<ChildWaiter>::make();
        }
        for (auto& waiter : waiters) {
            (void)waiter.result.send(empty {});
        }
        return true;
    }
};
struct ProcessWaiter {
    usize                  id;
    oneshot::Sender<empty> result;
};
struct ProcessFields {
    bool                       accepting { true }, stopping {}, finished {};
    Option<io::Result<empty>>  result;
    usize                      next_waiter { 1 };
    Vec<ProcessWaiter>         waiters;
    usize                      reservations {};
    Vec<sync::Arc<ChildState>> children;
};
struct ProcessState {
    sync::Mutex<ProcessFields> fields { ProcessFields {} };
    Notify                     changed;
#if RSTD_OS_UNIX
    rstd::signal::Subscription signals;
    ProcessState(Notify changed, rstd::signal::Subscription signals)
        : changed(rstd::move(changed)), signals(rstd::move(signals)) {}
#else
    explicit ProcessState(Notify changed): changed(rstd::move(changed)) {}
#endif
    void wake() const { (void)changed.notifier().notify(); }
    void stop() const {
        Vec<sync::Arc<ChildState>> children;
        {
            auto state = fields.lock().unwrap();
            if (state->stopping) return;
            state->accepting = false;
            state->stopping  = true;
            for (const auto& child : state->children) children.push(child.clone());
        }
        for (const auto& child : children) (void)child->terminate();
        wake();
    }
    void finish(Option<io::Error> error = None()) const {
        Vec<ProcessWaiter> waiters;
        {
            auto state = fields.lock().unwrap();
            if (state->finished) return;
            state->accepting = false;
            state->finished  = true;
            state->result    = Some(error ? io::Result<empty>(Err(error.take().unwrap()))
                                          : io::Result<empty>(Ok(empty {})));
            waiters          = rstd::move(state->waiters);
            state->waiters   = Vec<ProcessWaiter>::make();
        }
        for (auto& waiter : waiters) {
            (void)waiter.result.send(empty {});
        }
    }
};
struct ProcessTask {
    sync::Arc<ProcessState> state;
    explicit ProcessTask(sync::Arc<ProcessState> state): state(rstd::move(state)) {}
    ProcessTask(const ProcessTask&)     = delete;
    ProcessTask(ProcessTask&&) noexcept = default;
    ~ProcessTask() {
        if (! state) return;
        state->stop();
        state->finish(Some(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted })));
    }
};
struct ProcessWaitRegistration {
    sync::Arc<ProcessState> state;
    usize                   id;
    ~ProcessWaitRegistration() {
        auto fields = state->fields.lock().unwrap();
        for (usize i {}; i < fields->waiters.len(); ++i)
            if (fields->waiters[i].id == id) {
                fields->waiters.remove(i);
                break;
            }
    }
};
struct ChildWaitRegistration {
    sync::Arc<ChildState> child;
    usize                 id;
    ~ChildWaitRegistration() {
        auto state = child->fields.lock().unwrap();
        for (usize i {}; i < state->waiters.len(); ++i)
            if (state->waiters[i].id == id) {
                state->waiters.remove(i);
                break;
            }
    }
};

struct ChildResultGuard {
    sync::Arc<ChildState> child;
    ~ChildResultGuard() { child->fields.lock().unwrap()->waiting = false; }
};

export class ProcessHandle;
export class Child {
    sync::Arc<ChildState> state_;
    explicit Child(sync::Arc<ChildState> state): state_(rstd::move(state)) {}
    friend class ProcessHandle;
    friend class SpawnReservation;
    static auto wait_finished_owned(sync::Arc<ChildState> child) -> coro<io::Result<empty>> {
        auto  pair = oneshot::channel<empty>();
        usize id {};
        {
            auto state = child->fields.lock().unwrap();
            if (state->finished) co_return Ok(empty {});
            id = state->next_waiter++;
            state->waiters.push(ChildWaiter { id, rstd::move(pair.template get<0>()) });
        }
        ChildWaitRegistration pending { child.clone(), id };
        auto                  result = co_await rstd::move(pair.template get<1>());
        if (result.is_err())
            co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted }));
        co_return Ok(empty {});
    }
    static auto wait_owned(sync::Arc<ChildState> child) -> coro<ChildResult> {
        {
            auto state = child->fields.lock().unwrap();
            if (state->waiting)
                co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::ResourceBusy }));
            state->waiting    = true;
            state->stdin_pipe = None();
        }
        ChildResultGuard guard { child.clone() };
        auto             waited = co_await wait_finished_owned(child.clone());
        if (waited.is_err()) co_return Err(rstd::move(waited).unwrap_err());
        auto state = child->fields.lock().unwrap();
        if (state->error) co_return Err(state->error.take().unwrap());
        if (state->status) co_return Ok(*state->status);
        auto retried = state->child.try_wait();
        if (retried.is_err()) co_return Err(rstd::move(retried).unwrap_err());
        if (*retried) co_return Ok(**retried);
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    }
    static auto cleanup_owned(sync::Arc<ChildState> child) -> coro<ChildResult> {
        auto killed = child->terminate();
        if (killed.is_err()) co_return Err(rstd::move(killed).unwrap_err());
        co_return co_await wait_owned(rstd::move(child));
    }
    static auto kill_owned(sync::Arc<ChildState> child) -> coro<io::Result<empty>> {
        auto result = co_await cleanup_owned(rstd::move(child));
        if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
        co_return Ok(empty {});
    }

public:
    Child(const Child&)                        = delete;
    Child(Child&&) noexcept                    = default;
    auto operator=(Child&&) noexcept -> Child& = delete;
    ~Child() {
        if (state_) (void)state_->terminate();
    }
    auto id() const -> i32 { return state_->pid; }
    auto take_stdin() -> Option<pipe::Sender> {
        return state_->fields.lock().unwrap()->stdin_pipe.take();
    }
    auto take_stdout() -> Option<pipe::Receiver> {
        return state_->fields.lock().unwrap()->stdout_pipe.take();
    }
    auto take_stderr() -> Option<pipe::Receiver> {
        return state_->fields.lock().unwrap()->stderr_pipe.take();
    }
    // Closes stdin unless taken by the caller. try_wait() leaves stdin open.
    // Canceling a wait does not release the supervisor's reap ownership.
    auto wait() const -> coro<ChildResult> { return wait_owned(state_.clone()); }
    // Observes the end of supervision, not successful exit; does not consume wait errors.
    auto wait_finished() const -> coro<io::Result<empty>> {
        return wait_finished_owned(state_.clone());
    }
    auto try_wait() const -> io::Result<Option<rstd::process::ExitStatus>> {
        (void)state_->poll_exit();
        auto state = state_->fields.lock().unwrap();
        if (state->waiting)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::ResourceBusy }));
        if (state->error) return Err(state->error.take().unwrap());
        if (state->status) return Ok(state->status);
        if (! state->finished) return Ok(None());
        return state->child.try_wait();
    }
    // Requests termination; kill() also waits for the child to be reaped.
    auto start_kill() const -> io::Result<empty> { return state_->terminate(); }
    auto kill() const -> coro<io::Result<empty>> { return kill_owned(state_.clone()); }
    auto terminate() const -> io::Result<empty> { return start_kill(); }
    auto cleanup() const -> coro<ChildResult> { return cleanup_owned(state_.clone()); }
};

export class SpawnReservation {
    sync::Arc<ProcessState> state_;
    bool                    active_ { true };
    explicit SpawnReservation(sync::Arc<ProcessState> state): state_(rstd::move(state)) {}
    friend class ProcessHandle;
    auto adopt_native(rstd::process::Child& native) -> io::Result<Child> {
        auto pid = i32(native.id().to_primitive());
        if (! active_ || pid <= i32())
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
#if RSTD_OS_WINDOWS
        auto notice = ::alloc::boxed::Box<NotifyHandle>::make(state_->changed.notifier());
        auto watch  = sys::process::ProcessWait::make(
            native.as_handle(), ChildState::notify_exit, notice.as_mut_ptr().as_raw_ptr());
        if (watch.is_err()) return Err(rstd::move(watch).unwrap_err());
#endif
        Option<sync::Arc<ChildState>> child;
        bool                          stopping;
        {
            auto state = state_->fields.lock().unwrap();
            if (state->finished)
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
            for (usize index {}; index < state->children.len();) {
                auto owned = state->children[index].clone();
                if (owned->pid != pid) {
                    ++index;
                    continue;
                }
                auto old = owned->fields.lock().unwrap();
                if (! old->finished)
                    return Err(
                        io::Error::from_kind(io::ErrorKind { io::ErrorKind::AlreadyExists }));
                state->children.remove(index);
            }
            stopping = state->stopping;
#if RSTD_OS_WINDOWS
            child = Some(sync::Arc<ChildState>::make(
                rstd::move(native), rstd::move(notice), rstd::move(watch).unwrap()));
#else
            child = Some(sync::Arc<ChildState>::make(rstd::move(native)));
#endif
            state->children.push(child->clone());
            --state->reservations;
            active_ = false;
        }
        if (stopping) (void)(*child)->terminate();
        state_->wake();
        return Ok(Child { child.take().unwrap() });
    }

public:
    SpawnReservation(const SpawnReservation&) = delete;
    SpawnReservation(SpawnReservation&& other) noexcept
        : state_(rstd::move(other.state_)), active_(rstd::exchange(other.active_, false)) {}
    ~SpawnReservation() {
        if (! active_) return;
        {
            auto state = state_->fields.lock().unwrap();
            --state->reservations;
        }
        state_->wake();
    }
    // Transfers the sole wait/reap right only on success. The caller must own this child PID.
    auto adopt(i32 pid) -> io::Result<Child> {
#if RSTD_OS_UNIX
        auto native = os::unix::process::ChildExt::from_raw_pid(pid);
        if (native.is_err()) return Err(rstd::move(native).unwrap_err());
        return adopt_native(*native);
#else
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
};

struct SpawnRollback {
    rstd::process::Child& child;
    bool                  active { true };
    ~SpawnRollback() {
        if (! active) return;
        // Only setup failures block here; an unregistered child must not lose its reap owner.
        (void)child.kill();
        (void)child.wait();
    }
};

export class ProcessHandle {
    sync::Arc<ProcessState> state_;
    explicit ProcessHandle(sync::Arc<ProcessState> state): state_(rstd::move(state)) {}
    friend class ProcessSupervisor;

public:
    ProcessHandle(const ProcessHandle&)                        = delete;
    ProcessHandle(ProcessHandle&&) noexcept                    = default;
    auto operator=(ProcessHandle&&) noexcept -> ProcessHandle& = default;
    auto clone() const -> ProcessHandle { return ProcessHandle { state_.clone() }; }
    auto accepting() const -> bool { return state_->fields.lock().unwrap()->accepting; }
    // Reserve before fork so shutdown cannot finish between creation and adoption.
    auto prepare() const -> io::Result<SpawnReservation> {
        auto state = state_->fields.lock().unwrap();
        if (! state->accepting)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        ++state->reservations;
        return Ok(SpawnReservation { state_.clone() });
    }
    // Reuses Command configuration; reservation and reap ownership stay inside this entry point.
    auto spawn(rstd::process::Command& command) const -> io::Result<Child> {
#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
        auto prepared = prepare();
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
#if RSTD_OS_WINDOWS
        auto native = command.spawn_with_async_pipes();
#else
        auto native = command.spawn();
#endif
        if (native.is_err()) return Err(rstd::move(native).unwrap_err());
        SpawnRollback          rollback { *native };
        Option<pipe::Sender>   input;
        Option<pipe::Receiver> output, error;
        if (auto descriptor = native->take_stdin(); descriptor.is_some()) {
#if RSTD_OS_WINDOWS
            auto handle = rstd::move(*descriptor).into_owned_handle();
            if (handle.is_err()) return Err(rstd::move(handle).unwrap_err());
            auto endpoint = pipe::Sender::from_owned_handle(rstd::move(handle).unwrap());
#else
            auto endpoint = pipe::Sender::from_owned_fd(rstd::move(*descriptor).into_owned_fd());
#endif
            if (endpoint.is_err()) return Err(rstd::move(endpoint).unwrap_err());
            input = Some(rstd::move(endpoint).unwrap());
        }
        if (auto descriptor = native->take_stdout(); descriptor.is_some()) {
#if RSTD_OS_WINDOWS
            auto handle = rstd::move(*descriptor).into_owned_handle();
            if (handle.is_err()) return Err(rstd::move(handle).unwrap_err());
            auto endpoint = pipe::Receiver::from_owned_handle(rstd::move(handle).unwrap());
#else
            auto endpoint = pipe::Receiver::from_owned_fd(rstd::move(*descriptor).into_owned_fd());
#endif
            if (endpoint.is_err()) return Err(rstd::move(endpoint).unwrap_err());
            output = Some(rstd::move(endpoint).unwrap());
        }
        if (auto descriptor = native->take_stderr(); descriptor.is_some()) {
#if RSTD_OS_WINDOWS
            auto handle = rstd::move(*descriptor).into_owned_handle();
            if (handle.is_err()) return Err(rstd::move(handle).unwrap_err());
            auto endpoint = pipe::Receiver::from_owned_handle(rstd::move(handle).unwrap());
#else
            auto endpoint = pipe::Receiver::from_owned_fd(rstd::move(*descriptor).into_owned_fd());
#endif
            if (endpoint.is_err()) return Err(rstd::move(endpoint).unwrap_err());
            error = Some(rstd::move(endpoint).unwrap());
        }
        auto adopted = prepared->adopt_native(*native);
        if (adopted.is_err()) return Err(rstd::move(adopted).unwrap_err());
        rollback.active = false;
        {
            auto state         = adopted->state_->fields.lock().unwrap();
            state->stdin_pipe  = rstd::move(input);
            state->stdout_pipe = rstd::move(output);
            state->stderr_pipe = rstd::move(error);
        }
        return adopted;
#else
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
    // The caller retains a native child's wait/reap right when this returns an error.
    auto adopt(i32 pid) const -> io::Result<Child> {
        auto prepared = prepare();
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
        return prepared->adopt(pid);
    }
};

auto supervise_processes(ProcessTask task) -> coro<void> {
    auto& state = task.state;
    for (;;) {
        auto                       notice = state->changed.notified();
        Vec<sync::Arc<ChildState>> children;
        {
            auto fields = state->fields.lock().unwrap();
            for (const auto& child : fields->children) children.push(child.clone());
        }
        for (const auto& child : children) {
            if (! child->poll_exit()) continue;
            auto fields = state->fields.lock().unwrap();
            for (usize i {}; i < fields->children.len(); ++i)
                if (fields->children[i].as_ptr().as_raw_ptr() == child.as_ptr().as_raw_ptr()) {
                    fields->children.remove(i);
                    break;
                }
        }
        bool finished;
        {
            auto fields = state->fields.lock().unwrap();
            finished =
                fields->stopping && fields->children.is_empty() && fields->reservations == usize();
        }
        if (finished) {
            state->finish();
            co_return;
        }
#if RSTD_OS_UNIX
        auto signal = async::signal::recv(state->signals);
        if (signal.is_err()) {
            state->stop();
            state->finish(Some(rstd::move(signal).unwrap_err()));
            co_return;
        }
        auto selected = co_await async::select(rstd::move(signal).unwrap(), rstd::move(notice));
        Option<io::Error> error;
        if (selected.is_left()) {
            auto result = rstd::move(selected).unwrap_left();
            if (result.is_err())
                error = Some(rstd::move(result).unwrap_err());
            else if (result->is_none())
                error = Some(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        } else {
            auto result = rstd::move(selected).unwrap_right();
            if (result.is_err()) error = Some(rstd::move(result).unwrap_err());
        }
#else
        auto              result = co_await rstd::move(notice);
        Option<io::Error> error;
        if (result.is_err()) error = Some(rstd::move(result).unwrap_err());
#endif
        if (error.is_some()) {
            state->stop();
            state->finish(rstd::move(error));
            co_return;
        }
    }
}

export class ProcessSupervisor {
    sync::Arc<ProcessState> state_;
    explicit ProcessSupervisor(sync::Arc<ProcessState> state): state_(rstd::move(state)) {
        (void)async::spawn(supervise_processes(ProcessTask { state_.clone() }));
    }
    static auto wait_shutdown_owned(sync::Arc<ProcessState> state) -> coro<io::Result<empty>> {
        state->stop();
        auto  pair = oneshot::channel<empty>();
        usize id;
        {
            auto fields = state->fields.lock().unwrap();
            if (fields->finished) {
                co_return Ok(empty {});
            }
            id = fields->next_waiter++;
            fields->waiters.push(ProcessWaiter { id, rstd::move(pair.template get<0>()) });
        }
        ProcessWaitRegistration pending { state.clone(), id };
        auto                    result = co_await rstd::move(pair.template get<1>());
        if (result.is_err())
            co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted }));
        co_return Ok(empty {});
    }
    static auto shutdown_owned(sync::Arc<ProcessState> state) -> coro<io::Result<empty>> {
        auto waited = co_await wait_shutdown_owned(state.clone());
        if (waited.is_err()) co_return Err(rstd::move(waited).unwrap_err());
        auto result = state->fields.lock().unwrap()->result.take();
        if (! result)
            co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        co_return result.take().unwrap();
    }

public:
    ProcessSupervisor(const ProcessSupervisor&)                        = delete;
    ProcessSupervisor(ProcessSupervisor&&) noexcept                    = default;
    auto operator=(ProcessSupervisor&&) noexcept -> ProcessSupervisor& = delete;
    ~ProcessSupervisor() {
        if (state_) state_->stop();
    }
    // Requires an I/O-enabled runtime. Unix subscribes to child signals before spawning.
    static auto make() -> io::Result<ProcessSupervisor> {
        if (CURRENT_RUNTIME == nullptr || ! has_current_runtime_worker())
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        if (! CURRENT_RUNTIME->io_enabled())
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
        auto worker = CURRENT_RUNTIME->current_poll_worker();
        if (worker.is_err()) return Err(rstd::move(worker).unwrap_err());
#if RSTD_OS_UNIX
        if (! worker->poll_capabilities().contains(PollCapability::Readiness))
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
        auto kind = rstd::signal::unix::child();
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
        auto signals = rstd::signal::subscribe(*kind);
        if (signals.is_err()) return Err(rstd::move(signals).unwrap_err());
        auto notice = Notify::make();
        if (notice.is_err()) return Err(rstd::move(notice).unwrap_err());
        return Ok(ProcessSupervisor { sync::Arc<ProcessState>::make(
            rstd::move(notice).unwrap(), rstd::move(signals).unwrap()) });
#elif RSTD_OS_WINDOWS
        if (! worker->poll_capabilities().contains(PollCapability::FileCompletion))
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
        auto notice = Notify::make();
        if (notice.is_err()) return Err(rstd::move(notice).unwrap_err());
        return Ok(ProcessSupervisor { sync::Arc<ProcessState>::make(rstd::move(notice).unwrap()) });
#else
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
    auto handle() const -> ProcessHandle { return ProcessHandle { state_.clone() }; }
    void stop_accepting() const { state_->fields.lock().unwrap()->accepting = false; }
    void start_shutdown() const { state_->stop(); }
    // Requires the owning runtime to keep running. Each wait owns its state and may be canceled.
    auto shutdown() const -> coro<io::Result<empty>> { return shutdown_owned(state_.clone()); }
    auto wait_shutdown() const -> coro<io::Result<empty>> {
        return wait_shutdown_owned(state_.clone());
    }
    auto take_shutdown_result() -> Option<io::Result<empty>> {
        return state_->fields.lock().unwrap()->result.take();
    }
};
} // namespace rstd::async
