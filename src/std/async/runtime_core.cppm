export module rstd:async.runtime_core;
import :async.forward;
import rstd.alloc;
import :sync;
import :thread;
import :time;

using namespace rstd;

using ::alloc::vec::Vec;

struct RuntimeInner;
struct TaskStateBase;

inline thread_local RuntimeInner* CURRENT_RUNTIME { nullptr };

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

struct TimerEntry {
    usize         id {};
    time::Instant deadline {};
    task::Waker  waker;

    TimerEntry() = delete;
    TimerEntry(usize id, time::Instant deadline, task::Waker waker)
        : id(id), deadline(deadline), waker(rstd::move(waker)) {}

    TimerEntry(const TimerEntry&)            = delete;
    TimerEntry& operator=(const TimerEntry&) = delete;
    TimerEntry(TimerEntry&&) noexcept        = default;
    auto operator=(TimerEntry&&) noexcept -> TimerEntry& = default;
};

struct RuntimeState {
    Vec<TaskRef>    ready;
    Vec<TaskRef>    tasks;
    Vec<TimerEntry> timers;
    Option<thread::Thread> parker;
    usize next_timer_id { 1 };
    bool  notified { false };
};

struct RuntimeInner {
    sync::Mutex<RuntimeState> state;
    sync::Weak<RuntimeInner>  self;

    RuntimeInner() : state(RuntimeState {}), self(sync::Weak<RuntimeInner>::make()) {}

    void init_self(sync::Weak<RuntimeInner> weak) { self = rstd::move(weak); }

    auto weak() -> sync::Weak<RuntimeInner> { return self.clone(); }

    void notify_locked(RuntimeState& st) {
        st.notified = true;
        if (st.parker.is_some()) {
            thread::unpark(*st.parker);
        }
    }

    void notify() {
        auto st = state.lock().unwrap_unchecked();
        notify_locked(*st);
    }

    void set_parker(thread::Thread thread) {
        auto st    = state.lock().unwrap_unchecked();
        st->parker = Some(rstd::move(thread));
    }

    void clear_parker() {
        auto st    = state.lock().unwrap_unchecked();
        st->parker = None();
    }

    void remove_task_locked(RuntimeState& st, TaskStateBase* task);
    void spawn(TaskRef task);
    void schedule(TaskRef task);

    auto add_timer(time::Instant deadline, task::Waker waker) -> usize {
        auto st = state.lock().unwrap_unchecked();
        auto id = st->next_timer_id++;
        st->timers.push(TimerEntry { id, deadline, rstd::move(waker) });
        notify_locked(*st);
        return id;
    }

    void update_timer(usize id, task::Waker waker) {
        auto st = state.lock().unwrap_unchecked();
        for (usize i = 0; i < st->timers.len(); ++i) {
            if (st->timers[i].id == id) {
                st->timers[i].waker = rstd::move(waker);
                notify_locked(*st);
                return;
            }
        }
    }

    void cancel_timer(usize id) {
        auto st = state.lock().unwrap_unchecked();
        for (usize i = 0; i < st->timers.len(); ++i) {
            if (st->timers[i].id == id) {
                st->timers.remove(i);
                return;
            }
        }
    }

    void wake_expired_timers() {
        auto wakers = Vec<task::Waker>::make();
        {
            auto st  = state.lock().unwrap_unchecked();
            auto now = time::Instant::now();
            for (usize i = 0; i < st->timers.len();) {
                if (st->timers[i].deadline <= now) {
                    auto entry = st->timers.remove(i);
                    wakers.push(rstd::move(entry.waker));
                } else {
                    ++i;
                }
            }
        }

        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
    }

    auto next_timer_delay_locked(RuntimeState& st) -> Option<time::Duration> {
        if (st.timers.is_empty()) {
            return None();
        }

        auto now      = time::Instant::now();
        auto deadline = st.timers[0].deadline;
        for (usize i = 1; i < st.timers.len(); ++i) {
            if (st.timers[i].deadline < deadline) {
                deadline = st.timers[i].deadline;
            }
        }

        if (deadline <= now) {
            return Some<time::Duration>(time::Duration_ZERO);
        }
        return Some(deadline - now);
    }

    void wait_for_work() {
        auto timeout = Option<time::Duration> {};
        {
            auto st = state.lock().unwrap_unchecked();
            if (! st->ready.is_empty() || st->notified) {
                st->notified = false;
                return;
            }
            timeout      = next_timer_delay_locked(*st);
            st->notified = false;
        }

        if (timeout.is_some()) {
            thread::park_timeout(*timeout);
        } else {
            thread::park();
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

inline void RuntimeInner::remove_task_locked(RuntimeState& st, TaskStateBase* task) {
    for (usize i = 0; i < st.tasks.len(); ++i) {
        if (st.tasks[i].get() == task) {
            st.tasks.remove(i);
            return;
        }
    }
}

inline void RuntimeInner::spawn(TaskRef task) {
    auto st = state.lock().unwrap_unchecked();
    task->queued = true;
    st->tasks.push(task.clone());
    st->ready.push(rstd::move(task));
    notify_locked(*st);
}

inline void RuntimeInner::schedule(TaskRef task) {
    auto st = state.lock().unwrap_unchecked();
    if (task->completed || task->queued) {
        return;
    }
    task->queued = true;
    st->ready.push(rstd::move(task));
    notify_locked(*st);
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
    rt->remove_task_locked(*st, this);
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
            rt->remove_task_locked(*st, this);
            rt->notify_locked(*st);
            should_complete = true;
        }
    }

    if (should_complete) {
        complete_abort();
    }
}

inline auto task_waker_clone(voidp data) -> voidp {
    return new TaskRef(static_cast<TaskRef*>(data)->clone());
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

inline const task::RawWakerVTable TASK_WAKER_VTABLE {
    &task_waker_clone,
    &task_waker_wake,
    &task_waker_wake_by_ref,
    &task_waker_drop,
};

inline auto make_task_waker(const TaskRef& task) -> task::Waker {
    return task::Waker::from_raw(task::RawWaker {
        new TaskRef(task.clone()),
        &TASK_WAKER_VTABLE,
    });
}

inline auto runtime_waker_clone(voidp data) -> voidp {
    return new sync::Weak<RuntimeInner>(static_cast<sync::Weak<RuntimeInner>*>(data)->clone());
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

inline const task::RawWakerVTable RUNTIME_WAKER_VTABLE {
    &runtime_waker_clone,
    &runtime_waker_wake,
    &runtime_waker_wake_by_ref,
    &runtime_waker_drop,
};

inline auto make_runtime_waker(RuntimeInner& runtime) -> task::Waker {
    return task::Waker::from_raw(task::RawWaker {
        new sync::Weak<RuntimeInner>(runtime.weak()),
        &RUNTIME_WAKER_VTABLE,
    });
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
