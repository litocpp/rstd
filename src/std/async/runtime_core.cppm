export module rstd:async.runtime_core;
import :async.awaitable;
import :async.forward;
import rstd.alloc;
import :sync;
import :thread;

using namespace rstd;

using ::alloc::vec::Vec;

struct RuntimeInner;
struct TaskStateBase;

struct RuntimeWorkerId {
    usize index {};

    static constexpr auto current_thread() noexcept -> RuntimeWorkerId {
        return RuntimeWorkerId { 0 };
    }

    constexpr auto as_usize() const noexcept -> usize { return index; }

    friend constexpr auto operator==(RuntimeWorkerId, RuntimeWorkerId) noexcept -> bool = default;
};

enum class TaskLifecycle {
    Idle,
    Queued,
    RunningRuntime,
    ExternalQueued,
    RunningExternal,
    Completed,
};

inline thread_local RuntimeInner* CURRENT_RUNTIME { nullptr };
inline thread_local RuntimeWorkerId CURRENT_RUNTIME_WORKER {};
inline thread_local bool CURRENT_RUNTIME_WORKER_ACTIVE { false };

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

enum class RuntimeKind {
    CurrentThread,
    ThreadPool,
};

struct RuntimeConfig {
    bool enable_io { false };
    bool enable_time { false };

    static constexpr auto all() noexcept -> RuntimeConfig {
        return RuntimeConfig { true, true };
    }
};

class TaskRef {
    TaskStateBase* ptr { nullptr };

    explicit TaskRef(TaskStateBase* ptr) noexcept : ptr(ptr) {}

public:
    TaskRef() noexcept = default;

    static auto adopt(TaskStateBase* ptr) noexcept -> TaskRef { return TaskRef { ptr }; }

    TaskRef(const TaskRef&)            = delete;
    TaskRef& operator=(const TaskRef&) = delete;

    TaskRef(TaskRef&& other) noexcept : ptr(rstd::exchange(other.ptr, nullptr)) {}

    auto operator=(TaskRef&& other) noexcept -> TaskRef&;
    ~TaskRef();

    auto clone() const noexcept -> TaskRef;
    void reset() noexcept;
    void schedule() const;

    auto get() const noexcept -> TaskStateBase* { return ptr; }
    auto operator->() const noexcept -> TaskStateBase* { return ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }
};

struct ReadyQueue {
    Vec<TaskRef> m_tasks;

    ReadyQueue() : m_tasks(Vec<TaskRef>::make()) {}

    auto is_empty() const -> bool { return m_tasks.is_empty(); }

    void push(TaskRef task) { m_tasks.push(rstd::move(task)); }

    auto pop_front() -> Option<TaskRef> {
        if (m_tasks.is_empty()) {
            return None();
        }
        return Some(m_tasks.remove(0));
    }

    void clear() { m_tasks.clear(); }
};

struct TaskRegistry {
    Vec<TaskRef> m_tasks;

    TaskRegistry() : m_tasks(Vec<TaskRef>::make()) {}

    void insert(TaskRef task) { m_tasks.push(rstd::move(task)); }

    auto is_empty() const -> bool { return m_tasks.is_empty(); }

    auto clone_all() const -> Vec<TaskRef> {
        auto tasks = Vec<TaskRef>::make();
        for (usize i = 0; i < m_tasks.len(); ++i) {
            tasks.push(m_tasks[i].clone());
        }
        return tasks;
    }

    void remove(TaskStateBase* task) {
        for (usize i = 0; i < m_tasks.len(); ++i) {
            if (m_tasks[i].get() == task) {
                m_tasks.remove(i);
                return;
            }
        }
    }
};

enum class WorkerWait {
    Retry,
    Park,
    Stop,
};

struct WorkerFields {
    ReadyQueue             m_ready;
    Option<thread::Thread> m_parker;
    bool                   m_notified { false };
    bool                   m_parked { false };
    bool                   m_stopping { false };

    WorkerFields() : m_ready(), m_parker(None()) {}
};

struct WorkerState {
    sync::Mutex<WorkerFields> m_fields;

    WorkerState() : m_fields(WorkerFields {}) {}
};

class WorkerHandle {
    RuntimeWorkerId       m_id;
    sync::Arc<WorkerState> m_state;

