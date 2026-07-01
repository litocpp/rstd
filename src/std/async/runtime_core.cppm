export module rstd:async.runtime_core;
import :async.forward;
import rstd.alloc;
import :sync;
import :thread;

using namespace rstd;

using ::alloc::vec::Vec;

struct RuntimeInner;
struct TaskStateBase;

inline thread_local RuntimeInner* CURRENT_RUNTIME { nullptr };

enum class RuntimeKind {
    CurrentThread,
    ThreadPool,
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

    auto take_all() -> Vec<TaskRef> {
        auto tasks = rstd::move(m_tasks);
        m_tasks    = Vec<TaskRef>::make();
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

struct CurrentThreadScheduler {
    ReadyQueue             m_ready;
    Option<thread::Thread> m_parker;
    bool                   m_notified { false };
    bool                   m_parked { false };

    CurrentThreadScheduler() : m_ready(), m_parker(None()) {}

    void push_ready(TaskRef task) { m_ready.push(rstd::move(task)); }

    auto pop_ready() -> Option<TaskRef> { return m_ready.pop_front(); }

    void notify_locked() {
        bool should_unpark = m_parked && ! m_notified;
        m_notified = true;
        if (should_unpark && m_parker.is_some()) {
            thread::unpark(*m_parker);
        }
    }

    void set_parker(thread::Thread thread) { m_parker = Some(rstd::move(thread)); }

    void clear_parker() { m_parker = None(); }

    void clear_ready() { m_ready.clear(); }

    void begin_park_locked() { m_parked = true; }

    void end_park_locked() { m_parked = false; }

    auto should_park_locked() -> bool {
        if (! m_ready.is_empty() || m_notified) {
            m_notified = false;
            return false;
        }
        m_notified = false;
        return true;
    }
};

struct ThreadPoolScheduler {
    ReadyQueue m_ready;
    bool       m_stopping { false };

    ThreadPoolScheduler() : m_ready() {}

    void push_ready(TaskRef task) { m_ready.push(rstd::move(task)); }

    auto pop_ready() -> Option<TaskRef> { return m_ready.pop_front(); }

    void stop_locked() { m_stopping = true; }

    auto is_stopping() const -> bool { return m_stopping; }

    void clear_ready() { m_ready.clear(); }
};

struct RuntimeState {
    TaskRegistry           m_registry;
    CurrentThreadScheduler m_scheduler;
    ThreadPoolScheduler    m_thread_pool;

    RuntimeState() : m_registry(), m_scheduler(), m_thread_pool() {}
};

struct RuntimeInner {
    RuntimeKind              m_kind;
    sync::Mutex<RuntimeState> state;
    sync::Weak<RuntimeInner>  self;
    sync::Condvar             m_worker_cvar;

    explicit RuntimeInner(RuntimeKind kind = RuntimeKind::CurrentThread)
        : m_kind(kind),
          state(RuntimeState {}),
          self(sync::Weak<RuntimeInner>::make()),
          m_worker_cvar(sync::Condvar::make()) {}

    void init_self(sync::Weak<RuntimeInner> weak) { self = rstd::move(weak); }

    auto weak() -> sync::Weak<RuntimeInner> { return self.clone(); }

    auto is_thread_pool() const -> bool { return m_kind == RuntimeKind::ThreadPool; }

    void notify_locked(RuntimeState& st) { st.m_scheduler.notify_locked(); }

    void notify() {
        auto st = state.lock().unwrap_unchecked();
        notify_locked(*st);
    }

    void set_parker(thread::Thread thread) {
        auto st = state.lock().unwrap_unchecked();
        st->m_scheduler.set_parker(rstd::move(thread));
    }

    void clear_parker() {
        auto st = state.lock().unwrap_unchecked();
        st->m_scheduler.clear_parker();
    }

    void spawn(TaskRef task);
    void schedule(TaskRef task);
    void drain_ready();
    void worker_loop();
    void request_stop();
    void abort_all_tasks();

    void wait_for_work() {
        {
            auto st = state.lock().unwrap_unchecked();
            if (! st->m_scheduler.should_park_locked()) {
                return;
            }
            st->m_scheduler.begin_park_locked();
        }

        thread::park();

        {
            auto st = state.lock().unwrap_unchecked();
            st->m_scheduler.end_park_locked();
        }
    }
};

struct TaskStateBase {
    rstd::sync::atomic::Atomic<usize> refs { 1 };
    sync::Weak<RuntimeInner>          runtime;
    bool                             queued { false };
    bool                             completed { false };

    explicit TaskStateBase(sync::Weak<RuntimeInner> runtime) : runtime(rstd::move(runtime)) {}
    virtual ~TaskStateBase() = default;

    virtual void poll(task::Context& cx) = 0;
    virtual void complete_abort()        = 0;

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
    void finish();
    void abort();
};

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
    auto st = state.lock().unwrap_unchecked();
    if (is_thread_pool() && st->m_thread_pool.is_stopping()) {
        return;
    }

    task->queued = true;
    st->m_registry.insert(task.clone());
    if (is_thread_pool()) {
        st->m_thread_pool.push_ready(rstd::move(task));
        m_worker_cvar.notify_one();
    } else {
        st->m_scheduler.push_ready(rstd::move(task));
        notify_locked(*st);
    }
}

inline void RuntimeInner::schedule(TaskRef task) {
    auto st = state.lock().unwrap_unchecked();
    if (task->completed || task->queued) {
        return;
    }
    if (is_thread_pool() && st->m_thread_pool.is_stopping()) {
        return;
    }

    task->queued = true;
    if (is_thread_pool()) {
        st->m_thread_pool.push_ready(rstd::move(task));
        m_worker_cvar.notify_one();
    } else {
        st->m_scheduler.push_ready(rstd::move(task));
        notify_locked(*st);
    }
}

inline void TaskStateBase::schedule(TaskRef self) {
    auto rt = runtime.upgrade();
    if (rt) {
        rt->schedule(rstd::move(self));
    }
}

inline void TaskStateBase::finish() {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    auto st = rt->state.lock().unwrap_unchecked();
    if (completed) {
        return;
    }
    completed = true;
    queued    = false;
    st->m_registry.remove(this);
    rt->notify_locked(*st);
}

inline void TaskStateBase::abort() {
    auto rt = runtime.upgrade();
    if (! rt) {
        return;
    }

    bool should_complete = false;
    {
        auto st = rt->state.lock().unwrap_unchecked();
        if (! completed) {
            completed = true;
            queued    = false;
            st->m_registry.remove(this);
            rt->notify_locked(*st);
            should_complete = true;
        }
    }

    if (should_complete) {
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
    task_state->poll(cx);
}

inline void RuntimeInner::drain_ready() {
    if (is_thread_pool()) {
        return;
    }

    while (true) {
        auto task_state = TaskRef {};
        {
            auto st   = state.lock().unwrap_unchecked();
            auto next = st->m_scheduler.pop_ready();
            if (next.is_none()) {
                return;
            }

            task_state         = rstd::move(next).unwrap_unchecked();
            task_state->queued = false;
            if (task_state->completed) {
                continue;
            }
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

struct ParkerScope {
    RuntimeInner* runtime;

    explicit ParkerScope(RuntimeInner& runtime) : runtime(rstd::addressof(runtime)) {
        runtime.set_parker(thread::current());
    }

    ~ParkerScope() { runtime->clear_parker(); }
};

inline void RuntimeInner::worker_loop() {
    auto scope = RuntimeScope { *this };

    for (;;) {
        auto task_state = TaskRef {};
        {
            auto st = state.lock().unwrap_unchecked();
            for (;;) {
                if (st->m_thread_pool.is_stopping()) {
                    return;
                }

                auto next = st->m_thread_pool.pop_ready();
                if (next.is_some()) {
                    task_state         = rstd::move(next).unwrap_unchecked();
                    task_state->queued = false;
                    if (task_state->completed) {
                        task_state.reset();
                        continue;
                    }
                    break;
                }

                m_worker_cvar.wait(st);
            }
        }

        poll_runtime_task(task_state);
    }
}

inline void RuntimeInner::request_stop() {
    if (! is_thread_pool()) {
        return;
    }

    {
        auto st = state.lock().unwrap_unchecked();
        st->m_thread_pool.stop_locked();
    }
    m_worker_cvar.notify_all();
}

inline void RuntimeInner::abort_all_tasks() {
    auto tasks = Vec<TaskRef>::make();
    {
        auto st = state.lock().unwrap_unchecked();
        tasks   = st->m_registry.take_all();
        st->m_scheduler.clear_ready();
        st->m_thread_pool.clear_ready();
    }

    while (! tasks.is_empty()) {
        auto task = rstd::move(tasks.pop()).unwrap_unchecked();
        task->abort();
    }
}
