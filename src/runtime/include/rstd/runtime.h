#ifndef RSTD_RUNTIME_H
#define RSTD_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Alignment must be a nonzero power of two and be preserved when releasing storage.
// Zero-byte allocations are releasable; failed reallocation leaves the old storage valid.
void* __rstd_alloc(size_t size, size_t align);
void  __rstd_dealloc(void* pointer, size_t size, size_t align);
void* __rstd_realloc(void* pointer, size_t old_size, size_t align, size_t new_size);
void* __rstd_alloc_zeroed(size_t size, size_t align);

typedef bool (*rstd_panic_write_fn)(void* context, const uint8_t* data, size_t size);
typedef bool (*rstd_panic_format_fn)(const void* data, void* context, rstd_panic_write_fn write);

struct rstd_panic_info {
    const void*          data;
    rstd_panic_format_fn format;
    const char*          file;
    const char*          function;
    uint32_t             line;
    uint32_t             column;
    bool                 can_unwind;
    bool                 force_no_backtrace;
};

__attribute__((noreturn)) void rstd_panic_impl(const struct rstd_panic_info* info);

#ifdef __cplusplus
}
#endif

#endif
