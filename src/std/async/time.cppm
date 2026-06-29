export module rstd:async.time;
export import :async.forward;
export import :async.runtime_core;
import :sync;
import :time;

namespace rstd::async
{

export struct Elapsed {
    friend constexpr auto operator==(Elapsed, Elapsed) noexcept -> bool = default;
};

export struct YieldNow {
    using Output = void;

    bool yielded { false };

    auto poll(pin::Pin<mut_ref<YieldNow>> self, task::Context& cx) -> task::Poll<void> {
        auto& value = *self.get_unchecked_mut();
        if (! value.yielded) {
            value.yielded = true;
            cx.waker().wake_by_ref();
            return task::Poll<void>::Pending();
        }
        return task::Poll<void>::Ready();
    }
};

export class Sleep {
    time::Instant deadline;
    Option<usize> timer_id;
    sync::Weak<RuntimeInner> runtime;

    void cancel() {
        if (timer_id.is_some()) {
            if (auto rt = runtime.upgrade()) {
                rt->cancel_timer(*timer_id);
            }
            timer_id = None();
        }
    }

public:
    using Output = void;

    explicit Sleep(time::Duration duration)
        : deadline(time::Instant::now() + duration),
          timer_id(None()),
          runtime(sync::Weak<RuntimeInner>::make()) {}

    Sleep(const Sleep&)            = delete;
    Sleep& operator=(const Sleep&) = delete;

    Sleep(Sleep&& other) noexcept
        : deadline(other.deadline),
          timer_id(other.timer_id.take()),
          runtime(rstd::move(other.runtime)) {}

    auto operator=(Sleep&& other) noexcept -> Sleep& {
        if (this != &other) {
            cancel();
            deadline = other.deadline;
            timer_id = other.timer_id.take();
            runtime  = rstd::move(other.runtime);
        }
        return *this;
    }

    ~Sleep() { cancel(); }

    auto poll(pin::Pin<mut_ref<Sleep>> self, task::Context& cx) -> task::Poll<void> {
        auto& sleep = *self.get_unchecked_mut();
        if (time::Instant::now() >= sleep.deadline) {
            sleep.cancel();
            return task::Poll<void>::Ready();
        }

        auto* rt = CURRENT_RUNTIME;
        if (rt == nullptr) {
            rstd::panic { "async::sleep polled without an async runtime" };
        }

        if (sleep.timer_id.is_none()) {
            sleep.runtime  = rt->weak();
            sleep.timer_id = Some(rt->add_timer(sleep.deadline, cx.waker().clone()));
        } else if (auto runtime = sleep.runtime.upgrade()) {
            runtime->update_timer(*sleep.timer_id, cx.waker().clone());
        }

        return task::Poll<void>::Pending();
    }
};

export template<future::FutureLike F>
class Timeout {
    F     future_;
    Sleep delay_;
    bool  completed { false };

public:
    using Output = Result<mtp::void_empty_t<future::future_output_t<F>>, Elapsed>;

    Timeout(F future, time::Duration duration)
        : future_(rstd::move(future)), delay_(duration) {}

    Timeout(const Timeout&)            = delete;
    Timeout& operator=(const Timeout&) = delete;
    Timeout(Timeout&&) noexcept        = default;
    auto operator=(Timeout&&) noexcept -> Timeout& = default;

    auto poll(pin::Pin<mut_ref<Timeout>> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self.get_unchecked_mut();
        if (value.completed) {
            rstd::panic { "async::Timeout polled after completion" };
        }

        auto future_out = future::poll(value.future_, cx);
        if (future_out.is_ready()) {
            value.completed = true;
            if constexpr (mtp::is_void<future::future_output_t<F>>) {
                rstd::move(future_out).take();
                return task::Poll<Output>::Ready(Ok(empty {}));
            } else {
                return task::Poll<Output>::Ready(Ok(rstd::move(future_out).take()));
            }
        }

        auto delay_out = future::poll(value.delay_, cx);
        if (delay_out.is_ready()) {
            rstd::move(delay_out).take();
            value.completed = true;
            return task::Poll<Output>::Ready(Err(Elapsed {}));
        }

        return task::Poll<Output>::Pending();
    }
};

export inline auto yield_now() -> YieldNow {
    return YieldNow {};
}

export inline auto sleep(time::Duration duration) -> Sleep {
    return Sleep { duration };
}

export template<future::FutureLike F>
auto timeout(F future, time::Duration duration) -> Timeout<F> {
    return Timeout<F> { rstd::move(future), duration };
}

} // namespace rstd::async
