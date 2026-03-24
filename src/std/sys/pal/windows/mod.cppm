export module rstd:sys.pal.windows;
export import :sys.pal.windows.futex;
export import :sys.pal.windows.time;

namespace rstd::sys::pal::windows
{
export using pal::windows::time::Instant;
export using pal::windows::time::SystemTime;
} // namespace rstd::sys::pal::windows

