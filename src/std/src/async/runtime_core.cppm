module;
#include <rstd/macro.hpp>

export module rstd:async.runtime_core;
import :async.awaitable;
import :async.blocking_pool;
import :async.facility;
import :async.forward;
import :async.poll;
import :env;
import rstd.alloc;
import :sync;
import :thread;

using namespace rstd;
using namespace rstd::literals;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using AsyncPoll = rstd::async::Poll;
using rstd::async::PollApplyStatus;
using rstd::async::PollBatch;
using rstd::async::PollCapabilities;
using rstd::async::PollCommand;
using rstd::async::PollEventData;
using rstd::async::PollEventKind;
using rstd::async::PollEventOwner;
using rstd::async::OperationKey;
using rstd::async::RegistrationKey;
using rstd::async::SourceKey;
using rstd::async::TimerKey;
using rstd::async::IoBackendPreference;
using rstd::async::PollState;
using rstd::async::PollTimeout;
using rstd::async::PollWake;

struct RuntimeInner;
struct TaskStateBase;
class TaskRefControl;
class RuntimeWorker;

class TaskAccess {
    TaskRefControl* m_control { nullptr };
    TaskStateBase*  m_task { nullptr };

    TaskAccess(TaskRefControl* control, TaskStateBase* task) noexcept
        : m_control(control), m_task(task) {}

    friend class TaskRefControl;

public:
    TaskAccess() noexcept                            = default;
    TaskAccess(const TaskAccess&)                    = delete;
    auto operator=(const TaskAccess&) -> TaskAccess& = delete;
    TaskAccess(TaskAccess&& other) noexcept
        : m_control(rstd::exchange(other.m_control, nullptr)),
          m_task(rstd::exchange(other.m_task, nullptr)) {}
    auto operator=(TaskAccess&& other) noexcept -> TaskAccess&;
    ~TaskAccess();

    auto     get() const noexcept -> TaskStateBase* { return m_task; }
    auto     operator->() const noexcept -> TaskStateBase* { return m_task; }
    explicit operator bool() const noexcept { return m_task != nullptr; }
};

class TaskRefControl {
public:
    using DestroyFn = void (*)(voidp);

private:
    static constexpr usize DETACHED { rstd::size_t(1) };
    static constexpr usize ACCESS { rstd::size_t(2) };

    rstd::sync::atomic::Atomic<usize> m_strong { usize(1) };
    rstd::sync::atomic::Atomic<usize> m_access_state { usize() };
    TaskStateBase*                    m_task { nullptr };
    voidp                             m_storage_owner { nullptr };
    DestroyFn                         m_destroy_storage { nullptr };

    static void destroy_scoped_control(voidp owner) { delete static_cast<TaskRefControl*>(owner); }

public:
    TaskRefControl(voidp storage_owner, DestroyFn destroy_storage) noexcept
        : m_storage_owner(storage_owner), m_destroy_storage(destroy_storage) {}

    TaskRefControl(const TaskRefControl&)                    = delete;
    auto operator=(const TaskRefControl&) -> TaskRefControl& = delete;

    static auto make_scoped() -> TaskRefControl* {
        auto* control            = new TaskRefControl { nullptr, &destroy_scoped_control };
        control->m_storage_owner = control;
        return control;
    }

    void attach(TaskStateBase* task) {
        if (m_task != nullptr) {
            rstd::panic { "TaskRefControl attached more than once" };
        }
        m_task = task;
    }

    void inc_ref() noexcept {
        auto previous = m_strong.fetch_add(usize(1), rstd::sync::atomic::Ordering::Relaxed);
        if (previous >= usize::MAX / usize(2)) {
            rstd::panic { "TaskRefControl strong count overflow" };
        }
    }

    void dec_ref() noexcept {
        if (m_strong.fetch_sub(usize(1), rstd::sync::atomic::Ordering::AcqRel) == usize(1)) {
            rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
            m_destroy_storage(m_storage_owner);
        }
    }

    auto acquire() -> Option<TaskAccess> {
        inc_ref();
        auto state = m_access_state.load(rstd::sync::atomic::Ordering::Acquire);
        while ((state & DETACHED) == usize()) {
            if (state > usize::MAX - ACCESS) {
                rstd::panic { "TaskRefControl access count overflow" };
            }
            if (m_access_state.compare_exchange_weak(state,
                                                     state + ACCESS,
                                                     rstd::sync::atomic::Ordering::AcqRel,
                                                     rstd::sync::atomic::Ordering::Acquire)) {
                return Some(TaskAccess { this, m_task });
            }
        }
        dec_ref();
        return None();
    }

    void release_access() noexcept {
        m_access_state.fetch_sub(ACCESS, rstd::sync::atomic::Ordering::Release);
    }

    void detach() {
        m_access_state.fetch_or(DETACHED, rstd::sync::atomic::Ordering::AcqRel);
        while ((m_access_state.load(rstd::sync::atomic::Ordering::Acquire) & ~DETACHED) !=
               usize()) {
            thread::yield_now();
        }
    }
};

struct RuntimeWorkerId {
    usize index {};

    static constexpr auto current_thread() noexcept -> RuntimeWorkerId {
        return RuntimeWorkerId { usize() };
    }

    constexpr auto as_usize() const noexcept -> usize { return index; }

    friend constexpr auto operator==(RuntimeWorkerId, RuntimeWorkerId) noexcept -> bool = default;
};

enum class TaskLifecycle
{
    Created,
    Queued,
    RunningRuntime,
    Waiting,
    FacilityQueued,
    FacilityRunning,
    Completed,
};

struct FacilityId {
    rstd::uintptr_t value {};

    friend constexpr auto operator==(FacilityId, FacilityId) noexcept -> bool = default;
};

struct TaskControl {
    RuntimeWorkerId owner_worker {};
    TaskLifecycle   lifecycle { TaskLifecycle::Created };
    u64             schedule_generation {};
    FacilityId      facility_id {};
    u64             facility_generation {};
    bool            wake_requested { false };
    bool            cancel_requested { false };
};

inline thread_local RuntimeInner*   CURRENT_RUNTIME { nullptr };
inline thread_local RuntimeWorkerId CURRENT_RUNTIME_WORKER {};
inline thread_local bool            CURRENT_RUNTIME_WORKER_ACTIVE { false };
inline thread_local RuntimeWorker*  CURRENT_RUNTIME_WORKER_CONTEXT { nullptr };

inline thread_local async::ExecutionDomainKind CURRENT_EXECUTION_DOMAIN {
    async::ExecutionDomainKind::RuntimeWorker
};

inline auto current_runtime_worker_id() noexcept -> RuntimeWorkerId {
    return CURRENT_RUNTIME_WORKER;
}

