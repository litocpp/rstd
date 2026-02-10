module rstd;
import :thread.lifecycle;
import :thread.thread;
import rstd.core;

namespace rstd::thread::lifecycle
{

extern "C" void* thread_start(void* data) {
    using namespace rstd;
    using namespace rstd::alloc::boxed;
    using namespace rstd::sync;
    using namespace rstd::thread;

    auto init = Box<ThreadInit>::from_raw(*static_cast<mut_ptr<ThreadInit>*>(data));

    // Set the current thread
    // TODO: Implement set_current

    // Run the closure
    // init->start.as_ref().call_once();

    return {};
}

} // namespace rstd::thread::lifecycle
