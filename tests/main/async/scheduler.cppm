module;
#include <rstd/test/gtest.hpp>

export module rstd:async.scheduler_tests;
import :async.runtime_core;

using namespace rstd;

struct FifoTracked {
    int  value;
    int* drops;

    FifoTracked(int value, int& drops): value(value), drops(&drops) {}
    FifoTracked(const FifoTracked&) = delete;
    FifoTracked(FifoTracked&& other) noexcept
        : value(other.value), drops(rstd::exchange(other.drops, nullptr)) {}
    auto operator=(FifoTracked&&) -> FifoTracked& = delete;
    ~FifoTracked() {
        if (drops != nullptr) ++*drops;
    }
};

TEST(RstdAsyncScheduler, FifoMaintainsOrderAcrossCompactionAndClearsOwners) {
    WorkerFifo<FifoTracked> queue;
    int                     drops {};
    for (int i = 0; i < 256; ++i) queue.push(FifoTracked(i, drops));
    for (int i = 0; i < 8192; ++i) {
        {
            auto value = queue.pop_front();
            ASSERT_TRUE(value.is_some());
            EXPECT_EQ(value->value, i);
        }
        EXPECT_EQ(drops, i + 1);
        queue.push(FifoTracked(i + 256, drops));
    }
    for (int i = 8192; i < 8448; ++i) {
        auto value = queue.pop_front();
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(value->value, i);
    }
    EXPECT_TRUE(queue.is_empty());
    EXPECT_EQ(drops, 8448);
    EXPECT_TRUE(queue.pop_front().is_none());
    queue.push(FifoTracked(1, drops));
    queue.push(FifoTracked(2, drops));
    queue.clear();
    EXPECT_TRUE(queue.is_empty());
    EXPECT_EQ(drops, 8450);
}

TEST(RstdAsyncScheduler, InboxPreservesStopAndScheduleOrder) {
    WorkerInbox inbox;
    auto        ticket = [](u64 generation) {
        return ScheduleTicket(TaskRef {}, RuntimeWorkerId {}, generation);
    };
    for (rstd::uint64_t i = 0; i < 128; ++i)
        inbox.push(WorkerCommand::schedule(ticket(u64(i + 1))));
    inbox.push(WorkerCommand::stop());
    inbox.push(WorkerCommand::schedule(ticket(u64(129))));
    for (rstd::uint64_t i = 0; i < 128; ++i) {
        auto command = inbox.pop_front();
        ASSERT_TRUE(command.is_some());
        EXPECT_EQ(command->kind(), WorkerCommandKind::Schedule);
        EXPECT_EQ(command->take_ticket().generation(), u64(i + 1));
    }
    auto stop = inbox.pop_front();
    ASSERT_TRUE(stop.is_some());
    EXPECT_EQ(stop->kind(), WorkerCommandKind::Stop);
    auto last = inbox.pop_front();
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ(last->take_ticket().generation(), u64(129));
    EXPECT_TRUE(inbox.is_empty());
}
