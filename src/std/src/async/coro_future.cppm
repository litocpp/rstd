export module rstd:async.coro_future;
export import :async.task;
import :async.coro_driver;
import :async.runtime_driver;
import :async.runtime_core;
import :async.facility;
import :async.atomic_waker;
import :sync;

import rstd.alloc;

using namespace rstd;

namespace rstd::async
{

template<typename T>
auto finish_coro_future(RuntimeCoroDriver<T>& driver) -> task::Poll<T> {
    if constexpr (mtp::is_void<T>) {
        driver.take_result();
        driver.release_frame();
        return task::Poll<void>::Ready();
    } else {
        auto result = driver.take_result();
        driver.release_frame();
        return task::Poll<T>::Ready(rstd::move(result));
    }
}

template<typename T>
class CoroFutureState {
    using Stored = mtp::void_empty_t<T>;
    struct Fields {
        Option<RuntimeCoroDriver<T>> driver;
        Option<Stored>               result;
        usize                        generation {};
        bool                         queued {};
        bool                         closed {};
        explicit Fields(RuntimeCoroDriver<T> value): driver(Some(rstd::move(value))) {}
    };
    sync::Mutex<Fields> fields_;
    AtomicWaker         waker_;

    struct Job {
        sync::Arc<CoroFutureState> state;
        usize                      generation;
        static void                run(voidp data) {
            auto& job = *static_cast<Job*>(data);
            job.state->run(job.state, job.generation);
        }
        static void cancel(voidp data) {
            auto& job = *static_cast<Job*>(data);
            job.state->cancel(job.generation);
        }
        static void drop(voidp data) { ::alloc::raw_delete(static_cast<Job*>(data)); }
    };

    static void
    submit(const sync::Arc<CoroFutureState>& state, FacilityEndpoint endpoint, usize generation) {
        static const RawFacilityJobVTable vtable { Job::run, Job::cancel, Job::drop };
        auto*                             job = ::alloc::raw_new<Job>(state.clone(), generation);
        (void)endpoint.submit(FacilityJob::from_raw(RawFacilityJob { job, &vtable }));
    }

    void finish(Fields& fields) {
        if constexpr (mtp::is_void<T>) {
            fields.driver->take_result();
            fields.result = Some(empty {});
        } else {
            fields.result = Some(fields.driver->take_result());
        }
        fields.driver = None();
    }

    void run(const sync::Arc<CoroFutureState>& self, usize generation) {
        Option<FacilityEndpoint> endpoint;
        bool                     wake = false;
        usize                    next_generation {};
        {
            auto fields = fields_.lock().unwrap_unchecked();
            if (fields->closed || ! fields->queued || fields->generation != generation) return;
            fields->queued = false;
            auto scope     = ExecutionDomainScope { ExecutionDomainKind::ExternalExecutor };
            auto cx        = task::Context { task::Waker::noop() };
            auto action    = fields->driver->resume_external_segment(cx, AwaitMode::FuturePoll);
            if (action.is_submit_facility()) {
                fields->queued  = true;
                next_generation = ++fields->generation;
                endpoint        = Some(action.take_endpoint());
            } else {
                if (action.is_final_suspended()) finish(*fields);
                wake = true;
            }
        }
        if (endpoint.is_some()) submit(self, endpoint.take().unwrap_unchecked(), next_generation);
        if (wake) waker_.wake();
    }

    void cancel(usize generation) {
        {
            auto fields = fields_.lock().unwrap_unchecked();
            if (fields->closed || ! fields->queued || fields->generation != generation) return;
            fields->queued = false;
        }
        waker_.wake();
    }

public:
    explicit CoroFutureState(RuntimeCoroDriver<T> driver): fields_(rstd::move(driver)) {}

    void handoff(const sync::Arc<CoroFutureState>& self,
                 FacilityEndpoint                  endpoint,
                 const task::Waker&                waker) {
        usize generation {};
        waker_.register_waker(waker);
        {
            auto fields    = fields_.lock().unwrap_unchecked();
            fields->queued = true;
            generation     = ++fields->generation;
        }
        submit(self, rstd::move(endpoint), generation);
    }

