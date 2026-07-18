module;
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#include <pthread.h>
#endif
export module rstd:sys.libc.pthread;

#if RSTD_OS_UNIX
export namespace rstd::sys::libc
{
using ::pthread_mutex_destroy;
using ::pthread_mutex_lock;
using ::pthread_mutex_t;
using ::pthread_mutex_trylock;
using ::pthread_mutex_unlock;

using ::pthread_cond_broadcast;
using ::pthread_cond_destroy;
using ::pthread_cond_init;
using ::pthread_cond_signal;
using ::pthread_cond_t;
using ::pthread_cond_timedwait;
using ::pthread_cond_wait;
using ::pthread_condattr_destroy;
using ::pthread_condattr_init;
using ::pthread_condattr_t;
#if RSTD_OS_APPLE
using ::pthread_cond_timedwait_relative_np;
#else
using ::pthread_condattr_setclock;
#endif

using ::pthread_attr_init;
using ::pthread_attr_destroy;
using ::pthread_attr_setstacksize;
using ::pthread_attr_t;

using ::pthread_create;
using ::pthread_join;
using ::pthread_detach;
using ::pthread_self;
using ::pthread_equal;
using ::pthread_t;

#if RSTD_OS_LINUX
using ::pthread_setname_np;
#endif

constexpr auto pthread_mutex_initializer() noexcept -> pthread_mutex_t {
    return PTHREAD_MUTEX_INITIALIZER;
}

constexpr auto pthread_cond_initializer() noexcept -> pthread_cond_t {
    return PTHREAD_COND_INITIALIZER;
}
} // namespace rstd::sys::libc
#endif
