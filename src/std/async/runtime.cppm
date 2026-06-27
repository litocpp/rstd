export module rstd:async.runtime;
export import :async.forward;
export import :async.runtime_core;
import :async.spawn;
import rstd.alloc;
import :sync;

namespace rstd::async
{

export class Runtime {
    sync::Arc<RuntimeInner> inner;

    void run_ready() {
        while (true) {
            auto task_state = TaskRef {};
            {
                auto st = inner->state.lock().unwrap_unchecked();
                if (st->ready.is_empty()) {
                    return;
                }
                task_state = st->ready.remove(0);
                task_state->queued = false;
                if (task_state->completed) {
                    continue;
                }
            }

            auto waker = make_task_waker(task_state);
            auto cx    = task::Context { waker };
            task_state->poll(cx);
        }
    }

public:
    Runtime() : inner(sync::Arc<RuntimeInner>::make()) {
        inner->init_self(inner.downgrade());
    }

    Runtime(const Runtime&)            = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept        = default;
    auto operator=(Runtime&&) noexcept -> Runtime& = default;
    ~Runtime()                         = default;

    template<future::FutureLike F>
    auto spawn_local(F future) -> JoinHandle<future::future_output_t<F>> {
        return ::spawn_on(*inner.as_ptr().as_raw_ptr(), rstd::move(future));
    }

    template<future::FutureLike F>
    auto block_on(F future) -> future::future_output_t<F> {
        auto& runtime = *inner.as_ptr().as_raw_ptr();
        auto  scope   = RuntimeScope { runtime };
        auto  parker  = ParkerScope { runtime };
        auto  waker   = make_runtime_waker(runtime);
        auto  cx      = task::Context { waker };

        while (true) {
            inner->wake_expired_timers();
            auto out = future::poll(future, cx);
            if (out.is_ready()) {
                run_ready();
                if constexpr (mtp::is_void<future::future_output_t<F>>) {
                    rstd::move(out).take();
                    return;
                } else {
                    return rstd::move(out).take();
                }
            }

            run_ready();
            inner->wake_expired_timers();
            inner->wait_for_work();
        }
    }
};

export template<future::FutureLike F>
auto block_on(F future) -> future::future_output_t<F> {
    auto runtime = Runtime {};
    return runtime.block_on(rstd::move(future));
}

} // namespace rstd::async
