export module rstd:sys.pal.unix;
export import :sys.pal.unix.futex;
export import :sys.pal.unix.sync;
export import :sys.pal.unix.time;

namespace rstd::sys::pal::unix
{
export using pal::unix::sync::mutex::Mutex;
export using pal::unix::sync::condvar::Condvar;
export using pal::unix::time::Instant;
export using pal::unix::time::SystemTime;

export [[noreturn]]
void abort_internal() {
    libc::abort();
}
} // namespace rstd::sys::pal::unix
