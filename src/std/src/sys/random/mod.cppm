export module rstd:sys.random;
import rstd.io.error;

namespace rstd::sys::random
{
using Read = rstd::io::Result<usize> (*)(void*, mut_ref<u8[]>);
/// The callback must be valid for the supplied state and bounded writable slices.
[[unsafe]]
auto fill_from(void* state, Read read, mut_ref<u8[]> output) -> rstd::io::Result<empty>;
auto fill_bytes(mut_ref<u8[]> output) -> rstd::io::Result<empty>;
} // namespace rstd::sys::random
