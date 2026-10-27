#ifndef RSTD_RUNTIME_PLATFORM_H
#define RSTD_RUNTIME_PLATFORM_H

#if defined(__wasm__) && __is_target_os(unknown)
#define RSTD_RUNTIME_WASM         1
#define RSTD_RUNTIME_EXPORT(name) __attribute__((export_name(name)))

#if defined(__wasm_atomics__) || defined(__wasm_shared_memory__)
#error "rstd wasm runtime does not support shared memory or atomics on unknown OS targets"
#endif

#else
#define RSTD_RUNTIME_WASM 0
#define RSTD_RUNTIME_EXPORT(name)
#endif

#endif
