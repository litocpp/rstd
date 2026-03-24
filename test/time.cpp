#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd::time;

TEST(Time, Duration) {
    auto d1 = Duration::from_secs(1);
    auto d2 = Duration::from_millis(1000);
    EXPECT_EQ(d1, d2);

    auto d3 = Duration::from_nanos(1'000'000'005);
    EXPECT_EQ(d3.as_secs(), 1);
    EXPECT_EQ(d3.subsec_nanos(), 5);

    auto d4 = d1 + Duration::from_nanos(5);
    EXPECT_EQ(d4, d3);

    auto d5 = d4 - d1;
    EXPECT_EQ(d5, Duration::from_nanos(5));
}

TEST(Time, Instant) {
    auto now = Instant::now();
    // Busy wait for a bit to ensure some time passes
    int sum = 0;
    for (int i = 0; i < 1000000; ++i) { sum += i; }
    (void)sum;
    auto later = Instant::now();
    EXPECT_GE(later, now);
    
    auto elapsed = now.elapsed();
    EXPECT_GE(elapsed.as_nanos(), 0);
}

TEST(Time, SystemTime) {
    auto now = SystemTime::now();
    int sum = 0;
    for (int i = 0; i < 1000000; ++i) { sum += i; }
    (void)sum;
    auto later = SystemTime::now();
    EXPECT_GE(later, now);

    auto res = later.duration_since(now);
    EXPECT_TRUE(res.is_ok());
    EXPECT_GE(res.unwrap().as_nanos(), 0);
}

TEST(Time, Arithmetic) {
    auto now = Instant::now();
    auto dur = Duration::from_secs(1);
    auto later = now + dur;
    EXPECT_EQ(later - now, dur);
    EXPECT_EQ(later - dur, now);
}
