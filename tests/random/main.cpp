#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace random = rstd::random;

struct CountingSource {
    using Error = rstd::convert::Infallible;
    u8             value {};
    constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        for (usize i {}; i < output.len(); ++i) output[i] = value++;
        return Ok(empty {});
    }
};

constexpr auto source_works() -> bool {
    CountingSource source;
    array<u8, 3>   bytes {};
    auto           result = random::fill_bytes(source, bytes.as_mut_slice());
    return result.is_ok() && bytes[usize(2)] == u8(2) && source.value == u8(3);
}
static_assert(source_works());
static_assert(! random::RandomSource<const CountingSource>);

TEST(Random, SourceBorrowAndConstexpr) {
    EXPECT_TRUE(source_works());
    CountingSource source;
    EXPECT_EQ(random::sample(random::Standard<u32> {}, source).unwrap(), u32(0x03020100));
    auto distribution = random::UniformInt<u32>::make(u32(10), u32(15)).unwrap();
    EXPECT_EQ(random::sample(distribution, source).unwrap(), u32(14));
}

auto main() -> int {
    return rstd::test::run_registered().to_primitive();
}
