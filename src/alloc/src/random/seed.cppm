export module rstd.alloc:random.seed;
export import rstd.core;
import :vec;

namespace rstd::random
{
export class SeedSeq {
    ::alloc::vec::Vec<u32> words;

public:
    SeedSeq() = default;
    static auto make(ref<u32[]> input) -> SeedSeq;
    auto        size() const -> usize;
    auto        param() const [[clang::lifetimebound]] -> ref<u32[]>;
    void        generate(mut_ref<u32[]> output) const;
};
} // namespace rstd::random
