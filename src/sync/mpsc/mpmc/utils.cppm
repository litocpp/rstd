export module rstd.sync.mpsc:mpmc.utils;

namespace rstd::sync::mpsc::mpmc::utils
{

consteval auto align_size() -> int {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
    return 128;
#elif defined(__arm__) || defined(__mips__) || defined(__ppc64__)
    return 32;
#else
    return 64;
#endif
}

template<typename T>
struct alignas(align_size()) CachePadded {
    T value;

    auto operator->() -> T* { return &value; }
    auto operator*() -> T& { return value; }
};

} // namespace rstd::sync::mpsc::mpmc::utils