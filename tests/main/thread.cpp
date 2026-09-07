#include <rstd/test/gtest.hpp>
#include <atomic>
import rstd;
using namespace rstd;

static_assert(! mtp::copy<thread::JoinHandle<void>>);

TEST(Thread, ExplicitJoinHandleStorage) {
    auto handle = Option<thread::JoinHandle<void>> {};
    handle      = Some(thread::spawn([] {
                       }).unwrap());

    ASSERT_TRUE(handle.is_some());
    EXPECT_TRUE(rstd::move(*handle).join().is_ok());
}

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
    auto result =
        thread::builder::Builder::make().name(rstd::format("{}", "test-thread")).spawn([] {
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

    EXPECT_GT(id.as_u64().get(), u64());
}

TEST(Thread, Sleep) {
    auto start = rstd::time::Instant::now();
    thread::sleep(rstd::time::Duration::from_millis(u64(100)));
    auto elapsed = start.elapsed();
    EXPECT_GE(elapsed.as_secs_f64(), 0.1);
}

#if ! __has_feature(address_sanitizer)
TEST(Thread, MultipleThreads) {
    auto counter = std::atomic<int>(0);
    {
        auto handle1 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(u64(50)));
                           counter++;
                       }).unwrap();

        auto handle2 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(u64(30)));
                           counter++;
                       }).unwrap();

        auto handle3 = thread::spawn([&counter] {
                           thread::sleep(rstd::time::Duration::from_millis(u64(10)));
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

TEST(Thread, ParkConsumesPreexistingNotification) {
    auto handle = thread::spawn([] {
                      auto current = thread::current();
                      thread::unpark(current);
                      thread::park();
                  }).unwrap();

    EXPECT_TRUE(rstd::move(handle).join().is_ok());
}

TEST(Thread, UnparkWakesParkedThread) {
    auto state         = std::atomic<int> {};
    auto handle        = thread::spawn([&state] {
                      state.store(1);
                      thread::park();
                      state.store(2);
                         }).unwrap();
    auto parked_thread = handle.thread();

    while (state.load() != 1) thread::yield_now();
    thread::unpark(parked_thread);

    EXPECT_TRUE(rstd::move(handle).join().is_ok());
    EXPECT_EQ(state.load(), 2);
}

TEST(Thread, ParkTimeoutReturns) {
    auto handle = thread::spawn([] {
                      thread::park_timeout(time::Duration::from_millis(u64(10)));
                  }).unwrap();

    EXPECT_TRUE(rstd::move(handle).join().is_ok());
}

TEST(Thread, UnparkStoresSingleNotification) {
    auto ready         = std::atomic<bool> {};
    auto start         = std::atomic<bool> {};
    auto handle        = thread::spawn([&ready, &start] {
                      ready.store(true);
                      while (! start.load()) thread::yield_now();

                      thread::park();
                      auto before = time::Instant::now();
                      thread::park_timeout(time::Duration::from_millis(u64(40)));
                      return before.elapsed() >= time::Duration::from_millis(u64(10));
                         }).unwrap();
    auto parked_thread = handle.thread();

    while (! ready.load()) thread::yield_now();
    thread::unpark(parked_thread);
    thread::unpark(parked_thread);
    start.store(true);

    auto result = rstd::move(handle).join();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.unwrap_unchecked());
}
