#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd::time;
using namespace rstd;

TEST(Time, Duration) {
    auto d1 = Duration::from_secs(u64(1));
    auto d2 = Duration::from_millis(u64(1000));
    EXPECT_EQ(d1, d2);

    auto d3 = Duration::from_nanos(u64(1'000'000'005));
    EXPECT_EQ(d3.as_secs(), u64(1));
    EXPECT_EQ(d3.subsec_nanos(), u32(5));

    auto d4 = d1 + Duration::from_nanos(u64(5));
    EXPECT_EQ(d4, d3);

    auto d5 = d4 - d1;
    EXPECT_EQ(d5, Duration::from_nanos(u64(5)));
}

TEST(Time, Instant) {
    auto now = Instant::now();
    // Busy wait for a bit to ensure some time passes
    int sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;
    }
    (void)sum;
    auto later = Instant::now();
    EXPECT_GE(later, now);

    auto elapsed = now.elapsed();
    EXPECT_GE(elapsed.as_nanos(), u128());
}

TEST(Time, SystemTime) {
    auto now = SystemTime::now();
    int  sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;
    }
    (void)sum;
    auto later = SystemTime::now();
    EXPECT_GE(later, now);

    auto res = later.duration_since(now);
    EXPECT_TRUE(res.is_ok());
    EXPECT_GE(res.unwrap().as_nanos(), u128());
}

TEST(Time, UnixEpoch) {
    auto epoch = SystemTime::unix_epoch();
    auto zero  = epoch.duration_since(epoch);
    ASSERT_TRUE(zero.is_ok());
    EXPECT_EQ(zero.unwrap(), Duration::from_secs(u64()));

    auto since_epoch = SystemTime::now().duration_since(epoch);
    ASSERT_TRUE(since_epoch.is_ok());
    EXPECT_GT(since_epoch.unwrap().as_secs(), u64(1'000'000'000));
}

TEST(Time, UnixTimestampRoundTrip) {
    auto value = SystemTime::from_unix_time(i64(-1), u32(500'000'000)).unwrap();
    auto unix  = value.as_unix_time();

    EXPECT_EQ(unix.seconds, i64(-1));
    EXPECT_EQ(unix.nanoseconds, u32(500'000'000));

    auto before_epoch = SystemTime::unix_epoch().duration_since(value);
    ASSERT_TRUE(before_epoch.is_ok());
    EXPECT_EQ(before_epoch.unwrap(), Duration::from_millis(u64(500)));
}

TEST(Time, RejectsInvalidUnixNanoseconds) {
    EXPECT_TRUE(SystemTime::from_unix_time(i64(), NANOS_PER_SEC).is_none());
}

TEST(Time, Arithmetic) {
    auto now   = Instant::now();
    auto dur   = Duration::from_secs(u64(1));
    auto later = now + dur;
    EXPECT_EQ(later - now, dur);
    EXPECT_EQ(later - dur, now);
}

TEST(Time, DurationCheckedArithmeticPreservesBoundaries) {
    auto almost_second = Duration::new_(u64(), NANOS_PER_SEC - u32(1));
    auto normalized    = almost_second.checked_add(Duration_NANOSECOND).unwrap();
    EXPECT_EQ(normalized, Duration_SECOND);

    EXPECT_TRUE(Duration_MAX.checked_add(Duration_NANOSECOND).is_none());
    EXPECT_EQ(Duration_MAX.saturating_add(Duration_NANOSECOND), Duration_MAX);
    EXPECT_TRUE(Duration_MAX.checked_mul(u32(2)).is_none());
    EXPECT_EQ(Duration_MAX.saturating_mul(u32(2)), Duration_MAX);
    EXPECT_TRUE(Duration_SECOND.checked_sub(Duration_MAX).is_none());
    EXPECT_EQ(Duration_SECOND.saturating_sub(Duration_MAX), Duration_ZERO);
    EXPECT_TRUE(Duration_SECOND.checked_div(u32()).is_none());
}

TEST(Time, UnixTimestampCheckedArithmeticCoversSignedLimits) {
    auto maximum = SystemTime::from_unix_time(i64::MAX, NANOS_PER_SEC - u32(1)).unwrap();
    EXPECT_TRUE(maximum.checked_add(Duration_NANOSECOND).is_none());

    auto minimum = SystemTime::from_unix_time(i64::MIN, u32()).unwrap();
    EXPECT_TRUE(minimum.checked_sub(Duration_NANOSECOND).is_none());
}
