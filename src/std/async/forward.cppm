module;
#include <coroutine>
export module rstd:async.forward;
export import rstd.core;

namespace std
{
export using std::coroutine_handle;
export using std::coroutine_traits;
export using std::suspend_always;
} // namespace std

namespace rstd::async
{

export struct JoinError {
    enum class Kind
    {
        Aborted,
    };

    Kind kind { Kind::Aborted };

    constexpr auto is_aborted() const noexcept -> bool { return kind == Kind::Aborted; }
    friend constexpr auto operator==(JoinError, JoinError) noexcept -> bool = default;
};

export template<typename T>
class coro;

export template<typename T>
class JoinHandle;

export class Runtime;

export class RuntimeHandle;

export struct YieldNow;

export class Sleep;

} // namespace rstd::async

export namespace rstd::prelude
{

using rstd::async::coro;

} // namespace rstd::prelude
