module;
#include <rstd/macro.hpp>
export module rstd:sys.libc;
export import :sys.libc.std;

#if RSTD_OS_UNIX
export import :sys.libc.pthread;
export import :sys.libc.unix;
#if RSTD_OS_LINUX
export import :sys.libc.linux;
#endif
#elif RSTD_OS_WINDOWS
export import :sys.libc.windows;
#endif