inline auto has_current_runtime_worker() noexcept -> bool {
    return CURRENT_RUNTIME_WORKER_ACTIVE;
}

inline auto current_execution_domain() noexcept -> async::ExecutionDomainKind {
    return CURRENT_EXECUTION_DOMAIN;
}

enum class RuntimeKind
{
    CurrentThread,
    ThreadPool,
};

enum class RuntimeLifecycle
{
    Building,
    Running,
    Stopping,
    Stopped,
};

enum class WorkerLifecycle
{
    Created,
    Starting,
    Running,
    Draining,
    Stopped,
};

enum class WorkerAttachResult
{
    Attached,
    Busy,
    WrongThread,
    Closed,
};

auto default_io_backend_preference() -> IoBackendPreference {
#if RSTD_OS_LINUX
    if (rstd::env::var_os("RSTD_ASYNC_DISABLE_IO_URING"_str).is_some()) {
        return IoBackendPreference::ReadinessEmulationRequired;
    }
#endif
    return IoBackendPreference::Auto;
}

struct RuntimeConfig {
    static constexpr usize DEFAULT_MAX_BLOCKING_THREADS { rstd::size_t(512) };

    bool                enable_io;
    bool                enable_time;
    IoBackendPreference io_backend;
    usize               max_blocking_threads;

    static auto disabled() -> RuntimeConfig {
        return RuntimeConfig {
            false, false, default_io_backend_preference(), DEFAULT_MAX_BLOCKING_THREADS
        };
    }

    static auto all() -> RuntimeConfig {
        return RuntimeConfig {
            true, true, default_io_backend_preference(), DEFAULT_MAX_BLOCKING_THREADS
        };
    }
};

auto blocking_thread_name(Option<String> name) -> Option<String> {
    if (name.is_none()) return Some(String::make("rstd-blocking"_str));
    name->push_str("-blocking"_str);
    return name;
}

class TaskRef {
    TaskRefControl* control { nullptr };

    explicit TaskRef(TaskRefControl* control) noexcept: control(control) {}

public:
    TaskRef() noexcept = default;

    static auto adopt(TaskRefControl* control) noexcept -> TaskRef { return TaskRef { control }; }

    TaskRef(const TaskRef&)            = delete;
    TaskRef& operator=(const TaskRef&) = delete;

    TaskRef(TaskRef&& other) noexcept: control(rstd::exchange(other.control, nullptr)) {}

    auto operator=(TaskRef&& other) noexcept -> TaskRef&;
    ~TaskRef();

    auto clone() const noexcept -> TaskRef;
    void reset() noexcept;
    void schedule() const;
    auto access() const -> Option<TaskAccess>;

    auto     identity() const noexcept -> TaskRefControl* { return control; }
    explicit operator bool() const noexcept { return control != nullptr; }

    void abort() const;
};

class ScheduleTicket {
    TaskRef         m_task;
    RuntimeWorkerId m_owner;
    u64             m_generation {};

public:
    ScheduleTicket() noexcept = default;

    ScheduleTicket(TaskRef task, RuntimeWorkerId owner, u64 generation)
        : m_task(rstd::move(task)), m_owner(owner), m_generation(generation) {}

    ScheduleTicket(const ScheduleTicket&)                        = delete;
    ScheduleTicket& operator=(const ScheduleTicket&)             = delete;
    ScheduleTicket(ScheduleTicket&&) noexcept                    = default;
    auto operator=(ScheduleTicket&&) noexcept -> ScheduleTicket& = default;

    auto owner() const noexcept -> RuntimeWorkerId { return m_owner; }
    auto generation() const noexcept -> u64 { return m_generation; }
    auto access_task() const -> Option<TaskAccess> { return m_task.access(); }
    auto take_task() -> TaskRef { return rstd::move(m_task); }
};

class RuntimeExecutionLease {
    TaskRef    m_task;
    TaskAccess m_access;
    u64        m_generation {};

public:
    RuntimeExecutionLease() noexcept = default;

    RuntimeExecutionLease(TaskRef task, TaskAccess access, u64 generation)
        : m_task(rstd::move(task)), m_access(rstd::move(access)), m_generation(generation) {}

    RuntimeExecutionLease(const RuntimeExecutionLease&)                        = delete;
    RuntimeExecutionLease& operator=(const RuntimeExecutionLease&)             = delete;
    RuntimeExecutionLease(RuntimeExecutionLease&&) noexcept                    = default;
    auto operator=(RuntimeExecutionLease&&) noexcept -> RuntimeExecutionLease& = default;

    auto generation() const noexcept -> u64 { return m_generation; }
    auto task() noexcept -> TaskRef& { return m_task; }
    auto state() noexcept -> TaskStateBase& { return *m_access.get(); }
    auto take_task() -> TaskRef { return rstd::move(m_task); }
};

enum class FacilityProgress
{
    RuntimeDriven,
    ExternallyDriven,
};

enum class FacilityEffect
{
    CompletionOnly,
    ExecuteTaskSegment,
};

struct FacilityToken {
    FacilityId      facility_id;
    RuntimeWorkerId owner_worker;
    u64             generation {};
    FacilityEffect  effect { FacilityEffect::CompletionOnly };
};

struct RawFacilityCancellationVTable {
    using CancelFn = void (*)(voidp);
    using DropFn   = void (*)(voidp);

    CancelFn cancel;
    DropFn   drop;
};

struct RawFacilityCancellation {
    voidp                                data { nullptr };
    const RawFacilityCancellationVTable* vtable { nullptr };

    static auto from_raw_parts(voidp data, const RawFacilityCancellationVTable* vtable) noexcept
        -> RawFacilityCancellation {
        return RawFacilityCancellation { data, vtable };
    }
};

class FacilityCancellation {
    FacilityToken           m_token;
    RawFacilityCancellation m_raw;

public:
    FacilityCancellation() = default;

    FacilityCancellation(const FacilityCancellation&)                    = delete;
    auto operator=(const FacilityCancellation&) -> FacilityCancellation& = delete;

    FacilityCancellation(FacilityCancellation&& other) noexcept
        : m_token(other.m_token), m_raw(rstd::exchange(other.m_raw, {})) {}

    auto operator=(FacilityCancellation&& other) noexcept -> FacilityCancellation& {
        if (this != &other) {
            cancel();
            m_token = other.m_token;
            m_raw   = rstd::exchange(other.m_raw, {});
        }
        return *this;
    }

    ~FacilityCancellation() { cancel(); }

    static auto from_raw(FacilityToken token, RawFacilityCancellation raw) -> FacilityCancellation {
        auto cancellation    = FacilityCancellation {};
        cancellation.m_token = token;
        cancellation.m_raw   = raw;
        return cancellation;
    }

