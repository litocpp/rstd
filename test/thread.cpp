#include <gtest/gtest.h>
import rstd;
using namespace rstd;

TEST(Thread, BasicSpawn) {
    auto result = thread::spawn([] {
        // Do some work
    });

    ASSERT_TRUE(result.is_ok());
    auto handle      = result.unwrap();
    auto join_result = rstd::move(handle).join();
    ASSERT_TRUE(join_result.is_ok());
}

TEST(Thread, SpawnWithValue) {
    auto result = thread::spawn([] {
        return 42;
    });

    ASSERT_TRUE(result.is_ok());
    auto handle      = result.unwrap();
    auto join_result = rstd::move(handle).join();
    ASSERT_TRUE(join_result.is_ok());
}

TEST(Thread, BuilderWithName) {
    auto result = thread::builder::Builder::make()
        .name(rstd::format("{}", "test-thread"))
        .spawn([] {
            // Do some work
        });

    ASSERT_TRUE(result.is_ok());
    auto handle        = result.unwrap();
    auto thread_handle = handle.thread();
    ASSERT_TRUE(thread_handle.name().is_some());
}

TEST(Thread, ThreadId) {
    auto id1 = thread::ThreadId::make();
    auto id2 = thread::ThreadId::make();

    EXPECT_NE(id1, id2);
}

TEST(Thread, Current) {
    auto current = thread::current();
    auto id      = current.id();

    EXPECT_GT(id.as_u64().get(), 0);
}

TEST(Thread, Sleep) {
    auto start = rstd::time::Instant::now();
    thread::sleep(rstd::time::Duration::from_millis(100));
    auto elapsed = start.elapsed();
    EXPECT_GE(elapsed.as_secs_f64(), 0.1);
}

#if !__has_feature(address_sanitizer)
TEST(Thread, MultipleThreads) {
    auto counter = std::atomic<int>(0);
    {
        auto handle1 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(50));
                           counter++;
                       }).unwrap();

        auto handle2 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(30));
                           counter++;
                       }).unwrap();

        auto handle3 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(10));
                           counter++;
                       }).unwrap();

        rstd::move(handle1).join().unwrap_unchecked();
        rstd::move(handle2).join().unwrap_unchecked();
        rstd::move(handle3).join().unwrap_unchecked();
    }

    EXPECT_EQ(counter.load(), 3);
}
#endif

TEST(Thread, ThreadHandleId) {
    auto result = thread::spawn([] {
        return thread::current().id();
    });

    ASSERT_TRUE(result.is_ok());
    auto handle      = result.unwrap();
    auto join_result = rstd::move(handle).join();
    ASSERT_TRUE(join_result.is_ok());
}