export module rstd:sys;
export import rstd.sys.libc;
export import rstd.sys.sync.thread_parking;
export import rstd.sys.sync.mutex;
export import rstd.sys.sync.condvar;
export import rstd.sys.thread;
export import rstd.sys.pal;
export import rstd.sys.socket;

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

} // namespace rstd::sys