    auto token() const noexcept -> FacilityToken { return m_token; }

    void cancel() {
        auto raw = rstd::exchange(m_raw, {});
        if (raw.vtable != nullptr) {
            raw.vtable->cancel(raw.data);
        }
    }

    void disarm() {
        auto raw = rstd::exchange(m_raw, {});
        if (raw.vtable != nullptr) {
            raw.vtable->drop(raw.data);
        }
    }
};

class FacilityRequest {
    FacilityProgress        m_progress { FacilityProgress::ExternallyDriven };
    FacilityEffect          m_effect { FacilityEffect::ExecuteTaskSegment };
    FacilityId              m_id;
    async::FacilityEndpoint m_endpoint;

    explicit FacilityRequest(async::FacilityEndpoint endpoint)
        : m_id(FacilityId { endpoint.id() }), m_endpoint(rstd::move(endpoint)) {}

public:
    FacilityRequest() = default;

    static auto execution(async::FacilityEndpoint endpoint) -> FacilityRequest {
        return FacilityRequest { rstd::move(endpoint) };
    }

    auto progress() const noexcept -> FacilityProgress { return m_progress; }
    auto effect() const noexcept -> FacilityEffect { return m_effect; }
    auto id() const noexcept -> FacilityId { return m_id; }
    auto take_endpoint() -> async::FacilityEndpoint { return rstd::move(m_endpoint); }
};

enum class FacilitySubmitResult
{
    Accepted,
    Rejected,
    Unsupported,
};

class FacilityExecutionToken;

class FacilityTicket {
    TaskRef       m_task;
    FacilityToken m_token;

public:
    FacilityTicket() = default;

    FacilityTicket(TaskRef task, FacilityToken token): m_task(rstd::move(task)), m_token(token) {}

    FacilityTicket(const FacilityTicket&)                        = delete;
    FacilityTicket& operator=(const FacilityTicket&)             = delete;
    FacilityTicket(FacilityTicket&&) noexcept                    = default;
    auto operator=(FacilityTicket&&) noexcept -> FacilityTicket& = default;

    auto token() const noexcept -> FacilityToken { return m_token; }
    auto access_task() const -> Option<TaskAccess> { return m_task.access(); }
    auto into_execution_token() -> FacilityExecutionToken;
};

class FacilityExecutionLease {
    TaskRef       m_task;
    TaskAccess    m_access;
    FacilityToken m_token;

public:
    FacilityExecutionLease(TaskRef task, TaskAccess access, FacilityToken token)
        : m_task(rstd::move(task)), m_access(rstd::move(access)), m_token(token) {}

    FacilityExecutionLease(const FacilityExecutionLease&)                        = delete;
    FacilityExecutionLease& operator=(const FacilityExecutionLease&)             = delete;
    FacilityExecutionLease(FacilityExecutionLease&&) noexcept                    = default;
    auto operator=(FacilityExecutionLease&&) noexcept -> FacilityExecutionLease& = default;

    auto token() const noexcept -> FacilityToken { return m_token; }
    auto task() noexcept -> TaskRef& { return m_task; }
    auto state() noexcept -> TaskStateBase& { return *m_access.get(); }
    auto take_task() -> TaskRef { return rstd::move(m_task); }
};

class FacilityExecutionToken {
    TaskRef       m_task;
    FacilityToken m_token;

    friend class FacilityTicket;

    FacilityExecutionToken(TaskRef task, FacilityToken token)
        : m_task(rstd::move(task)), m_token(token) {}

public:
    FacilityExecutionToken(const FacilityExecutionToken&)                        = delete;
    FacilityExecutionToken& operator=(const FacilityExecutionToken&)             = delete;
    FacilityExecutionToken(FacilityExecutionToken&&) noexcept                    = default;
    auto operator=(FacilityExecutionToken&&) noexcept -> FacilityExecutionToken& = default;

    auto token() const noexcept -> FacilityToken { return m_token; }
    auto access_task() const -> Option<TaskAccess> { return m_task.access(); }
    auto take_task() -> TaskRef { return rstd::move(m_task); }
};

inline auto FacilityTicket::into_execution_token() -> FacilityExecutionToken {
    return FacilityExecutionToken { rstd::move(m_task), m_token };
}

namespace rstd::async
{

class FacilityJob {
    ::FacilityExecutionToken m_token;

public:
    explicit FacilityJob(::FacilityExecutionToken token): m_token(rstd::move(token)) {}

    FacilityJob(const FacilityJob&)                    = delete;
    auto operator=(const FacilityJob&) -> FacilityJob& = delete;
    FacilityJob(FacilityJob&&) noexcept                = default;
    auto operator=(FacilityJob&& other) noexcept -> FacilityJob&;
    ~FacilityJob();

    void run();
    void cancel();
};

inline auto FacilityEndpoint::submit(FacilityJob job) -> FacilityEndpointSubmitResult {
    auto raw = rstd::exchange(m_raw, {});
    if (raw.vtable == nullptr) {
        job.cancel();
        return FacilityEndpointSubmitResult::Unsupported;
    }
    auto result = raw.vtable->submit(raw.data, rstd::move(job));
    raw.vtable->drop(raw.data);
    return result;
}

} // namespace rstd::async

enum class FacilityEventKind
{
    Ready,
    Error,
    Canceled,
    ExecutionReturned,
};

class FacilityEvent;

class FacilityCompletionToken {
    TaskRef       m_task;
    FacilityToken m_token;

public:
    FacilityCompletionToken() = default;

    FacilityCompletionToken(TaskRef task, FacilityToken token)
        : m_task(rstd::move(task)), m_token(token) {}

    FacilityCompletionToken(const FacilityCompletionToken&)                        = delete;
    FacilityCompletionToken& operator=(const FacilityCompletionToken&)             = delete;
    FacilityCompletionToken(FacilityCompletionToken&&) noexcept                    = default;
    auto operator=(FacilityCompletionToken&&) noexcept -> FacilityCompletionToken& = default;

    auto token() const noexcept -> FacilityToken { return m_token; }
    auto access_task() const -> Option<TaskAccess> { return m_task.access(); }
    auto into_event(FacilityEventKind kind) -> FacilityEvent;
    auto into_poll_event(PollEventData event) -> FacilityEvent;
    auto complete(FacilityEventKind kind) -> bool;
    auto complete_poll(PollEventData event) -> bool;
};

class FacilityCompletionSubmitResult {
    FacilitySubmitResult            m_status { FacilitySubmitResult::Accepted };
    Option<FacilityCompletionToken> m_token;
    Option<FacilityCancellation>    m_cancellation;

