export module rstd:alloc;

export import :alloc.rc;
export import :alloc.str;
export import :alloc.boxed;
export import :alloc.ffi;
export import :alloc.string;
export import :alloc.sync;

namespace rstd
{
namespace ffi
{
export using rstd::alloc::ffi::CString;
}
} // namespace rstd