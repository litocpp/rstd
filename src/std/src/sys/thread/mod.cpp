module;
#include <rstd/macro.hpp>
module rstd.sys.thread;
import rstd.sys.libc;
import rstd.thread.thread;
import rstd.alloc;

using rstd::mut_ptr;
using ::alloc::boxed::Box;
using rstd::thread::ThreadInit;
using namespace rstd::sys::libc;

void rstd_thread_start_impl(void* data) {
    auto init = Box<ThreadInit>::from_raw(
        mut_ptr<ThreadInit>::from_raw_parts(static_cast<ThreadInit*>(data)));

    init->init();

    // Run the closure
    init->start->operator()();
}

#if RSTD_OS_WINDOWS
extern "C" DWORD __stdcall rstd_thread_start_win(void* data) {
#else
extern "C" void* rstd_thread_start(void* data) {
#endif
    rstd_thread_start_impl(data);
    return {};
}

namespace rstd::sys::thread
{
auto available_parallelism() -> Option<usize> {
    return backend::Thread::available_parallelism();
}
} // namespace rstd::sys::thread