    FacilityCompletionSubmitResult(FacilitySubmitResult            status,
                                   Option<FacilityCompletionToken> token,
                                   Option<FacilityCancellation>    cancellation)
        : m_status(status), m_token(rstd::move(token)), m_cancellation(rstd::move(cancellation)) {}

public:
    static auto accepted(FacilityCancellation cancellation) -> FacilityCompletionSubmitResult {
        return FacilityCompletionSubmitResult { FacilitySubmitResult::Accepted,
                                                None(),
                                                Some(rstd::move(cancellation)) };
    }

    static auto rejected(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        return FacilityCompletionSubmitResult { FacilitySubmitResult::Rejected,
                                                Some(rstd::move(token)),
                                                None() };
    }

    static auto unsupported(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        return FacilityCompletionSubmitResult { FacilitySubmitResult::Unsupported,
                                                Some(rstd::move(token)),
                                                None() };
    }

    auto status() const noexcept -> FacilitySubmitResult { return m_status; }
    auto take_token() -> FacilityCompletionToken { return m_token.take().unwrap_unchecked(); }
    auto take_cancellation() -> FacilityCancellation {
        return m_cancellation.take().unwrap_unchecked();
    }
};

class FacilityEvent {
    TaskRef               m_task;
    FacilityToken         m_token;
    FacilityEventKind     m_kind { FacilityEventKind::Ready };
    Option<PollEventData> m_poll_event;

public:
    FacilityEvent() = default;

    FacilityEvent(TaskRef task, FacilityToken token, FacilityEventKind kind)
        : m_task(rstd::move(task)), m_token(token), m_kind(kind), m_poll_event(None()) {}

    FacilityEvent(TaskRef task, FacilityToken token, PollEventData event)
        : m_task(rstd::move(task)),
          m_token(token),
          m_kind(event.kind() == PollEventKind::BackendError ? FacilityEventKind::Error
                                                             : FacilityEventKind::Ready),
          m_poll_event(Some(rstd::move(event))) {}

    FacilityEvent(const FacilityEvent&)                        = delete;
    FacilityEvent& operator=(const FacilityEvent&)             = delete;
    FacilityEvent(FacilityEvent&&) noexcept                    = default;
    auto operator=(FacilityEvent&&) noexcept -> FacilityEvent& = default;

    auto token() const noexcept -> FacilityToken { return m_token; }
    auto kind() const noexcept -> FacilityEventKind { return m_kind; }
    auto access_task() const -> Option<TaskAccess> { return m_task.access(); }
    auto take_task() -> TaskRef { return rstd::move(m_task); }
    auto has_poll_event() const noexcept -> bool { return m_poll_event.is_some(); }
    auto take_poll_event() -> PollEventData { return m_poll_event.take().unwrap_unchecked(); }
};

inline auto FacilityCompletionToken::into_event(FacilityEventKind kind) -> FacilityEvent {
    return FacilityEvent { rstd::move(m_task), m_token, kind };
}

inline auto FacilityCompletionToken::into_poll_event(PollEventData event) -> FacilityEvent {
    return FacilityEvent { rstd::move(m_task), m_token, rstd::move(event) };
}

class FacilityEventBatch {
    RuntimeWorkerId    m_owner;
    Vec<FacilityEvent> m_events;

public:
    FacilityEventBatch(): m_owner(), m_events(Vec<FacilityEvent>::make()) {}

    FacilityEventBatch(RuntimeWorkerId owner, Vec<FacilityEvent> events)
        : m_owner(owner), m_events(rstd::move(events)) {}

    FacilityEventBatch(const FacilityEventBatch&)                        = delete;
    FacilityEventBatch& operator=(const FacilityEventBatch&)             = delete;
    FacilityEventBatch(FacilityEventBatch&&) noexcept                    = default;
    auto operator=(FacilityEventBatch&&) noexcept -> FacilityEventBatch& = default;

    auto is_empty() const -> bool { return m_events.is_empty(); }
    auto owner_worker() const noexcept -> RuntimeWorkerId { return m_owner; }

    auto pop_front() -> Option<FacilityEvent> {
        if (m_events.is_empty()) {
            return None();
        }
        return Some(m_events.remove(usize()));
    }
};

enum class TaskActionKind
{
    None,
    Schedule,
    SubmitCompletion,
    SubmitFacility,
    CompleteValue,
    CompleteAbort,
};

class TaskAction {
    TaskActionKind                  m_kind { TaskActionKind::None };
    ScheduleTicket                  m_ticket;
    FacilityTicket                  m_facility_ticket;
    FacilityRequest                 m_facility_request;
    Option<FacilityCompletionToken> m_completion_token;
    Option<FacilityCancellation>    m_cancellation;
    TaskRef                         m_terminal_task;

    explicit TaskAction(TaskActionKind kind)
        : m_kind(kind),
          m_ticket(),
          m_facility_ticket(),
          m_facility_request(),
          m_completion_token(None()),
          m_cancellation(None()),
          m_terminal_task() {}

    explicit TaskAction(ScheduleTicket ticket)
        : m_kind(TaskActionKind::Schedule),
          m_ticket(rstd::move(ticket)),
          m_facility_ticket(),
          m_facility_request(),
          m_completion_token(None()),
          m_cancellation(None()),
          m_terminal_task() {}

    TaskAction(FacilityTicket ticket, FacilityRequest request)
        : m_kind(TaskActionKind::SubmitFacility),
          m_ticket(),
          m_facility_ticket(rstd::move(ticket)),
          m_facility_request(rstd::move(request)),
          m_completion_token(None()),
          m_cancellation(None()),
          m_terminal_task() {}

    explicit TaskAction(FacilityCompletionToken token)
        : m_kind(TaskActionKind::SubmitCompletion),
          m_ticket(),
          m_facility_ticket(),
          m_facility_request(),
          m_completion_token(Some(rstd::move(token))),
          m_cancellation(None()),
          m_terminal_task() {}

    TaskAction(TaskActionKind kind, TaskRef task, Option<FacilityCancellation> cancellation)
        : m_kind(kind),
          m_ticket(),
          m_facility_ticket(),
          m_facility_request(),
          m_completion_token(None()),
          m_cancellation(rstd::move(cancellation)),
          m_terminal_task(rstd::move(task)) {}

public:
    static auto none() noexcept -> TaskAction { return TaskAction { TaskActionKind::None }; }

    static auto schedule(ScheduleTicket ticket) -> TaskAction {
        return TaskAction { rstd::move(ticket) };
    }

    static auto submit_facility(FacilityTicket ticket, FacilityRequest request) -> TaskAction {
        return TaskAction { rstd::move(ticket), rstd::move(request) };
    }

    static auto submit_completion(FacilityCompletionToken token) -> TaskAction {
        return TaskAction { rstd::move(token) };
    }

