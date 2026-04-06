module;
#include <memory>
#include <type_traits>
#include <iterator>
export module rstd.basic:mtp.std;

namespace std
{
export using std::iter_value_t;
}

export namespace rstd::mtp
{

/// Deduces the return type of invoking a callable with the given argument types.
using std::invoke_result;
/// Helper alias for `invoke_result<F, Args...>::type`.
using std::invoke_result_t;
/// Determines whether a callable can be invoked with the given argument types.
using std::is_invocable;
/// `true` if `F` is invocable with `Args...`.
using std::is_invocable_v;
/// The value type of an iterator.
using std::iter_value_t;

/// Provides information about allocator types and operations.
using std::allocator_traits;
/// Default deleter that calls `delete` on a pointer.
using std::default_delete;
} // namespace rstd::mtp