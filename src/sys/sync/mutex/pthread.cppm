module;
#include <pthread.h>
#include <memory>
#include <utility>
#include <new>

export module rstd.sys:sync.mutex.pthread;
export import :sync.once_box;
export import :pal;

using rstd::sys::sync::OnceBox;

namespace rstd::sys::sync
{

export class Mutex {
    OnceBox<pal::Mutex> pal;

    constexpr Mutex() noexcept = delete;
public:

    void lock() { pthread_mutex_lock(get()); }

    bool try_lock() { return pthread_mutex_trylock(get()) == 0; }

    void unlock() { pthread_mutex_unlock(get()); }

    ~Mutex() {
        if (! pal_) return;
        // If we can acquire it now it was unlocked; destroy it.
        if (pthread_mutex_trylock(pal_.get()) == 0) {
            pthread_mutex_unlock(pal_.get());
            pthread_mutex_destroy(pal_.get());
            pal_.reset();
        } else {
            // Locked: leak the underlying pthread_mutex_t to mirror Rust behavior.
            (void)pal_.release();
        }
    }

private:
    pthread_mutex_t* get() {
        if (! pal_) {
            pthread_mutex_t* m =
                static_cast<pthread_mutex_t*>(::operator new(sizeof(pthread_mutex_t)));
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            // make it recursive to mirror reentrant-safe behavior
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(m, &attr);
            pthread_mutexattr_destroy(&attr);
            pal_.reset(m);
        }
        return pal_.get();
    }

    std::unique_ptr<pthread_mutex_t, void (*)(pthread_mutex_t*)> pal_ {
        nullptr, [](pthread_mutex_t*) { /* noop */ }
    };
};

} // namespace rstd::sys::sync