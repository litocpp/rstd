module;
#include <rstd/macro.hpp>
export module rstd:sys.thread.unix;
export import :sys.libc.pthread;
export import :io;
export import :alloc;
export import :thread.thread;
export import rstd.core;

#ifdef __linux__

using namespace rstd::sys::libc;
using rstd::alloc::boxed::Box;
using rstd::thread::ThreadInit;

extern "C" {
void* thread_start(void* data) {
    using namespace rstd;
    auto init = Box<ThreadInit>::from_raw(*static_cast<mut_ptr<ThreadInit>*>(data));
    return {};
}
}

namespace rstd::sys::thread::unix
{

export struct Thread {
    pthread_t id;

    auto make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread> {
        auto attr_ = mem::MaybeUninit<libc::pthread_attr_t>::uninit();
        assert_eq(libc::pthread_attr_init(attr_.as_mut_ptr()), 0);

        auto attr = mem::DropGuard { rstd::move(attr_), [](auto p) {
                                        assert_eq(libc::pthread_attr_destroy(p->as_mut_ptr()), 0);
                                    } };

        auto raw = rstd::move(init).into_raw();

        auto native = libc::pthread_t {};
        auto ret    = libc::pthread_create(&native, attr->as_ptr(), thread_start, &raw);
        if (ret == 0) {
            return Ok(Thread { .id = native });
        } else {
            Box<ThreadInit>::from_raw(raw);
            return Err(rstd::io::error::Error::from_raw_os_error(ret));
        }
    }
};

}; // namespace rstd::sys::thread::unix

#endif