    static auto complete_value(TaskRef task) -> TaskAction {
        return TaskAction { TaskActionKind::CompleteValue, rstd::move(task), None() };
    }

    static auto complete_abort(TaskRef task, Option<FacilityCancellation> cancellation = None())
        -> TaskAction {
        return TaskAction { TaskActionKind::CompleteAbort,
                            rstd::move(task),
                            rstd::move(cancellation) };
    }

    auto kind() const noexcept -> TaskActionKind { return m_kind; }
    auto take_ticket() -> ScheduleTicket { return rstd::move(m_ticket); }
    auto take_facility_ticket() -> FacilityTicket { return rstd::move(m_facility_ticket); }
    auto take_facility_request() -> FacilityRequest { return rstd::move(m_facility_request); }
    auto take_completion_token() -> FacilityCompletionToken {
        return m_completion_token.take().unwrap_unchecked();
    }
    auto take_cancellation() -> Option<FacilityCancellation> { return m_cancellation.take(); }
    auto take_terminal_task() -> TaskRef { return rstd::move(m_terminal_task); }
};

enum class TaskPollActionKind
{
    None,
    SubmitCompletion,
    SubmitFacility,
    Complete,
};

class TaskPollAction {
    TaskPollActionKind m_kind { TaskPollActionKind::None };
    FacilityRequest    m_request;
    FacilityId         m_completion_id;

    explicit TaskPollAction(TaskPollActionKind kind)
        : m_kind(kind), m_request(), m_completion_id() {}

    explicit TaskPollAction(FacilityRequest request)
        : m_kind(TaskPollActionKind::SubmitFacility),
          m_request(rstd::move(request)),
          m_completion_id() {}

    explicit TaskPollAction(FacilityId id)
        : m_kind(TaskPollActionKind::SubmitCompletion), m_request(), m_completion_id(id) {}

public:
    static auto none() -> TaskPollAction { return TaskPollAction { TaskPollActionKind::None }; }

    static auto submit_facility(async::FacilityEndpoint endpoint) -> TaskPollAction {
        return TaskPollAction { FacilityRequest::execution(rstd::move(endpoint)) };
    }

    static auto submit_completion(FacilityId id) -> TaskPollAction { return TaskPollAction { id }; }

    static auto complete() -> TaskPollAction {
        return TaskPollAction { TaskPollActionKind::Complete };
    }

    auto is_submit_facility() const noexcept -> bool {
        return m_kind == TaskPollActionKind::SubmitFacility;
    }

    auto is_submit_completion() const noexcept -> bool {
        return m_kind == TaskPollActionKind::SubmitCompletion;
    }

    auto is_complete() const noexcept -> bool { return m_kind == TaskPollActionKind::Complete; }

    auto take_request() -> FacilityRequest { return rstd::move(m_request); }
    auto completion_id() const noexcept -> FacilityId { return m_completion_id; }
};

struct ReadyQueue {
    Vec<ScheduleTicket> m_tickets;

    ReadyQueue(): m_tickets(Vec<ScheduleTicket>::make()) {}

    auto is_empty() const -> bool { return m_tickets.is_empty(); }

    void push(ScheduleTicket ticket) { m_tickets.push(rstd::move(ticket)); }

    auto pop_front() -> Option<ScheduleTicket> {
        if (m_tickets.is_empty()) {
            return None();
        }
        return Some(m_tickets.remove(usize()));
    }

    void clear() { m_tickets.clear(); }
};

enum class WorkerCommandKind
{
    Schedule,
    FacilityComplete,
    FacilityBatch,
    Poll,
    Stop,
};

class WorkerCommand {
    WorkerCommandKind          m_kind;
    Option<ScheduleTicket>     m_ticket;
    Option<FacilityEvent>      m_event;
    Option<FacilityEventBatch> m_batch;
    Option<PollCommand>        m_poll;

    explicit WorkerCommand(ScheduleTicket ticket)
        : m_kind(WorkerCommandKind::Schedule),
          m_ticket(Some(rstd::move(ticket))),
          m_event(None()),
          m_batch(None()),
          m_poll(None()) {}

    explicit WorkerCommand(FacilityEvent event)
        : m_kind(WorkerCommandKind::FacilityComplete),
          m_ticket(None()),
          m_event(Some(rstd::move(event))),
          m_batch(None()),
          m_poll(None()) {}

    explicit WorkerCommand(FacilityEventBatch batch)
        : m_kind(WorkerCommandKind::FacilityBatch),
          m_ticket(None()),
          m_event(None()),
          m_batch(Some(rstd::move(batch))),
          m_poll(None()) {}

    explicit WorkerCommand(PollCommand command)
        : m_kind(WorkerCommandKind::Poll),
          m_ticket(None()),
          m_event(None()),
          m_batch(None()),
          m_poll(Some(rstd::move(command))) {}

    explicit WorkerCommand(WorkerCommandKind kind)
        : m_kind(kind), m_ticket(None()), m_event(None()), m_batch(None()), m_poll(None()) {}

public:
    static auto schedule(ScheduleTicket ticket) -> WorkerCommand {
        return WorkerCommand { rstd::move(ticket) };
    }

    static auto facility_complete(FacilityEvent event) -> WorkerCommand {
        return WorkerCommand { rstd::move(event) };
    }

    static auto facility_batch(FacilityEventBatch batch) -> WorkerCommand {
        return WorkerCommand { rstd::move(batch) };
    }

    static auto poll(PollCommand command) -> WorkerCommand {
        return WorkerCommand { rstd::move(command) };
    }

    static auto stop() -> WorkerCommand { return WorkerCommand { WorkerCommandKind::Stop }; }

    auto kind() const noexcept -> WorkerCommandKind { return m_kind; }

    auto take_ticket() -> ScheduleTicket { return m_ticket.take().unwrap_unchecked(); }
    auto take_event() -> FacilityEvent { return m_event.take().unwrap_unchecked(); }
    auto take_batch() -> FacilityEventBatch { return m_batch.take().unwrap_unchecked(); }
    auto take_poll() -> PollCommand { return m_poll.take().unwrap_unchecked(); }
};

struct WorkerInbox {
    Vec<WorkerCommand> m_commands;

    WorkerInbox(): m_commands(Vec<WorkerCommand>::make()) {}

    auto is_empty() const -> bool { return m_commands.is_empty(); }

    void push(WorkerCommand command) { m_commands.push(rstd::move(command)); }

    auto pop_front() -> Option<WorkerCommand> {
        if (m_commands.is_empty()) {
            return None();
        }
        return Some(m_commands.remove(usize()));
    }

    void clear() { m_commands.clear(); }
};

struct TaskRegistry {
    Vec<TaskRef> m_tasks;

    TaskRegistry(): m_tasks(Vec<TaskRef>::make()) {}

