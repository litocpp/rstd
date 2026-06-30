export module rstd:sys;
export import :sys.libc;
export import :sys.sync.thread_parking;
export import :sys.sync.mutex;
export import :sys.sync.condvar;
export import :sys.thread;
export import :sys.pal;
export import :sys.fd;
export import :sys.socket;

export namespace rstd::sys
{

using pal::abort_internal;
using pal::exit_internal;
using pal::getpid_internal;
using pal::getenv_internal;
using pal::setenv_internal;
using pal::unsetenv_internal;
using pal::ArgcArgv;
using pal::args_capture;
using pal::args_argc_argv;
using pal::Instant;
using pal::SystemTime;

}
