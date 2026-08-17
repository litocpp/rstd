module;
#include <new>
#include <stdio.h>
#include <stdlib.h>

module rstd.runtime;

using namespace rstd;

extern "C" {

void* __rstd_alloc(rstd::size_t size, rstd::size_t align) {
    void* result;
    if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        result = ::operator new(size, std::align_val_t { align }, std::nothrow_t {});
    } else {
        result = ::operator new(size, std::nothrow_t {});
    }
    return result;
}

void __rstd_dealloc(void* ptr, rstd::size_t size, rstd::size_t align) {
    if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(ptr, size, std::align_val_t { align });
    } else {
        ::operator delete(ptr, size);
    }
}

void* __rstd_realloc(void* ptr, rstd::size_t old_size, rstd::size_t align, rstd::size_t new_size) {
    void* new_ptr = __rstd_alloc(new_size, align);
    if (new_ptr) {
        __builtin_memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(ptr, old_size, align);
    }
    return new_ptr;
}

void* __rstd_alloc_zeroed(rstd::size_t size, rstd::size_t align) {
    void* ptr = __rstd_alloc(size, align);
    if (ptr) {
        __builtin_memset(ptr, 0, size);
    }
    return ptr;
}

[[noreturn]]
void rstd_panic_impl(rstd::panic_::PanicInfo const& info) {
    auto& loc = info.location;

    fprintf(
        stderr, "thread 'main' panicked at %s:%u:%u:\n", loc.file_name(), loc.line(), loc.column());

    if (info.fmt) {
        FILE* f = stderr;
        info.fmt(
            info.data, &f, +[](void* ctx, rstd::uint8_t const* buf, rstd::size_t len) -> bool {
                return fwrite(buf, 1, len, *static_cast<FILE**>(ctx)) == len;
            });
    }

    fputc('\n', stderr);
    fflush(stderr);
    abort();
}
}
