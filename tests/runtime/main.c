#include <rstd/runtime.h>

static bool aligned(const void* pointer, size_t alignment) {
    return pointer != NULL && (uintptr_t)pointer % alignment == 0;
}

int main(void) {
    const size_t alignments[] = { 1, 2, 4, 8, 16, 64, 4096 };
    const size_t sizes[]      = { 0, 1, 264, 4097 };
    for (size_t a = 0; a < sizeof(alignments) / sizeof(*alignments); ++a) {
        const size_t alignment = alignments[a];
        for (size_t s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s) {
            const size_t   size  = sizes[s];
            unsigned char* bytes = __rstd_alloc_zeroed(size, alignment);
            if (! aligned(bytes, alignment)) return 1;
            for (size_t index = 0; index < size; ++index) {
                if (bytes[index] != 0) return 2;
                bytes[index] = (unsigned char)index;
            }
            const size_t   grown_size = size + 512;
            unsigned char* grown      = __rstd_realloc(bytes, size, alignment, grown_size);
            if (! aligned(grown, alignment)) return 3;
            for (size_t index = 0; index < size; ++index) {
                if (grown[index] != (unsigned char)index) return 4;
            }
            unsigned char* shrunk = __rstd_realloc(grown, grown_size, alignment, size);
            if (! aligned(shrunk, alignment)) return 5;
            for (size_t index = 0; index < size; ++index) {
                if (shrunk[index] != (unsigned char)index) return 6;
            }
            __rstd_dealloc(shrunk, size, alignment);
            void* from_null = __rstd_realloc(NULL, 0, alignment, size);
            if (! aligned(from_null, alignment)) return 7;
            __rstd_dealloc(from_null, size, alignment);
            __rstd_dealloc(NULL, 0, alignment);
        }
        unsigned char* bytes = __rstd_alloc(16, alignment);
        if (! aligned(bytes, alignment)) return 8;
        bytes[0] = 42;
        if (__rstd_realloc(bytes, 16, alignment, SIZE_MAX) != NULL) return 9;
        if (bytes[0] != 42) return 10;
        __rstd_dealloc(bytes, 16, alignment);
    }
    const size_t invalid[] = { 0, 3, 6 };
    for (size_t index = 0; index < sizeof(invalid) / sizeof(*invalid); ++index) {
        if (__rstd_alloc(16, invalid[index]) != NULL) return 11;
    }
    return 0;
}
