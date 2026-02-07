export module rstd:sys.pal.unix;
export import :sys.pal.unix.futex;
export import :sys.pal.unix.sync;

namespace rstd::sys::pal::unix
{
export using pal::unix::sync::mutex::Mutex;
}
