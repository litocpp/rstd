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

extern "C" void* rstd_thread_start(void* data);

namespace rstd::sys::thread::unix
{

export struct Thread {
    pthread_t id;

    static auto make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread> {
        libc::pthread_attr_t attr {};
        assert_eq(libc::pthread_attr_init(&attr), 0);

        if (stack != 0) {
            assert_eq(libc::pthread_attr_setstacksize(&attr, stack), 0);
        }

        auto raw = rstd::move(init).into_raw();

        auto native = libc::pthread_t {};
        auto ret    = libc::pthread_create(&native, &attr, rstd_thread_start, raw.p);
        if (ret == 0) {
            assert_eq(libc::pthread_attr_destroy(&attr), 0);
            return Ok(Thread { .id = native });
        } else {
            Box<ThreadInit>::from_raw(raw);
            libc::pthread_attr_destroy(&attr);
            return Err(rstd::io::error::Error::from_raw_os_error(ret));
        }
    }

    auto join() const -> rstd::io::Result<voidp> {
        auto* ret = static_cast<voidp>(nullptr);
        auto  err = libc::pthread_join(id, &ret);
        if (err != 0) {
            return Err(rstd::io::error::Error::from_raw_os_error(err));
        }
        return Ok(ret);
    }

    auto detach() const -> rstd::io::Result<i32> {
        auto err = libc::pthread_detach(id);
        if (err != 0) {
            return Err(rstd::io::error::Error::from_raw_os_error(err));
        }
        return Ok(0);
    }

    static auto current() -> Thread { return Thread { .id = libc::pthread_self() }; }

    auto operator==(const Thread& other) const -> bool {
        return libc::pthread_equal(id, other.id) != 0;
    }

    static void set_name(ref<ffi::CStr> name) {
        libc::pthread_setname_np(current().id, (char const*)name.p);
    }
};

}; // namespace rstd::sys::thread::unix

#endif
