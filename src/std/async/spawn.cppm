export module rstd:async.spawn;
export import :async.forward;
export import :async.runtime_core;
import rstd.alloc;
import :sync;

using namespace rstd;
using rstd::async::JoinError;

struct JoinHandleFactory;

template<typename T>
struct JoinState {
    struct Fields {
        Option<T>          value;
        Option<JoinError>  error;
        Option<task::Waker> waker;
        Option<TaskRef>    task;
        bool               ready { false };
        bool               consumed { false };
    };

    sync::Mutex<Fields> fields;

    JoinState() : fields(Fields {}) {}

    void set_task(TaskRef task) {
        auto f = fields.lock().unwrap_unchecked();
        f->task = Some(rstd::move(task));
    }

    auto complete_value(T in) -> Option<task::Waker> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            return None();
        }
        f->value = Some(rstd::move(in));
        f->ready = true;
        f->task  = None();
        return f->waker.take();
    }

    auto complete_abort() -> Option<task::Waker> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            return None();
        }
        f->error = Some(JoinError {});
        f->ready = true;
        f->task  = None();
        return f->waker.take();
    }

    auto poll(task::Context& cx) -> task::Poll<Result<T, JoinError>> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            if (f->consumed) {
                rstd::panic { "JoinHandle polled after completion" };
            }
            f->consumed = true;
            if (f->error.is_some()) {
                return task::Poll<Result<T, JoinError>>::Ready(Err(rstd::move(f->error).unwrap_unchecked()));
            }
            return task::Poll<Result<T, JoinError>>::Ready(Ok(rstd::move(f->value).unwrap_unchecked()));
        }
        f->waker = Some(cx.waker().clone());
        return task::Poll<Result<T, JoinError>>::Pending();
    }

    auto is_ready() const -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->ready;
    }

    void abort_task() {
        auto task = Option<TaskRef> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->task.is_some()) {
                task = Some(f->task->clone());
            }
        }
        if (task.is_some()) {
            (*task)->abort();
        }
    }
};

template<>
struct JoinState<void> {
    struct Fields {
        Option<JoinError>  error;
        Option<task::Waker> waker;
        Option<TaskRef>    task;
        bool               ready { false };
        bool               consumed { false };
    };

    sync::Mutex<Fields> fields;

    JoinState() : fields(Fields {}) {}

    void set_task(TaskRef task) {
        auto f = fields.lock().unwrap_unchecked();
        f->task = Some(rstd::move(task));
    }

    auto complete_value() -> Option<task::Waker> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            return None();
        }
        f->ready = true;
        f->task  = None();
        return f->waker.take();
    }

    auto complete_abort() -> Option<task::Waker> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            return None();
        }
        f->error = Some(JoinError {});
        f->ready = true;
        f->task  = None();
        return f->waker.take();
    }

    auto poll(task::Context& cx) -> task::Poll<Result<empty, JoinError>> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->ready) {
            if (f->consumed) {
                rstd::panic { "JoinHandle polled after completion" };
            }
            f->consumed = true;
            if (f->error.is_some()) {
                return task::Poll<Result<empty, JoinError>>::Ready(
                    Err(rstd::move(f->error).unwrap_unchecked()));
            }
            return task::Poll<Result<empty, JoinError>>::Ready(Ok(empty {}));
        }
        f->waker = Some(cx.waker().clone());
        return task::Poll<Result<empty, JoinError>>::Pending();
    }

    auto is_ready() const -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->ready;
    }

    void abort_task() {
        auto task = Option<TaskRef> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->task.is_some()) {
                task = Some(f->task->clone());
            }
        }
        if (task.is_some()) {
            (*task)->abort();
        }
    }
};

template<typename T, typename F>
struct TaskState : TaskStateBase {
    F                       future;
    sync::Arc<JoinState<T>> join;

    TaskState(sync::Weak<RuntimeInner> runtime, F future, sync::Arc<JoinState<T>> join)
        : TaskStateBase(rstd::move(runtime)), future(rstd::move(future)), join(rstd::move(join)) {}

    void poll(task::Context& cx) override {
        auto out = future::poll(future, cx);
        if (out.is_pending()) {
            return;
        }

        auto join_waker = Option<task::Waker> {};
        if constexpr (mtp::is_void<T>) {
            rstd::move(out).take();
            join_waker = join->complete_value();
        } else {
            join_waker = join->complete_value(rstd::move(out).take());
        }

        finish();
        if (join_waker.is_some()) {
            rstd::move(*join_waker).wake();
        }
    }

    void complete_abort() override {
        auto join_waker = join->complete_abort();
        if (join_waker.is_some()) {
            rstd::move(*join_waker).wake();
        }
    }
};

template<future::FutureLike F>
auto spawn_on(RuntimeInner& runtime, F future) -> rstd::async::JoinHandle<future::future_output_t<F>>;

namespace rstd::async
{

export template<typename T>
class JoinHandle {
    sync::Arc<JoinState<T>> state;

    explicit JoinHandle(sync::Arc<JoinState<T>> state) : state(rstd::move(state)) {}

    friend struct ::JoinHandleFactory;

public:
    using Output = Result<mtp::void_empty_t<T>, JoinError>;

    JoinHandle(const JoinHandle&)            = delete;
    JoinHandle& operator=(const JoinHandle&) = delete;
    JoinHandle(JoinHandle&&) noexcept        = default;
    auto operator=(JoinHandle&&) noexcept -> JoinHandle& = default;
    ~JoinHandle() = default;

    void abort() { state->abort_task(); }

    auto is_finished() const -> bool { return state->is_ready(); }

    auto poll(mut_ref<JoinHandle> self, task::Context& cx) -> task::Poll<Output> {
        return self->state->poll(cx);
    }
};

} // namespace rstd::async

struct JoinHandleFactory {
    template<typename T>
    static auto make(sync::Arc<JoinState<T>> state) -> rstd::async::JoinHandle<T> {
        return rstd::async::JoinHandle<T> { rstd::move(state) };
    }
};

template<future::FutureLike F>
auto spawn_on(RuntimeInner& runtime, F future) -> rstd::async::JoinHandle<future::future_output_t<F>> {
    using Output = future::future_output_t<F>;

    auto join = sync::Arc<JoinState<Output>>::make();
    auto task = TaskRef::adopt(
        new TaskState<Output, F>(runtime.weak(), rstd::move(future), join.clone()));
    join->set_task(task.clone());
    runtime.spawn(rstd::move(task));

    return JoinHandleFactory::make<Output>(rstd::move(join));
}

namespace rstd::async
{

export template<future::FutureLike F>
auto spawn(F future) -> JoinHandle<future::future_output_t<F>> {
    auto* runtime = CURRENT_RUNTIME;
    if (runtime == nullptr) {
        rstd::panic { "spawn called without an async runtime" };
    }
    return spawn_on(*runtime, rstd::move(future));
}

export template<future::FutureLike F>
auto spawn_local(F future) -> JoinHandle<future::future_output_t<F>> {
    auto* runtime = CURRENT_RUNTIME;
    if (runtime == nullptr) {
        rstd::panic { "spawn_local called without an async runtime" };
    }
    if (runtime->is_thread_pool()) {
        rstd::panic { "spawn_local is only supported by current-thread runtime" };
    }
    return spawn_on(*runtime, rstd::move(future));
}

} // namespace rstd::async
