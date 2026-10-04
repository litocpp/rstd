export module rstd:random;
export import rstd.core;
export import :io.error;

namespace rstd::random
{
export struct SystemRng {
    using Error = io::error::Error;
    auto fill_bytes(mut_ref<u8[]> output) -> io::Result<empty>;
};

// No weak fallback. On error, discard the possibly partially filled buffer.
// Linux returns WouldBlock if the OS entropy pool is not initialized.
export auto fill_secure(mut_ref<u8[]> output) -> io::Result<empty>;

export template<typename D>
    requires DistributionFor<mtp::rm_ref<D>, SystemRng>
auto random(D&& distribution) -> io::Result<typename mtp::rm_ref<D>::Output> {
    SystemRng source;
    return sample(rstd::forward<D>(distribution), source);
}
} // namespace rstd::random
