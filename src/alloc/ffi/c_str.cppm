export module rstd.alloc:ffi.c_str;
export import :boxed;

namespace rstd::alloc::ffi::c_str
{

struct CString {
    boxed::Box<u8[]> inner;
};

} // namespace rstd::alloc::ffi::c_str