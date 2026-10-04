#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace random = rstd::random;
extern "C" void random_oracle_mt32(rstd::uint32_t, rstd::uint32_t*, rstd::size_t);
extern "C" void random_oracle_mt64(rstd::uint64_t, rstd::uint64_t*, rstd::size_t);
extern "C" void
random_oracle_seed(const rstd::uint32_t*, rstd::size_t, rstd::uint32_t*, rstd::size_t);
extern "C" void random_oracle_seeded(const rstd::uint32_t*,
                                     rstd::size_t,
                                     rstd::uint32_t*,
                                     rstd::uint64_t*,
                                     rstd::size_t);

constexpr auto constexpr_engine() -> bool {
    auto engine = random::Mt19937::from_seed(u32(5489));
    auto first  = engine.next();
    auto range  = random::UniformInt<i16>::make(i16(-5), i16(5)).unwrap();
    auto value  = random::sample(range, engine).unwrap();
    return first == u32(3499211612u) && value >= i16(-5) && value <= i16(5);
}
static_assert(constexpr_engine());

TEST(RandomEngine, StandardVectorsAndIntegerSeeds) {
    array<rstd::uint32_t, 10000> expected32 {};
    array<rstd::uint64_t, 10000> expected64 {};
    for (auto seed : array<u64, 4> { u64(), u64(5489), u64(42), u64::MAX }) {
        random_oracle_mt32(
            static_cast<rstd::uint32_t>(seed.to_primitive()), expected32.data(), 10000);
        random_oracle_mt64(seed.to_primitive(), expected64.data(), 10000);
        auto a = random::Mt19937::from_seed(u32(static_cast<rstd::uint32_t>(seed.to_primitive())));
        auto b = random::Mt19937_64::from_seed(seed);
        for (usize i {}; i < usize(10000); ++i) {
            ASSERT_EQ(a.next(), u32(expected32[i]));
            ASSERT_EQ(b.next(), u64(expected64[i]));
        }
        if (seed == u64(5489)) {
            EXPECT_EQ(u32(expected32[usize(9999)]), u32(4123659995u));
            EXPECT_EQ(u64(expected64[usize(9999)]), u64(9981545732273789042ULL));
        }
    }
}

template<typename E>
void state_checks() {
    E    a;
    auto b = rstd::as<rstd::clone::Clone>(a).clone();
    EXPECT_TRUE(a == b);
    a.discard(u64(1277));
    for (int i = 0; i < 1277; ++i) (void)b.next();
    EXPECT_TRUE(a == b);
    auto         saved = a.clone();
    auto         word  = b.next().to_le_bytes();
    array<u8, 1> byte {};
    a.fill_bytes(byte.as_mut_slice()).unwrap();
    EXPECT_EQ(u8(byte[usize()]), u8(word[usize()]));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == saved);
    array<u8, 0> none {};
    a.fill_bytes(none.as_mut_slice()).unwrap();
    EXPECT_TRUE(a == b);
    array<u8, 2 * sizeof(typename E::ResultType) + 1> bytes {};
    a.fill_bytes(bytes.as_mut_slice()).unwrap();
    for (usize at {}; at < bytes.len();) {
        auto next = b.next().to_le_bytes();
        for (usize i {}; i < next.len() && at < bytes.len(); ++i)
            EXPECT_EQ(u8(bytes[at++]), u8(next[i]));
    }
    EXPECT_TRUE(a == b);
    a.seed();
    EXPECT_TRUE(a == E {});
    EXPECT_EQ(E::min(), typename E::ResultType());
    EXPECT_EQ(E::max(), E::ResultType::MAX);
}
TEST(RandomEngine, StateAndByteConsumption) {
    state_checks<random::Mt19937>();
    state_checks<random::Mt19937_64>();
}

TEST(RandomSeed, StandardExpansionAndOwnership) {
    array<u32, 5>              input { u32(), u32(1), u32::MAX, u32(42), u32(0x80000000) };
    array<rstd::uint32_t, 5>   raw { 0u, 1u, 0xffffffffu, 42u, 0x80000000u };
    array<u32, 624>            output {};
    array<rstd::uint32_t, 624> expected {};
    for (usize size : array<usize, 3> { usize(), usize(1), usize(5) }) {
        auto sequence = random::SeedSeq::make(ref<u32[]>::from_raw_parts(input.data(), size));
        EXPECT_EQ(sequence.size(), size);
        for (usize n : array<usize, 12> { usize(),
                                          usize(1),
                                          usize(6),
                                          usize(7),
                                          usize(38),
                                          usize(39),
                                          usize(67),
                                          usize(68),
                                          usize(312),
                                          usize(622),
                                          usize(623),
                                          usize(624) }) {
            random_oracle_seed(raw.data(), size.to_primitive(), expected.data(), n.to_primitive());
            for (int repeat = 0; repeat < 2; ++repeat) {
                sequence.generate(mut_ref<u32[]>::from_raw_parts(output.data(), n));
                for (usize i {}; i < n; ++i) ASSERT_EQ(output[i], u32(expected[i]));
            }
        }
        array<rstd::uint32_t, 1300> a {};
        array<rstd::uint64_t, 1300> b {};
        random_oracle_seeded(raw.data(), size.to_primitive(), a.data(), b.data(), 1300);
        auto left  = random::Mt19937::from_seed_sequence(sequence);
        auto right = random::Mt19937_64::from_seed_sequence(sequence);
        for (usize i {}; i < usize(1300); ++i) {
            ASSERT_EQ(left.next(), u32(a[i]));
            ASSERT_EQ(right.next(), u64(b[i]));
        }
    }
    auto owned     = random::SeedSeq::make(input.as_slice());
    input[usize()] = u32(99);
    EXPECT_EQ(owned.param()[usize()], u32());
}

struct FixedSeed {
    u32  first;
    void generate(mut_ref<u32[]> output) {
        for (auto& value : output) value = u32();
        output[usize()] = first;
    }
};
TEST(RandomSeed, CorrectsEffectiveZeroState) {
    FixedSeed zero { u32() }, ignored { u32(0x7fffffff) }, high32 { u32(0x80000000) };
    EXPECT_TRUE(random::Mt19937::from_seed_sequence(zero) ==
                random::Mt19937::from_seed_sequence(high32));
    EXPECT_TRUE(random::Mt19937::from_seed_sequence(zero) ==
                random::Mt19937::from_seed_sequence(ignored));
    EXPECT_TRUE(random::Mt19937_64::from_seed_sequence(zero) ==
                random::Mt19937_64::from_seed_sequence(ignored));
}
