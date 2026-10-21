/// Foreign function interface utilities, including C-compatible and OS-native string types.
export module rstd.ffi;
export import :os_str;
import rstd.alloc;

namespace rstd_alloc = alloc;

namespace rstd::ffi
{

/// An owned, C-compatible, nul-terminated string with no nul bytes in the middle.
export using rstd_alloc::ffi::CString;
export using rstd_alloc::ffi::NulError;
export using rstd_alloc::ffi::FromVecWithNulError;
export using rstd_alloc::ffi::IntoStringError;

} // namespace rstd::ffi
