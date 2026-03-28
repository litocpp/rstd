using Dtor = void (*)(void*);
extern "C" int __cxa_thread_atexit_impl(Dtor, void*, void*);
extern "C" int __cxa_thread_atexit(Dtor dtor, void* obj, void* dso_symbol) throw() {
    return __cxa_thread_atexit_impl(dtor, obj, dso_symbol);
}