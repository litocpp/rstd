export module rstd:sys.sync.mutex;
export import :sys.sync.mutex.futex;
export import :sys.sync.mutex.pthread;

namespace rstd::sys::sync::mutex
{
using namespace mutex::futex;
}
