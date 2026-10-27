#include <rstd/runtime.h>

static bool contains(const unsigned char* bytes, size_t size, unsigned char value) {
    for (size_t index = 0; index < size; ++index) {
        if (bytes[index] != value) return false;
    }
    return true;
}

static void fill(unsigned char* bytes, size_t size, unsigned char value) {
    for (size_t index = 0; index < size; ++index) bytes[index] = value;
}

__attribute__((export_name("rstd_runtime_in_place"))) int in_place(void) {
    unsigned char* first = __rstd_alloc(128, 8);
    void*          next  = __rstd_alloc(4096, 8);
    void*          guard = __rstd_alloc(64, 8);
    if (first == NULL || next == NULL || guard == NULL) return 1;
    fill(first, 128, 42);
    __rstd_dealloc(next, 4096, 8);
    unsigned char* grown = __rstd_realloc(first, 128, 8, 2048);
    if (grown != first || ! contains(grown, 128, 42)) return 2;
    unsigned char* shrunk = __rstd_realloc(grown, 2048, 8, 32);
    if (shrunk != grown || ! contains(shrunk, 32, 42)) return 3;
    if (__rstd_realloc(shrunk, 32, 8, 32) != shrunk) return 4;
    __rstd_dealloc(shrunk, 32, 8);
    __rstd_dealloc(guard, 64, 8);

    first = __rstd_alloc(64, 4096);
    if (first == NULL) return 5;
    fill(first, 64, 57);
    grown = __rstd_realloc(first, 64, 4096, 128 * 1024);
    if (grown == NULL || (uintptr_t)grown % 4096 != 0 || ! contains(grown, 64, 57)) return 6;
    shrunk = __rstd_realloc(grown, 128 * 1024, 4096, 16);
    if (shrunk != grown || ! contains(shrunk, 16, 57)) return 7;
    __rstd_dealloc(shrunk, 16, 4096);
    return 0;
}

__attribute__((export_name("rstd_runtime_reuse"))) int reuse(void) {
    const size_t size  = 48 * 1024;
    size_t       pages = 0;
    for (size_t iteration = 0; iteration < 16; ++iteration) {
        void* first  = __rstd_alloc(size, 16);
        void* second = __rstd_alloc(size, 16);
        void* guard  = __rstd_alloc(64, 8);
        if (first == NULL || second == NULL || guard == NULL) return 1;
        __rstd_dealloc(first, size, 16);
        __rstd_dealloc(second, size, 16);
        unsigned char* merged = __rstd_alloc(80 * 1024, 16);
        if (merged == NULL) return 2;
        fill(merged, 80 * 1024, 31);
        if (! contains(merged, 80 * 1024, 31)) return 3;
        __rstd_dealloc(merged, 80 * 1024, 16);
        __rstd_dealloc(guard, 64, 8);
        size_t current = __builtin_wasm_memory_size(0);
        if (iteration == 0)
            pages = current;
        else if (current != pages)
            return 4;
    }
    return 0;
}

__attribute__((export_name("rstd_runtime_initial_memory"))) int initial_memory(void) {
    const size_t pages = __builtin_wasm_memory_size(0);
    void*        bytes = __rstd_alloc(1024, 8);
    if (bytes == NULL || __builtin_wasm_memory_size(0) != pages) return 1;
    __rstd_dealloc(bytes, 1024, 8);
    return 0;
}

__attribute__((export_name("rstd_runtime_oom"))) int out_of_memory(void) {
    unsigned char* original = __rstd_alloc(128, 256);
    if (original == NULL) return 1;
    fill(original, 128, 99);
    void*  allocations[64];
    size_t count = 0;
    while (count < 64) {
        void* bytes = __rstd_alloc(48 * 1024, 8);
        if (bytes == NULL) break;
        allocations[count++] = bytes;
    }
    if (count == 64) return 2;
    if (__rstd_realloc(original, 128, 256, 512 * 1024) != NULL) return 3;
    if (! contains(original, 128, 99)) return 4;
    if (__rstd_realloc(original, 128, 3, 64) != NULL) return 5;
    if (__rstd_alloc(SIZE_MAX, 8) != NULL || __rstd_alloc_zeroed(SIZE_MAX, 8) != NULL) return 6;
    for (size_t index = 0; index < count; ++index) __rstd_dealloc(allocations[index], 48 * 1024, 8);
    __rstd_dealloc(original, 128, 256);
    void* recovered = __rstd_alloc(64 * 1024, 8);
    if (recovered == NULL) return 7;
    __rstd_dealloc(recovered, 64 * 1024, 8);
    return 0;
}

__attribute__((export_name("rstd_runtime_mixed"))) int mixed(void) {
    unsigned char* pointers[64]   = { 0 };
    size_t         sizes[64]      = { 0 };
    size_t         alignments[64] = { 0 };
    uint32_t       random         = 12345;
    for (size_t iteration = 0; iteration < 4096; ++iteration) {
        random                    = random * 1664525 + 1013904223;
        const size_t        slot  = (random >> 16) % 64;
        const unsigned char value = (unsigned char)(slot + 1);
        if (pointers[slot] != NULL) {
            if (! contains(pointers[slot], sizes[slot], value)) return 1;
            if ((random & 3) == 0) {
                __rstd_dealloc(pointers[slot], sizes[slot], alignments[slot]);
                pointers[slot] = NULL;
                continue;
            }
        }
        const size_t length = (random >> 8) % 2048 + 1;
        const size_t alignment =
            pointers[slot] == NULL ? (size_t)1 << (random % 9) : alignments[slot];
        unsigned char* bytes = __rstd_realloc(pointers[slot], sizes[slot], alignment, length);
        if (bytes == NULL || (uintptr_t)bytes % alignment != 0) return 2;
        if (pointers[slot] != NULL &&
            ! contains(bytes, sizes[slot] < length ? sizes[slot] : length, value))
            return 3;
        fill(bytes, length, value);
        pointers[slot]   = bytes;
        sizes[slot]      = length;
        alignments[slot] = alignment;
    }
    for (size_t slot = 0; slot < 64; ++slot) {
        if (pointers[slot] == NULL) continue;
        if (! contains(pointers[slot], sizes[slot], (unsigned char)(slot + 1))) return 4;
        __rstd_dealloc(pointers[slot], sizes[slot], alignments[slot]);
    }
    return 0;
}
