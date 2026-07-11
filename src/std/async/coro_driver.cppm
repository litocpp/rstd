export module rstd:async.coro_driver;
import :async.runtime_core;
import :async.task;

using namespace rstd;

namespace rstd::async
{

enum class CoroDriveState
{
    Pending,
    Ready,
    External,
};

enum class CoroExternalSegmentState
{
    Runtime,
    External,
};

class CoroExternalSegmentResult {
    CoroExternalSegmentState m_state;
    ResumePlacement          m_placement;

    explicit CoroExternalSegmentResult(CoroExternalSegmentState state)
        : m_state(state), m_placement(ResumePlacement::runtime_worker()) {}

    explicit CoroExternalSegmentResult(ResumePlacement placement)
        : m_state(CoroExternalSegmentState::External), m_placement(rstd::move(placement)) {}

public:
    static auto Runtime() -> CoroExternalSegmentResult {
        return CoroExternalSegmentResult { CoroExternalSegmentState::Runtime };
    }

    static auto External(ResumePlacement placement) -> CoroExternalSegmentResult {
        return CoroExternalSegmentResult { rstd::move(placement) };
    }

    auto is_runtime() const noexcept -> bool {
        return m_state == CoroExternalSegmentState::Runtime;
    }

    auto is_external() const noexcept -> bool {
        return m_state == CoroExternalSegmentState::External;
    }

    auto take_placement() -> ResumePlacement { return rstd::move(m_placement); }
};

template<typename T>
class CoroDriveResult {
    CoroDriveState  m_state;
    Option<T>       m_value;
    ResumePlacement m_placement;

    CoroDriveResult(CoroDriveState state, ResumePlacement placement)
        : m_state(state), m_value(None()), m_placement(rstd::move(placement)) {}

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
        return CoroDriveResult { CoroDriveState::External, rstd::move(placement) };
    }

    auto is_pending() const noexcept -> bool { return m_state == CoroDriveState::Pending; }
    auto is_ready() const noexcept -> bool { return m_state == CoroDriveState::Ready; }
    auto is_external() const noexcept -> bool { return m_state == CoroDriveState::External; }

    auto take() -> T { return rstd::move(m_value).unwrap_unchecked(); }
    auto take_placement() -> ResumePlacement { return rstd::move(m_placement); }
};

template<>
class CoroDriveResult<void> {
    CoroDriveState  m_state;
    ResumePlacement m_placement;

    explicit CoroDriveResult(CoroDriveState state)
        : m_state(state), m_placement(ResumePlacement::runtime_worker()) {}

    explicit CoroDriveResult(ResumePlacement placement)
        : m_state(CoroDriveState::External), m_placement(rstd::move(placement)) {}

public:
    static auto Pending() -> CoroDriveResult { return CoroDriveResult { CoroDriveState::Pending }; }
    static auto Ready() -> CoroDriveResult { return CoroDriveResult { CoroDriveState::Ready }; }

    static auto External(ResumePlacement placement) -> CoroDriveResult {
        return CoroDriveResult { rstd::move(placement) };
    }

    auto is_pending() const noexcept -> bool { return m_state == CoroDriveState::Pending; }
    auto is_ready() const noexcept -> bool { return m_state == CoroDriveState::Ready; }
    auto is_external() const noexcept -> bool { return m_state == CoroDriveState::External; }

    constexpr void take() const noexcept {}
    auto           take_placement() -> ResumePlacement { return rstd::move(m_placement); }
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
    if (handle.done()) {
        completed = true;
        if constexpr (mtp::is_void<T>) {
            return CoroDriveResult<void>::Ready();
        } else {
            return CoroDriveResult<T>::Ready(promise.take_result());
        }
    }

    while (true) {
        auto await_context = async::AwaitContext { cx, current_execution_domain() };
        if (promise.awaiting) {
            auto awaiting = promise.awaiting;
            if (awaiting.resume(await_context) == async::AwaitOperationState::Pending) {
                return CoroDriveResult<T>::Pending();
            }
            auto placement = awaiting.placement();
            if (! placement.is_runtime_worker()) {
                promise.awaiting = {};
                return CoroDriveResult<T>::External(rstd::move(placement));
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
auto resume_coro_external_segment(coro<T>& task, bool completed, task::Context& cx)
    -> CoroExternalSegmentResult {
    auto& handle = CoroAccess<T>::handle(task);
    if (! handle) {
        rstd::panic { "empty async coro resumed" };
    }
    if (completed) {
        rstd::panic { "async coro resumed after completion" };
    }

    auto& promise    = handle.promise();
    promise.awaiting = {};
    handle.resume();

    if (handle.done() || ! promise.awaiting) {
        return CoroExternalSegmentResult::Runtime();
    }

    auto await_context = AwaitContext { cx, ExecutionDomainKind::ExternalExecutor };
    auto awaiting      = promise.awaiting;
    if (awaiting.resume_external(await_context) == AwaitOperationState::Pending) {
        return CoroExternalSegmentResult::Runtime();
    }

    auto placement = awaiting.placement();
    if (! placement.is_external_executor()) {
        return CoroExternalSegmentResult::Runtime();
    }

    promise.awaiting = {};
    return CoroExternalSegmentResult::External(rstd::move(placement));
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
    explicit CoroWaitOperation(coro<T>&& task): m_coro(rstd::move(task)) {}

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

    auto placement() const -> ResumePlacement { return ResumePlacement::runtime_worker(); }

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
    explicit CoroWaitOperation(coro<void>&& task): m_coro(rstd::move(task)) {}

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

    auto placement() const -> ResumePlacement { return ResumePlacement::runtime_worker(); }

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