    void insert(TaskRef task) { m_tasks.push(rstd::move(task)); }

    auto is_empty() const -> bool { return m_tasks.is_empty(); }

    auto clone_all() const -> Vec<TaskRef> {
        auto tasks = Vec<TaskRef>::make();
        for (rstd::size_t i = 0; i < m_tasks.len().to_primitive(); ++i) {
            tasks.push(m_tasks[usize(i)].clone());
        }
        return tasks;
    }

    void remove(TaskRefControl* task) {
        for (rstd::size_t i = 0; i < m_tasks.len().to_primitive(); ++i) {
            auto index = usize(i);
            if (m_tasks[index].identity() == task) {
                m_tasks.remove(index);
                return;
            }
        }
    }
};

struct WorkerFields {
    WorkerInbox              m_inbox;
    Option<PollWake>         m_poll_wake;
    PollCapabilities         m_poll_capabilities;
    Option<thread::ThreadId> m_attached_thread;
    WorkerLifecycle          m_lifecycle { WorkerLifecycle::Created };
    bool                     m_attached { false };
    bool                     m_pending_wake { false };
    u32                      m_next_operation_key {};
    u32                      m_next_registration_key {};
    u32                      m_next_timer_key {};
    u32                      m_next_source_key {};

    WorkerFields(): m_inbox(), m_poll_wake(None()), m_attached_thread(None()) {}
};

struct WorkerState {
    sync::Mutex<WorkerFields> m_fields;

    WorkerState(): m_fields(WorkerFields {}) {}
};

class WorkerHandle {
    RuntimeWorkerId        m_id;
    sync::Arc<WorkerState> m_state;

    WorkerHandle(RuntimeWorkerId id, sync::Arc<WorkerState> state)
        : m_id(id), m_state(rstd::move(state)) {}

    static void notify_locked(WorkerFields& fields) {
        if (fields.m_poll_wake.is_some()) {
            (void)fields.m_poll_wake->wake();
        } else {
            fields.m_pending_wake = true;
        }
    }

public:
    static auto make(RuntimeWorkerId id) -> WorkerHandle {
        return WorkerHandle { id, sync::Arc<WorkerState>::make() };
    }

    WorkerHandle(const WorkerHandle&)                        = delete;
    WorkerHandle& operator=(const WorkerHandle&)             = delete;
    WorkerHandle(WorkerHandle&&) noexcept                    = default;
    auto operator=(WorkerHandle&&) noexcept -> WorkerHandle& = default;

    auto clone() const -> WorkerHandle { return WorkerHandle { m_id, m_state.clone() }; }

    auto id() const noexcept -> RuntimeWorkerId { return m_id; }

    auto is_same(const WorkerHandle& other) const noexcept -> bool {
        return m_id == other.m_id && sync::Arc<WorkerState>::ptr_eq(m_state, other.m_state);
    }

    auto attach(thread::ThreadId id) const -> WorkerAttachResult {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Running) {
            return WorkerAttachResult::Closed;
        }
        if (fields->m_attached_thread.is_some() && *fields->m_attached_thread != id) {
            return WorkerAttachResult::WrongThread;
        }
        if (fields->m_attached) {
            return WorkerAttachResult::Busy;
        }
        if (fields->m_attached_thread.is_none()) {
            fields->m_attached_thread = Some(id);
        }
        fields->m_attached = true;
        return WorkerAttachResult::Attached;
    }

    void detach(thread::ThreadId id) const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (! fields->m_attached || fields->m_attached_thread != Some(id)) {
            rstd::panic { "async worker detached from a thread that does not own it" };
        }
        fields->m_attached = false;
    }

    void start_current() const {
        auto fields         = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_lifecycle = WorkerLifecycle::Running;
    }

    auto begin_start() const -> bool {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Created) {
            return false;
        }
        fields->m_lifecycle = WorkerLifecycle::Starting;
        return true;
    }

    auto finish_start() const -> bool {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Starting) {
            return false;
        }
        fields->m_lifecycle = WorkerLifecycle::Running;
        return true;
    }

    auto schedule(ScheduleTicket ticket) const -> Result<empty, ScheduleTicket> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Running) {
            return Err(rstd::move(ticket));
        }
        fields->m_inbox.push(WorkerCommand::schedule(rstd::move(ticket)));
        notify_locked(*fields);
        return Ok(empty {});
    }

    auto complete_facility(FacilityEvent event) const -> Result<empty, FacilityEvent> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Running) {
            return Err(rstd::move(event));
        }
        fields->m_inbox.push(WorkerCommand::facility_complete(rstd::move(event)));
        notify_locked(*fields);
        return Ok(empty {});
    }

    auto complete_facility_batch(FacilityEventBatch batch) const
        -> Result<empty, FacilityEventBatch> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Running) {
            return Err(rstd::move(batch));
        }
        fields->m_inbox.push(WorkerCommand::facility_batch(rstd::move(batch)));
        notify_locked(*fields);
        return Ok(empty {});
    }

    auto submit_poll(PollCommand command) const -> Result<empty, PollCommand> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Running) {
            return Err(rstd::move(command));
        }
        fields->m_inbox.push(WorkerCommand::poll(rstd::move(command)));
        notify_locked(*fields);
        return Ok(empty {});
    }

    auto allocate_operation_key() const -> OperationKey {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_next_operation_key == u32::MAX) {
            rstd::panic { "async worker exhausted operation keys" };
        }
        return OperationKey { fields->m_next_operation_key++, u32(1) };
    }

    auto allocate_registration_key() const -> RegistrationKey {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_next_registration_key == u32::MAX) {
            rstd::panic { "async worker exhausted registration keys" };
        }
        return RegistrationKey { fields->m_next_registration_key++, u32(1) };
    }

    auto allocate_timer_key() const -> TimerKey {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_next_timer_key == u32::MAX) {
            rstd::panic { "async worker exhausted timer keys" };
        }
        return TimerKey { fields->m_next_timer_key++, u32(1) };
    }

    auto allocate_source_key() const -> SourceKey {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_next_source_key == u32::MAX) {
            rstd::panic { "async worker exhausted IO source keys" };
        }
        return SourceKey { fields->m_next_source_key++, u32(1) };
    }

    auto poll_capabilities() const -> PollCapabilities {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        return fields->m_poll_capabilities;
    }

    void wake() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle == WorkerLifecycle::Stopped) {
            return;
        }
        notify_locked(*fields);
    }

    auto pop_command() const -> Option<WorkerCommand> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        return fields->m_inbox.pop_front();
    }

    auto install_poll(PollWake wake, PollCapabilities capabilities) const -> bool {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_poll_wake.is_some()) {
            return false;
        }
        fields->m_poll_wake         = Some(rstd::move(wake));
        fields->m_poll_capabilities = capabilities;
        if (fields->m_pending_wake || ! fields->m_inbox.is_empty()) {
            (void)fields->m_poll_wake->wake();
            fields->m_pending_wake = false;
        }
        return true;
    }

    void clear_poll() const {
        auto fields                 = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_poll_wake         = None();
        fields->m_poll_capabilities = PollCapabilities::none();
    }

    void request_stop() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle == WorkerLifecycle::Stopped ||
            fields->m_lifecycle == WorkerLifecycle::Draining) {
            return;
        }
        if (fields->m_lifecycle == WorkerLifecycle::Running) {
            fields->m_inbox.push(WorkerCommand::stop());
        } else {
            fields->m_lifecycle = WorkerLifecycle::Draining;
        }
        notify_locked(*fields);
    }

    void begin_draining() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_lifecycle != WorkerLifecycle::Stopped) {
            fields->m_lifecycle = WorkerLifecycle::Draining;
        }
    }

    void finish_stop() const {
        auto fields                 = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_lifecycle         = WorkerLifecycle::Stopped;
        fields->m_poll_wake         = None();
        fields->m_poll_capabilities = PollCapabilities::none();
    }

    void clear_inbox() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_inbox.clear();
    }
};

