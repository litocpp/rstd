export module rstd.core:hint;

export namespace rstd::hint
{
/// Emits a hardware hint to the processor that the current thread is in a spin loop.
void spin_loop() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#else
#endif
}

/// Hides a value from the optimizer while preserving normal ownership semantics.
template<typename T>
auto black_box(T&& value) noexcept -> T&& {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(__builtin_addressof(value)) : "memory");
#else
    asm volatile("" : : "g"(&value) : "memory");
#endif
    return static_cast<T&&>(value);
}
} // namespace rstd::hint
