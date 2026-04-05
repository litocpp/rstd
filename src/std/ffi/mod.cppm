/// Foreign function interface utilities, including C-compatible and OS-native string types.
export module rstd:ffi;
export import :ffi.os_str;
import rstd.alloc;

namespace rstd_alloc = alloc;

namespace rstd::ffi
{

/// An owned, C-compatible, nul-terminated string with no nul bytes in the middle.
export using rstd_alloc::ffi::CString;

}
