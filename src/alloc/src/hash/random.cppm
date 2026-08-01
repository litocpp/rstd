export module rstd.alloc:hash.random;
export import rstd.core;

namespace rstd::hash
{

constexpr auto mix_seed(u64 value) noexcept -> u64 {
    value ^= value >> u64(30);
    value = value.wrapping_mul(u64(0xbf58476d1ce4e5b9ULL));
    value ^= value >> u64(27);
    value = value.wrapping_mul(u64(0x94d049bb133111ebULL));
    return value ^ (value >> u64(31));
}

inline auto next_seed() noexcept -> u64 {
    constexpr auto                   increment = u64(0x9e3779b97f4a7c15ULL);
    static sync::atomic::Atomic<u64> counter { increment };
#if __has_builtin(__builtin_readcyclecounter)
    auto entropy = u64(__builtin_readcyclecounter());
#else
    auto entropy = u64();
#endif
    auto sequence = counter.fetch_add(increment, sync::atomic::Ordering::Relaxed);
    auto address  = as_cast<u64>(reinterpret_cast<rstd::uintptr_t>(&counter));
    return mix_seed(entropy ^ sequence ^ address);
}

export class RandomState {
    u64 k0;
    u64 k1;

public:
    using Hasher = DefaultHasher;

    RandomState() noexcept: k0(next_seed()), k1(next_seed()) {}
    RandomState(u64 first, u64 second) noexcept: k0(first), k1(second) {}

    auto build_hasher() const noexcept -> Hasher { return Hasher(k0, k1); }
};

} // namespace rstd::hash
