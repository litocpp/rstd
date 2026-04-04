module;
#include <new>
#include <stdio.h>
#include <stdlib.h>

module rstd.runtime;

using rstd::u8;
using rstd::usize;

extern "C" {

void* __rstd_alloc(usize size, usize align) {
    if (align != __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t { align }, std::nothrow_t {});
    } else {
        return ::operator new(size, std::nothrow_t {});
    }
}

void __rstd_dealloc(void* ptr, usize size, usize align) {
    if (align != __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(ptr, size, std::align_val_t { align });
    } else {
        ::operator delete(ptr, size);
    }
}

void* __rstd_realloc(void* ptr, usize old_size, usize align, usize new_size) {
    void* new_ptr = __rstd_alloc(new_size, align);
    if (new_ptr) {
        __builtin_memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(ptr, old_size, align);
    }
    return new_ptr;
}

void* __rstd_alloc_zeroed(usize size, usize align) {
    void* ptr = __rstd_alloc(size, align);
    if (ptr) {
        __builtin_memset(ptr, 0, size);
    }
    return ptr;
}

[[noreturn]]
void rstd_panic_impl(rstd::panic_::PanicInfo const& info) {
    auto& loc = info.location;

    fprintf(stderr, "thread 'main' panicked at %s:%u:%u:\n",
            loc.file_name(), loc.line(), loc.column());

    if (info.fmt) {
        FILE* f = stderr;
        info.fmt(info.data, &f,
                 +[](void* ctx, u8 const* buf, usize len) -> bool {
                     return fwrite(buf, 1, len, *static_cast<FILE**>(ctx)) == len;
                 });
    }

    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

}
