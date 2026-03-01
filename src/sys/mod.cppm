export module rstd:sys;
export import :sys.sync.thread_parking;
export import :sys.sync.mutex;
export import :sys.thread;
export import :sys.pal;

export namespace rstd::sys
{

using pal::abort_internal;

}