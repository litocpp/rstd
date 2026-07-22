#include <stdint.h>
#include <stdlib.h>

namespace
{

using Guard = uint64_t;

auto initialized(Guard* guard) noexcept -> unsigned char* {
    return reinterpret_cast<unsigned char*>(guard);
}

auto lock(Guard* guard) noexcept -> unsigned char* {
    return initialized(guard) + 1;
}

} // namespace

using Dtor = void (*)(void*);
extern "C" int __cxa_thread_atexit_impl(Dtor, void*, void*);
extern "C" int __cxa_thread_atexit(Dtor dtor, void* obj, void* dso_symbol) noexcept {
    return __cxa_thread_atexit_impl(dtor, obj, dso_symbol);
}

extern "C" int __cxa_guard_acquire(Guard* guard) noexcept {
    if (__atomic_load_n(initialized(guard), __ATOMIC_ACQUIRE) != 0) return 0;
    for (;;) {
        unsigned char expected {};
        if (__atomic_compare_exchange_n(
                lock(guard), &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            if (__atomic_load_n(initialized(guard), __ATOMIC_ACQUIRE) == 0) return 1;
            __atomic_store_n(lock(guard), 0, __ATOMIC_RELEASE);
            return 0;
        }
        if (__atomic_load_n(initialized(guard), __ATOMIC_ACQUIRE) != 0) return 0;
    }
}

extern "C" void __cxa_guard_release(Guard* guard) noexcept {
    __atomic_store_n(initialized(guard), 1, __ATOMIC_RELEASE);
    __atomic_store_n(lock(guard), 0, __ATOMIC_RELEASE);
}

extern "C" void __cxa_guard_abort(Guard* guard) noexcept {
    __atomic_store_n(lock(guard), 0, __ATOMIC_RELEASE);
}

extern "C" [[noreturn]]
void __cxa_pure_virtual() {
    abort();
}

extern "C" [[noreturn]]
void __cxa_deleted_virtual() {
    abort();
}
