#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace random = rstd::random;

struct LimitedSource {
    using Error = u32;
    usize remaining;
    auto  fill_bytes(mut_ref<u8[]> out) -> Result<empty, Error> {
        if (remaining == usize()) return Err(u32(91));
        --remaining;
        for (usize i {}; i < out.len(); ++i) out[i] = u8();
        return Ok(empty {});
    }
};

TEST(RandomSequence, BoundariesByteProxyAndPartialFailure) {
    array<u8, 0>  none {};
    array<u8, 1>  single { u8(7) };
    LimitedSource failure { usize() };
    EXPECT_TRUE(random::shuffle(none.as_mut_slice(), failure).is_ok());
    EXPECT_TRUE(random::shuffle(single.as_mut_slice(), failure).is_ok());
    EXPECT_EQ(single[usize()], u8(7));
    auto          engine = random::Mt19937::from_seed(u32(99));
    array<u8, 16> bytes {};
    for (usize i {}; i < bytes.len(); ++i) bytes[i] = u8(i.to_primitive());
    random::shuffle(bytes.as_mut_slice(), engine).unwrap();
    u32 seen {};
    for (auto byte : bytes) seen |= u32(1) << u64(u8(byte).to_primitive());
    EXPECT_EQ(seen, u32(65535));
    LimitedSource partial { usize(3) };
    EXPECT_EQ(random::shuffle(bytes.as_mut_slice(), partial).unwrap_err(), u32(91));
    seen = u32();
    for (auto byte : bytes) seen |= u32(1) << u64(u8(byte).to_primitive());
    EXPECT_EQ(seen, u32(65535));
    auto index = random::UniformInt<usize>::make(usize(), bytes.len() - usize(1)).unwrap();
    EXPECT_TRUE(bytes[random::sample(index, engine).unwrap()] < u8(16));
}

TEST(RandomSequence, MoveOnlyElements) {
    auto values = Vec<Vec<u32>>::make();
    for (unsigned i = 0; i < 8; ++i) {
        auto value = Vec<u32>::make();
        value.push(u32(i));
        values.push(rstd::move(value));
    }
    auto source = random::Mt19937_64::from_seed(u64(123));
    random::shuffle(values.as_mut_slice().as_mut_ref(), source).unwrap();
    u32 seen {};
    for (const auto& value : values) seen |= u32(1) << u64(value[usize()].to_primitive());
    EXPECT_EQ(seen, u32(255));
}
