/// The rstd standard library module, re-exporting all public submodules.
export module rstd;
export import rstd.core;
export import :time;
export import :forward;
export import :sys;
export import :sync;
export import :thread;
export import :process;
export import :env;
export import :panicking;
export import :alloc;