export module rstd.alloc;

export import rstd.rc;
export import rstd.str;
export import rstd.alloc.boxed;
export import rstd.alloc.ffi;
export import rstd.alloc.string;
export import rstd.alloc.sync;

namespace rstd
{
namespace ffi
{
export using rstd::alloc::ffi::CString;
}
} // namespace rstd