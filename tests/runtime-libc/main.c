#define _POSIX_C_SOURCE 200112L

#include <rstd/runtime.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned reallocations, zeroed_allocations, aligned_allocations;
static size_t   last_reallocation_size, last_zeroed_size;
static bool     fail_reallocation, fail_zeroed_allocation, fail_aligned_allocation;

void* __real_realloc(void* pointer, size_t size);
void* __real_calloc(size_t count, size_t size);
int   __real_posix_memalign(void** result, size_t alignment, size_t size);

void* __wrap_realloc(void* pointer, size_t size) {
    ++reallocations;
    last_reallocation_size = size;
    if (fail_reallocation) {
        fail_reallocation = false;
        return NULL;
    }
    return __real_realloc(pointer, size);
}

void* __wrap_calloc(size_t count, size_t size) {
    ++zeroed_allocations;
    last_zeroed_size = count * size;
    if (fail_zeroed_allocation) {
        fail_zeroed_allocation = false;
        return NULL;
    }
    return __real_calloc(count, size);
}

int __wrap_posix_memalign(void** result, size_t alignment, size_t size) {
    ++aligned_allocations;
    if (fail_aligned_allocation) {
        fail_aligned_allocation = false;
        return ENOMEM;
    }
    return __real_posix_memalign(result, alignment, size);
}

#define CHECK(condition)                    \
    do {                                    \
        if (! (condition)) return __LINE__; \
    } while (0)

static int check_libc_dispatch(void) {
    unsigned char* bytes = __rstd_alloc_zeroed(32, 1);
    CHECK(bytes != NULL && zeroed_allocations == 1 && last_zeroed_size == 32);
    for (size_t index = 0; index < 32; ++index) {
        CHECK(bytes[index] == 0);
        bytes[index] = (unsigned char)(index + 1);
    }
    bytes = __rstd_realloc(bytes, 32, 1, 512);
    CHECK(bytes != NULL && reallocations == 1 && last_reallocation_size == 512);
    for (size_t index = 0; index < 32; ++index) CHECK(bytes[index] == index + 1);
    bytes = __rstd_realloc(bytes, 512, 1, 16);
    CHECK(bytes != NULL && reallocations == 2 && last_reallocation_size == 16);
    for (size_t index = 0; index < 16; ++index) CHECK(bytes[index] == index + 1);

    fail_reallocation = true;
    CHECK(__rstd_realloc(bytes, 16, 1, 1024) == NULL);
    CHECK(! fail_reallocation && reallocations == 3);
    for (size_t index = 0; index < 16; ++index) CHECK(bytes[index] == index + 1);
    CHECK(__rstd_realloc(bytes, 16, 0, 1024) == NULL && reallocations == 3);
    CHECK(__rstd_realloc(bytes, 16, 3, 1024) == NULL && reallocations == 3);
    bytes = __rstd_realloc(bytes, 16, 1, 0);
    CHECK(bytes != NULL && reallocations == 4 && last_reallocation_size == 1);
    __rstd_dealloc(bytes, 0, 1);

    fail_zeroed_allocation = true;
    CHECK(__rstd_alloc_zeroed(32, 1) == NULL && ! fail_zeroed_allocation);
    CHECK(zeroed_allocations == 2);
    bytes = __rstd_alloc_zeroed(0, 1);
    CHECK(bytes != NULL && zeroed_allocations == 3 && last_zeroed_size == 1);
    __rstd_dealloc(bytes, 0, 1);
    CHECK(__rstd_alloc_zeroed(32, 0) == NULL && zeroed_allocations == 3);
    CHECK(__rstd_alloc_zeroed(32, 3) == NULL && zeroed_allocations == 3);
    bytes = __rstd_realloc(NULL, 0, 1, 32);
    CHECK(bytes != NULL && reallocations == 4);
    __rstd_dealloc(bytes, 32, 1);
    return 0;
}

static int check_alignment_fallback(void) {
    const size_t alignments[] = { 2, 4, _Alignof(max_align_t), 4096 };
    for (size_t a = 0; a < sizeof(alignments) / sizeof(*alignments); ++a) {
        const size_t   alignment = alignments[a];
        unsigned       before    = aligned_allocations;
        unsigned char* bytes     = __rstd_alloc(1, alignment);
        CHECK(bytes != NULL && (uintptr_t)bytes % alignment == 0);
        CHECK(aligned_allocations == before + 1);
        bytes[0] = 42;

        before = reallocations;
        bytes  = __rstd_realloc(bytes, 1, alignment, 1);
        CHECK(bytes != NULL && (uintptr_t)bytes % alignment == 0 && bytes[0] == 42);
        CHECK(reallocations == before);
        fail_aligned_allocation = true;
        CHECK(__rstd_realloc(bytes, 1, alignment, 1) == NULL);
        CHECK(! fail_aligned_allocation && bytes[0] == 42 && reallocations == before);

        bytes = __rstd_realloc(bytes, 1, alignment, 8192);
        CHECK(bytes != NULL && (uintptr_t)bytes % alignment == 0 && bytes[0] == 42);
        CHECK(reallocations == before + (alignment <= _Alignof(max_align_t)));
        __rstd_dealloc(bytes, 8192, alignment);

        before = zeroed_allocations;
        bytes  = __rstd_alloc_zeroed(1, alignment);
        CHECK(bytes != NULL && (uintptr_t)bytes % alignment == 0 && bytes[0] == 0);
        CHECK(zeroed_allocations == before);
        bytes = __rstd_realloc(bytes, 1, alignment, 0);
        CHECK(bytes != NULL && (uintptr_t)bytes % alignment == 0);
        __rstd_dealloc(bytes, 0, alignment);
    }
    return 0;
}

int main(void) {
    int result = check_libc_dispatch();
    if (! result) result = check_alignment_fallback();
    if (result) fprintf(stderr, "runtime libc check failed at line %d\n", result);
    return result != 0;
}
