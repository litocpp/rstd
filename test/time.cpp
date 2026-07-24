#include <array>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd::time;
using namespace rstd;

constexpr auto CALENDAR_EPOCH = OffsetDateTime::from_unix_time(i64(), u32());
static_assert(CALENDAR_EPOCH.is_ok());
static_assert(CALENDAR_EPOCH->date().year() == i32(1970));
static_assert(CALENDAR_EPOCH->date().month() == Month::January);
static_assert(CALENDAR_EPOCH->date().day() == u8(1));
static_assert(CALENDAR_EPOCH->time() == Time {});
static_assert(OffsetDateTime::UNIX_EPOCH == *CALENDAR_EPOCH);
static_assert(UtcOffset::UTC.is_utc());
static_assert(Impled<OffsetDateTime, convert::TryFrom<SystemTime>>);
static_assert(Impled<ComponentRange, error::Error>);
static_assert(Impled<IndeterminateOffset, error::Error>);

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

TEST(Time, MonthRange) {
    auto january  = Month::from_number(u8(1));
    auto december = Month::from_number(u8(12));
    ASSERT_TRUE(january.is_ok());
    ASSERT_TRUE(december.is_ok());
    EXPECT_EQ(january->number(), u8(1));
    EXPECT_EQ(december->number(), u8(12));

    auto zero = Month::from_number(u8());
    auto high = Month::from_number(u8(13));
    ASSERT_TRUE(zero.is_err());
    ASSERT_TRUE(high.is_err());
    EXPECT_EQ(zero.unwrap_err().name(), "month"_str);
    EXPECT_FALSE(high.unwrap_err().is_conditional());
}

