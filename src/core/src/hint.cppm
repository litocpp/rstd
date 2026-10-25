export module rstd.core:hint;
export import :arch;

export namespace rstd::hint
{
/// Emits a hardware hint to the processor that the current thread is in a spin loop.
inline void spin_loop() {
    arch::spin_loop();
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
