module rstd;
import :thread.lifecycle;
import :thread.thread;
import rstd.core;

using rstd::alloc::boxed::Box;

namespace rstd::thread::lifecycle
{

extern "C" void* thread_start(void* data) {

    auto init = Box<ThreadInit>::from_raw(
        mut_ptr<ThreadInit>::from_raw_parts(static_cast<ThreadInit*>(data)));

    // Set the current thread
    // TODO: Implement set_current

    // Run the closure
    init->start();

    return {};
}

} // namespace rstd::thread::lifecycle
