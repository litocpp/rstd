module;
#include <rstd/macro.hpp>

//! The rstd standard library module, re-exporting all public submodules.
export module rstd;
export import rstd.core;
export import rstd.error;
export import :time;
export import :forward;
export import :io;
export import :bytes;
export import :path;
export import :panicking;
export import :fs;
#if ! RSTD_OS_UNKNOWN
export import :sync;
export import :thread;
export import :async;
export import :net;
export import :os;
export import :process;
export import :env;
export import :alloc;
#endif
