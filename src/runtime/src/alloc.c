#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <rstd/runtime.h>
#include "platform.h"

#if RSTD_RUNTIME_WASM

extern unsigned char __heap_base;

struct FreeBlock {
    size_t            size;
    struct FreeBlock* next;
};

struct AllocationHeader {
    unsigned char* block;
    size_t         size;
};

static struct FreeBlock* free_blocks;
static unsigned char*    heap_cursor;

static const size_t WASM_PAGE_SIZE = 65536;

static bool checked_add(size_t left, size_t right, size_t* result) {
    *result = left + right;
    return *result >= left;
}

static bool align_up(size_t value, size_t alignment, size_t* result) {
    const size_t mask = alignment - 1;
    size_t       adjusted;
    if (! checked_add(value, mask, &adjusted)) return false;
    *result = adjusted & ~mask;
    return true;
}

static unsigned char* memory_end(void) {
    const size_t pages = __builtin_wasm_memory_size(0);
    return (unsigned char*)(pages * WASM_PAGE_SIZE);
}

static bool grow_memory(unsigned char* required) {
    unsigned char* current = memory_end();
    if (required <= current) return true;
    const size_t missing = (size_t)(required - current);
    size_t       rounded;
    if (! checked_add(missing, WASM_PAGE_SIZE - 1, &rounded)) return false;
    const size_t pages = rounded / WASM_PAGE_SIZE;
    return __builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

static void*
allocation_from(unsigned char* block, size_t block_size, size_t size, size_t alignment) {
    size_t       payload_address;
    const size_t block_address = (size_t)block;
    if (! checked_add(block_address, sizeof(struct AllocationHeader), &payload_address) ||
        ! align_up(payload_address, alignment, &payload_address))
        return NULL;
    size_t allocation_end;
    size_t block_end;
    if (! checked_add(payload_address, size, &allocation_end) ||
        ! checked_add(block_address, block_size, &block_end) || allocation_end > block_end)
        return NULL;
    struct AllocationHeader* header = (struct AllocationHeader*)payload_address - 1;
    header->block                   = block;
    header->size                    = block_size;
    return (void*)payload_address;
}

static void* take_free_block(size_t size, size_t alignment) {
    struct FreeBlock** link = &free_blocks;
    while (*link != NULL) {
        struct FreeBlock* block      = *link;
        struct FreeBlock* next       = block->next;
        const size_t      block_size = block->size;
        void* result = allocation_from((unsigned char*)block, block_size, size, alignment);
        if (result != NULL) {
            *link = next;
            size_t       remainder_address;
            const size_t payload_end = (size_t)result + size;
            if (align_up(payload_end, _Alignof(struct FreeBlock), &remainder_address)) {
                const size_t used_size = remainder_address - (size_t)block;
                if (used_size <= block_size &&
                    block_size - used_size >=
                        sizeof(struct AllocationHeader) + _Alignof(struct AllocationHeader)) {
                    struct FreeBlock* remainder     = (struct FreeBlock*)remainder_address;
                    remainder->size                 = block_size - used_size;
                    remainder->next                 = next;
                    *link                           = remainder;
                    struct AllocationHeader* header = (struct AllocationHeader*)result - 1;
                    header->size                    = used_size;
                }
            }
            return result;
        }
        link = &block->next;
    }
    return NULL;
}

static void* allocate_fresh(size_t size, size_t alignment) {
    if (heap_cursor == NULL) heap_cursor = &__heap_base;
    unsigned char* block = heap_cursor;
    size_t         payload_address;
    const size_t   block_address = (size_t)block;
    if (! checked_add(block_address, sizeof(struct AllocationHeader), &payload_address) ||
        ! align_up(payload_address, alignment, &payload_address))
        return NULL;
    size_t end_address;
    if (! checked_add(payload_address, size, &end_address)) return NULL;
    size_t aligned_end;
    if (! align_up(end_address, _Alignof(struct FreeBlock), &aligned_end)) return NULL;
    unsigned char* required = (unsigned char*)aligned_end;
    if (! grow_memory(required)) return NULL;
    heap_cursor = required;
    return allocation_from(block, aligned_end - block_address, size, alignment);
}

static void insert_free_block(unsigned char* address, size_t size) {
    struct FreeBlock* block = (struct FreeBlock*)address;
    block->size             = size;
    struct FreeBlock** link = &free_blocks;
    while (*link != NULL && *link < block) link = &(*link)->next;
    block->next = *link;
    *link       = block;
    if (block->next != NULL && (unsigned char*)block + block->size == (unsigned char*)block->next) {
        block->size += block->next->size;
        block->next = block->next->next;
    }
    if (link != &free_blocks) {
        struct FreeBlock* previous = free_blocks;
        while (previous->next != block) previous = previous->next;
        if ((unsigned char*)previous + previous->size == (unsigned char*)block) {
            previous->size += block->size;
            previous->next = block->next;
        }
    }
}

#else

#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#endif

#endif

RSTD_RUNTIME_EXPORT("__rstd_alloc")
void* __rstd_alloc(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (size == 0) size = 1;
#if RSTD_RUNTIME_WASM
    if (align < _Alignof(struct AllocationHeader)) align = _Alignof(struct AllocationHeader);
    void* reused = take_free_block(size, align);
    return reused != NULL ? reused : allocate_fresh(size, align);
#else
    if (align <= _Alignof(max_align_t)) return malloc(size);
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    void* result = NULL;
    return posix_memalign(&result, align, size) == 0 ? result : NULL;
#endif
#endif
}

RSTD_RUNTIME_EXPORT("__rstd_dealloc")
void __rstd_dealloc(void* pointer, size_t size, size_t align) {
    (void)size;
#if RSTD_RUNTIME_WASM
    (void)align;
    if (pointer == NULL) return;
    struct AllocationHeader* header = (struct AllocationHeader*)pointer - 1;
    insert_free_block(header->block, header->size);
#else
#if defined(_WIN32)
    if (align > _Alignof(max_align_t)) {
        _aligned_free(pointer);
        return;
    }
#else
    (void)align;
#endif
    free(pointer);
#endif
}

RSTD_RUNTIME_EXPORT("__rstd_realloc")
void* __rstd_realloc(void* pointer, size_t old_size, size_t align, size_t new_size) {
    if (pointer == NULL) return __rstd_alloc(new_size, align);
    void* replacement = __rstd_alloc(new_size, align);
    if (replacement != NULL) {
        __builtin_memcpy(replacement, pointer, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(pointer, old_size, align);
    }
    return replacement;
}

RSTD_RUNTIME_EXPORT("__rstd_alloc_zeroed")
void* __rstd_alloc_zeroed(size_t size, size_t align) {
    void* pointer = __rstd_alloc(size, align);
    if (pointer != NULL) __builtin_memset(pointer, 0, size);
    return pointer;
}
