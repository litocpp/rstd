module;
#include <rstd/macro.hpp>
module rstd;
import :thread.lifecycle;
import :thread.thread;
import rstd.core;

using rstd::mut_ptr;
using rstd_alloc::boxed::Box;
using namespace rstd::sys::libc;

#if RSTD_OS_WINDOWS
extern "C" DWORD __stdcall rstd_thread_start_win(void* data) {
#else
extern "C" void* rstd_thread_start(void* data) {
#endif
    auto init = Box<ThreadInit>::from_raw(
        mut_ptr<ThreadInit>::from_raw_parts(static_cast<ThreadInit*>(data)));

    init->init();

    // Run the closure
    init->start->operator()();

    return {};
}

namespace rstd::thread
{
auto Thread::set_current() && -> rstd::Result<empty, Thread> {
    return ::rstd::thread::set_current(rstd::move(*this));
}
} // namespace rstd::thread
