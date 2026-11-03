#include <random>
#include <cstdint>
#include <cstddef>

extern "C" void random_oracle_mt32(std::uint32_t seed, std::uint32_t* output, std::size_t n) {
    std::mt19937 engine(seed);
    for (std::size_t i = 0; i < n; ++i) output[i] = engine();
}
extern "C" void random_oracle_mt64(std::uint64_t seed, std::uint64_t* output, std::size_t n) {
    std::mt19937_64 engine(seed);
    for (std::size_t i = 0; i < n; ++i) output[i] = engine();
}
extern "C" void random_oracle_seed(const std::uint32_t* input,
                                   std::size_t          size,
                                   std::uint32_t*       output,
                                   std::size_t          n) {
    std::seed_seq sequence(input, input + size);
    sequence.generate(output, output + n);
}
extern "C" void random_oracle_seeded(const std::uint32_t* input,
                                     std::size_t          size,
                                     std::uint32_t*       a,
                                     std::uint64_t*       b,
                                     std::size_t          n) {
    std::seed_seq   sequence(input, input + size);
    std::mt19937    left(sequence);
    std::mt19937_64 right(sequence);
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = left();
        b[i] = right();
    }
}
