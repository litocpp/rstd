#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct ReadyValue {
    using Output = int;

    auto poll(mut_ref<ReadyValue>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(13);
    }
};

struct PendingValue {
    using Output = void;

    std::atomic<bool>* polled;

    auto poll(mut_ref<PendingValue> self, task::Context&) -> task::Poll<void> {
        self->polled->store(true, std::memory_order_release);
        return task::Poll<void>::Pending();
    }
};

auto reenter_runtime(async::Runtime& runtime) -> async::coro<void> {
    runtime.block_on(async::yield_now());
    co_return;
}

TEST(RstdAsyncLifecycle, BuilderRejectsZeroWorkerThreadPool) {
    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(0);

    auto runtime = builder.build();
    ASSERT_TRUE(runtime.is_err());
    EXPECT_EQ(rstd::move(runtime).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
}

TEST(RstdAsyncLifecycle, BuilderPublishesConfiguredDriverCapabilities) {
    auto none = async::RuntimeBuilder::current_thread().build().unwrap();
    EXPECT_FALSE(none.io_enabled());
    EXPECT_FALSE(none.time_enabled());

    auto io_only = async::RuntimeBuilder::current_thread().enable_io().build().unwrap();
    EXPECT_TRUE(io_only.io_enabled());
    EXPECT_FALSE(io_only.time_enabled());

    auto time_only = async::RuntimeBuilder::current_thread().enable_time().build().unwrap();
    EXPECT_FALSE(time_only.io_enabled());
    EXPECT_TRUE(time_only.time_enabled());

    auto all = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_TRUE(all.io_enabled());
    EXPECT_TRUE(all.time_enabled());
}

TEST(RstdAsyncLifecycle, MoveAssignmentStopsPreviousRuntimeBeforeReplacement) {
    auto polled  = std::atomic<bool> { false };
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto pending = runtime.spawn(PendingValue { &polled });
    while (! polled.load(std::memory_order_acquire)) {
        hint::spin_loop();
    }

    auto replacement = async::RuntimeBuilder::current_thread().build().unwrap();
    runtime          = rstd::move(replacement);

    EXPECT_TRUE(pending.is_finished());
    auto joined = runtime.block_on(rstd::move(pending));
    ASSERT_TRUE(joined.is_err());
    EXPECT_TRUE(rstd::move(joined).unwrap_err().is_aborted());
    EXPECT_EQ(runtime.block_on(ReadyValue {}), 13);
}

TEST(RstdAsyncLifecycle, ExpiredRuntimeHandleRejectsAdmission) {
    EXPECT_DEATH(
        {
            auto handle = [] {
                auto runtime =
                    async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
                return runtime.handle();
            }();
            (void)handle.spawn(ReadyValue {});
        },
        "");
}

TEST(RstdAsyncLifecycle, CurrentThreadRuntimeRejectsReentrantBlockOn) {
    EXPECT_DEATH(
        {
            auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
            runtime.block_on(reenter_runtime(runtime));
        },
        "");
}

TEST(RstdAsyncLifecycle, CurrentThreadRuntimeRejectsDifferentThread) {
    EXPECT_DEATH(
        {
            auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
            runtime.block_on(async::yield_now());
            auto other = thread::spawn([&runtime] {
                             runtime.block_on(async::yield_now());
                         }).unwrap();
            (void)rstd::move(other).join();
        },
        "");
}

} // namespace