    WorkerHandle(RuntimeWorkerId id, sync::Arc<WorkerState> state)
        : m_id(id), m_state(rstd::move(state)) {}

    static void notify_locked(WorkerFields& fields) {
        bool should_unpark = fields.m_parked && ! fields.m_notified;
        fields.m_notified  = true;
        if (should_unpark && fields.m_parker.is_some()) {
            thread::unpark(*fields.m_parker);
        }
    }

public:
    static auto make(RuntimeWorkerId id) -> WorkerHandle {
        return WorkerHandle { id, sync::Arc<WorkerState>::make() };
    }

    WorkerHandle(const WorkerHandle&)            = delete;
    WorkerHandle& operator=(const WorkerHandle&) = delete;
    WorkerHandle(WorkerHandle&&) noexcept        = default;
    auto operator=(WorkerHandle&&) noexcept -> WorkerHandle& = default;

    auto clone() const -> WorkerHandle { return WorkerHandle { m_id, m_state.clone() }; }

    auto id() const noexcept -> RuntimeWorkerId { return m_id; }

    auto schedule(TaskRef task) const -> bool {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_stopping) {
            return false;
        }
        fields->m_ready.push(rstd::move(task));
        notify_locked(*fields);
        return true;
    }

    void wake() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        notify_locked(*fields);
    }

    auto pop_ready() const -> Option<TaskRef> {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        return fields->m_ready.pop_front();
    }

    void set_parker(thread::Thread thread) const {
        auto fields      = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_parker = Some(rstd::move(thread));
    }

    void clear_parker() const {
        auto fields      = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_parker = None();
    }

    auto prepare_wait() const -> WorkerWait {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        if (fields->m_stopping) {
            return WorkerWait::Stop;
        }
        if (! fields->m_ready.is_empty() || fields->m_notified) {
            fields->m_notified = false;
            return WorkerWait::Retry;
        }
        fields->m_notified = false;
        fields->m_parked   = true;
        return WorkerWait::Park;
    }

    void finish_wait() const {
        auto fields      = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_parked = false;
    }

    void stop() const {
        auto fields        = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_stopping = true;
        notify_locked(*fields);
    }

    void clear_ready() const {
        auto fields = m_state->m_fields.lock().unwrap_unchecked();
        fields->m_ready.clear();
    }
};

struct RuntimeSharedState {
    TaskRegistry m_registry;
    usize        m_next_worker { 0 };
    bool         m_stopping { false };
};

class RuntimeShared {
    Vec<WorkerHandle> m_workers;

    auto normalize(RuntimeWorkerId worker) const -> usize {
        return worker.as_usize() % m_workers.len();
    }

public:
    sync::Mutex<RuntimeSharedState> state;
    sync::Condvar                  task_cvar;

    explicit RuntimeShared(usize worker_count)
        : m_workers(Vec<WorkerHandle>::make()),
          state(RuntimeSharedState {}),
          task_cvar(sync::Condvar::make()) {
        for (usize i = 0; i < worker_count; ++i) {
            m_workers.push(WorkerHandle::make(RuntimeWorkerId { i }));
        }
    }

    auto worker(RuntimeWorkerId id) const -> const WorkerHandle& {
        return m_workers[normalize(id)];
    }

    auto worker_handle(RuntimeWorkerId id) const -> WorkerHandle {
        return worker(id).clone();
    }

    auto next_worker_locked(RuntimeSharedState& shared) const -> RuntimeWorkerId {
        auto worker = RuntimeWorkerId { shared.m_next_worker % m_workers.len() };
        shared.m_next_worker = (shared.m_next_worker + 1) % m_workers.len();
        return worker;
    }

    void stop_workers_locked(RuntimeSharedState& shared) const {
        shared.m_stopping = true;
        for (usize i = 0; i < m_workers.len(); ++i) {
            m_workers[i].stop();
        }
    }

    void clear_ready_locked() const {
        for (usize i = 0; i < m_workers.len(); ++i) {
            m_workers[i].clear_ready();
        }
    }
};

class RuntimeWorker {
    RuntimeInner* m_runtime;
    WorkerHandle m_handle;

public:
    RuntimeWorker(RuntimeInner& runtime, WorkerHandle handle)
        : m_runtime(rstd::addressof(runtime)), m_handle(rstd::move(handle)) {}

    void run();
};

