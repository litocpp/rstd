#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct FrameProbe {
    int* drops;

    explicit FrameProbe(int& drops): drops(rstd::addressof(drops)) {}
    FrameProbe(const FrameProbe&)            = delete;
    FrameProbe& operator=(const FrameProbe&) = delete;

    FrameProbe(FrameProbe&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}

    ~FrameProbe() {
        if (drops != nullptr) {
            ++*drops;
        }
    }
};

struct PendingFrameFuture {
    using Output = void;

    FrameProbe         probe;
    std::atomic<bool>* polled;

    auto poll(mut_ref<PendingFrameFuture> self, task::Context&) -> task::Poll<void> {
        self->polled->store(true, std::memory_order_release);
        return task::Poll<void>::Pending();
    }
};

static_assert(sizeof(async::coro<int>) == sizeof(std::coroutine_handle<>));
static_assert(sizeof(async::coro<void>) == sizeof(std::coroutine_handle<>));
static_assert(alignof(async::coro<int>) == alignof(std::coroutine_handle<>));

auto tracked_leaf(FrameProbe probe) -> async::coro<int> {
    (void)probe;
    co_await async::yield_now();
    co_return 37;
}

auto tracked_parent(FrameProbe probe) -> async::coro<int> {
    co_return co_await tracked_leaf(rstd::move(probe));
}

TEST(RstdAsyncFrame, PublicCoroStoresOnlyCompilerHandle) {
    EXPECT_EQ(sizeof(async::coro<int>), sizeof(std::coroutine_handle<>));
    EXPECT_EQ(sizeof(async::coro<void>), sizeof(std::coroutine_handle<>));
}

TEST(RstdAsyncFrame, MoveTransfersSoleFrameOwnership) {
    int  drops    = 0;
    auto original = tracked_leaf(FrameProbe { drops });
    EXPECT_EQ(drops, 0);

    auto moved = rstd::move(original);
    EXPECT_EQ(async::block_on(rstd::move(moved)), 37);
    EXPECT_EQ(drops, 1);
}

TEST(RstdAsyncFrame, NestedChildFrameDestroysOwnedStateOnce) {
    int drops = 0;

    EXPECT_EQ(async::block_on(tracked_parent(FrameProbe { drops })), 37);
    EXPECT_EQ(drops, 1);
}

TEST(RstdAsyncFrame, ShutdownDestroysPendingFutureFrameOnce) {
    int  drops  = 0;
    auto polled = std::atomic<bool> { false };
    auto joined = [&] {
        auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
        auto handle  = runtime.spawn(PendingFrameFuture { FrameProbe { drops }, &polled });
        while (! polled.load(std::memory_order_acquire)) {
            hint::spin_loop();
        }
        return handle;
    }();

    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    EXPECT_EQ(drops, 1);
}

} // namespace
