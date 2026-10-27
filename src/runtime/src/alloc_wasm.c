#include <rstd/runtime.h>
#include "platform.h"

#if RSTD_RUNTIME_WASM

extern unsigned char __heap_base;
static uintptr_t     heap_break;

// The runtime owns the linear-memory heap from __heap_base onward.
static void* wasm_morecore(ptrdiff_t increment) {
    if (heap_break == 0) heap_break = (uintptr_t)&__heap_base;
    if (increment < 0 || (size_t)increment > UINTPTR_MAX - heap_break) return (void*)-1;
    const uintptr_t previous = heap_break;
    const uintptr_t required = previous + (size_t)increment;
    const uint64_t  current  = (uint64_t)__builtin_wasm_memory_size(0) * 65536;
    if (required > current) {
        const size_t pages = (size_t)(((uint64_t)required - current + 65535) / 65536);
        if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return (void*)-1;
    }
    heap_break = required;
    return (void*)previous;
}

#define DLMALLOC_EXPORT      static __attribute__((unused))
#define USE_DL_PREFIX        1
#define USE_LOCKS            0
#define HAVE_MMAP            0
#define HAVE_MREMAP          0
#define HAVE_MORECORE        1
#define MORECORE             wasm_morecore
#define MORECORE_CONTIGUOUS  1
#define MORECORE_CANNOT_TRIM 1
#define malloc_getpagesize   ((size_t)65536)
#define DEFAULT_GRANULARITY  ((size_t)65536)
#define NO_MALLINFO          1
#define NO_MALLOC_STATS      1
#define MALLOC_FAILURE_ACTION
#define ABORT __builtin_trap()
#define LACKS_SYS_TYPES_H
#define LACKS_ERRNO_H
#define LACKS_TIME_H
#define LACKS_STDLIB_H
#define LACKS_STRING_H
#define LACKS_STRINGS_H
#define LACKS_UNISTD_H
#define USE_BUILTIN_FFS 1
#define ffs             __builtin_ffs
#define memcpy          __builtin_memcpy
#define memset          __builtin_memset

enum
{
    ENOMEM = 12,
    EINVAL = 22
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-null-pointer-arithmetic"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "../dlmalloc/malloc.c"
#pragma clang diagnostic pop

RSTD_RUNTIME_EXPORT("__rstd_alloc")
void* __rstd_alloc(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    return dlmemalign(align, size == 0 ? 1 : size);
}

RSTD_RUNTIME_EXPORT("__rstd_dealloc")
void __rstd_dealloc(void* pointer, size_t size, size_t align) {
    (void)size;
    (void)align;
    dlfree(pointer);
}

RSTD_RUNTIME_EXPORT("__rstd_realloc")
void* __rstd_realloc(void* pointer, size_t old_size, size_t align, size_t new_size) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (new_size == 0) new_size = 1;
    if (pointer == NULL) return __rstd_alloc(new_size, align);
    if (align <= MALLOC_ALIGNMENT) return dlrealloc(pointer, new_size);
    // Moving realloc only guarantees dlmalloc's base alignment.
    void* result = dlrealloc_in_place(pointer, new_size);
    if (result != NULL) return result;
    result = dlmemalign(align, new_size);
    if (result != NULL) {
        __builtin_memcpy(result, pointer, old_size < new_size ? old_size : new_size);
        dlfree(pointer);
    }
    return result;
}

RSTD_RUNTIME_EXPORT("__rstd_alloc_zeroed")
void* __rstd_alloc_zeroed(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (size == 0) size = 1;
    if (align <= MALLOC_ALIGNMENT) return dlcalloc(1, size);
    void* result = dlmemalign(align, size);
    if (result != NULL) __builtin_memset(result, 0, size);
    return result;
}

#endif