class CurrentThreadWorkerAttachment {
    WorkerHandle     m_handle;
    thread::ThreadId m_thread;

public:
    explicit CurrentThreadWorkerAttachment(WorkerHandle handle)
        : m_handle(rstd::move(handle)), m_thread(thread::current().id()) {
        switch (m_handle.attach(m_thread)) {
        case WorkerAttachResult::Attached: return;
        case WorkerAttachResult::Busy:
            rstd::panic {
                "current-thread runtime does not allow concurrent or reentrant block_on"
            };
        case WorkerAttachResult::WrongThread:
            rstd::panic { "current-thread runtime is bound to a different thread" };
        case WorkerAttachResult::Closed:
            rstd::panic { "current-thread runtime worker is not running" };
        }
        rstd::unreachable();
    }

    CurrentThreadWorkerAttachment(const CurrentThreadWorkerAttachment&)                    = delete;
    auto operator=(const CurrentThreadWorkerAttachment&) -> CurrentThreadWorkerAttachment& = delete;

    ~CurrentThreadWorkerAttachment() { m_handle.detach(m_thread); }
};

struct RuntimeSharedState {
    TaskRegistry      m_registry;
    RuntimeLifecycle  m_lifecycle { RuntimeLifecycle::Building };
    usize             m_next_worker {};
    usize             m_running_workers {};
    Option<io::Error> m_worker_start_error {};
};

class RuntimeShared {
    Vec<WorkerHandle> m_workers;

    auto normalize(RuntimeWorkerId worker) const -> usize {
        return worker.as_usize() % m_workers.len();
    }

public:
    sync::Mutex<RuntimeSharedState> state;
    sync::Condvar                   task_cvar;
    sync::Condvar                   worker_cvar;

    explicit RuntimeShared(usize worker_count, RuntimeKind kind)
        : m_workers(Vec<WorkerHandle>::make()),
          state(RuntimeSharedState {}),
          task_cvar(sync::Condvar::make()),
          worker_cvar(sync::Condvar::make()) {
        for (rstd::size_t i = 0; i < worker_count.to_primitive(); ++i) {
            m_workers.push(WorkerHandle::make(RuntimeWorkerId { usize(i) }));
        }
        if (kind == RuntimeKind::CurrentThread) {
            auto shared               = state.lock().unwrap_unchecked();
            shared->m_lifecycle       = RuntimeLifecycle::Running;
            shared->m_running_workers = usize(1);
            m_workers[usize()].start_current();
        }
    }

    auto worker_count() const -> usize { return m_workers.len(); }

    auto worker(RuntimeWorkerId id) const -> const WorkerHandle& {
        return m_workers[normalize(id)];
    }

    auto worker_handle(RuntimeWorkerId id) const -> WorkerHandle { return worker(id).clone(); }

    auto next_worker_locked(RuntimeSharedState& shared) const -> RuntimeWorkerId {
        auto worker          = RuntimeWorkerId { shared.m_next_worker % m_workers.len() };
        shared.m_next_worker = (shared.m_next_worker + usize(1)) % m_workers.len();
        return worker;
    }

    void request_worker_stop() const {
        for (rstd::size_t i = 0; i < m_workers.len().to_primitive(); ++i) {
            m_workers[usize(i)].request_stop();
        }
    }

    void clear_inbox() const {
        for (rstd::size_t i = 0; i < m_workers.len().to_primitive(); ++i) {
            m_workers[usize(i)].clear_inbox();
        }
    }
};

class RuntimeWorker {
    static constexpr usize DEFAULT_COOPERATIVE_BUDGET { rstd::size_t(64) };

    RuntimeInner*     m_runtime;
    WorkerHandle      m_handle;
    ReadyQueue        m_ready;
    Vec<PollCommand>  m_local_poll_commands;
    Option<PollState> m_poll_state;
    Option<io::Error> m_poll_init_error;
    usize             m_cooperative_budget { DEFAULT_COOPERATIVE_BUDGET };
    bool              m_stop_requested { false };

    void drain_inbox();
    void drain_local_poll();
    void apply_poll(PollCommand command);
    void dispatch_poll_batch(PollBatch batch);
    void poll_backend(PollTimeout timeout);

public:
    RuntimeWorker(RuntimeInner& runtime, WorkerHandle handle);
    ~RuntimeWorker();

    auto poll_initialized() const noexcept -> bool { return m_poll_state.is_some(); }
    auto reserve_operation(PollEventOwner owner) -> io::Result<OperationKey>;
    void abandon_operation(OperationKey key);
    auto defer_poll(PollCommand command) -> Result<empty, PollCommand>;
    auto enqueue_ready(ScheduleTicket ticket) -> Result<empty, ScheduleTicket>;
    auto complete_facility(FacilityEvent event) -> Result<empty, FacilityEvent>;
    auto matches(const WorkerHandle& handle) const noexcept -> bool {
        return m_handle.is_same(handle);
    }
    void drain_ready();
    void wait_for_work();
    void run();
};

struct RuntimeInner {
    RuntimeKind                m_kind;
    RuntimeConfig              m_config;
    async::RuntimeBlockingPool m_blocking_pool;
    RuntimeShared              m_shared;
    sync::Weak<RuntimeInner>   self;

