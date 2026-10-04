#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace random = rstd::random;

static_assert(random::DistributionFor<const random::UniformInt<u32>, random::Mt19937>);
static_assert(! random::DistributionFor<const random::Normal<f64>, random::Mt19937>);

struct Bytes {
    using Error = u32;
    u8    byte {};
    usize calls {};
    usize limit { usize::MAX };
    auto  fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        if (++calls > limit) return Err(u32(73));
        for (usize i {}; i < output.len(); ++i) output[i] = byte;
        return Ok(empty {});
    }
};

template<typename T>
void integer_checks() {
    Bytes ones { .byte = u8(255) };
    auto  full = random::UniformInt<T>::make(T::MIN, T::MAX).unwrap();
    EXPECT_EQ(random::sample(full, ones).unwrap(), T::MAX);
    Bytes zeros;
    EXPECT_EQ(random::sample(full, zeros).unwrap(), T::MIN);
    auto single = random::UniformInt<T>::make(T::MAX, T::MAX).unwrap();
    zeros.limit = usize();
    EXPECT_EQ(random::sample(single, zeros).unwrap(), T::MAX);
    EXPECT_EQ(zeros.calls, usize(1));
    EXPECT_TRUE(random::UniformInt<T>::make(T(2), T(1)).is_err());
    auto engine = random::Mt19937::from_seed(u32(42));
    auto range  = random::UniformInt<T>::make(T(0), T(17)).unwrap();
    for (int i = 0; i < 300; ++i) {
        auto value = random::sample(range, engine).unwrap();
        EXPECT_TRUE(value >= T() && value <= T(17));
    }
}
TEST(RandomDistribution, IntegerWidthsAndBoundaries) {
    integer_checks<u8>();
    integer_checks<u16>();
    integer_checks<u32>();
    integer_checks<u64>();
    integer_checks<u128>();
    integer_checks<usize>();
    integer_checks<i8>();
    integer_checks<i16>();
    integer_checks<i32>();
    integer_checks<i64>();
    integer_checks<i128>();
    integer_checks<isize>();
    auto source = random::Mt19937::from_seed(u32(42));
    auto cross  = random::UniformInt<i8>::make(i8(-7), i8(8)).unwrap();
    for (int i = 0; i < 300; ++i) {
        auto value = random::sample(cross, source).unwrap();
        EXPECT_TRUE(value >= i8(-7) && value <= i8(8));
    }
}

TEST(RandomDistribution, ExhaustiveSmallRangesAndRejectionErrors) {
    for (unsigned high : array<unsigned, 6> { 1u, 2u, 5u, 7u, 8u, 16u }) {
        array<u32, 256> counts {};
        auto            dist = random::UniformInt<u8>::make(u8(), u8(high)).unwrap();
        for (unsigned word = 0; word < 256; ++word) {
            Bytes source { .byte = u8(word), .limit = usize(1) };
            auto  result = random::sample(dist, source);
            if (result.is_ok())
                ++counts[usize(result.unwrap().to_primitive())];
            else
                EXPECT_EQ(result.unwrap_err(), u32(73));
        }
        EXPECT_TRUE(counts[usize()] > u32());
        for (unsigned value = 1; value <= high; ++value)
            EXPECT_EQ(counts[usize(value)], counts[usize()]);
    }
    Bytes rejecting { .byte = u8(255), .limit = usize(4) };
    auto  dice = random::UniformInt<u32>::make(u32(1), u32(6)).unwrap();
    EXPECT_EQ(random::sample(dice, rejecting).unwrap_err(), u32(73));
    EXPECT_EQ(rejecting.calls, usize(5));
    Bytes bool_source { .byte = u8(2) };
    EXPECT_FALSE(random::sample(random::Standard<bool> {}, bool_source).unwrap());
    bool_source.byte = u8(3);
    EXPECT_TRUE(random::sample(random::Standard<bool> {}, bool_source).unwrap());
}

