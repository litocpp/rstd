module;
#include <chrono>

export module rstd.alloc:hash.random;
export import rstd.core;

namespace rstd::hash
{

constexpr auto mix_seed(u64 value) noexcept -> u64 {
    value ^= value >> u64(30);
    value *= u64(0xbf58476d1ce4e5b9ULL);
    value ^= value >> u64(27);
    value *= u64(0x94d049bb133111ebULL);
    return value ^ (value >> u64(31));
}

inline auto next_seed() noexcept -> u64 {
    constexpr auto                   increment = u64(0x9e3779b97f4a7c15ULL);
    static sync::atomic::Atomic<u64> counter { increment };
    auto now = as_cast<u64>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto sequence = counter.fetch_add(increment, sync::atomic::Ordering::Relaxed);
    auto address  = as_cast<u64>(reinterpret_cast<rstd::uintptr_t>(&counter));
    return mix_seed(now ^ sequence ^ address);
}

export class RandomState {
    u64 k0;
    u64 k1;

public:
    RandomState() noexcept: k0(next_seed()), k1(next_seed()) {}
    RandomState(u64 first, u64 second) noexcept: k0(first), k1(second) {}

    template<typename K>
    auto operator()(const K& key) const noexcept -> u64
        requires rstd::Impled<K, Hash>
    {
        DefaultHasher state(k0, k1);
        rstd::as<Hash>(key).hash(state);
        return state.finish();
    }
};

} // namespace rstd::hash
