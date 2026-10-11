#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace stream = rstd::stream;
namespace future = rstd::future;
namespace async  = rstd::async;
using rstd::task::Poll;
using rstd::task::Context;
using rstd::time::Duration;

struct DelayedItem {
    using Output = int;
    async::Sleep delay;
    int          value;
    auto         poll(mut_ref<DelayedItem> self, Context& cx) -> Poll<int> {
        if (future::poll(self->delay, cx).is_pending()) return Poll<int>::Pending();
        return Poll<int>::Ready(self->value);
    }
};

auto timeout_and_retry() -> coro<empty> {
    int  started = 0;
    auto source  = stream::then(stream::once(13), [&](int value) {
        ++started;
        return DelayedItem { async::Sleep(Duration::from_millis(u64(30))), value };
    });
    auto first   = co_await async::timeout(stream::next(source), Duration {});
    EXPECT_TRUE(first.is_err());
    EXPECT_EQ(started, 1);
    auto second = co_await async::timeout(stream::next(source), Duration::from_secs(u64(2)));
    EXPECT_TRUE(second.is_ok());
    if (second.is_err()) co_return empty {};
    EXPECT_EQ(rstd::move(second).unwrap().unwrap(), 13);
    EXPECT_EQ(started, 1);
    EXPECT_TRUE((co_await stream::next(source)).is_none());
    co_return empty {};
}
TEST(StreamRuntime, TimeoutRetainsPendingFuture) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    runtime.block_on(timeout_and_retry());
}

auto complete_later(async::CompletionHandle<int> sender) -> coro<empty> {
    co_await async::sleep(Duration::from_millis(u64(5)));
    EXPECT_TRUE(sender.complete(23).is_ok());
    co_return empty {};
}

auto consume_completion(async::RuntimeHandle runtime) -> coro<empty> {
    auto pair = async::Completion<int>::make().unwrap();
    auto source =
        stream::then(stream::once(rstd::move(pair.get<0>())), [](async::Completion<int> receiver) {
            return receiver;
        });
    auto sender = runtime.spawn(complete_later(rstd::move(pair.get<1>())));
    {
        auto first = co_await async::timeout(stream::next(source), Duration {});
        EXPECT_TRUE(first.is_err());
    }
    auto result = co_await stream::next(source);
    EXPECT_TRUE(result.is_some());
    if (result.is_none()) co_return empty {};
    EXPECT_EQ(rstd::move(result).unwrap().unwrap(), 23);
    EXPECT_TRUE((co_await rstd::move(sender)).is_ok());
    co_return empty {};
}
TEST(StreamRuntime, CompletionWorksThroughPollAndNativeAwaitPaths) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    runtime.block_on(consume_completion(runtime.handle()));
    auto pair = async::Completion<int>::make().unwrap();
    EXPECT_TRUE(pair.get<1>().complete(7).is_ok());
    EXPECT_EQ(runtime.block_on(rstd::move(pair.get<0>())).unwrap(), 7);
}

TEST(StreamRuntime, DroppingStreamClosesCompletionReceiver) {
    auto pair = async::Completion<int>::make().unwrap();
    {
        auto source = stream::then(stream::once(rstd::move(pair.get<0>())),
                                   [](async::Completion<int> receiver) {
                                       return receiver;
                                   });
        auto cx     = Context(rstd::task::Waker::noop());
        EXPECT_TRUE(future::poll_next(source, cx).is_pending());
    }
    EXPECT_TRUE(pair.get<1>().complete(1).is_err());
}

struct ObserveProgress {
    using Output = empty;
    int* progress;
    auto poll(mut_ref<ObserveProgress> self, Context&) -> Poll<empty> {
        EXPECT_GT(*self->progress, 0);
        EXPECT_LT(*self->progress, 1000);
        return Poll<empty>::Ready(empty {});
    }
};
TEST(StreamRuntime, ReadyConsumerYieldsToJoinedFuture) {
    int  progress = 0;
    auto source =
        stream::inspect(stream::iter(rstd::iter::range(usize(0), usize(1000))), [&](const usize&) {
            ++progress;
        });
    auto collect = stream::collect<Vec<usize>>(rstd::move(source));
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result = runtime.block_on(async::join(rstd::move(collect), ObserveProgress { &progress }));
    EXPECT_TRUE(result.get<0>().len() == usize(1000));
    EXPECT_EQ(progress, 1000);
}
