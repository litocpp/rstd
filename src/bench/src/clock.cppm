module;
#include <rstd/enum.hpp>

export module rstd.bench:clock;
export import rstd;

using namespace rstd::prelude;

export namespace rstd::bench
{

class ClockError final {
    RSTD_ENUM(ClockError, (Stalled))
};

struct MonotonicClock {
    using Trait                  = MonotonicClock;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = MonotonicClock;

        auto now_ns() const noexcept -> u64 { return trait_call<0>(this); }
        auto resolution() const noexcept -> Result<time::Duration, ClockError> {
            return trait_call<1>(this);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::now_ns, &T::resolution>;
};

class SteadyClock {
    time::Instant origin_ { time::Instant::now() };

public:
    SteadyClock() = default;

    auto now_ns() const noexcept -> u64 {
        auto const nanos = origin_.elapsed().as_nanos();
        if (nanos > u128(u64::MAX.to_primitive())) return u64::MAX;
        return u64(nanos.to_primitive());
    }

    auto resolution() const noexcept -> Result<time::Duration, ClockError> {
        auto best = u64::MAX;
        for (usize sample; sample < usize(20); ++sample) {
            auto const start = now_ns();
            auto       end   = start;
            for (usize attempt; attempt < usize(1'000'000) && end == start; ++attempt) {
                end = now_ns();
            }
            if (end == start) return Err(ClockError::Stalled());
            auto const elapsed = end - start;
            if (elapsed < best) best = elapsed;
        }
        return Ok(time::Duration::from_nanos(best));
    }
};

} // namespace rstd::bench
