module;
#include <rstd/macro.hpp>

module rstd;

import :sys.thread.unix;

using rstd_alloc::boxed::Box;
using rstd::thread::ThreadInit;
using namespace rstd::sys::libc;

extern "C" void* rstd_thread_start(void* data);

namespace rstd::sys::thread::unix
{

static auto os_error(int code) noexcept -> rstd::io::error::Error {
    return rstd::io::error::Error::from_raw_os_error(
        rstd::io::error::RawOsError(static_cast<rstd::int32_t>(code)));
}

auto Thread::make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread> {
    libc::pthread_attr_t attr {};
    rstd_assert_eq(libc::pthread_attr_init(&attr), 0);

    if (stack != usize {}) {
        rstd_assert_eq(libc::pthread_attr_setstacksize(&attr, stack.to_primitive()), 0);
    }

    auto raw = rstd::move(init).into_raw();

    auto native = libc::pthread_t {};
    auto result = libc::pthread_create(&native, &attr, rstd_thread_start, raw.p);
    if (result == 0) {
        rstd_assert_eq(libc::pthread_attr_destroy(&attr), 0);
        return Ok(Thread { .id = native });
    }

    Box<ThreadInit>::from_raw(raw);
    libc::pthread_attr_destroy(&attr);
    return Err(os_error(result));
}

auto Thread::join() const -> rstd::io::Result<voidp> {
    auto* value = static_cast<voidp>(nullptr);
    auto  error = libc::pthread_join(id, &value);
    if (error != 0) return Err(os_error(error));
    return Ok(value);
}

auto Thread::detach() const -> rstd::io::Result<i32> {
    auto error = libc::pthread_detach(id);
    if (error != 0) return Err(os_error(error));
    return Ok(i32 {});
}

auto Thread::current() -> Thread {
    return Thread { .id = libc::pthread_self() };
}

auto Thread::operator==(const Thread& other) const -> bool {
    return libc::pthread_equal(id, other.id) != 0;
}

void Thread::set_name(ref<ffi::CStr> name) {
    libc::pthread_setname_np(current().id, name.as_ptr());
}

void Thread::sleep(rstd::time::Duration dur) {
    libc::timespec ts { .tv_sec  = static_cast<long>(dur.as_secs().to_primitive()),
                        .tv_nsec = static_cast<long>(dur.subsec_nanos().to_primitive()) };
    libc::nanosleep(&ts, nullptr);
}

void Thread::yield_now() {
    libc::sched_yield();
}

auto Thread::available_parallelism() -> Option<usize> {
    auto count = libc::online_processor_count();
    return count > 0 ? Some(usize(static_cast<rstd::size_t>(count))) : None();
}

} // namespace rstd::sys::thread::unix
