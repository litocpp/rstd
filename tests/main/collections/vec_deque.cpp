#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

struct QueueOwned {
    i32  key;
    int* alive;
    QueueOwned(i32 value, int& count): key(value), alive(&count) { ++*alive; }
    QueueOwned(QueueOwned&& other): key(other.key), alive(rstd::exchange(other.alive, nullptr)) {}
    QueueOwned(const QueueOwned&)               = delete;
    auto operator=(QueueOwned&&) -> QueueOwned& = delete;
    ~QueueOwned() {
        if (alive) --*alive;
    }
};

TEST(VecDeque, AlternatingAndMoveOnly) {
    int alive = 0;
    {
        ::alloc::collections::VecDeque<QueueOwned> queue;
        for (int i = 0; i < 20; ++i) queue.push_back(QueueOwned(i32(i), alive));
        for (int i = 0; i < 10; ++i) EXPECT_EQ(queue.pop_front()->key, i32(i));
        for (int i = 20; i < 30; ++i) queue.push_back(QueueOwned(i32(i), alive));
        for (int i = 10; i < 30; ++i) EXPECT_EQ(queue.pop_front()->key, i32(i));
        EXPECT_TRUE(queue.is_empty());
    }
    EXPECT_EQ(alive, 0);
}

TEST(VecDeque, EarlyDropReleasesBothStacks) {
    int alive = 0;
    {
        ::alloc::collections::VecDeque<QueueOwned> queue;
        for (int i = 0; i < 4; ++i) queue.push_back(QueueOwned(i32(i), alive));
        EXPECT_EQ(queue.pop_front()->key, 0_i32);
        queue.push_back(QueueOwned(4_i32, alive));
        EXPECT_EQ(alive, 4);
    }
    EXPECT_EQ(alive, 0);
}
