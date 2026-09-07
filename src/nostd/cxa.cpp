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

#if defined(__APPLE__)
// `__cxa_thread_atexit_impl` is a glibc-only ABI entry point that is not
// exported by Apple's runtime. Register thread-local destructors through a
// per-thread keyed list so `thread_local` objects are torn down at thread exit.
#include <pthread.h>
struct TsdRecord {
    Dtor          dtor;
    void*         obj;
    TsdRecord*    next;
};
static pthread_key_t   g_tsd_key;
static pthread_once_t  g_tsd_once = PTHREAD_ONCE_INIT;

extern "C" void rstd_tsd_run(void* raw) {
    auto* record = static_cast<TsdRecord*>(raw);
    while (record != nullptr) {
        auto* next = record->next;
        record->dtor(record->obj);
        free(record);
        record = next;
    }
}

extern "C" void rstd_tsd_make_key() {
    (void)pthread_key_create(&g_tsd_key, &rstd_tsd_run);
}

extern "C" int __cxa_thread_atexit_impl(Dtor dtor, void* obj, void*) {
    pthread_once(&g_tsd_once, &rstd_tsd_make_key);
    auto* head   = static_cast<TsdRecord*>(pthread_getspecific(g_tsd_key));
    auto* record = static_cast<TsdRecord*>(malloc(sizeof(TsdRecord)));
    if (record == nullptr) return -1;
    record->dtor  = dtor;
    record->obj   = obj;
    record->next  = head;
    (void)pthread_setspecific(g_tsd_key, record);
    return 0;
}
#endif

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
