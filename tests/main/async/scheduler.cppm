module;
#include <rstd/test/gtest.hpp>

export module rstd:async.scheduler_tests;
import :async.runtime_core;
import :async.poll;

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

TEST(RstdAsyncScheduler, TaskRegistryPreservesOrderAfterRemovalAndCompaction) {
    TaskRegistry registry;
    auto         held = Vec<TaskRef>::make();
    for (int i = 0; i < 128; ++i) {
        auto task = TaskRef::adopt(TaskRefControl::make_scoped());
        registry.insert(task.clone());
        held.push(rstd::move(task));
    }

    auto old = held[usize(0)].clone();
    for (int i = 0; i < 64; ++i) registry.remove(held[usize(i)].identity());
    for (int i = 0; i < 20; ++i) {
        auto task = TaskRef::adopt(TaskRefControl::make_scoped());
        registry.insert(task.clone());
        held.push(rstd::move(task));
    }
    registry.remove(old.identity());
    registry.remove(old.identity());

    auto remaining = registry.clone_all();
    ASSERT_EQ(remaining.len(), usize(84));
    for (int i = 0; i < 84; ++i)
        EXPECT_TRUE(remaining[usize(i)].identity() == held[usize(i + 64)].identity());
    for (int i = 64; i < 148; ++i) {
        registry.remove(held[usize(i)].identity());
        registry.remove(held[usize(i)].identity());
    }
    EXPECT_TRUE(registry.is_empty());
    EXPECT_TRUE(registry.clone_all().is_empty());
}

struct RegistryTracked {
    TaskRefControl control;
    int*           drops;

    explicit RegistryTracked(int& drops): control(this, &destroy), drops(&drops) {}
    static void destroy(voidp owner) { ++*static_cast<RegistryTracked*>(owner)->drops; }
};

TEST(RstdAsyncScheduler, TaskRegistryRetainsControlUntilLastReference) {
    int             drops {};
    RegistryTracked owner(drops);
    TaskRegistry    registry;
    auto            task = TaskRef::adopt(&owner.control);
    auto            held = task.clone();
    registry.insert(task.clone());
    task = TaskRef {};
    EXPECT_EQ(drops, 0);
    registry.remove(&owner.control);
    registry.remove(&owner.control);
    EXPECT_EQ(drops, 0);
    held = TaskRef {};
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(registry.is_empty());
}

TEST(RstdAsyncScheduler, TaskRegistryRejectsDuplicateRegistration) {
    TaskRegistry first;
    TaskRegistry second;
    auto         task = TaskRef::adopt(TaskRefControl::make_scoped());
    first.insert(task.clone());
    EXPECT_DEATH(first.insert(task.clone()), "TaskRefControl already registered");
    EXPECT_DEATH(second.insert(task.clone()), "TaskRefControl already registered");
    first.remove(task.identity());
    EXPECT_DEATH(second.insert(task.clone()), "TaskRefControl already registered");
    EXPECT_TRUE(first.is_empty());
    EXPECT_TRUE(second.is_empty());
}

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

TEST(RstdAsyncScheduler, PendingAndInstalledPollReceiveInboxWake) {
    auto worker = WorkerHandle::make(RuntimeWorkerId {});
    worker.start_current();
    auto submit = [&worker](u64 generation) {
        return worker.schedule(ScheduleTicket(TaskRef {}, RuntimeWorkerId {}, generation));
    };
    ASSERT_TRUE(submit(u64(1)).is_ok());

    auto initialized = async::Poll::init();
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap_unchecked();
    ASSERT_TRUE(worker.install_poll(rstd::move(poll.wake), async::Poll::capabilities(poll.state)));
    for (u64 generation(1); generation <= u64(2); ++generation) {
        auto events = async::Poll::poll(poll.state, async::PollTimeout::Immediate);
        ASSERT_TRUE(events.is_ok());
        bool notified = false;
        while (auto event = events->pop_front())
            notified = notified || event->kind() == async::PollEventKind::Wake;
        EXPECT_TRUE(notified);
        auto command = worker.pop_command();
        ASSERT_TRUE(command.is_some());
        EXPECT_EQ(command->take_ticket().generation(), generation);
        EXPECT_TRUE(worker.pop_command().is_none());
        if (generation == u64(1)) ASSERT_TRUE(submit(u64(2)).is_ok());
    }
    worker.clear_poll();
    (void)async::Poll::shutdown(poll.state);
}
