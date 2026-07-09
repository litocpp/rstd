export module rstd:async.runtime_driver;
import :async.task;
import :async.coro_driver;

using namespace rstd;

namespace rstd::async
{

template<typename T>
class RuntimeCoroDriver {
    coro<T> m_coro;
    bool    m_completed { false };

public:
    explicit RuntimeCoroDriver(coro<T>&& task) : m_coro(rstd::move(task)) {}

    RuntimeCoroDriver(const RuntimeCoroDriver&)            = delete;
    auto operator=(const RuntimeCoroDriver&) -> RuntimeCoroDriver& = delete;
    RuntimeCoroDriver(RuntimeCoroDriver&&) noexcept                    = default;
    auto operator=(RuntimeCoroDriver&&) noexcept -> RuntimeCoroDriver& = default;
    ~RuntimeCoroDriver()                                              = default;

    auto drive(task::Context& cx) {
        return resume_coro(m_coro, m_completed, cx);
    }
};

template<typename T>
auto make_runtime_driver(coro<T> task) -> RuntimeCoroDriver<T> {
    return RuntimeCoroDriver<T> { rstd::move(task) };
}

} // namespace rstd::async