    auto poll(const sync::Arc<CoroFutureState>& self, task::Context& cx) -> task::Poll<T> {
        Option<FacilityEndpoint> endpoint;
        Option<Stored>           completed;
        usize                    generation {};
        waker_.register_context(cx);
        auto locked = fields_.try_lock();
        if (locked.is_err()) return task::Poll<T>::Pending();
        {
            auto fields = rstd::move(locked).unwrap_unchecked();
            if (fields->closed) rstd::panic { "CoroFuture polled after completion" };
            if (fields->queued) {
                return task::Poll<T>::Pending();
            }
            if (fields->result.is_none()) {
                auto action = fields->driver->drive(cx, AwaitMode::FuturePoll);
                if (action.is_final_suspended())
                    finish(*fields);
                else if (action.is_submit_facility()) {
                    fields->queued = true;
                    generation     = ++fields->generation;
                    endpoint       = Some(action.take_endpoint());
                } else if (! action.is_suspended()) {
                    rstd::panic { "CoroFuture await operation has no poll path" };
                }
            }
            if (fields->result.is_some()) {
                fields->closed = true;
                completed      = fields->result.take();
            }
        }
        if (completed.is_some()) {
            waker_.clear();
            if constexpr (mtp::is_void<T>)
                return task::Poll<void>::Ready();
            else
                return task::Poll<T>::Ready(completed.take().unwrap_unchecked());
        }
        if (endpoint.is_some()) submit(self, endpoint.take().unwrap_unchecked(), generation);
        return task::Poll<T>::Pending();
    }

    // Wait for an executing segment before releasing its frame and borrowed inputs.
    void close() {
        {
            auto fields    = fields_.lock().unwrap_unchecked();
            fields->closed = true;
            fields->queued = false;
            fields->driver = None();
            fields->result = None();
        }
        waker_.clear();
    }
};

/// Adapts a coroutine to Future while preserving operation ownership across polls.
export template<typename T>
class CoroFuture {
    Option<RuntimeCoroDriver<T>>          driver_;
    Option<sync::Arc<CoroFutureState<T>>> external_;

    void close() {
        if (external_.is_some()) (*external_)->close();
        external_ = None();
        driver_   = None();
    }

public:
    using Output = T;
    explicit CoroFuture(coro<T> value): driver_(Some(make_runtime_driver(rstd::move(value)))) {}
    CoroFuture(const CoroFuture&)                    = delete;
    auto operator=(const CoroFuture&) -> CoroFuture& = delete;
    CoroFuture(CoroFuture&& other) noexcept
        : driver_(other.driver_.take()), external_(other.external_.take()) {}
    auto operator=(CoroFuture&& other) noexcept -> CoroFuture& {
        if (this != &other) {
            close();
            driver_   = other.driver_.take();
            external_ = other.external_.take();
        }
        return *this;
    }
    ~CoroFuture() { close(); }

    auto poll(mut_ref<CoroFuture> self, task::Context& cx) -> task::Poll<T> {
        if (self->external_.is_some()) return (*self->external_)->poll(*self->external_, cx);
        if (self->driver_.is_none()) rstd::panic { "CoroFuture polled after completion or move" };
        auto action = self->driver_->drive(cx, AwaitMode::FuturePoll);
        if (action.is_final_suspended()) {
            auto result   = finish_coro_future(*self->driver_);
            self->driver_ = None();
            return result;
        }
        if (action.is_submit_facility()) {
            self->external_ =
                Some(sync::Arc<CoroFutureState<T>>::make(self->driver_.take().unwrap_unchecked()));
            (*self->external_)->handoff(*self->external_, action.take_endpoint(), cx.waker());
        } else if (! action.is_suspended()) {
            rstd::panic { "CoroFuture await operation has no poll path" };
        }
        return task::Poll<T>::Pending();
    }
};

template<typename T>
auto coro<T>::into_future() -> Future {
    return CoroFuture<T>(rstd::move(*this));
}
inline auto coro<void>::into_future() -> Future {
    return CoroFuture<void>(rstd::move(*this));
}

/// Existing futures pass through; native awaitables use the coroutine driver's poll mode.
export template<AwaitableInput A>
auto into_future(A awaitable) {
    if constexpr (FutureInput<A>)
        return rstd::move(awaitable);
    else if constexpr (IntoFutureInput<A>)
        return as<IntoFuture>(awaitable).into_future();
    else
        return CoroFuture<await_output_t<A>>(into_coro(rstd::move(awaitable)));
}

} // namespace rstd::async