struct RuntimeInner {
    RuntimeKind             m_kind;
    RuntimeConfig           m_config;
    RuntimeShared           m_shared;
    sync::Weak<RuntimeInner> self;

    explicit RuntimeInner(RuntimeKind kind = RuntimeKind::CurrentThread,
                          RuntimeConfig config = RuntimeConfig {},
                          usize worker_count = 0)
        : m_kind(kind),
          m_config(config),
          m_shared(kind == RuntimeKind::CurrentThread ? usize { 1 } : worker_count),
          self(sync::Weak<RuntimeInner>::make()) {}

    void init_self(sync::Weak<RuntimeInner> weak) { self = rstd::move(weak); }

    auto weak() -> sync::Weak<RuntimeInner> { return self.clone(); }

    auto is_thread_pool() const -> bool { return m_kind == RuntimeKind::ThreadPool; }

    auto io_enabled() const -> bool { return m_config.enable_io; }

    auto time_enabled() const -> bool { return m_config.enable_time; }

    void notify_locked(RuntimeSharedState&) {
        if (! is_thread_pool()) {
            m_shared.worker(RuntimeWorkerId::current_thread()).wake();
        }
    }

    void notify() { m_shared.worker(RuntimeWorkerId::current_thread()).wake(); }

    void set_parker(thread::Thread thread) {
        m_shared.worker(RuntimeWorkerId::current_thread()).set_parker(rstd::move(thread));
    }

    void clear_parker() { m_shared.worker(RuntimeWorkerId::current_thread()).clear_parker(); }

    void spawn(TaskRef task);
    void schedule(TaskRef task);
    void drain_ready();
    void worker_loop(RuntimeWorkerId worker);
    void request_stop();
    void abort_all_tasks();

    void wait_for_work() {
        auto& worker = m_shared.worker(RuntimeWorkerId::current_thread());
        auto  wait   = worker.prepare_wait();
        if (wait != WorkerWait::Park) {
            return;
        }

        thread::park();
        worker.finish_wait();
    }
};

struct TaskStateBase {
    rstd::sync::atomic::Atomic<usize> refs { 1 };
    sync::Weak<RuntimeInner>          runtime;
    RuntimeWorkerId                   owner_worker {};
    TaskLifecycle                    lifecycle { TaskLifecycle::Idle };
    bool                             wake_requested { false };
    bool                             cancel_requested { false };

    explicit TaskStateBase(sync::Weak<RuntimeInner> runtime) : runtime(rstd::move(runtime)) {}
    virtual ~TaskStateBase() = default;

    virtual void poll(TaskRef& self, task::Context& cx) = 0;
    virtual void complete_abort()        = 0;
    virtual void run_external_continuation(TaskRef& self);

    void inc_ref() noexcept {
        refs.fetch_add(1, rstd::sync::atomic::Ordering::Relaxed);
    }

    void dec_ref() noexcept {
        if (refs.fetch_sub(1, rstd::sync::atomic::Ordering::AcqRel) == 1) {
            rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
            delete this;
        }
    }

    void schedule(TaskRef self);
    auto finish() -> bool;
    void abort();
    void end_runtime_execution(TaskRef& self);
    auto prepare_external_handoff() -> bool;
    auto begin_external_execution() -> bool;
    void cancel_external_handoff(TaskRef& self);
    void end_external_execution(TaskRef& self);
};

inline void TaskStateBase::run_external_continuation(TaskRef&) {
    rstd::panic { "async task cannot resume on external executor" };
}

inline auto TaskRef::operator=(TaskRef&& other) noexcept -> TaskRef& {
    if (this != &other) {
        reset();
        ptr = rstd::exchange(other.ptr, nullptr);
    }
    return *this;
}

inline TaskRef::~TaskRef() { reset(); }

inline auto TaskRef::clone() const noexcept -> TaskRef {
    if (ptr != nullptr) {
        ptr->inc_ref();
    }
    return TaskRef { ptr };
}

inline void TaskRef::reset() noexcept {
    if (ptr != nullptr) {
        auto* current = rstd::exchange(ptr, nullptr);
        current->dec_ref();
    }
}

inline void TaskRef::schedule() const {
    if (ptr != nullptr) {
        ptr->schedule(clone());
    }
}

