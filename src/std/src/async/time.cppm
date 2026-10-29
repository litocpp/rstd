export module rstd:async.time;
export import :async.forward;
import :async.awaitable;
import :async.reactor;
import :async.runtime_core;
import :async.timer_facility;
import :time;

using namespace rstd::literals;

namespace rstd::async
{

export struct Elapsed {
    friend constexpr auto operator==(Elapsed, Elapsed) noexcept -> bool = default;
};

export struct YieldNow {
    using Output = void;

    bool yielded { false };

    auto poll(mut_ref<YieldNow> self, task::Context& cx) -> task::Poll<void> {
        auto& value = *self;
        if (! value.yielded) {
            value.yielded = true;
            cx.waker().wake_by_ref();
            return task::Poll<void>::Pending();
        }
        return task::Poll<void>::Ready();
    }
};

export class Sleep {
#if LITO_FEAT_ASYNC_DEBUG
    debug::WaitGuard debug_wait_;
#endif
    time::Instant                     deadline;
    Option<TimerRegistration>         timer;
    Option<TimerFacilityRegistration> facility_timer;
    Option<FacilityEventKind>         facility_result;
    bool                              facility_submitted { false };
    bool                              completed { false };

    void cancel() {
#if LITO_FEAT_ASYNC_DEBUG
        debug_wait_.reset();
#endif
        timer          = None();
        facility_timer = None();
    }

    auto finish() -> AwaitTransition {
#if LITO_FEAT_ASYNC_DEBUG
        debug_wait_.reset();
#endif
        completed = true;
        return AwaitTransition::continue_();
    }

public:
    using Output = void;

    explicit Sleep(time::Duration duration)
        : deadline(time::Instant::now() + duration),
          timer(None()),
          facility_timer(None()),
          facility_result(None()) {}

    Sleep(const Sleep&)            = delete;
    Sleep& operator=(const Sleep&) = delete;

    Sleep(Sleep&& other) noexcept
        :
#if LITO_FEAT_ASYNC_DEBUG
          debug_wait_(rstd::move(other.debug_wait_)),
#endif
          deadline(other.deadline),
          timer(other.timer.take()),
          facility_timer(other.facility_timer.take()),
          facility_result(other.facility_result.take()),
          facility_submitted(rstd::exchange(other.facility_submitted, false)),
          completed(rstd::exchange(other.completed, false)) {
    }

    auto operator=(Sleep&& other) noexcept -> Sleep& {
        if (this != &other) {
            cancel();
#if LITO_FEAT_ASYNC_DEBUG
            debug_wait_ = rstd::move(other.debug_wait_);
#endif
            deadline           = other.deadline;
            timer              = other.timer.take();
            facility_timer     = other.facility_timer.take();
            facility_result    = other.facility_result.take();
            facility_submitted = rstd::exchange(other.facility_submitted, false);
            completed          = rstd::exchange(other.completed, false);
        }
        return *this;
    }

    ~Sleep() { cancel(); }

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        auto* rt = CURRENT_RUNTIME;
        if (rt == nullptr) {
            rstd::panic { "async::sleep advanced without an async runtime" };
        }
        if (! rt->time_enabled()) {
            rstd::panic { "async::sleep requires a runtime with time enabled" };
        }
        if (facility_result.is_some()) {
            auto result        = facility_result.take().unwrap_unchecked();
            facility_timer     = None();
            facility_submitted = false;
            if (result != FacilityEventKind::Ready) {
                rstd::panic { "async::sleep timer facility failed" };
            }
            return finish();
        }
        if (time::Instant::now() >= deadline) {
            return finish();
        }
        if (facility_submitted) {
            return AwaitTransition::suspend();
        }
        facility_submitted = true;
        return AwaitTransition::submit_completion(TIMER_FACILITY_ID);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        if (! facility_submitted || facility_timer.is_some()) {
            return FacilityCompletionSubmitResult::rejected(rstd::move(token));
        }
        auto submitted = TimerFacilityRegistration::submit(deadline, rstd::move(token));
        if (submitted.is_err()) {
            return FacilityCompletionSubmitResult::rejected(
                rstd::move(submitted).unwrap_err_unchecked());
        }
        auto registration = rstd::move(submitted).unwrap_unchecked();
        auto cancellation = registration.cancellation();
#if LITO_FEAT_ASYNC_DEBUG
        debug_wait_.begin("timer"_str, registration.debug_id(), {});
#endif
        facility_timer = Some(rstd::move(registration));
        return FacilityCompletionSubmitResult::accepted(rstd::move(cancellation));
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        if (! facility_submitted || facility_result.is_some()) {
            return false;
        }
        if (event.has_poll_event()) {
            auto poll_event = event.take_poll_event();
            if (poll_event.kind() != PollEventKind::Timer &&
                poll_event.kind() != PollEventKind::BackendError) {
                return false;
            }
            facility_result =
                Some(poll_event.kind() == PollEventKind::Timer ? FacilityEventKind::Ready
                                                               : FacilityEventKind::Error);
        } else {
            facility_result = Some(event.kind());
        }
        return true;
    }

    void take_output() const {
        if (! completed) {
            rstd::panic { "async::sleep output taken before completion" };
        }
    }

    auto poll(mut_ref<Sleep> self, task::Context& cx) -> task::Poll<void> {
        auto& sleep = *self;
        auto* rt    = CURRENT_RUNTIME;
        if (rt == nullptr) {
            rstd::panic { "async::sleep polled without an async runtime" };
        }
        if (! rt->time_enabled()) {
            rstd::panic { "async::sleep requires a runtime with time enabled" };
        }

        if (time::Instant::now() >= sleep.deadline) {
            sleep.cancel();
            return task::Poll<void>::Ready();
        }

        if (sleep.timer.is_none()) {
            auto timer = TimerRegistration::register_deadline(sleep.deadline, cx.waker().clone());
            if (timer.is_err()) {
                rstd::panic { "async::sleep failed to register timer" };
            }
            sleep.timer = Some(rstd::move(timer).unwrap_unchecked());
        } else {
            auto updated = sleep.timer->update_waker(cx.waker().clone());
            if (updated.is_err()) {
                rstd::panic { "async::sleep failed to update timer" };
            }
        }

#if LITO_FEAT_ASYNC_DEBUG
        sleep.debug_wait_.begin("timer"_str, sleep.timer->debug_id(), {});
#endif
        return task::Poll<void>::Pending();
    }
};

template<>
struct AwaitableTraits<Sleep> {
    using Output = void;

    static auto make_suspension(Sleep&& sleep) {
        return AwaitSuspension<Sleep> { rstd::move(sleep) };
    }
};

export template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
class Timeout {
    F     future_;
    Sleep delay_;
    bool  completed { false };

public:
    using Output = Result<mtp::void_empty_t<future::future_output_t<F>>, Elapsed>;

    Timeout(F future, time::Duration duration): future_(rstd::move(future)), delay_(duration) {}

    Timeout(const Timeout&)                        = delete;
    Timeout& operator=(const Timeout&)             = delete;
    Timeout(Timeout&&) noexcept                    = default;
    auto operator=(Timeout&&) noexcept -> Timeout& = default;

    auto poll(mut_ref<Timeout> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
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

export template<typename F>
    requires Impled<mtp::rm_cvf<F>, future::Future<future::future_output_t<F>>>
auto timeout(F future, time::Duration duration) -> Timeout<F> {
    return Timeout<F> { rstd::move(future), duration };
}

} // namespace rstd::async
