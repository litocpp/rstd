module;
#include <rstd/macro.hpp>

//! The I/O module, providing traits, types, and functions for input and output.
export module rstd:io;
export import :io.error;
export import :io.traits;
export import :io.buffered;
export import :io.cursor;
export import :io.range;
export import :io.util;
#if ! RSTD_OS_UNKNOWN
export import :io.stdio;
#endif
