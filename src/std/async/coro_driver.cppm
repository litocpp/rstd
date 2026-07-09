export module rstd:async.coro_driver;
import :async.task;

using namespace rstd;

namespace rstd::async
{

enum class CoroDriveState {
    Pending,
    Ready,
    External,
};

template<typename T>
class CoroDriveResult {
    CoroDriveState  m_state;
    Option<T>       m_value;
    ResumePlacement m_placement;

    CoroDriveResult(CoroDriveState state, ResumePlacement placement)
        : m_state(state), m_value(None()), m_placement(placement) {}

public:
    static auto Pending() -> CoroDriveResult {
        return CoroDriveResult { CoroDriveState::Pending, ResumePlacement::runtime_worker() };
    }

    static auto Ready(T value) -> CoroDriveResult {
        auto out    = CoroDriveResult { CoroDriveState::Ready, ResumePlacement::runtime_worker() };
        out.m_value = Some(rstd::move(value));
        return out;
    }

    static auto External(ResumePlacement placement) -> CoroDriveResult {
        return CoroDriveResult { CoroDriveState::External, placement };
    }

    auto is_pending() const noexcept -> bool { return m_state == CoroDriveState::Pending; }
    auto is_ready() const noexcept -> bool { return m_state == CoroDriveState::Ready; }
    auto is_external() const noexcept -> bool { return m_state == CoroDriveState::External; }

    auto take() -> T { return rstd::move(m_value).unwrap_unchecked(); }
    auto placement() const noexcept -> ResumePlacement { return m_placement; }
};

template<>
class CoroDriveResult<void> {
    CoroDriveState  m_state;
    ResumePlacement m_placement;

    explicit CoroDriveResult(CoroDriveState state)
        : m_state(state), m_placement(ResumePlacement::runtime_worker()) {}

    explicit CoroDriveResult(ResumePlacement placement)
        : m_state(CoroDriveState::External), m_placement(placement) {}

public:
    static auto Pending() -> CoroDriveResult { return CoroDriveResult { CoroDriveState::Pending }; }
    static auto Ready() -> CoroDriveResult { return CoroDriveResult { CoroDriveState::Ready }; }

    static auto External(ResumePlacement placement) -> CoroDriveResult {
        return CoroDriveResult { placement };
    }

    auto is_pending() const noexcept -> bool { return m_state == CoroDriveState::Pending; }
    auto is_ready() const noexcept -> bool { return m_state == CoroDriveState::Ready; }
    auto is_external() const noexcept -> bool { return m_state == CoroDriveState::External; }

    constexpr void take() const noexcept {}
    auto placement() const noexcept -> ResumePlacement { return m_placement; }
};

template<typename T>
auto resume_coro(coro<T>& task, bool& completed, task::Context& cx) -> CoroDriveResult<T> {
    auto& handle = CoroAccess<T>::handle(task);
    if (! handle) {
        rstd::panic { "empty async coro resumed" };
    }
    if (completed) {
        rstd::panic { "async coro resumed after completion" };
    }

    auto& promise = handle.promise();
    while (true) {
        auto await_context = async::AwaitContext { cx };
        if (promise.awaiting) {
            auto awaiting = promise.awaiting;
            if (awaiting.resume(await_context) == async::AwaitOperationState::Pending) {
                return CoroDriveResult<T>::Pending();
            }
            auto placement = awaiting.placement();
            if (! placement.is_runtime_worker()) {
                return CoroDriveResult<T>::External(placement);
            }
            promise.awaiting = {};
            handle.resume();
        } else {
            handle.resume();
        }

        if (handle.done()) {
            completed = true;
            if constexpr (mtp::is_void<T>) {
                return CoroDriveResult<void>::Ready();
            } else {
                return CoroDriveResult<T>::Ready(promise.take_result());
            }
        }
    }
}

template<typename T>
class CoroWaitOperation {
public:
    using Output = T;

private:
    coro<T>   m_coro;
    bool      m_completed { false };
    Option<T> m_ready;

public:
    explicit CoroWaitOperation(coro<T>&& task) : m_coro(rstd::move(task)) {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = resume_coro(m_coro, m_completed, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        if (out.is_external()) {
            rstd::panic { "child async coro cannot resume on external executor" };
        }
        m_ready.insert(out.take());
        return AwaitOperationState::Ready;
    }

    constexpr auto placement() const noexcept -> ResumePlacement {
        return ResumePlacement::runtime_worker();
    }

    auto take_output() -> T { return rstd::move(m_ready).unwrap_unchecked(); }
};

template<>
class CoroWaitOperation<void> {
public:
    using Output = void;

private:
    coro<void> m_coro;
    bool       m_completed { false };

public:
    explicit CoroWaitOperation(coro<void>&& task) : m_coro(rstd::move(task)) {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = resume_coro(m_coro, m_completed, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        if (out.is_external()) {
            rstd::panic { "child async coro cannot resume on external executor" };
        }
        out.take();
        return AwaitOperationState::Ready;
    }

    constexpr auto placement() const noexcept -> ResumePlacement {
        return ResumePlacement::runtime_worker();
    }

    constexpr void take_output() const noexcept {}
};

template<typename T>
struct AwaitableTraits<coro<T>> {
    using Output = T;

    static auto make_suspension(coro<T>&& task) {
        auto operation = CoroWaitOperation<T> { rstd::move(task) };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }
};

} // namespace rstd::async
