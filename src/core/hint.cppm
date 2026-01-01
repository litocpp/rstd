export module rstd.core:hint;

export namespace rstd::hint
{
void spin_loop() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#else
#endif
}
} // namespace rstd::hint