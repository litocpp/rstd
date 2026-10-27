#include <rstd/runtime.h>
#include "platform.h"

#if RSTD_RUNTIME_WASM

__attribute__((import_module("rstd"), import_name("panic"))) void
rstd_host_panic(const uint8_t* data, size_t size);

static bool panic_write(void* context, const uint8_t* data, size_t size) {
    (void)context;
    rstd_host_panic(data, size);
    return true;
}

#else

#include <stdio.h>
#include <stdlib.h>

static bool panic_write(void* context, const uint8_t* data, size_t size) {
    return fwrite(data, 1, size, (FILE*)context) == size;
}

#endif

void rstd_panic_impl(const struct rstd_panic_info* info) {
#if RSTD_RUNTIME_WASM
    static const char prefix[] = "rstd panic: ";
    rstd_host_panic((const uint8_t*)prefix, sizeof(prefix) - 1);
    if (info->format != NULL) info->format(info->data, NULL, panic_write);
    __builtin_trap();
#else
    fprintf(stderr,
            "thread 'main' panicked at %s:%u:%u:\n",
            info->file,
            (unsigned)info->line,
            (unsigned)info->column);
    if (info->format != NULL) info->format(info->data, stderr, panic_write);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
#endif
}