inline void RuntimeInner::spawn(TaskRef task) {
    bool should_abort = false;
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        if (is_thread_pool() && st->m_stopping) {
            task->lifecycle = TaskLifecycle::Completed;
            should_abort    = true;
        } else {
            task->lifecycle = TaskLifecycle::Queued;
            st->m_registry.insert(task.clone());
            auto worker = RuntimeWorkerId::current_thread();
            if (is_thread_pool()) {
                auto use_current_worker =
                    has_current_runtime_worker() && CURRENT_RUNTIME == this &&
                    current_execution_domain() == async::ExecutionDomainKind::RuntimeWorker;
                worker = use_current_worker ? current_runtime_worker_id()
                                            : m_shared.next_worker_locked(*st);
            }
            task->owner_worker = worker;
            (void)m_shared.worker(worker).schedule(rstd::move(task));
        }
    }

    if (should_abort) {
        task->complete_abort();
    }
}

inline void RuntimeInner::schedule(TaskRef task) {
    bool should_abort = false;
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        switch (task->lifecycle) {
            case TaskLifecycle::Completed:
            case TaskLifecycle::Queued:
                return;
            case TaskLifecycle::RunningRuntime:
            case TaskLifecycle::ExternalQueued:
            case TaskLifecycle::RunningExternal:
                task->wake_requested = true;
                return;
            case TaskLifecycle::Idle:
                break;
        }

        if (is_thread_pool() && st->m_stopping) {
            task->cancel_requested = true;
            task->lifecycle        = TaskLifecycle::Completed;
            st->m_registry.remove(task.get());
            should_abort = true;
        } else {
            task->lifecycle = TaskLifecycle::Queued;
            auto worker = task->owner_worker;
            (void)m_shared.worker(worker).schedule(rstd::move(task));
        }
    }

    if (should_abort) {
        m_shared.task_cvar.notify_all();
        task->complete_abort();
    }
}

inline void TaskStateBase::schedule(TaskRef self) {
    auto rt = runtime.upgrade();
    if (rt) {
        rt->schedule(rstd::move(self));
    }
}

inline auto TaskStateBase::finish() -> bool {
    auto rt = runtime.upgrade();
    if (! rt) {
        return false;
    }

    bool should_abort = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle == TaskLifecycle::Completed) {
            return false;
        }

        should_abort    = cancel_requested;
        lifecycle       = TaskLifecycle::Completed;
        wake_requested  = false;
        st->m_registry.remove(this);
        rt->notify_locked(*st);
    }

    rt->m_shared.task_cvar.notify_all();
    if (should_abort) {
        complete_abort();
        return false;
    }
    return true;
}

inline void TaskStateBase::abort() {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    bool should_complete = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle == TaskLifecycle::Completed) {
            return;
        }

        cancel_requested = true;
        if (lifecycle == TaskLifecycle::Idle || lifecycle == TaskLifecycle::Queued ||
            lifecycle == TaskLifecycle::ExternalQueued) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_complete = true;
        }
    }

    if (should_complete) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
}

inline void TaskStateBase::end_runtime_execution(TaskRef& self) {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    bool should_abort = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle != TaskLifecycle::RunningRuntime) {
            return;
        }

        if (cancel_requested ||
            (rt->is_thread_pool() && st->m_stopping)) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_abort = true;
        } else if (wake_requested) {
            lifecycle      = TaskLifecycle::Queued;
            wake_requested = false;
            (void)rt->m_shared.worker(owner_worker).schedule(self.clone());
        } else {
            lifecycle = TaskLifecycle::Idle;
        }
    }

    if (should_abort) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
}

inline auto TaskStateBase::prepare_external_handoff() -> bool {
    auto rt = runtime.upgrade();
    if (! rt) {
        return false;
    }

    bool should_abort = false;
    bool transferred  = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle != TaskLifecycle::RunningRuntime &&
            lifecycle != TaskLifecycle::RunningExternal) {
            return false;
        }

        if (cancel_requested ||
            (rt->is_thread_pool() && st->m_stopping)) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_abort = true;
        } else {
            lifecycle      = TaskLifecycle::ExternalQueued;
            wake_requested = false;
            transferred    = true;
        }
    }

    if (should_abort) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
    return transferred;
}