template<typename T>
void real_checks() {
    Bytes zero, ones { .byte = u8(255) };
    EXPECT_EQ(random::sample(random::Standard<T> {}, zero).unwrap(), T());
    auto highest = random::sample(random::Standard<T> {}, ones).unwrap();
    EXPECT_TRUE(highest < T(1) && highest > T(0.99));
    auto adjacent = random::UniformReal<T>::make(T(1).next_down(), T(1)).unwrap();
    EXPECT_EQ(random::sample(adjacent, ones).unwrap(), T(1).next_down());
    auto negative = random::UniformReal<T>::make(T(-10), T(-2)).unwrap();
    EXPECT_EQ(random::sample(negative, zero).unwrap(), T(-10));
    EXPECT_TRUE(random::sample(negative, ones).unwrap() < T(-2));
    EXPECT_TRUE(random::UniformReal<T>::make(T(1), T(1)).is_err());
    EXPECT_TRUE(random::UniformReal<T>::make(T::NAN_, T(1)).is_err());
    EXPECT_TRUE(random::UniformReal<T>::make(T(), T::INFINITY_).is_err());
    EXPECT_TRUE(random::UniformReal<T>::make(-T::MAX, T::MAX).is_err());
    EXPECT_TRUE(random::Normal<T>::make(T(), T()).is_err());
    EXPECT_TRUE(random::Normal<T>::make(T::NAN_, T(1)).is_err());
    EXPECT_TRUE(random::Normal<T>::make(T(), T::INFINITY_).is_err());
}
TEST(RandomDistribution, RealAndProbabilityBounds) {
    real_checks<f32>();
    real_checks<f64>();
    Bytes failing { .limit = usize() };
    for (f64 p : array<f64, 2> { f64(), f64(1) }) {
        auto dist = random::Bernoulli::make(p).unwrap();
        EXPECT_EQ(random::sample(dist, failing).unwrap(), p == f64(1));
    }
    EXPECT_EQ(failing.calls, usize());
    EXPECT_TRUE(random::Bernoulli::make(f64(-0.1)).is_err());
    EXPECT_TRUE(random::Bernoulli::make(f64::NAN_).is_err());
    auto half = random::Bernoulli::make(f64(0.5)).unwrap();
    EXPECT_EQ(random::sample(half, failing).unwrap_err(), u32(73));
}

struct QuarterSource {
    using Error = u32;
    usize calls {};
    usize limit { usize::MAX };
    auto  fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        if (++calls > limit) return Err(u32(73));
        for (usize i {}; i < output.len(); ++i) output[i] = u8();
        output[output.len() - usize(1)] = u8(0x40);
        return Ok(empty {});
    }
};
struct TwiceByte {
    using Output = u16;
    template<random::RandomSource R>
    auto sample(R& source) const -> Result<u16, random::rng_error_t<R>> {
        auto value = rstd_try(random::sample(random::Standard<u8> {}, source));
        return Ok(u16(value.to_primitive()) * u16(2));
    }
};
TEST(RandomDistribution, CustomDistributionAndNormalCache) {
    Bytes custom { .byte = u8(11) };
    EXPECT_EQ(random::sample(TwiceByte {}, custom).unwrap(), u16(22));
    EXPECT_EQ(custom.calls, usize(1));
    auto          normal = random::Normal<f64>::make(f64(), f64(1)).unwrap();
    QuarterSource source { .limit = usize(1) };
    EXPECT_EQ(random::sample(normal, source).unwrap_err(), u32(73));
    source.limit = usize::MAX;
    auto a       = random::sample(normal, source).unwrap();
    EXPECT_EQ(source.calls, usize(4));
    auto b = random::sample(normal, source).unwrap();
    EXPECT_EQ(source.calls, usize(4));
    EXPECT_EQ(a, b);
    normal.reset();
    (void)random::sample(normal, source).unwrap();
    EXPECT_EQ(source.calls, usize(6));
    normal.reset();
    (void)random::sample(normal, source).unwrap();
    auto copy = normal.clone();
    EXPECT_EQ(random::sample(normal, source).unwrap(), random::sample(copy, source).unwrap());
    auto extreme = random::Normal<f64>::make(-f64::MAX, f64::MAX).unwrap();
    EXPECT_TRUE(random::sample(extreme, source).unwrap().is_infinite());
}

TEST(RandomDistribution, NormalMomentsAndReplay) {
    auto engine = random::Mt19937_64::from_seed(u64(123));
    auto normal = random::Normal<f64>::make(f64(), f64(1)).unwrap();
    (void)random::sample(normal, engine).unwrap();
    auto saved_engine = engine.clone();
    auto saved_normal = normal.clone();
    for (int i = 0; i < 100; ++i)
        ASSERT_EQ(random::sample(normal, engine).unwrap(),
                  random::sample(saved_normal, saved_engine).unwrap());
    f64 sum {}, squares {};
    for (int i = 0; i < 50000; ++i) {
        auto value = random::sample(normal, engine).unwrap();
        sum += value;
        squares += value * value;
    }
    EXPECT_TRUE((sum / f64(50000)).abs() < f64(0.03));
    EXPECT_TRUE((squares / f64(50000) - f64(1)).abs() < f64(0.04));
}
