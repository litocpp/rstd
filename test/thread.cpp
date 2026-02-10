#include <gtest/gtest.h>
import rstd;
using namespace rstd;
/*
TEST(Thread, BasicSpawn) {
    auto result = thread::spawn([] {
        // Do some work
    });

    ASSERT_TRUE(result.is_ok());
    auto handle      = result.unwrap();
    auto join_result = handle.join();
    ASSERT_TRUE(join_result.is_ok());
}

TEST(Thread, SpawnWithValue) {
    auto result = thread::spawn([] {
        return 42;
    });

    ASSERT_TRUE(result.is_ok());
    auto handle      = result.unwrap();
    auto join_result = handle.join();
    ASSERT_TRUE(join_result.is_ok());
    // For now just check that join succeeds
}

TEST(Thread, BuilderWithName) {
    auto result = thread::Builder::make().name("test-thread").spawn([] {
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

    EXPECT_GT(id.as_u64(), 0);
}

TEST(Thread, Sleep) {
    using namespace std::chrono;

    auto start = steady_clock::now();
    thread::sleep(duration<double>(0.1));
    auto end = steady_clock::now();

    auto elapsed = duration_cast<duration<double>>(end - start);
    EXPECT_GE(elapsed.count(), 0.1);
}

TEST(Thread, MultipleThreads) {
    using namespace std::chrono;

    // auto counter = 0;
    // {
    //     auto handle1 = thread::spawn([&counter] {
    //                        thread::sleep(duration<double>(0.05));
    //                        counter++;
    //                    }).unwrap();

    //     auto handle2 = thread::spawn([&counter] {
    //                        thread::sleep(duration<double>(0.03));
    //                        counter++;
    //                    }).unwrap();

    //     auto handle3 = thread::spawn([&counter] {
    //                        thread::sleep(duration<double>(0.01));
    //                        counter++;
    //                    }).unwrap();

    //     handle1.join().unwrap();
    //     handle2.join().unwrap();
    //     handle3.join().unwrap();
    // }

    // EXPECT_EQ(counter, 3);
}

TEST(Thread, ThreadHandleId) {
    // auto result = thread::spawn([] {
    //     return thread::current().id();
    // });

    // ASSERT_TRUE(result.is_ok());
    // auto handle      = result.unwrap();
    // auto join_result = handle.join();
    // ASSERT_TRUE(join_result.is_ok());
}

*/