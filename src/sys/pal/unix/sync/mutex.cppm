module;

#include <pthread.h>

export module rstd.sys:pal.unix.sync.mutex;

namespace rstd::sys::pal::unix::sync::mutex
{

export class Mutex {
    pthread_mutex_t inner;

    Mutex(pthread_mutex_t inner): inner(inner) {}

public:
    static auto make() -> Mutex { return { PTHREAD_MUTEX_INITIALIZER }; }

    // pub(super) fn raw(&self)->*mut libc::pthread_mutex_t { self.inner.get() }

    /// # Safety
    /// May only be called once per instance of `Self`.
    //        pub unsafe fn init(self : Pin<&mut Self>) {}
};

} // namespace rstd::pal::unix::sync::mutex