export module rstd:sys;
export import :sys.sync.thread_parking;
export import :sys.sync.mutex;
export import :sys.thread;
export import :sys.pal;

export namespace rstd::sys
{

using pal::abort_internal;
using pal::exit_internal;
using pal::getpid_internal;
using pal::getenv_internal;
using pal::setenv_internal;
using pal::unsetenv_internal;
using pal::Instant;
using pal::SystemTime;

}