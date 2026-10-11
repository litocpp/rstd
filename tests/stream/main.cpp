#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
namespace stream = rstd::stream;
namespace future = rstd::future;
using rstd::task::Context;
using rstd::task::Poll;

struct CountWakes {
    int         count {};
    static void wake(void* ptr) { ++static_cast<CountWakes*>(ptr)->count; }
    static void drop(void*) {}
    static auto clone(void* ptr) -> rstd::task::RawWaker;
    static const rstd::task::RawWakerVTable vtable;
    auto waker() -> rstd::task::Waker { return rstd::task::Waker::from_raw_parts(this, &vtable); }
};
const rstd::task::RawWakerVTable CountWakes::vtable { clone, wake, wake, drop };
auto                             CountWakes::clone(void* ptr) -> rstd::task::RawWaker {
    return rstd::task::RawWaker::from_raw_parts(ptr, &vtable);
}

template<typename T>
struct ReadyValue {
    using Output = T;
    T    value;
    auto poll(mut_ref<ReadyValue> self, Context&) -> Poll<T> {
        return Poll<T>::Ready(rstd::move(self->value));
    }
};

TEST(Stream, IteratorIsLazyAndPreservesBorrowAndOrder) {
    array<int, 3> values { 2, 4, 6 };
    auto          cx       = Context(rstd::task::Waker::noop());
    int           observed = 0;
    auto source = stream::inspect(stream::iter(values.iter_mut()), [&](const mut_ref<int>& value) {
        observed += *value;
    });
    EXPECT_EQ(observed, 0);
    auto first = rstd::move(future::poll_next(source, cx)).take().unwrap();
    *first     = 9;
    EXPECT_EQ(values[usize(0)], 9);
    EXPECT_EQ(observed, 2);
    auto mapped  = stream::map(rstd::move(source), [](mut_ref<int> value) {
        return *value;
    });
    auto collect = stream::collect<Vec<int>>(rstd::move(mapped));
    auto out     = future::poll(collect, cx);
    ASSERT_TRUE(out.is_ready());
    auto items = rstd::move(out).take();
    ASSERT_TRUE(items.len() == usize(2));
    EXPECT_EQ(items[usize(0)], 4);
    EXPECT_EQ(items[usize(1)], 6);
    EXPECT_EQ(observed, 12);
}

TEST(Stream, MoveOnlyItemsAreNotCopied) {
    auto source = stream::once(Box<int>::make(17));
    auto mapped = stream::map(rstd::move(source), [](Box<int> value) {
        return value;
    });
    auto cx     = Context(rstd::task::Waker::noop());
    auto out    = rstd::move(future::poll_next(mapped, cx)).take().unwrap();
    EXPECT_EQ(*out, 17);
    EXPECT_TRUE(rstd::move(future::poll_next(mapped, cx)).take().is_none());
}

struct NonFused {
    using Item = int;
    int* polls;
    auto poll_next(mut_ref<NonFused> self, Context&) -> Poll<Option<int>> {
        ++*self->polls;
        return Poll<Option<int>>::Ready(*self->polls == 1 ? Option<int>(None()) : Some(9));
    }
};

TEST(Stream, FuseAndChainDoNotRepollEndedFirstSource) {
    int  polls  = 0;
    auto cx     = Context(rstd::task::Waker::noop());
    auto source = stream::fuse(NonFused { &polls });
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_none());
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_none());
    EXPECT_EQ(polls, 1);
    polls      = 0;
    auto chain = stream::chain(NonFused { &polls }, stream::once(7));
    EXPECT_EQ(rstd::move(future::poll_next(chain, cx)).take().unwrap(), 7);
    EXPECT_TRUE(rstd::move(future::poll_next(chain, cx)).take().is_none());
    EXPECT_EQ(polls, 1);
}

struct Gate {
    bool              ready {};
    int               calls {};
    int               drops {};
    rstd::task::Waker waker;
};
struct GatedFuture {
    using Output = int;
    Gate* gate;
    int   value;
    GatedFuture(Gate& state, int value): gate(&state), value(value) {}
    GatedFuture(const GatedFuture&) = delete;
    GatedFuture(GatedFuture&& other)
        : gate(rstd::exchange(other.gate, nullptr)), value(other.value) {}
    ~GatedFuture() {
        if (gate) ++gate->drops;
    }
    auto poll(mut_ref<GatedFuture> self, Context& cx) -> Poll<int> {
        if (! self->gate->ready) {
            self->gate->waker.clone_from(cx.waker());
            return Poll<int>::Pending();
        }
        self->gate->waker.reset();
        return Poll<int>::Ready(self->value);
    }
};