TEST(Time, DateComponentRangesAndLeapYears) {
    auto leap = Date::from_calendar_date(i32(2000), Month::February, u8(29));
    ASSERT_TRUE(leap.is_ok());
    EXPECT_EQ(leap->ordinal(), u16(60));
    EXPECT_EQ(leap->month(), Month::February);
    EXPECT_EQ(leap->day(), u8(29));

    EXPECT_TRUE(Date::from_calendar_date(i32(1900), Month::February, u8(29)).is_err());
    EXPECT_TRUE(Date::from_calendar_date(i32(2001), Month::January, u8()).is_err());
    EXPECT_TRUE(Date::from_ordinal_date(i32(2001), u16(366)).is_err());
    EXPECT_TRUE(Date::from_ordinal_date(i32(2000), u16(366)).is_ok());
    EXPECT_TRUE(Date::from_calendar_date(i32(-9999), Month::January, u8(1)).is_ok());
    EXPECT_TRUE(Date::from_calendar_date(i32(9999), Month::December, u8(31)).is_ok());
    EXPECT_TRUE(Date::from_calendar_date(i32(-10'000), Month::January, u8(1)).is_err());
    EXPECT_TRUE(Date::from_calendar_date(i32(10'000), Month::January, u8(1)).is_err());
    EXPECT_TRUE(
        Date::from_calendar_date(i32(2000), Month(static_cast<Month::Value>(0)), u8(1)).is_err());

    auto invalid_day = Date::from_calendar_date(i32(2001), Month::February, u8(29));
    ASSERT_TRUE(invalid_day.is_err());
    EXPECT_EQ(invalid_day.unwrap_err().name(), "day"_str);
    EXPECT_TRUE(invalid_day.unwrap_err().is_conditional());
}

TEST(Time, TimeComponentRanges) {
    auto last = Time::from_hms_nano(u8(23), u8(59), u8(59), NANOS_PER_SEC - u32(1));
    ASSERT_TRUE(last.is_ok());
    EXPECT_EQ(last->hour(), u8(23));
    EXPECT_EQ(last->minute(), u8(59));
    EXPECT_EQ(last->second(), u8(59));
    EXPECT_EQ(last->nanosecond(), u32(999'999'999));

    EXPECT_TRUE(Time::from_hms_nano(u8(24), u8(), u8(), u32()).is_err());
    EXPECT_TRUE(Time::from_hms_nano(u8(), u8(60), u8(), u32()).is_err());
    EXPECT_TRUE(Time::from_hms_nano(u8(), u8(), u8(60), u32()).is_err());
    EXPECT_TRUE(Time::from_hms_nano(u8(), u8(), u8(), NANOS_PER_SEC).is_err());
}

TEST(Time, UtcOffsetRange) {
    EXPECT_TRUE(UtcOffset::UTC.is_utc());
    EXPECT_EQ(UtcOffset::from_whole_seconds(i32(3'661))->whole_seconds(), i32(3'661));
    EXPECT_EQ(UtcOffset::from_whole_seconds(i32(-3'661))->whole_seconds(), i32(-3'661));
    EXPECT_TRUE(UtcOffset::from_whole_seconds(i32(86'399)).is_ok());
    EXPECT_TRUE(UtcOffset::from_whole_seconds(i32(-86'399)).is_ok());
    EXPECT_TRUE(UtcOffset::from_whole_seconds(i32(86'400)).is_err());
    EXPECT_TRUE(UtcOffset::from_whole_seconds(i32(-86'400)).is_err());
}

TEST(Time, UnixTimestampCalendarBoundaries) {
    auto before_epoch_nanosecond = OffsetDateTime::from_unix_time(i64(-1), u32(999'999'999));
    ASSERT_TRUE(before_epoch_nanosecond.is_ok());
    EXPECT_EQ(before_epoch_nanosecond->date().year(), i32(1969));
    EXPECT_EQ(before_epoch_nanosecond->date().month(), Month::December);
    EXPECT_EQ(before_epoch_nanosecond->date().day(), u8(31));
    EXPECT_EQ(before_epoch_nanosecond->time().hour(), u8(23));
    EXPECT_EQ(before_epoch_nanosecond->time().minute(), u8(59));
    EXPECT_EQ(before_epoch_nanosecond->time().second(), u8(59));
    EXPECT_EQ(before_epoch_nanosecond->time().nanosecond(), u32(999'999'999));

    auto before_epoch_second = OffsetDateTime::from_unix_time(i64(-1), u32());
    ASSERT_TRUE(before_epoch_second.is_ok());
    EXPECT_EQ(before_epoch_second->date(), before_epoch_nanosecond->date());
    EXPECT_EQ(before_epoch_second->time().second(), u8(59));
    EXPECT_EQ(before_epoch_second->time().nanosecond(), u32());

    auto previous_day = OffsetDateTime::from_unix_time(i64(-86'400), u32());
    auto next_day     = OffsetDateTime::from_unix_time(i64(86'400), u32());
    ASSERT_TRUE(previous_day.is_ok());
    ASSERT_TRUE(next_day.is_ok());
    EXPECT_EQ(previous_day->date(), before_epoch_second->date());
    EXPECT_EQ(previous_day->time(), Time {});
    EXPECT_EQ(next_day->date().year(), i32(1970));
    EXPECT_EQ(next_day->date().month(), Month::January);
    EXPECT_EQ(next_day->date().day(), u8(2));

    auto leap_day = OffsetDateTime::from_unix_time(i64(951'782'400), u32(123));
    ASSERT_TRUE(leap_day.is_ok());
    EXPECT_EQ(leap_day->date().year(), i32(2000));
    EXPECT_EQ(leap_day->date().month(), Month::February);
    EXPECT_EQ(leap_day->date().day(), u8(29));
    EXPECT_EQ(leap_day->time().nanosecond(), u32(123));

    EXPECT_TRUE(OffsetDateTime::from_unix_time(i64(), NANOS_PER_SEC).is_err());
    EXPECT_TRUE(OffsetDateTime::from_unix_time(i64::MIN, u32()).is_err());
    EXPECT_TRUE(OffsetDateTime::from_unix_time(i64::MAX, u32()).is_err());
}

TEST(Time, OffsetConversionPreservesInstantAndCrossesCalendarBoundaries) {
    auto utc  = OffsetDateTime::from_unix_time(i64(), u32(123)).unwrap();
    auto east = UtcOffset::from_whole_seconds(i32(3'661)).unwrap();
    auto west = UtcOffset::from_whole_seconds(i32(-1)).unwrap();

    auto eastern = utc.checked_to_offset(east);
    auto western = utc.checked_to_offset(west);
    ASSERT_TRUE(eastern.is_some());
    ASSERT_TRUE(western.is_some());
    EXPECT_EQ(eastern->date(), utc.date());
    EXPECT_EQ(eastern->time().hour(), u8(1));
    EXPECT_EQ(eastern->time().minute(), u8(1));
    EXPECT_EQ(eastern->time().second(), u8(1));
    EXPECT_EQ(western->date().year(), i32(1969));
    EXPECT_EQ(western->date().month(), Month::December);
    EXPECT_EQ(western->date().day(), u8(31));
    EXPECT_EQ(western->time().hour(), u8(23));
    EXPECT_EQ(western->time().minute(), u8(59));
    EXPECT_EQ(western->time().second(), u8(59));
    EXPECT_EQ(eastern->unix_time().seconds, utc.unix_time().seconds);
    EXPECT_EQ(eastern->unix_time().nanoseconds, utc.unix_time().nanoseconds);
    EXPECT_EQ(*eastern, utc);

    auto minimum =
        PlainDateTime::make(Date::from_calendar_date(i32(-9999), Month::January, u8(1)).unwrap(),
                            Time {})
            .assume_utc();
    auto maximum =
        PlainDateTime::make(Date::from_calendar_date(i32(9999), Month::December, u8(31)).unwrap(),
                            Time::from_hms_nano(u8(23), u8(59), u8(59), u32()).unwrap())
            .assume_utc();
    EXPECT_TRUE(minimum.checked_to_offset(west).is_none());
    EXPECT_TRUE(maximum.checked_to_offset(east).is_none());
}

TEST(Time, SystemTimeCalendarInteropIsFallible) {
    auto epoch = rstd::try_from<OffsetDateTime>(SystemTime::unix_epoch());
    ASSERT_TRUE(epoch.is_ok());
    EXPECT_EQ(*epoch, OffsetDateTime::UNIX_EPOCH);

    auto before    = SystemTime::from_unix_time(i64(-1), u32(500'000'000)).unwrap();
    auto converted = rstd::try_from<OffsetDateTime>(before);
    ASSERT_TRUE(converted.is_ok());
    EXPECT_EQ(converted->unix_time().seconds, i64(-1));
    EXPECT_EQ(converted->unix_time().nanoseconds, u32(500'000'000));

    auto out_of_range = SystemTime::from_unix_time(i64(253'402'300'800), u32());
    ASSERT_TRUE(out_of_range.is_some());
    EXPECT_TRUE(rstd::try_from<OffsetDateTime>(*out_of_range).is_err());
}

TEST(Time, LocalDateTimeUsesTheOffsetForItsInstant) {
    auto before = OffsetDateTime::now_utc();
    auto local  = OffsetDateTime::now_local();
    auto after  = OffsetDateTime::now_utc();
    ASSERT_TRUE(local.is_ok());
    EXPECT_GE(*local, before);
    EXPECT_LE(*local, after);
    EXPECT_GE(local->time().hour(), u8());
    EXPECT_LT(local->time().hour(), u8(24));
    EXPECT_LT(local->time().minute(), u8(60));
    EXPECT_LT(local->time().second(), u8(60));
    EXPECT_LT(local->time().nanosecond(), NANOS_PER_SEC);
}

TEST(Time, LocalOffsetLookupIsConsistentAcrossThreads) {
    constexpr int      unset = 100'000;
    auto               fixed = OffsetDateTime::from_unix_time(i64(1'700'000'000), u32()).unwrap();
    std::array<int, 8> offsets;
    offsets.fill(unset);

    std::vector<std::thread> workers;
    workers.reserve(offsets.size());
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        workers.emplace_back([fixed, index, &offsets] {
            auto offset = UtcOffset::local_offset_at(fixed);
            if (offset.is_ok()) offsets[index] = offset->whole_seconds().to_primitive();
        });
    }
    for (auto& worker : workers) worker.join();

    ASSERT_NE(offsets.front(), unset);
    for (auto offset : offsets) EXPECT_EQ(offset, offsets.front());
}
