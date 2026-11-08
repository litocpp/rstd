module;
#include <rstd/test/gtest.hpp>

export module rstd:async.frame_scope_tests;
import :async.frame_allocator;
import :async.task;
import :async.coro_future;
import :async.executor;
import :alloc.shared_tracking;
import :thread;
import rstd.alloc;

using namespace rstd;
using rstd::alloc::Allocation;
using rstd::alloc::AllocError;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;

struct FrameLayoutRecord {
    void*  pointer {};
    Layout requested {};
    Layout returned {};
    int    allocations {};
    int    deallocations {};
    bool   matched_pointer {};
    bool   reject_allocation {};
};

struct FrameLayoutAllocator {
    FrameLayoutRecord* record;
};

namespace rstd
{
template<>
struct Impl<alloc::TransferableAllocator, ::FrameLayoutAllocator> {};

template<>
struct Impl<alloc::Allocator, ::FrameLayoutAllocator>
    : DefaultInImpl<alloc::Allocator, ::FrameLayoutAllocator> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        if (self().record->reject_allocation) rstd::panic { "unexpected frame allocator call" };
        auto allocation = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (allocation.is_ok()) {
            auto& record     = *self().record;
            record.pointer   = allocation->pointer;
            record.requested = layout;
            ++record.allocations;
        }
        return allocation;
    }

    void deallocate(void* pointer, Layout layout) const noexcept {
        auto& record           = *self().record;
        record.matched_pointer = pointer == record.pointer;
        record.returned        = layout;
        ++record.deallocations;
        if (record.matched_pointer)
            as<Allocator>(::alloc::GLOBAL).deallocate(pointer, record.requested);
    }
};
} // namespace rstd

namespace
{

[[clang::noinline]]
auto unstarted_frame() -> async::coro<int> {
    co_return 17;
}

[[clang::noinline]]
auto frame_for_layout_contract() -> async::coro<void> {
    co_return;
}

TEST(RstdAsyncFrameAllocator, DeallocatesFrameWithOriginalAddressAndLayout) {
    auto record  = FrameLayoutRecord {};
    auto tracker = rstd::alloc::SharedTrackingAllocator<FrameLayoutAllocator>::make(
        FrameLayoutAllocator { &record });
    auto owner = rstd::alloc::SharedAllocator::make(rstd::move(tracker));
    {
        auto scope = async::FrameAllocatorScope(&owner);
        auto frame = frame_for_layout_contract();
        ASSERT_EQ(record.allocations, 1);
        ASSERT_NE(record.pointer, nullptr);
    }
    EXPECT_EQ(record.deallocations, 1);
    EXPECT_TRUE(record.matched_pointer);
    EXPECT_EQ(record.returned.size, record.requested.size);
    EXPECT_EQ(record.returned.align, record.requested.align);
}

TEST(RstdAsyncFrameAllocator, RejectsOverflowBeforeCallingAllocator) {
    auto record              = FrameLayoutRecord {};
    record.reject_allocation = true;
    auto tracker             = rstd::alloc::SharedTrackingAllocator<FrameLayoutAllocator>::make(
        FrameLayoutAllocator { &record });
    auto owner = rstd::alloc::SharedAllocator::make(rstd::move(tracker));
    auto scope = async::FrameAllocatorScope(&owner);
    EXPECT_DEATH((void)async::allocate_coro_frame(static_cast<rstd::size_t>(-1)),
                 "invalid coroutine frame layout");
}

TEST(RstdAsyncFrameAllocator, ZeroSizedAllocationPreservesLayoutForSmallAlignments) {
    for (rstd::size_t alignment = 1; alignment <= 4; alignment *= 2) {
        auto record  = FrameLayoutRecord {};
        auto tracker = rstd::alloc::SharedTrackingAllocator<FrameLayoutAllocator>::make(
            FrameLayoutAllocator { &record });
        auto owner   = rstd::alloc::SharedAllocator::make(rstd::move(tracker));
        auto scope   = async::FrameAllocatorScope(&owner);
        auto pointer = async::allocate_coro_frame(0, static_cast<rstd::align_val_t>(alignment));
        ASSERT_NE(pointer, nullptr);
        EXPECT_EQ(record.allocations, 1);
        EXPECT_GE(record.requested.align, usize(alignment));
        EXPECT_GE(record.requested.align, usize(alignof(void*)));
        EXPECT_GE(record.requested.align, usize(alignof(Layout)));
        async::deallocate_coro_frame(pointer, 0);
        EXPECT_EQ(record.deallocations, 1);
        EXPECT_TRUE(record.matched_pointer);
        EXPECT_EQ(record.returned.size, record.requested.size);
        EXPECT_EQ(record.returned.align, record.requested.align);
    }
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
