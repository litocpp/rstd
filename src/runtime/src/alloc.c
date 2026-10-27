#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <rstd/runtime.h>
#include "platform.h"

#if ! RSTD_RUNTIME_WASM

#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#endif

void* __rstd_alloc(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (size == 0) size = 1;
#if defined(_WIN32)
    if (align <= _Alignof(max_align_t)) return malloc(size);
    return _aligned_malloc(size, align);
#else
    // Small allocations may provide less than max_align_t alignment.
    if (align <= _Alignof(max_align_t) && align <= size) return malloc(size);
    if (align < sizeof(void*)) align = sizeof(void*);
    void* result = NULL;
    return posix_memalign(&result, align, size) == 0 ? result : NULL;
#endif
}

void __rstd_dealloc(void* pointer, size_t size, size_t align) {
    (void)size;
#if defined(_WIN32)
    if (align > _Alignof(max_align_t)) {
        _aligned_free(pointer);
        return;
    }
#else
    (void)align;
#endif
    free(pointer);
}

void* __rstd_realloc(void* pointer, size_t old_size, size_t align, size_t new_size) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (pointer == NULL) return __rstd_alloc(new_size, align);
    if (new_size == 0) new_size = 1;
    if (align <= _Alignof(max_align_t) && align <= new_size) return realloc(pointer, new_size);
    void* replacement = __rstd_alloc(new_size, align);
    if (replacement != NULL) {
        __builtin_memcpy(replacement, pointer, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(pointer, old_size, align);
    }
    return replacement;
}

void* __rstd_alloc_zeroed(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (size == 0) size = 1;
    if (align <= _Alignof(max_align_t) && align <= size) return calloc(1, size);
    void* pointer = __rstd_alloc(size, align);
    if (pointer != NULL) __builtin_memset(pointer, 0, size);
    return pointer;
}

#endif
