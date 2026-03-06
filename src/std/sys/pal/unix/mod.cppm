export module rstd:sys.pal.unix;
export import :sys.pal.unix.futex;
export import :sys.pal.unix.sync;

namespace rstd::sys::pal::unix
{
export using pal::unix::sync::mutex::Mutex;
export using pal::unix::sync::condvar::Condvar;

export [[noreturn]]
void abort_internal() {
    libc::abort();
}
} // namespace rstd::sys::pal::unix
