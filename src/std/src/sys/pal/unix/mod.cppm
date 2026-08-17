export module rstd:sys.pal.unix;
import :sys.pal.unix.futex;
import :sys.pal.unix.sync;
import :sys.pal.unix.time;
import rstd.core;

export namespace rstd::sys::pal::unix
{

using pal::unix::sync::mutex::Mutex;
using pal::unix::sync::condvar::Condvar;
using pal::unix::time::Instant;
using pal::unix::time::SystemTime;
using pal::unix::time::local_offset_at_unix_time;

[[noreturn]]
void abort_internal();
[[noreturn]]
void exit_internal(int code);
auto getpid_internal() -> u32;
auto getenv_internal(const char* name) -> const char*;
auto setenv_internal(const char* name, const char* value) -> bool;
auto unsetenv_internal(const char* name) -> bool;

struct ArgcArgv {
    int                argc;
    char const* const* argv;
};

void args_capture(int argc, char const* const* argv);
auto args_argc_argv() -> ArgcArgv;

} // namespace rstd::sys::pal::unix
