module;
#include <rstd/test/gtest.hpp>

export module rstd:async.frame_scope_tests;
import :async.frame_allocator;
import :async.task;
import :async.coro_future;
import :async.executor;
import :alloc.shared_tracking;
import :thread;

using namespace rstd;

namespace
{

[[clang::noinline]]
auto unstarted_frame() -> async::coro<int> {
    co_return 17;
}

[[clang::noinline]]
auto external_then_create(async::AnyExecutor                      executor,
                          rstd::alloc::SharedTrackingAllocator<>& tracker) -> async::coro<bool> {
    if (! co_await rstd::move(executor)) co_return false;
    auto                       before = tracker.snapshot().allocations;
    auto                       child  = unstarted_frame();
    auto                       after  = tracker.snapshot().allocations;
    auto                       value  = co_await rstd::move(child);
    co_return after > before&& value == 17;
}

TEST(RstdAsyncFrameAllocator, NestedScopesRestoreAllocatorAndGlobalFallback) {
    auto tracker_a   = rstd::alloc::SharedTrackingAllocator<>::make();
    auto tracker_b   = rstd::alloc::SharedTrackingAllocator<>::make();
    auto allocator_a = rstd::alloc::SharedAllocator::make(tracker_a.clone());
    auto allocator_b = rstd::alloc::SharedAllocator::make(tracker_b.clone());
    EXPECT_FALSE(async::has_frame_allocator_scope());
    {
        auto scope_a = async::FrameAllocatorScope(&allocator_a);
        auto a       = unstarted_frame();
        EXPECT_EQ(tracker_a.snapshot().allocations, u64(1));
        EXPECT_EQ(tracker_b.snapshot().allocations, u64());
        {
            auto scope_b = async::FrameAllocatorScope(&allocator_b);
            auto b       = unstarted_frame();
            EXPECT_EQ(tracker_a.snapshot().allocations, u64(1));
            EXPECT_EQ(tracker_b.snapshot().allocations, u64(1));
            {
                auto scope_global = async::FrameAllocatorScope(nullptr);
                auto global       = unstarted_frame();
                EXPECT_EQ(tracker_a.snapshot().allocations, u64(1));
                EXPECT_EQ(tracker_b.snapshot().allocations, u64(1));
            }
        }
        auto another_a = unstarted_frame();
        EXPECT_EQ(tracker_a.snapshot().allocations, u64(2));
        EXPECT_EQ(tracker_b.snapshot().allocations, u64(1));
    }
    EXPECT_FALSE(async::has_frame_allocator_scope());
    EXPECT_EQ(tracker_a.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker_b.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrameAllocator, UnstartedFrameOwnsAllocatorAcrossThreads) {
    using Tracker = rstd::alloc::SharedTrackingAllocator<>;
    struct Escaped {
        async::coro<int>  frame;
        Tracker::Observer observer;
    };
    auto escaped = []() -> Escaped {
        auto tracker   = Tracker::make();
        auto observer  = tracker.downgrade();
        auto allocator = rstd::alloc::SharedAllocator::make(rstd::move(tracker));
        auto scope     = async::FrameAllocatorScope(&allocator);
        return Escaped { unstarted_frame(), rstd::move(observer) };
    }();

    ASSERT_FALSE(escaped.observer.expired());
    ASSERT_TRUE(escaped.observer.snapshot().is_some());
    EXPECT_EQ(escaped.observer.snapshot()->allocations, u64(1));
    auto worker = thread::spawn([frame = rstd::move(escaped.frame)]() mutable {
                      auto local = rstd::move(frame);
                  }).unwrap();
    rstd::move(worker).join().unwrap();
    EXPECT_TRUE(escaped.observer.expired());
}

TEST(RstdAsyncFrameAllocator, ExternalFutureRestoresCapturedAllocator) {
    auto tracker_a   = rstd::alloc::SharedTrackingAllocator<>::make();
    auto tracker_b   = rstd::alloc::SharedTrackingAllocator<>::make();
    auto allocator_a = rstd::alloc::SharedAllocator::make(tracker_a.clone());
    auto allocator_b = rstd::alloc::SharedAllocator::make(tracker_b.clone());
    auto context     = async::LocalExecutorContext::make();
    auto executor    = async::AnyExecutor::from_executor(context.executor());
    auto adapted     = [&] {
        auto scope = async::FrameAllocatorScope(&allocator_a);
        return external_then_create(executor.clone(), tracker_a).into_future();
    }();
    auto cx = task::Context { task::Waker::noop() };
    ASSERT_TRUE(future::poll(adapted, cx).is_pending());
    {
        auto scope_b = async::FrameAllocatorScope(&allocator_b);
        EXPECT_EQ(context.run_ready(), usize(1));
        auto after_a = tracker_a.snapshot().allocations;
        auto b_frame = unstarted_frame();
        EXPECT_EQ(tracker_a.snapshot().allocations, after_a);
        EXPECT_EQ(tracker_b.snapshot().allocations, u64(1));
    }
    auto completed = future::poll(adapted, cx);
    ASSERT_TRUE(completed.is_ready());
    EXPECT_TRUE(rstd::move(completed).take());
    EXPECT_FALSE(async::has_frame_allocator_scope());
    EXPECT_EQ(tracker_a.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker_b.snapshot().live_bytes, u64());
}

} // namespace
