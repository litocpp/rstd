export module rstd.sys:sync.mutex;
export import :sync.mutex.futex;
export import :sync.mutex.pthread;

namespace rstd::sys::sync::mutex
{
using namespace mutex::futex;
}
