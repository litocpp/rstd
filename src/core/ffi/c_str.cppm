export module rstd.core:ffi.c_str;
import :meta;

namespace rstd::ffi::c_str
{

struct CStr {
    char const* inner;
};
static_assert(meta::transparent<CStr, char const*>);

} // namespace rstd::ffi::c_str