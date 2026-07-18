module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.condvar;
export import :sys.sync.condvar.futex;
export import :sys.sync.condvar.pthread;

namespace rstd::sys::sync::condvar
{

#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export using condvar::futex::Condvar;
#else
export using condvar::pthread::Condvar;
#endif

} // namespace rstd::sys::sync::condvar
