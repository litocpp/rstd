module;
#include <memory>
#include <type_traits>
#include <iterator>
#include <tuple>
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

/// A compile-time sequence of `usize` values.
using std::index_sequence;
/// Generates an `index_sequence<0, 1, ..., N-1>`.
using std::make_index_sequence;

// template<typename _Fn, typename _Tuple, size_t... _Idx>
// constexpr decltype(auto) __apply_impl(_Fn&& __f, _Tuple&& __t, index_sequence<_Idx...>) {
//     return std::__invoke(std::forward<_Fn>(__f), std::get<_Idx>(std::forward<_Tuple>(__t))...);
// }
//
// template<typename _Fn, typename _Tuple>
// constexpr decltype(auto)
// apply(_Fn&& __f, _Tuple&& __t) noexcept(__unpack_std_tuple<is_nothrow_invocable, _Fn, _Tuple>) {
//     using _Indices = make_index_sequence<tuple_size_v<remove_reference_t<_Tuple>>>;
//     return std::__apply_impl(std::forward<_Fn>(__f), std::forward<_Tuple>(__t), _Indices {});
// }

/// Invokes a callable with the elements of a tuple as arguments.
using std::apply;

/// Provides information about allocator types and operations.
using std::allocator_traits;
/// Default deleter that calls `delete` on a pointer.
using std::default_delete;
} // namespace rstd::mtp