module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.once;

#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export import :sys.sync.once.futex;
namespace rstd::sys::sync::once
{
namespace backend = futex;
}
#else
export import :sys.sync.once.queue;
namespace rstd::sys::sync::once
{
namespace backend = queue;
}
#endif
