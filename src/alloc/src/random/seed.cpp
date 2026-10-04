module rstd.alloc;
import :random.seed;

using namespace rstd::prelude;

auto rstd::random::SeedSeq::make(ref<u32[]> input) -> SeedSeq {
    SeedSeq result;
    result.words = ::alloc::vec::Vec<u32>::with_capacity(input.len());
    for (auto word : input) result.words.push(rstd::move(word));
    return result;
}

auto rstd::random::SeedSeq::size() const -> usize {
    return words.len();
}
auto rstd::random::SeedSeq::param() const -> ref<u32[]> {
    return words.as_slice();
}
void rstd::random::SeedSeq::generate(mut_ref<u32[]> output) const {
    generate_seed_sequence(words.as_slice(), output);
}
