#include <rstd/test/gtest.hpp>
#include <thread>
#include <vector>
import rstd;

using namespace rstd;

TEST(Mutex, LockProvidesMutableAccess) {
    auto mutex = sync::Mutex<i32> { i32(1) };
    {
        auto guard = mutex.lock().unwrap();
        *guard += i32(1);
    }
    EXPECT_EQ(*mutex.lock().unwrap(), i32(2));
}

TEST(Mutex, SerializesConcurrentMutation) {
    auto mutex = sync::Mutex<i32> { i32() };
    auto work  = [&mutex] {
        for (int i = 0; i < 10'000; ++i) {
            auto guard = mutex.lock().unwrap();
            *guard += i32(1);
        }
    };

    auto threads = std::vector<std::thread> {};
    for (int i = 0; i < 4; ++i) threads.emplace_back(work);
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(*mutex.lock().unwrap(), i32(40'000));
}
