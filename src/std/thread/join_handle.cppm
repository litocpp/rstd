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

    auto thread(this Self const& self) -> Thread { return self.inner.thread(); }

    auto join(this Self&& self) -> Result<ret_t> { return rstd::move(self).inner.join(); }

    auto is_finished(this Self const& self) -> bool { return self.inner.is_finished(); }

    friend struct lifecycle::JoinInner<T>;
};

} // namespace rstd::thread
