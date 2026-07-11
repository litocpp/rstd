module;
#include <atomic>
#include <chrono>

export module rstd.alloc:hash.random;
export import rstd.core;

namespace rstd::hash
{

constexpr auto mix_seed(u64 value) noexcept -> u64 {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

inline auto next_seed() noexcept -> u64 {
    static std::atomic<u64> counter { 0x9e3779b97f4a7c15ULL };
    auto                    now =
        static_cast<u64>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto sequence = counter.fetch_add(0x9e3779b97f4a7c15ULL, std::memory_order_relaxed);
    return mix_seed(now ^ sequence ^ reinterpret_cast<usize>(&counter));
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
