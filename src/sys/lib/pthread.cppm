module;
#include <pthread.h>
export module rstd:sys.lib.pthread;

export namespace rstd::sys::lib::pthread
{
using ::pthread_mutex_destroy;
using ::pthread_mutex_lock;
using ::pthread_mutex_t;
using ::pthread_mutex_trylock;
using ::pthread_mutex_unlock;


using ::pthread_t;

constexpr auto pthread_mutex_initializer() noexcept -> pthread_mutex_t {
    return PTHREAD_MUTEX_INITIALIZER;
}
} // namespace rstd::sys::lib::pthread