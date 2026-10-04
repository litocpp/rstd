export module rstd.core:random.seed;
import :prelude;

namespace rstd::random
{
export template<typename S>
concept SeedSequence = requires(S& sequence, mut_ref<u32[]> output) {
    { sequence.generate(output) } -> mtp::same_as<void>;
};

/// Input and output must not overlap. Expands seed material without adding entropy.
export void generate_seed_sequence(ref<u32[]> input, mut_ref<u32[]> output);
} // namespace rstd::random
