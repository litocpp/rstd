export module rstd:async.runtime_driver;
import :async.task;

using namespace rstd;

namespace rstd::async
{

template<typename T>
class RuntimeCoroDriver {
    coro<T> m_coro;
    bool    m_completed { false };

public:
    using Output = T;

    explicit RuntimeCoroDriver(coro<T>&& task) : m_coro(rstd::move(task)) {}

    RuntimeCoroDriver(const RuntimeCoroDriver&)            = delete;
    auto operator=(const RuntimeCoroDriver&) -> RuntimeCoroDriver& = delete;
    RuntimeCoroDriver(RuntimeCoroDriver&&) noexcept                    = default;
    auto operator=(RuntimeCoroDriver&&) noexcept -> RuntimeCoroDriver& = default;
    ~RuntimeCoroDriver()                                              = default;

    auto poll(mut_ref<RuntimeCoroDriver> self, task::Context& cx) -> task::Poll<T> {
        return resume_coro(self->m_coro, self->m_completed, cx);
    }
};

template<typename T>
auto make_runtime_driver(coro<T> task) -> RuntimeCoroDriver<T> {
    return RuntimeCoroDriver<T> { rstd::move(task) };
}

} // namespace rstd::async
