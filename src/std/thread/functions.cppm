module;
#include <rstd/macro.hpp>
export module rstd:thread.functions;
export import :thread.builder;
export import :thread.thread;
export import :thread.join_handle;
export import :thread.current;
export import :sys;
export import rstd.core;

namespace rstd::thread
{

export template<typename F>
auto spawn(F&& f) -> io::Result<JoinHandle<mtp::invoke_result_t<F>>> {
    return builder::Builder::make().spawn(rstd::forward<F>(f));
}

export inline void sleep(cppstd::chrono::duration<double> dur) {
    sys::thread::Thread::sleep(dur);
}

export inline void yield_now() {
    sys::thread::Thread::yield_now();
}

export inline void park() {
    current().park();
}

export inline void park_timeout(cppstd::chrono::duration<double> timeout) {
    current().park_timeout(timeout);
}

export inline void unpark(const Thread& thread) {
    thread.unpark();
}

} // namespace rstd::thread
