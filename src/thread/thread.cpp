module;
#include <rstd/macro.hpp>
module rstd;
import :thread.lifecycle;
import :thread.thread;
import rstd.core;

using rstd::mut_ptr;
using rstd::alloc::boxed::Box;

extern "C" void* rstd_thread_start(void* data) {
    auto init = Box<ThreadInit>::from_raw(
        mut_ptr<ThreadInit>::from_raw_parts(static_cast<ThreadInit*>(data)));

    // TODO:  source_location::current bug on gcc
    // relocation against `.Lsrc_loc3' in read-only section `.text'
    // wrapper with if to avoid assert
    assert(init);
    if (init) {
        init->init();

        // Run the closure
        init->start->operator()();
    }

    return {};
}
