export module rstd:async.time;
export import :async.forward;
export import :async.runtime_core;
import :sync;
import :time;

namespace rstd::async
{

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

export inline auto yield_now() -> YieldNow {
    return YieldNow {};
}

export inline auto sleep(time::Duration duration) -> Sleep {
    return Sleep { duration };
}

} // namespace rstd::async