TEST(Stream, ThenRetainsOperationAcrossWaitersAndReplacesWaker) {
    Gate       gate;
    CountWakes old, current;
    auto       old_waker     = old.waker();
    auto       current_waker = current.waker();
    auto       old_cx        = Context(old_waker);
    auto       cx            = Context(current_waker);
    auto       source        = stream::then(stream::once(42), [&](int value) {
        ++gate.calls;
        return GatedFuture(gate, value);
    });
    {
        auto waiter = stream::next(source);
        EXPECT_TRUE(future::poll(waiter, old_cx).is_pending());
        EXPECT_TRUE(future::poll(waiter, old_cx).is_pending());
    }
    EXPECT_EQ(gate.calls, 1);
    EXPECT_EQ(gate.drops, 0);
    auto waiter = stream::next(source);
    EXPECT_TRUE(future::poll(waiter, cx).is_pending());
    gate.ready = true;
    gate.waker.wake_by_ref();
    EXPECT_EQ(old.count, 0);
    EXPECT_EQ(current.count, 1);
    EXPECT_EQ(rstd::move(future::poll(waiter, cx)).take().unwrap(), 42);
    EXPECT_EQ(gate.calls, 1);
    EXPECT_EQ(gate.drops, 1);
}

TEST(Stream, DestroyingThenDropsPendingOperation) {
    Gate gate;
    auto cx = Context(rstd::task::Waker::noop());
    {
        auto source = stream::then(stream::once(1), [&](int value) {
            return GatedFuture(gate, value);
        });
        EXPECT_TRUE(future::poll_next(source, cx).is_pending());
    }
    EXPECT_EQ(gate.drops, 1);
}

TEST(Stream, PendingPropagatesWithoutCallbacksOrAdvancingChain) {
    Gate gate;
    int  mapped = 0, inspected = 0, second = 0;
    auto first = stream::then(stream::once(3), [&](int value) {
        return GatedFuture(gate, value);
    });
    auto tail  = stream::inspect(stream::once(9), [&](const int&) {
        ++second;
    });
    auto source =
        stream::take(stream::inspect(stream::map(stream::chain(rstd::move(first), rstd::move(tail)),
                                                 [&](int value) {
                                                     ++mapped;
                                                     return value + 1;
                                                 }),
                                     [&](const int&) {
                                         ++inspected;
                                     }),
                     usize(1));
    auto cx = Context(rstd::task::Waker::noop());
    EXPECT_TRUE(future::poll_next(source, cx).is_pending());
    EXPECT_EQ(mapped, 0);
    EXPECT_EQ(inspected, 0);
    EXPECT_EQ(second, 0);
    gate.ready = true;
    EXPECT_EQ(rstd::move(future::poll_next(source, cx)).take().unwrap(), 4);
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_none());
    EXPECT_EQ(mapped, 1);
    EXPECT_EQ(inspected, 1);
    EXPECT_EQ(second, 0);
}

TEST(Stream, TakeDoesNotConsumeExtraItems) {
    int  calls  = 0;
    auto source = stream::inspect(stream::chain(stream::once(1), stream::once(2)), [&](const int&) {
        ++calls;
    });
    auto cx     = Context(rstd::task::Waker::noop());
    auto limited = stream::take(stream::by_ref(source), usize(1));
    EXPECT_EQ(rstd::move(future::poll_next(limited, cx)).take().unwrap(), 1);
    EXPECT_TRUE(rstd::move(future::poll_next(limited, cx)).take().is_none());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(rstd::move(future::poll_next(source, cx)).take().unwrap(), 2);
}

TEST(Stream, TryCollectStopsAtFirstErrorAndGenericMapDoesNot) {
    using Item = Result<int, int>;
    auto source =
        stream::chain(stream::once(Item(Ok(1))),
                      stream::chain(stream::once(Item(Err(9))), stream::once(Item(Ok(2)))));
    auto cx      = Context(rstd::task::Waker::noop());
    auto collect = stream::try_collect<Vec<int>>(stream::by_ref(source));
    auto out     = rstd::move(future::poll(collect, cx)).take();
    ASSERT_TRUE(out.is_err());
    EXPECT_EQ(rstd::move(out).unwrap_err(), 9);
    EXPECT_EQ(rstd::move(future::poll_next(source, cx)).take().unwrap().unwrap(), 2);
    auto errors = stream::map(stream::chain(stream::once(Item(Err(5))), stream::once(Item(Ok(6)))),
                              [](Item item) {
                                  return item;
                              });
    EXPECT_TRUE(rstd::move(future::poll_next(errors, cx)).take().unwrap().is_err());
    EXPECT_EQ(rstd::move(future::poll_next(errors, cx)).take().unwrap().unwrap(), 6);
}

TEST(Stream, TryForEachStopsOnCallbackError) {
    using Item   = Result<int, int>;
    int  calls   = 0;
    auto source  = stream::chain(stream::once(Item(Ok(1))), stream::once(Item(Ok(2))));
    auto consume = stream::try_for_each(stream::by_ref(source), [&](int) {
        ++calls;
        return ReadyValue<Result<empty, int>> { Err(8) };
    });
    auto cx      = Context(rstd::task::Waker::noop());
    auto out     = rstd::move(future::poll(consume, cx)).take();
    EXPECT_TRUE(out.is_err());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(rstd::move(future::poll_next(source, cx)).take().unwrap().unwrap(), 2);
}

