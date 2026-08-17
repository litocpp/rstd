export module rstd:sys.pal.windows;
import :sys.pal.windows.futex;
import :sys.pal.windows.sync;
import :sys.pal.windows.time;
import rstd.core;

export namespace rstd::sys::pal::windows
{

using pal::windows::sync::mutex::Mutex;
using pal::windows::sync::condvar::Condvar;
using pal::windows::time::Instant;
using pal::windows::time::SystemTime;
using pal::windows::time::local_offset_at_unix_time;

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

} // namespace rstd::sys::pal::windows
