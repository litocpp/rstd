module;
#include <rstd/macro.hpp>

//! The I/O module, providing traits, types, and functions for input and output.
export module rstd.io;
export import rstd.io.error;
export import :buffer;
export import :traits;
export import :buffered;
export import :cursor;
export import :range;
export import :util;
#if ! RSTD_OS_UNKNOWN
export import :stdio;
#endif