inline auto TaskStateBase::begin_external_execution() -> bool {
    auto rt = runtime.upgrade();
    if (! rt) {
        return false;
    }

    bool should_abort = false;
    bool acquired     = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle != TaskLifecycle::ExternalQueued) {
            return false;
        }

        if (cancel_requested ||
            (rt->is_thread_pool() && st->m_stopping)) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_abort = true;
        } else {
            lifecycle = TaskLifecycle::RunningExternal;
            acquired  = true;
        }
    }

    if (should_abort) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
    return acquired;
}

inline void TaskStateBase::cancel_external_handoff(TaskRef& self) {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    bool should_abort = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle != TaskLifecycle::ExternalQueued) {
            return;
        }

        if (cancel_requested ||
            (rt->is_thread_pool() && st->m_stopping)) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_abort = true;
        } else {
            lifecycle      = TaskLifecycle::Queued;
            wake_requested = false;
            (void)rt->m_shared.worker(owner_worker).schedule(self.clone());
        }
    }

    if (should_abort) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
}

inline void TaskStateBase::end_external_execution(TaskRef& self) {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    bool should_abort = false;
    {
        auto st = rt->m_shared.state.lock().unwrap_unchecked();
        if (lifecycle != TaskLifecycle::RunningExternal) {
            return;
        }

        if (cancel_requested ||
            (rt->is_thread_pool() && st->m_stopping)) {
            lifecycle      = TaskLifecycle::Completed;
            wake_requested = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_abort = true;
        } else {
            lifecycle      = TaskLifecycle::Queued;
            wake_requested = false;
            (void)rt->m_shared.worker(owner_worker).schedule(self.clone());
        }
    }

    if (should_abort) {
        rt->m_shared.task_cvar.notify_all();
        complete_abort();
    }
}

inline auto task_waker_clone(voidp data) -> task::RawWaker;
inline void task_waker_wake(voidp data);
inline void task_waker_wake_by_ref(voidp data);
inline void task_waker_drop(voidp data);

inline const task::RawWakerVTable TASK_WAKER_VTABLE {
    &task_waker_clone,
    &task_waker_wake,
    &task_waker_wake_by_ref,
    &task_waker_drop,
};

inline auto task_waker_clone(voidp data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(
        new TaskRef(static_cast<TaskRef*>(data)->clone()),
        rstd::addressof(TASK_WAKER_VTABLE));
}

inline void task_waker_wake(voidp data) {
    auto* task = static_cast<TaskRef*>(data);
    task->schedule();
    delete task;
}

inline void task_waker_wake_by_ref(voidp data) {
    static_cast<TaskRef*>(data)->schedule();
}

inline void task_waker_drop(voidp data) {
    delete static_cast<TaskRef*>(data);
}

inline auto make_task_waker(const TaskRef& task) -> task::Waker {
    return task::Waker::from_raw(task::RawWaker::from_raw_parts(
        new TaskRef(task.clone()),
        rstd::addressof(TASK_WAKER_VTABLE)));
}

inline void poll_runtime_task(TaskRef& task_state) {
    auto waker = make_task_waker(task_state);
    auto cx    = task::Context { waker };
    task_state->poll(task_state, cx);
    task_state->end_runtime_execution(task_state);
}

inline void RuntimeInner::drain_ready() {
    if (is_thread_pool()) {
        return;
    }

    auto& worker = m_shared.worker(RuntimeWorkerId::current_thread());
    while (true) {
        auto next = worker.pop_ready();
        if (next.is_none()) {
            return;
        }

        auto task_state = rstd::move(next).unwrap_unchecked();
        {
            auto st = m_shared.state.lock().unwrap_unchecked();
            if (task_state->lifecycle != TaskLifecycle::Queued) {
                continue;
            }
            task_state->lifecycle = TaskLifecycle::RunningRuntime;
        }

        poll_runtime_task(task_state);
    }
}

inline auto runtime_waker_clone(voidp data) -> task::RawWaker;
inline void runtime_waker_wake(voidp data);
inline void runtime_waker_wake_by_ref(voidp data);
inline void runtime_waker_drop(voidp data);

inline const task::RawWakerVTable RUNTIME_WAKER_VTABLE {
    &runtime_waker_clone,
    &runtime_waker_wake,
    &runtime_waker_wake_by_ref,
    &runtime_waker_drop,
};

inline auto runtime_waker_clone(voidp data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(
        new sync::Weak<RuntimeInner>(static_cast<sync::Weak<RuntimeInner>*>(data)->clone()),
        rstd::addressof(RUNTIME_WAKER_VTABLE));
}