    explicit RuntimeInner(RuntimeKind    kind,
                          RuntimeConfig  config,
                          usize          worker_count,
                          Option<String> thread_name)
        : m_kind(kind),
          m_config(config),
          m_blocking_pool(config.max_blocking_threads,
                          blocking_thread_name(rstd::move(thread_name))),
          m_shared(kind == RuntimeKind::CurrentThread ? usize(1) : worker_count, kind),
          self(sync::Weak<RuntimeInner>::make()) {}

    void init_self(sync::Weak<RuntimeInner> weak) { self = rstd::move(weak); }

    auto weak() -> sync::Weak<RuntimeInner> { return self.clone(); }

    auto is_thread_pool() const -> bool { return m_kind == RuntimeKind::ThreadPool; }

    auto io_enabled() const -> bool { return m_config.enable_io; }
    auto io_backend_preference() const noexcept -> IoBackendPreference {
        return m_config.io_backend;
    }

    auto time_enabled() const -> bool { return m_config.enable_time; }

    auto blocking_spawner() const -> async::BlockingSpawner { return m_blocking_pool.spawner(); }
    auto blocking_thread_count() const -> usize { return m_blocking_pool.thread_count(); }
    void set_blocking_keep_alive(time::Duration keep_alive) {
        m_blocking_pool.set_keep_alive(keep_alive);
    }
    void fail_next_blocking_spawn() { m_blocking_pool.fail_next_spawn(); }
    auto submit_blocking(async::BlockingJob job) -> io::Result<async::BlockingJobCancellation>;
    void begin_blocking_shutdown() { m_blocking_pool.begin_shutdown(); }
    void join_blocking_pool() { m_blocking_pool.join(); }

    auto current_poll_worker() -> io::Result<WorkerHandle>;
    auto reserve_current_operation(PollEventOwner owner) -> io::Result<OperationKey>;
    void abandon_current_operation(OperationKey key);
    auto defer_current_poll(PollCommand command) -> Result<empty, PollCommand>;

    void spawn(TaskRef task);
    auto schedule(ScheduleTicket ticket) -> Result<empty, ScheduleTicket>;
    auto lifecycle() -> RuntimeLifecycle;
    auto is_running() -> bool;
    auto is_stopping() -> bool;
    void retire(TaskRefControl* task);
    auto complete_facility(FacilityEvent event) -> Result<empty, FacilityEvent>;
    auto complete_facility_batch(FacilityEventBatch batch) -> Result<empty, FacilityEventBatch>;
    void worker_loop(RuntimeWorkerId worker);
    void worker_started();
    void worker_start_failed(io::Error error);
    auto wait_for_startup() -> io::Result<empty>;
    auto begin_shutdown() -> bool;
    void stop_workers();
    void finish_shutdown();
    void abort_all_tasks();
};

struct TaskStateBase {
    TaskRefControl*                           ref_control;
    sync::Weak<RuntimeInner>                  runtime;
    sync::Mutex<TaskControl>                  control;
    sync::Mutex<Option<FacilityEvent>>        completion_event;
    sync::Mutex<Option<FacilityCancellation>> completion_cancellation;

    TaskStateBase(TaskRefControl& ref_control, sync::Weak<RuntimeInner> runtime)
        : ref_control(rstd::addressof(ref_control)),
          runtime(rstd::move(runtime)),
          control(TaskControl {}),
          completion_event(Option<FacilityEvent> {}),
          completion_cancellation(Option<FacilityCancellation> {}) {
        ref_control.attach(this);
    }
    virtual ~TaskStateBase() = default;

    virtual auto poll(TaskRef& self, task::Context& cx) -> TaskPollAction = 0;
    virtual void complete_value()                                         = 0;
    virtual void complete_abort()                                         = 0;
    virtual auto submit_completion_facility(FacilityCompletionToken token)
        -> FacilityCompletionSubmitResult;
    virtual auto submit_facility(FacilityTicket ticket, FacilityRequest request)
        -> FacilitySubmitResult;
    virtual void run_facility_execution(FacilityExecutionToken token, TaskAccess access);

    auto activate(TaskRef self, RuntimeWorkerId owner) -> TaskAction;
    auto try_begin_runtime(ScheduleTicket ticket, TaskAccess access)
        -> Option<RuntimeExecutionLease>;
    void schedule(TaskRef self);
    void abort(TaskRef self);
    void install_completion_cancellation(FacilityCancellation cancellation);
    auto take_completion_cancellation() -> Option<FacilityCancellation>;
    auto take_completion_event() -> Option<FacilityEvent>;
    auto end_runtime_execution(RuntimeExecutionLease lease, TaskPollAction outcome) -> TaskAction;
    void complete_facility(FacilityEvent event);
    auto begin_facility_execution(FacilityExecutionToken token, TaskAccess access)
        -> Option<FacilityExecutionLease>;
    void cancel_facility_handoff(FacilityExecutionToken token);
    auto end_facility_execution(FacilityExecutionLease lease, TaskPollAction outcome) -> TaskAction;

    static void complete_locked(TaskControl& state);
    static auto make_schedule_action(TaskControl& state, TaskRef task) -> TaskAction;
    void        apply(TaskAction action);
};

auto make_task_waker(const TaskRef& task) -> task::Waker;

struct RuntimeScope {
    RuntimeInner* previous;

    explicit RuntimeScope(RuntimeInner& runtime): previous(CURRENT_RUNTIME) {
        CURRENT_RUNTIME = rstd::addressof(runtime);
    }

    ~RuntimeScope() { CURRENT_RUNTIME = previous; }
};

struct RuntimeWorkerScope {
    RuntimeWorkerId previous_worker;
    RuntimeWorker*  previous_context;
    bool            previous_active;

    RuntimeWorkerScope(RuntimeWorkerId worker, RuntimeWorker* context)
        : previous_worker(CURRENT_RUNTIME_WORKER),
          previous_context(CURRENT_RUNTIME_WORKER_CONTEXT),
          previous_active(CURRENT_RUNTIME_WORKER_ACTIVE) {
        CURRENT_RUNTIME_WORKER         = worker;
        CURRENT_RUNTIME_WORKER_ACTIVE  = true;
        CURRENT_RUNTIME_WORKER_CONTEXT = context;
    }

    ~RuntimeWorkerScope() {
        CURRENT_RUNTIME_WORKER         = previous_worker;
        CURRENT_RUNTIME_WORKER_ACTIVE  = previous_active;
        CURRENT_RUNTIME_WORKER_CONTEXT = previous_context;
    }
};

struct ExecutionDomainScope {
    async::ExecutionDomainKind previous;

    explicit ExecutionDomainScope(async::ExecutionDomainKind domain)
        : previous(CURRENT_EXECUTION_DOMAIN) {
        CURRENT_EXECUTION_DOMAIN = domain;
    }

    ~ExecutionDomainScope() { CURRENT_EXECUTION_DOMAIN = previous; }
};
