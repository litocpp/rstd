module;
#include <rstd/macro.hpp>
export module rstd.tests.std_sys_thread_module;
import rstd.sys.thread;
import rstd.alloc;

export auto std_sys_thread_module() -> bool {
    using namespace rstd;
    using ::alloc::boxed::Box;
    bool ran    = false;
    auto handle = thread::Thread::make(thread::ThreadId::make(), None());
    auto id     = handle.id();
    auto start  = [&] {
        ran = thread::current_id() == id;
    };
    auto init    = Box<thread::ThreadInit>::make(thread::ThreadInit {
        .handle = rstd::move(handle),
        .start  = Box<dyn<FnMut<void()>>>::make(rstd::move(start)),
    });
    auto spawned = sys::thread::Thread::make(usize(), rstd::move(init));
    if (spawned.is_err()) return false;
    auto joined = spawned->join();
#if RSTD_OS_WINDOWS
    auto closed = spawned->detach();
    if (closed.is_err()) return false;
#endif
    return joined.is_ok() && ran;
}
