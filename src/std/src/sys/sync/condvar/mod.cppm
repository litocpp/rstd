module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.condvar;
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export import :sys.sync.condvar.futex;
namespace rstd::sys::sync::condvar
{
namespace backend = futex;
}
#else
export import :sys.sync.condvar.pthread;
namespace rstd::sys::sync::condvar
{
namespace backend = pthread;
}
#endif

namespace rstd::sys::sync::condvar
{

export using backend::Condvar;

} // namespace rstd::sys::sync::condvar
