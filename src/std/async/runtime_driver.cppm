export module rstd:async.runtime_driver;
import :async.task;
import :async.coro_driver;

using namespace rstd;

namespace rstd::async
{

template<typename T>
class RuntimeCoroDriver {
    coro<T>        m_coro;
    CoroFrameState m_state { CoroFrameState::InitialSuspended };

public:
    explicit RuntimeCoroDriver(coro<T>&& task): m_coro(rstd::move(task)) {}

    RuntimeCoroDriver(const RuntimeCoroDriver&)                        = delete;
    auto operator=(const RuntimeCoroDriver&) -> RuntimeCoroDriver&     = delete;
    RuntimeCoroDriver(RuntimeCoroDriver&&) noexcept                    = default;
    auto operator=(RuntimeCoroDriver&&) noexcept -> RuntimeCoroDriver& = default;
    ~RuntimeCoroDriver()                                               = default;

    auto drive(task::Context& cx) { return resume_coro(m_coro, m_state, cx); }

    auto resume_external_segment(task::Context& cx) {
        return resume_coro_external_segment(m_coro, m_state, cx);
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        return complete_coro_facility(m_coro, m_state, event);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        return submit_coro_completion(m_coro, m_state, rstd::move(token));
    }

    auto take_result() -> T { return take_coro_result(m_coro, m_state); }
};

template<typename T>
auto make_runtime_driver(coro<T> task) -> RuntimeCoroDriver<T> {
    return RuntimeCoroDriver<T> { rstd::move(task) };
}

} // namespace rstd::async
