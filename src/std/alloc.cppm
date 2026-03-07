export module rstd:alloc_impl;
import rstd.core;
import :sys.libc.std;

using namespace rstd;
namespace libc = rstd::sys::libc;

extern "C" {

void* __rstd_alloc(usize size, usize align) {
    if (align <= alignof(libc::max_align_t)) {
        return libc::malloc(size);
    }
    void* ptr = nullptr;
    if (libc::posix_memalign(&ptr, align, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void __rstd_dealloc(void* ptr, usize size, usize align) {
    libc::free(ptr);
}

void* __rstd_realloc(void* ptr, usize old_size, usize align, usize new_size) {
    if (align <= alignof(libc::max_align_t)) {
        return libc::realloc(ptr, new_size);
    }
    
    // Aligned realloc fallback
    void* new_ptr = __rstd_alloc(new_size, align);
    if (new_ptr) {
        libc::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(ptr, old_size, align);
    }
    return new_ptr;
}

void* __rstd_alloc_zeroed(usize size, usize align) {
    if (align <= alignof(libc::max_align_t)) {
        return libc::calloc(1, size);
    }
    void* ptr = __rstd_alloc(size, align);
    if (ptr) {
        libc::memset(ptr, 0, size);
    }
    return ptr;
}

}
