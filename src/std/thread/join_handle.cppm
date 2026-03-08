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

    using ret_t = mtp::void_empty_t<T>;

    JoinHandle() = delete;

    static auto make(lifecycle::JoinInner<T> inner) -> Self { return Self { rstd::move(inner) }; }

    auto thread() const -> Thread { return inner.thread(); }

    auto join() && -> Result<ret_t> { return rstd::move(*this).inner.join(); }

    auto is_finished() const -> bool { return inner.is_finished(); }

    friend struct lifecycle::JoinInner<T>;
};

} // namespace rstd::thread
