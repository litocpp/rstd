module;
#include <memory>
#include <type_traits>
#include <iterator>
#include <tuple>
export module rstd.basic:mtp.std;

export namespace rstd::mtp
{

using std::invoke_result;
using std::invoke_result_t;
using std::is_invocable;
using std::is_invocable_v;
using std::iter_value_t;

using std::apply;
using std::index_sequence;
using std::make_index_sequence;

using std::allocator_traits;
using std::default_delete;
} // namespace rstd::mtp