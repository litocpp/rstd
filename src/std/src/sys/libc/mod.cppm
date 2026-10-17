module;
#include <rstd/macro.hpp>
export module rstd.sys.libc;
export import :std;

#if RSTD_OS_UNIX
export import :pthread;
export import :unix;
#if RSTD_OS_LINUX
export import :linux;
#endif
#elif RSTD_OS_WINDOWS
export import :windows;
#endif