inline void runtime_waker_wake(voidp data) {
    auto* runtime = static_cast<sync::Weak<RuntimeInner>*>(data);
    if (auto rt = runtime->upgrade()) {
        rt->notify();
    }
    delete runtime;
}

inline void runtime_waker_wake_by_ref(voidp data) {
    if (auto rt = static_cast<sync::Weak<RuntimeInner>*>(data)->upgrade()) {
        rt->notify();
    }
}

inline void runtime_waker_drop(voidp data) {
    delete static_cast<sync::Weak<RuntimeInner>*>(data);
}

inline auto make_runtime_waker(RuntimeInner& runtime) -> task::Waker {
    return task::Waker::from_raw(task::RawWaker::from_raw_parts(
        new sync::Weak<RuntimeInner>(runtime.weak()),
        rstd::addressof(RUNTIME_WAKER_VTABLE)));
}

struct RuntimeScope {
    RuntimeInner* previous;

    explicit RuntimeScope(RuntimeInner& runtime) : previous(CURRENT_RUNTIME) {
        CURRENT_RUNTIME = rstd::addressof(runtime);
    }

    ~RuntimeScope() { CURRENT_RUNTIME = previous; }
};

struct RuntimeWorkerScope {
    RuntimeWorkerId previous_worker;
    bool            previous_active;

    explicit RuntimeWorkerScope(RuntimeWorkerId worker)
        : previous_worker(CURRENT_RUNTIME_WORKER),
          previous_active(CURRENT_RUNTIME_WORKER_ACTIVE) {
        CURRENT_RUNTIME_WORKER        = worker;
        CURRENT_RUNTIME_WORKER_ACTIVE = true;
    }

    ~RuntimeWorkerScope() {
        CURRENT_RUNTIME_WORKER        = previous_worker;
        CURRENT_RUNTIME_WORKER_ACTIVE = previous_active;
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

struct ParkerScope {
    RuntimeInner* runtime;

    explicit ParkerScope(RuntimeInner& runtime) : runtime(rstd::addressof(runtime)) {
        runtime.set_parker(thread::current());
    }

    ~ParkerScope() { runtime->clear_parker(); }
};

inline void RuntimeInner::worker_loop(RuntimeWorkerId worker) {
    RuntimeWorker { *this, m_shared.worker_handle(worker) }.run();
}

inline void RuntimeWorker::run() {
    auto scope        = RuntimeScope { *m_runtime };
    auto worker_scope = RuntimeWorkerScope { m_handle.id() };

    m_handle.set_parker(thread::current());

    for (;;) {
        auto next = m_handle.pop_ready();
        if (next.is_none()) {
            auto wait = m_handle.prepare_wait();
            if (wait == WorkerWait::Stop) {
                m_handle.clear_parker();
                return;
            }
            if (wait == WorkerWait::Park) {
                thread::park();
                m_handle.finish_wait();
            }
            continue;
        }

        auto task_state = rstd::move(next).unwrap_unchecked();
        bool should_run = false;
        {
            auto st = m_runtime->m_shared.state.lock().unwrap_unchecked();
            if (st->m_stopping) {
                m_handle.clear_parker();
                return;
            }
            if (task_state->lifecycle == TaskLifecycle::Queued) {
                task_state->lifecycle = TaskLifecycle::RunningRuntime;
                should_run           = true;
            }
        }

        if (! should_run) {
            continue;
        }

        poll_runtime_task(task_state);
    }
}

inline void RuntimeInner::request_stop() {
    if (! is_thread_pool()) {
        return;
    }

    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        m_shared.stop_workers_locked(*st);
    }
}

inline void RuntimeInner::abort_all_tasks() {
    auto tasks = Vec<TaskRef>::make();
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        tasks   = st->m_registry.clone_all();
        m_shared.clear_ready_locked();
    }

    while (! tasks.is_empty()) {
        auto task = rstd::move(tasks.pop()).unwrap_unchecked();
        task->abort();
    }

    auto st = m_shared.state.lock().unwrap_unchecked();
    m_shared.task_cvar.wait_while(st, [](RuntimeSharedState const& runtime_state) {
        return ! runtime_state.m_registry.is_empty();
    });
}
