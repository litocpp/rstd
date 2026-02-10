module;
#include <rstd/macro.hpp>
export module rstd:thread.join_handle;
export import :thread.lifecycle;
export import :thread.thread;
export import rstd.core;

namespace rstd::thread
{

template<typename T>
class JoinHandle {
    lifecycle::JoinInner<T> inner;

    JoinHandle(lifecycle::JoinInner<T> inner): inner(rstd::move(inner)) {}

public:
    USE_TRAIT(JoinHandle)

    JoinHandle() = delete;

    auto thread() const -> Thread {
        return inner.thread();
    }

    auto join() -> Result<T> {
        return inner.join();
    }

    auto is_finished() const -> bool {
        return inner.is_finished();
    }

    friend struct lifecycle::JoinInner<T>;
};

export template<typename T>
auto make_join_handle(lifecycle::JoinInner<T> inner) -> JoinHandle<T> {
    return JoinHandle<T>(rstd::move(inner));
}

} // namespace rstd::thread