struct GatedSink {
    using Output = void;
    Gate* gate;
    auto  poll(mut_ref<GatedSink> self, Context& cx) -> Poll<void> {
        if (! self->gate->ready) {
            self->gate->waker.clone_from(cx.waker());
            return Poll<void>::Pending();
        }
        return Poll<void>::Ready();
    }
};

TEST(Stream, ForEachDoesNotReadAheadWhileCallbackIsPending) {
    Gate gate;
    int  pulled = 0;
    auto source = stream::inspect(stream::chain(stream::once(1), stream::once(2)), [&](const int&) {
        ++pulled;
    });
    auto consume = stream::for_each(rstd::move(source), [&](int) {
        ++gate.calls;
        return GatedSink { &gate };
    });
    auto cx      = Context(rstd::task::Waker::noop());
    EXPECT_TRUE(future::poll(consume, cx).is_pending());
    EXPECT_TRUE(future::poll(consume, cx).is_pending());
    EXPECT_EQ(pulled, 1);
    EXPECT_EQ(gate.calls, 1);
    gate.ready = true;
    EXPECT_TRUE(future::poll(consume, cx).is_ready());
    EXPECT_EQ(pulled, 2);
    EXPECT_EQ(gate.calls, 2);
}

TEST(Stream, TryForEachPropagatesSourceErrorWithoutInvokingCallback) {
    int  calls   = 0;
    auto consume = stream::try_for_each(stream::once(Result<int, int>(Err(4))), [&](int) {
        ++calls;
        return ReadyValue<Result<empty, int>> { Ok(empty {}) };
    });
    auto cx      = Context(rstd::task::Waker::noop());
    auto out     = rstd::move(future::poll(consume, cx)).take();
    EXPECT_EQ(rstd::move(out).unwrap_err(), 4);
    EXPECT_EQ(calls, 0);
}

TEST(Stream, TryConsumersCompleteSuccessfully) {
    auto cx      = Context(rstd::task::Waker::noop());
    auto collect = stream::try_collect<Vec<int>>(stream::once(Result<int, int>(Ok(2))));
    auto values  = rstd::move(future::poll(collect, cx)).take().unwrap();
    ASSERT_TRUE(values.len() == usize(1));
    EXPECT_EQ(values[usize(0)], 2);
    int  total   = 0;
    auto consume = stream::try_for_each(stream::once(Result<int, int>(Ok(5))), [&](int value) {
        total += value;
        return ReadyValue<Result<empty, int>> { Ok(empty {}) };
    });
    EXPECT_TRUE(rstd::move(future::poll(consume, cx)).take().is_ok());
    EXPECT_EQ(total, 5);
}

struct DropItem {
    int* drops;
    explicit DropItem(int& count): drops(&count) {}
    DropItem(const DropItem&) = delete;
    DropItem(DropItem&& other): drops(rstd::exchange(other.drops, nullptr)) {}
    ~DropItem() {
        if (drops) ++*drops;
    }
};

TEST(Stream, CollectDropsPartialCollectionOnError) {
    int drops  = 0;
    using Item = Result<DropItem, int>;
    auto source =
        stream::chain(stream::once(Item(Ok(DropItem(drops)))), stream::once(Item(Err(8))));
    auto collect = stream::try_collect<Vec<DropItem>>(rstd::move(source));
    auto cx      = Context(rstd::task::Waker::noop());
    EXPECT_TRUE(rstd::move(future::poll(collect, cx)).take().is_err());
    EXPECT_EQ(drops, 1);
}

TEST(Stream, EmptyDataItemIsNotEndOfStream) {
    auto source = stream::chain(stream::once(Vec<u8> {}), stream::once(Vec<u8> {}));
    auto cx     = Context(rstd::task::Waker::noop());
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_some());
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_some());
    EXPECT_TRUE(rstd::move(future::poll_next(source, cx)).take().is_none());
    auto collect = stream::collect<Vec<int>>(stream::empty<int>());
    EXPECT_TRUE(rstd::move(future::poll(collect, cx)).take().is_empty());
}

TEST(Stream, CollectYieldsDuringReadySourcesAndPreservesAllValues) {
    CountWakes wakes;
    auto       waker   = wakes.waker();
    auto       cx      = Context(waker);
    auto       source  = stream::iter(rstd::iter::range(usize(0), usize(200)));
    auto       collect = stream::collect<Vec<usize>>(rstd::move(source));
    auto       out     = future::poll(collect, cx);
    EXPECT_TRUE(out.is_pending());
    EXPECT_GT(wakes.count, 0);
    while (out.is_pending()) out = future::poll(collect, cx);
    auto items = rstd::move(out).take();
    EXPECT_TRUE(items.len() == usize(200));
    EXPECT_TRUE(items[usize(199)] == usize(199));
}

auto main() -> int {
    return rstd::test::run_registered().to_primitive();
}
