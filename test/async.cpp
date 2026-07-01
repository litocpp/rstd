#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace
{

struct ReadyInt {
    using Output = int;

    auto poll(pin::Pin<mut_ref<ReadyInt>>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(7);
    }
};

struct TwoPollInt {
    using Output = int;

    int* polls;

    auto poll(pin::Pin<mut_ref<TwoPollInt>>, task::Context& cx) -> task::Poll<int> {
        ++*polls;
        if (*polls == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(41);
    }
};

struct CountReadyInt {
    using Output = int;

    int* polls;
    int  value;

    auto poll(pin::Pin<mut_ref<CountReadyInt>>, task::Context&) -> task::Poll<int> {
        ++*polls;
        return task::Poll<int>::Ready(value);
    }
};

struct CountPendingInt {
    using Output = int;

    int* polls;
    int  value;

    auto poll(pin::Pin<mut_ref<CountPendingInt>>, task::Context& cx) -> task::Poll<int> {
        ++*polls;
        if (*polls == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(value);
    }
};

struct IndexedPendingInt {
    using Output = int;

    int* polls;
    int  index;
    int  value;

    auto poll(pin::Pin<mut_ref<IndexedPendingInt>>, task::Context& cx) -> task::Poll<int> {
        ++polls[index];
        if (polls[index] == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(value);
    }
};

struct DropPendingInt {
    using Output = int;

    int* drops;

    explicit DropPendingInt(int* drops): drops(drops) {}
    DropPendingInt(const DropPendingInt&)            = delete;
    DropPendingInt& operator=(const DropPendingInt&) = delete;

    DropPendingInt(DropPendingInt&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)) {}

    auto operator=(DropPendingInt&& other) noexcept -> DropPendingInt& {
        if (this != &other) {
            drops = rstd::exchange(other.drops, nullptr);
        }
        return *this;
    }

    ~DropPendingInt() {
        if (drops != nullptr) {
            ++*drops;
        }
    }

    auto poll(pin::Pin<mut_ref<DropPendingInt>>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Pending();
    }
};

struct NeverReadyCount {
    using Output = int;

    int* polls;

    auto poll(pin::Pin<mut_ref<NeverReadyCount>>, task::Context&) -> task::Poll<int> {
        ++*polls;
        return task::Poll<int>::Pending();
    }
};

struct SavedWakeState {
    struct Fields {
        Option<task::Waker> waker;
        bool                ready { false };
        int                 polls { 0 };
    };

    sync::Mutex<Fields> fields;

    SavedWakeState(): fields(Fields {}) {}
};

struct ExternalWakeInt {
    using Output = int;

    sync::Arc<SavedWakeState> state;

    auto poll(pin::Pin<mut_ref<ExternalWakeInt>> self, task::Context& cx)
        -> task::Poll<int> {
        auto& future = *self.get_unchecked_mut();
        auto  fields = future.state->fields.lock().unwrap();

        ++fields->polls;
        if (fields->ready) {
            return task::Poll<int>::Ready(fields->polls);
        }

        fields->waker = Some(cx.waker().clone());
        return task::Poll<int>::Pending();
    }
};

struct CounterStream {
    using Item = int;

    int next { 0 };

    auto poll_next(pin::Pin<mut_ref<CounterStream>> self, task::Context&)
        -> task::Poll<Option<int>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.next >= 2) {
            return task::Poll<Option<int>>::Ready(None());
        }
        return task::Poll<Option<int>>::Ready(Some(stream.next++));
    }
};

static_assert(future::StreamLike<CounterStream>);

struct MockAsyncIo {
    const u8*       read_data;
    usize           read_len;
    usize           read_pos { 0 };
    bytes::BytesMut written { bytes::BytesMut::make() };
    bool            read_pending { true };
    bool            write_pending { true };
    bool            flushed { false };
    bool            shutdown { false };

    auto poll_read(pin::Pin<mut_ref<MockAsyncIo>> self,
                   task::Context&                 cx,
                   bytes::BytesMut&               buf) -> task::Poll<io::Result<usize>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.read_pending) {
            stream.read_pending = false;
            cx.waker().wake_by_ref();
            return task::Poll<io::Result<usize>>::Pending();
        }

        auto remaining = stream.read_len - stream.read_pos;
        buf.extend_from_slice(stream.read_data + stream.read_pos, remaining);
        stream.read_pos = stream.read_len;
        return task::Poll<io::Result<usize>>::Ready(Ok(remaining));
    }

    auto poll_write(pin::Pin<mut_ref<MockAsyncIo>> self,
                    task::Context&                 cx,
                    const bytes::Bytes&            buf) -> task::Poll<io::Result<usize>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.write_pending) {
            stream.write_pending = false;
            cx.waker().wake_by_ref();
            return task::Poll<io::Result<usize>>::Pending();
        }

        stream.written.extend_from_slice(buf.data(), buf.len());
        return task::Poll<io::Result<usize>>::Ready(Ok(buf.len()));
    }

    auto poll_flush(pin::Pin<mut_ref<MockAsyncIo>> self, task::Context&)
        -> task::Poll<io::Result<empty>> {
        auto& stream  = *self.get_unchecked_mut();
        stream.flushed = true;
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }

    auto poll_shutdown(pin::Pin<mut_ref<MockAsyncIo>> self, task::Context&)
        -> task::Poll<io::Result<empty>> {
        auto& stream   = *self.get_unchecked_mut();
        stream.shutdown = true;
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }
};

static_assert(async::io::AsyncReadLike<MockAsyncIo>);
static_assert(async::io::AsyncWriteLike<MockAsyncIo>);

struct ErrorAsyncRead {
    auto poll_read(pin::Pin<mut_ref<ErrorAsyncRead>>,
                   task::Context&,
                   bytes::BytesMut&) -> task::Poll<io::Result<usize>> {
        return task::Poll<io::Result<usize>>::Ready(
            Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput })));
    }
};

struct ZeroAsyncWrite {
    auto poll_write(pin::Pin<mut_ref<ZeroAsyncWrite>>,
                    task::Context&,
                    const bytes::Bytes&) -> task::Poll<io::Result<usize>> {
        return task::Poll<io::Result<usize>>::Ready(Ok(0u));
    }

    auto poll_flush(pin::Pin<mut_ref<ZeroAsyncWrite>>, task::Context&)
        -> task::Poll<io::Result<empty>> {
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }

    auto poll_shutdown(pin::Pin<mut_ref<ZeroAsyncWrite>>, task::Context&)
        -> task::Poll<io::Result<empty>> {
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }
};

static_assert(async::io::AsyncWriteLike<ZeroAsyncWrite>);

async::coro<int> lazy_value(int& counter) {
    ++counter;
    co_return 5;
}

async::coro<int> await_two_poll(int& polls) {
    auto value = co_await TwoPollInt { &polls };
    co_return value + 1;
}

async::coro<int> child_value() {
    co_await async::yield_now();
    co_return 21;
}

async::coro<int> indexed_child_value(int value) {
    co_await async::yield_now();
    co_return value;
}

async::coro<bool> current_thread_has_name() {
    co_return thread::current().name().is_some();
}

async::coro<int> await_executor_once(async::AnyExecutor ex, int& value) {
    value       = 1;
    auto posted = co_await ex;
    value       = posted ? 2 : -1;
    co_return value;
}

async::coro<void> signal_after_yield(std::atomic<int>& signal) {
    co_await async::yield_now();
    signal.store(1, std::memory_order_release);
}

async::coro<void> signal_after_sleep(std::atomic<int>& signal) {
    co_await async::sleep(time::Duration::from_millis(1));
    signal.store(1, std::memory_order_release);
}

auto wait_for_signal(std::atomic<int>& signal) -> bool {
    for (int i = 0; i < 100; ++i) {
        if (signal.load(std::memory_order_acquire) != 0) {
            return true;
        }
        thread::sleep(time::Duration::from_millis(1));
    }
    return false;
}

async::coro<int> join_child() {
    auto handle = async::spawn_local(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap() * 2;
}

async::coro<int> join_spawned_child() {
    auto handle = async::spawn(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap() * 2;
}

async::coro<bool> abort_join_child() {
    auto handle = async::spawn_local(child_value());
    handle.abort();
    auto result = co_await rstd::move(handle);
    if (result.is_ok()) {
        co_return false;
    }
    co_return result.unwrap_err().is_aborted();
}

async::coro<bool> abort_queued_child(int& polls) {
    auto handle = async::spawn_local(NeverReadyCount { &polls });
    handle.abort();
    auto result = co_await rstd::move(handle);
    if (result.is_ok()) {
        co_return false;
    }
    co_return result.unwrap_err().is_aborted() && polls == 0;
}

async::coro<void> sleep_once() {
    co_await async::sleep(time::Duration::from_millis(10));
}

async::coro<int> sleep_child_value() {
    co_await async::sleep(time::Duration::from_millis(1));
    co_return 17;
}

async::coro<int> join_sleeping_child() {
    auto handle = async::spawn_local(sleep_child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap();
}

async::coro<int> join_spawned_sleeping_child() {
    auto handle = async::spawn(sleep_child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap();
}

async::coro<io::Result<empty>> wait_for_notify(async::Notify notify) {
    co_return co_await notify.notified();
}

async::coro<io::Result<empty>> join_spawned_notify_waiter(async::Notify notify) {
    auto handle = async::spawn(wait_for_notify(rstd::move(notify)));
    auto result = co_await rstd::move(handle);
    if (result.is_err()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::Other }));
    }
    co_return rstd::move(result).unwrap();
}

async::coro<Result<int, async::CompletionError<empty>>> wait_for_completion(
    async::Completion<int> completion) {
    co_return co_await completion;
}

async::coro<Result<int, async::CompletionError<empty>>> join_spawned_completion_waiter(
    async::Completion<int> completion) {
    auto handle = async::spawn(wait_for_completion(rstd::move(completion)));
    auto result = co_await rstd::move(handle);
    if (result.is_err()) {
        co_return Err(async::CompletionError<empty>::canceled());
    }
    co_return rstd::move(result).unwrap();
}

async::coro<Result<int, async::CompletionError<int>>> wait_for_fallible_completion(
    async::Completion<int, int> completion) {
    co_return co_await completion;
}

async::coro<io::Result<Vec<int>>> drain_completion_queue(async::CompletionQueue<int> queue) {
    auto out = Vec<int>::make();

    for (;;) {
        auto next = co_await queue.next();
        if (next.is_err()) {
            co_return Err(rstd::move(next).unwrap_err_unchecked());
        }

        auto item = rstd::move(next).unwrap_unchecked();
        if (item.is_none()) {
            co_return Ok(rstd::move(out));
        }
        out.push(rstd::move(item).unwrap_unchecked());
    }
}

async::coro<io::Result<Vec<int>>> join_spawned_queue_waiter(
    async::CompletionQueue<int> queue) {
    auto handle = async::spawn(drain_completion_queue(rstd::move(queue)));
    auto result = co_await rstd::move(handle);
    if (result.is_err()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::Other }));
    }
    co_return rstd::move(result).unwrap();
}

async::coro<int> join_externally_woken_child(sync::Arc<SavedWakeState> state) {
    auto handle = async::spawn(ExternalWakeInt { rstd::move(state) });
    auto result = co_await rstd::move(handle);
    co_return result.unwrap();
}

async::coro<int> join_many_spawned_children() {
    auto handles = Vec<async::JoinHandle<int>>::make();
    for (int i = 0; i < 64; ++i) {
        handles.push(async::spawn(indexed_child_value(i)));
    }

    auto results = co_await async::join_all(rstd::move(handles));
    int  sum     = 0;
    for (usize i = 0; i < results.len(); ++i) {
        sum += results[i].unwrap();
    }
    co_return sum;
}

async::coro<bool> join_spawned_thread_name_check() {
    auto handle = async::spawn(current_thread_has_name());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap();
}

async::coro<async::JoinHandle<int>> spawn_never_ready_child(int& polls) {
    auto handle = async::spawn(NeverReadyCount { &polls });
    co_return rstd::move(handle);
}

async::coro<int> oneshot_roundtrip() {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    auto sent    = tx.send(33);
    if (sent.is_err()) {
        co_return -1;
    }

    auto received = co_await rstd::move(rx);
    co_return received.unwrap();
}

async::coro<bool> oneshot_sender_dropped() {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    tx.close();

    auto received = co_await rstd::move(rx);
    co_return received.is_err();
}

coro<int> prelude_coro_value() {
    co_return 13;
}

struct WakerCounts {
    std::atomic<int> clones { 0 };
    std::atomic<int> wakes { 0 };
    std::atomic<int> wake_refs { 0 };
    std::atomic<int> drops { 0 };
};

extern const task::RawWakerVTable COUNT_WAKER_VTABLE;

auto count_clone(voidp data) -> task::RawWaker {
    auto* counts = static_cast<WakerCounts*>(data);
    ++counts->clones;
    return task::RawWaker::from_raw_parts(data, rstd::addressof(COUNT_WAKER_VTABLE));
}

void count_wake(voidp data) {
    ++static_cast<WakerCounts*>(data)->wakes;
}

void count_wake_by_ref(voidp data) {
    ++static_cast<WakerCounts*>(data)->wake_refs;
}

void count_drop(voidp data) {
    ++static_cast<WakerCounts*>(data)->drops;
}

const task::RawWakerVTable COUNT_WAKER_VTABLE {
    &count_clone,
    &count_wake,
    &count_wake_by_ref,
    &count_drop,
};

struct RawCloneSwitchCounts {
    std::atomic<int> clones { 0 };
    std::atomic<int> wake_refs_a { 0 };
    std::atomic<int> wake_refs_b { 0 };
    std::atomic<int> drops_a { 0 };
    std::atomic<int> drops_b { 0 };
};

auto switch_clone_a(voidp data) -> task::RawWaker;
void switch_wake_a(voidp) {}
void switch_wake_ref_a(voidp data) {
    ++static_cast<RawCloneSwitchCounts*>(data)->wake_refs_a;
}
void switch_drop_a(voidp data) {
    ++static_cast<RawCloneSwitchCounts*>(data)->drops_a;
}

auto switch_clone_b(voidp data) -> task::RawWaker;
void switch_wake_b(voidp) {}
void switch_wake_ref_b(voidp data) {
    ++static_cast<RawCloneSwitchCounts*>(data)->wake_refs_b;
}
void switch_drop_b(voidp data) {
    ++static_cast<RawCloneSwitchCounts*>(data)->drops_b;
}

const task::RawWakerVTable SWITCH_WAKER_VTABLE_A {
    &switch_clone_a,
    &switch_wake_a,
    &switch_wake_ref_a,
    &switch_drop_a,
};

const task::RawWakerVTable SWITCH_WAKER_VTABLE_B {
    &switch_clone_b,
    &switch_wake_b,
    &switch_wake_ref_b,
    &switch_drop_b,
};

auto switch_clone_a(voidp data) -> task::RawWaker {
    ++static_cast<RawCloneSwitchCounts*>(data)->clones;
    return task::RawWaker::from_raw_parts(data, rstd::addressof(SWITCH_WAKER_VTABLE_B));
}

auto switch_clone_b(voidp data) -> task::RawWaker {
    ++static_cast<RawCloneSwitchCounts*>(data)->clones;
    return task::RawWaker::from_raw_parts(data, rstd::addressof(SWITCH_WAKER_VTABLE_B));
}

struct BlockingCloneWakerCounts {
    std::atomic<int>  clones { 0 };
    std::atomic<int>  wakes { 0 };
    std::atomic<int>  wake_refs { 0 };
    std::atomic<int>  drops { 0 };
    std::atomic<bool> clone_entered { false };
    std::atomic<bool> release_clone { false };
};

extern const task::RawWakerVTable BLOCKING_CLONE_WAKER_VTABLE;

auto blocking_clone(voidp data) -> task::RawWaker {
    auto* counts = static_cast<BlockingCloneWakerCounts*>(data);
    ++counts->clones;
    counts->clone_entered.store(true, std::memory_order_release);
    while (! counts->release_clone.load(std::memory_order_acquire)) {
        hint::spin_loop();
    }
    return task::RawWaker::from_raw_parts(data, rstd::addressof(BLOCKING_CLONE_WAKER_VTABLE));
}

void blocking_wake(voidp data) {
    ++static_cast<BlockingCloneWakerCounts*>(data)->wakes;
}

void blocking_wake_by_ref(voidp data) {
    ++static_cast<BlockingCloneWakerCounts*>(data)->wake_refs;
}

void blocking_drop(voidp data) {
    ++static_cast<BlockingCloneWakerCounts*>(data)->drops;
}

const task::RawWakerVTable BLOCKING_CLONE_WAKER_VTABLE {
    &blocking_clone,
    &blocking_wake,
    &blocking_wake_by_ref,
    &blocking_drop,
};

struct SharedWakeState {
    sync::Mutex<bool> ready;
    async::AtomicWaker waker;

    SharedWakeState() : ready(false) {}

    void complete() {
        {
            auto locked = ready.lock().unwrap_unchecked();
            *locked = true;
        }
        waker.wake();
    }
};

struct SharedWakeFuture {
    using Output = int;

    sync::Arc<SharedWakeState> state;

    auto poll(pin::Pin<mut_ref<SharedWakeFuture>> self, task::Context& cx)
        -> task::Poll<int> {
        auto& future = *self.get_unchecked_mut();
        future.state->waker.register_context(cx);

        auto locked = future.state->ready.lock().unwrap_unchecked();
        if (*locked) {
            return task::Poll<int>::Ready(17);
        }
        return task::Poll<int>::Pending();
    }
};

} // namespace

struct TestExecutorState {
    sync::Mutex<Vec<async::ExecutorJob>> jobs;
    sync::Mutex<bool>                    closed;

    TestExecutorState() : jobs(Vec<async::ExecutorJob>::make()), closed(false) {}
};

struct TestExecutor {
    sync::Weak<TestExecutorState> state;

    explicit TestExecutor(sync::Weak<TestExecutorState> state) : state(rstd::move(state)) {}

    TestExecutor(const TestExecutor&)            = delete;
    auto operator=(const TestExecutor&) -> TestExecutor& = delete;
    TestExecutor(TestExecutor&&) noexcept                    = default;
    auto operator=(TestExecutor&&) noexcept -> TestExecutor& = default;

    auto clone() const -> TestExecutor { return TestExecutor { state.clone() }; }

    auto post_job(async::ExecutorJob job) -> bool {
        auto shared = state.upgrade();
        if (! shared) {
            return false;
        }

        auto is_closed = shared->closed.lock().unwrap_unchecked();
        if (*is_closed) {
            return false;
        }

        auto queue = shared->jobs.lock().unwrap_unchecked();
        queue->push(rstd::move(job));
        return true;
    }

    template<typename F>
    auto post(F job) -> bool {
        return post_job(async::ExecutorJob::make(rstd::move(job)));
    }

    auto is_closed() -> bool {
        auto shared = state.upgrade();
        if (! shared) {
            return true;
        }
        auto is_closed = shared->closed.lock().unwrap_unchecked();
        return *is_closed;
    }
};

struct TestExecutorContext {
    using executor_type = TestExecutor;

    sync::Arc<TestExecutorState> state;

    TestExecutorContext() : state(sync::Arc<TestExecutorState>::make()) {}

    auto executor() -> TestExecutor { return TestExecutor { state.downgrade() }; }

    auto take_ready() -> Vec<async::ExecutorJob> {
        auto queue = state->jobs.lock().unwrap_unchecked();
        auto out   = rstd::move(*queue);
        *queue     = Vec<async::ExecutorJob>::make();
        return out;
    }

    auto run_ready() -> usize {
        auto ready = take_ready();
        auto ran   = usize {};
        while (! ready.is_empty()) {
            auto job = ready.remove(0);
            job->operator()();
            ++ran;
        }
        return ran;
    }

    void close() {
        {
            auto is_closed = state->closed.lock().unwrap_unchecked();
            *is_closed     = true;
        }

        auto queue = state->jobs.lock().unwrap_unchecked();
        queue->clear();
    }

    auto is_closed() -> bool {
        auto is_closed = state->closed.lock().unwrap_unchecked();
        return *is_closed;
    }
};

template<>
struct rstd::Impl<rstd::async::Executor, TestExecutor>
    : rstd::LinkClassMethod<rstd::async::Executor, TestExecutor> {};

template<>
struct rstd::Impl<rstd::async::ExecutorContext, TestExecutorContext>
    : rstd::LinkClassMethod<rstd::async::ExecutorContext, TestExecutorContext> {};

TEST(AsyncCoro, PollHelpers) {
    auto ready = task::Poll<int>::Ready(3);
    ASSERT_TRUE(ready.is_ready());
    EXPECT_EQ(rstd::move(ready).take(), 3);

    auto pending = task::Poll<int>::Pending();
    EXPECT_TRUE(pending.is_pending());
}

TEST(AsyncCoro, WakerOwnsRawHandle) {
    auto counts = WakerCounts {};
    {
        auto waker = task::Waker::from_raw(task::RawWaker::from_raw_parts(
            &counts,
            rstd::addressof(COUNT_WAKER_VTABLE)));
        auto clone = waker.clone();
        waker.wake_by_ref();
        rstd::move(clone).wake();
    }

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.wake_refs.load(), 1);
    EXPECT_EQ(counts.wakes.load(), 1);
    EXPECT_EQ(counts.drops.load(), 1);
}

TEST(AsyncCoro, WakerCloneUsesReturnedRawWaker) {
    auto counts = RawCloneSwitchCounts {};
    {
        auto waker = task::Waker::from_raw(task::RawWaker::from_raw_parts(
            &counts,
            rstd::addressof(SWITCH_WAKER_VTABLE_A)));
        auto clone = waker.clone();

        EXPECT_EQ(clone.vtable(), rstd::addressof(SWITCH_WAKER_VTABLE_B));
        clone.wake_by_ref();
    }

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.wake_refs_a.load(), 0);
    EXPECT_EQ(counts.wake_refs_b.load(), 1);
    EXPECT_EQ(counts.drops_a.load(), 1);
    EXPECT_EQ(counts.drops_b.load(), 1);
}

TEST(AsyncCoro, WakerCloneFromSkipsEquivalentWaker) {
    auto counts = WakerCounts {};
    {
        auto waker = task::Waker::from_raw(task::RawWaker::from_raw_parts(
            &counts,
            rstd::addressof(COUNT_WAKER_VTABLE)));
        auto same = waker.clone();

        EXPECT_EQ(counts.clones.load(), 1);
        waker.clone_from(same);
        EXPECT_EQ(counts.clones.load(), 1);
    }

    EXPECT_EQ(counts.drops.load(), 2);
}

TEST(AsyncCoro, NoopWakerDoesNothing) {
    auto& waker = task::Waker::noop();
    auto  clone = waker.clone();

    waker.wake_by_ref();
    rstd::move(clone).wake();
    EXPECT_TRUE(waker.will_wake(task::Waker::noop()));
}

TEST(AsyncCoro, AtomicWakerWakesRegisteredWaker) {
    auto counts = WakerCounts {};
    auto atomic = async::AtomicWaker {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));

    atomic.register_waker(waker);
    atomic.wake();
    atomic.wake();

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.wakes.load(), 1);
}

TEST(AsyncCoro, AtomicWakerWakesLatestRegisteredWaker) {
    auto first_counts  = WakerCounts {};
    auto second_counts = WakerCounts {};
    auto atomic        = async::AtomicWaker {};

    auto first = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &first_counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto second = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &second_counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));

    atomic.register_waker(first);
    atomic.register_waker(second);
    atomic.wake();

    EXPECT_EQ(first_counts.wakes.load(), 0);
    EXPECT_EQ(second_counts.wakes.load(), 1);
}

TEST(AsyncCoro, AtomicWakerWakeDuringRegisterWakesNewWaker) {
    auto counts = BlockingCloneWakerCounts {};
    auto atomic = async::AtomicWaker {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(BLOCKING_CLONE_WAKER_VTABLE)));

    auto handle = thread::spawn([&atomic, &waker] {
        atomic.register_waker(waker);
    }).unwrap();

    for (int i = 0; i < 10000 && ! counts.clone_entered.load(std::memory_order_acquire); ++i) {
        thread::sleep(time::Duration::from_millis(1));
    }

    if (! counts.clone_entered.load(std::memory_order_acquire)) {
        counts.release_clone.store(true, std::memory_order_release);
        rstd::move(handle).join().unwrap();
        FAIL() << "blocking clone did not start";
    }

    atomic.wake();
    counts.release_clone.store(true, std::memory_order_release);
    rstd::move(handle).join().unwrap();
    atomic.wake();

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.wakes.load(), 1);
    EXPECT_EQ(counts.wake_refs.load(), 0);
}

TEST(AsyncExecutor, PostRunsWhenRunReadyIsCalled) {
    auto context = async::LocalExecutorContext::make();
    auto ex      = context.executor();
    int  value   = 0;

    EXPECT_TRUE(ex.post([&value] {
        value = 7;
    }));
    EXPECT_EQ(value, 0);

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value, 7);
    EXPECT_EQ(context.run_ready(), usize { 0 });
}

TEST(AsyncExecutor, CanWrapExternalExecutorContext) {
    auto context = TestExecutorContext {};
    auto ex      = async::AnyExecutor::from_executor(context.executor());
    int  value   = 0;

    EXPECT_TRUE(ex.post([&value] {
        value = 11;
    }));

    EXPECT_EQ(value, 0);

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value, 11);

    context.close();
    EXPECT_TRUE(context.is_closed());
    EXPECT_TRUE(ex.is_closed());
    EXPECT_FALSE(ex.post([] {}));
}

TEST(AsyncExecutor, TypedExecutorContextRunsThroughContext) {
    auto context = TestExecutorContext {};
    auto ex      = context.executor();
    int  value   = 0;

    EXPECT_TRUE(ex.post([&value] {
        value = 19;
    }));

    EXPECT_EQ(value, 0);
    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value, 19);

    context.close();
    EXPECT_TRUE(ex.is_closed());
    EXPECT_FALSE(ex.post([] {}));
}

TEST(AsyncExecutor, HandleCanPostFromAnotherThread) {
    auto context = async::LocalExecutorContext::make();
    auto value   = std::atomic<int> { 0 };
    auto ex      = context.executor();

    auto worker = thread::spawn([ex = rstd::move(ex), &value]() mutable {
        return ex.post([&value] {
            value.store(3, std::memory_order_release);
        });
    }).unwrap();

    auto posted = rstd::move(worker).join().unwrap();
    EXPECT_TRUE(posted);
    EXPECT_EQ(value.load(std::memory_order_acquire), 0);

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value.load(std::memory_order_acquire), 3);
}

TEST(AsyncExecutor, NestedPostRunsOnNextRunReady) {
    auto context = async::LocalExecutorContext::make();
    auto ex      = context.executor();
    int  value   = 0;

    EXPECT_TRUE(ex.post([nested = ex.clone(), &value]() mutable {
        value = 1;
        EXPECT_TRUE(nested.post([&value] {
            value = 2;
        }));
    }));

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value, 1);

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(value, 2);
}

TEST(AsyncExecutor, PostFailsAfterClose) {
    auto context = async::LocalExecutorContext::make();
    auto ex      = context.executor();

    EXPECT_TRUE(ex.post([] {}));
    context.close();

    EXPECT_TRUE(context.is_closed());
    EXPECT_TRUE(ex.is_closed());
    EXPECT_FALSE(ex.post([] {}));
    EXPECT_EQ(context.run_ready(), usize { 0 });
}

TEST(AsyncExecutor, CoAwaitExecutorCompletesAfterRunReady) {
    auto context = async::LocalExecutorContext::make();
    auto ex      = async::AnyExecutor::from_executor(context.executor());
    int  value   = 0;
    auto future  = await_executor_once(ex.clone(), value);

    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx = task::Context::from_waker(waker);

    auto first = future::poll(future, cx);
    EXPECT_TRUE(first.is_pending());
    EXPECT_EQ(value, 1);

    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(counts.wakes.load(), 1);

    auto second = future::poll(future, cx);
    ASSERT_TRUE(second.is_ready());
    EXPECT_EQ(rstd::move(second).take(), 2);
    EXPECT_EQ(value, 2);
}

TEST(AsyncCoro, SharedStateFutureWakesLatestTask) {
    auto state  = sync::Arc<SharedWakeState>::make();
    auto future = SharedWakeFuture { state.clone() };

    auto first_counts = WakerCounts {};
    auto first_waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &first_counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto first_cx = task::Context::from_waker(first_waker);
    auto first    = future::poll(future, first_cx);
    EXPECT_TRUE(first.is_pending());

    auto second_counts = WakerCounts {};
    auto second_waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &second_counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto second_cx = task::Context::from_waker(second_waker);
    auto second    = future::poll(future, second_cx);
    EXPECT_TRUE(second.is_pending());

    state->complete();
    EXPECT_EQ(first_counts.wakes.load(), 0);
    EXPECT_EQ(second_counts.wakes.load(), 1);

    auto done = future::poll(future, second_cx);
    ASSERT_TRUE(done.is_ready());
    EXPECT_EQ(rstd::move(done).take(), 17);
}

TEST(AsyncCoro, BlockOnReadyFuture) {
    EXPECT_EQ(async::block_on(ReadyInt {}), 7);
}

TEST(AsyncCoro, CoroutineIsLazy) {
    int counter = 0;
    auto task   = lazy_value(counter);
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(async::block_on(rstd::move(task)), 5);
    EXPECT_EQ(counter, 1);
}

TEST(AsyncCoro, CoroutineRePollsPendingChildAfterWake) {
    int polls = 0;
    EXPECT_EQ(async::block_on(await_two_poll(polls)), 42);
    EXPECT_EQ(polls, 2);
}

TEST(AsyncCoro, SpawnLocalJoinHandle) {
    EXPECT_EQ(async::block_on(join_child()), 42);
}

TEST(AsyncCoro, MultiThreadRuntimeSpawnJoinHandle) {
    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(2).build();
    auto runtime        = runtime_result.unwrap();

    EXPECT_EQ(runtime.block_on(join_spawned_child()), 42);
}

TEST(AsyncCoro, JoinHandleAbortCompletesWithError) {
    EXPECT_TRUE(async::block_on(abort_join_child()));
}

TEST(AsyncCoro, AbortQueuedTaskDoesNotPollFuture) {
    int polls = 0;

    EXPECT_TRUE(async::block_on(abort_queued_child(polls)));
    EXPECT_EQ(polls, 0);
}

TEST(AsyncCoro, SavedWakerReschedulesTaskFromExternalThread) {
    auto state  = sync::Arc<SavedWakeState>::make();
    auto worker = state.clone();

    auto handle = thread::spawn([worker = rstd::move(worker)] {
        for (;;) {
            auto waker = Option<task::Waker> {};
            {
                auto fields = worker->fields.lock().unwrap();
                if (fields->waker.is_some()) {
                    fields->ready = true;
                    waker         = fields->waker.take();
                }
            }

            if (waker.is_some()) {
                rstd::move(*waker).wake();
                return;
            }

            thread::sleep(time::Duration::from_millis(1));
        }
    }).unwrap();

    EXPECT_EQ(async::block_on(ExternalWakeInt { state.clone() }), 2);
    rstd::move(handle).join().unwrap();

    auto fields = state->fields.lock().unwrap();
    EXPECT_EQ(fields->polls, 2);
}

TEST(AsyncCoro, MultiThreadRuntimeExternalTaskWake) {
    auto state  = sync::Arc<SavedWakeState>::make();
    auto worker = state.clone();

    auto handle = thread::spawn([worker = rstd::move(worker)] {
        for (;;) {
            auto waker = Option<task::Waker> {};
            {
                auto fields = worker->fields.lock().unwrap();
                if (fields->waker.is_some()) {
                    fields->ready = true;
                    waker         = fields->waker.take();
                }
            }

            if (waker.is_some()) {
                rstd::move(*waker).wake();
                return;
            }

            thread::sleep(time::Duration::from_millis(1));
        }
    }).unwrap();

    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(2).build();
    auto runtime        = runtime_result.unwrap();

    EXPECT_EQ(runtime.block_on(join_externally_woken_child(state.clone())), 2);
    rstd::move(handle).join().unwrap();

    auto fields = state->fields.lock().unwrap();
    EXPECT_EQ(fields->polls, 2);
}

TEST(AsyncCoro, NotifyWakesCurrentThreadRuntimeFromExternalThread) {
    auto notify_result = async::Notify::make();
    ASSERT_TRUE(notify_result.is_ok());
    auto notify   = rstd::move(notify_result).unwrap_unchecked();
    auto notifier = notify.notifier();

    auto handle = thread::spawn([notifier = rstd::move(notifier)] {
        thread::sleep(time::Duration::from_millis(1));
        return notifier.notify();
    }).unwrap();

    auto result = async::block_on(wait_for_notify(rstd::move(notify)));
    ASSERT_TRUE(result.is_ok());

    auto wake = rstd::move(handle).join().unwrap();
    ASSERT_TRUE(wake.is_ok());
}

TEST(AsyncCoro, NotifyWakesSpawnedTaskFromExternalThread) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_io().build();
    auto runtime        = runtime_result.unwrap();

    auto notify_result = async::Notify::make();
    ASSERT_TRUE(notify_result.is_ok());
    auto notify   = rstd::move(notify_result).unwrap_unchecked();
    auto notifier = notify.notifier();

    auto handle = thread::spawn([notifier = rstd::move(notifier)] {
        thread::sleep(time::Duration::from_millis(1));
        return notifier.notify();
    }).unwrap();

    auto result = runtime.block_on(join_spawned_notify_waiter(rstd::move(notify)));
    ASSERT_TRUE(result.is_ok());

    auto wake = rstd::move(handle).join().unwrap();
    ASSERT_TRUE(wake.is_ok());
}

TEST(AsyncCoro, CompletionCompletesCurrentThreadRuntimeFromExternalThread) {
    auto completion_result = async::Completion<int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair       = rstd::move(completion_result).unwrap_unchecked();
    auto completion = rstd::move(pair.get<0>());
    auto completer  = rstd::move(pair.get<1>());

    auto handle = thread::spawn([completer = rstd::move(completer)]() mutable {
        thread::sleep(time::Duration::from_millis(1));
        return completer.complete(77);
    }).unwrap();

    auto result = async::block_on(wait_for_completion(rstd::move(completion)));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 77);

    auto completed = rstd::move(handle).join().unwrap();
    ASSERT_TRUE(completed.is_ok());
}

TEST(AsyncCoro, CompletionCompletesSpawnedTaskFromExternalThread) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_io().build();
    auto runtime        = runtime_result.unwrap();

    auto completion_result = async::Completion<int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair       = rstd::move(completion_result).unwrap_unchecked();
    auto completion = rstd::move(pair.get<0>());
    auto completer  = rstd::move(pair.get<1>());

    auto handle = thread::spawn([completer = rstd::move(completer)]() mutable {
        thread::sleep(time::Duration::from_millis(1));
        return completer.complete(88);
    }).unwrap();

    auto result = runtime.block_on(join_spawned_completion_waiter(rstd::move(completion)));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 88);

    auto completed = rstd::move(handle).join().unwrap();
    ASSERT_TRUE(completed.is_ok());
}

TEST(AsyncCoro, CompletionFailReturnsError) {
    auto completion_result = async::Completion<int, int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair       = rstd::move(completion_result).unwrap_unchecked();
    auto completion = rstd::move(pair.get<0>());
    auto completer  = rstd::move(pair.get<1>());

    ASSERT_TRUE(completer.fail(9).is_ok());

    auto result = async::block_on(wait_for_fallible_completion(rstd::move(completion)));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_failed());
    EXPECT_EQ(rstd::move(error).unwrap_failed(), 9);
}

TEST(AsyncCoro, CompletionRejectsDuplicateComplete) {
    auto completion_result = async::Completion<int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair       = rstd::move(completion_result).unwrap_unchecked();
    auto completion = rstd::move(pair.get<0>());
    auto completer  = rstd::move(pair.get<1>());

    ASSERT_TRUE(completer.complete(1).is_ok());
    auto duplicate = completer.complete(2);
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_EQ(rstd::move(duplicate).unwrap_err(), 2);

    auto result = async::block_on(wait_for_completion(rstd::move(completion)));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 1);
}

TEST(AsyncCoro, CompletionHandleDropCancelsReceiver) {
    auto completion_result = async::Completion<int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair       = rstd::move(completion_result).unwrap_unchecked();
    auto completion = rstd::move(pair.get<0>());
    {
        auto completer = rstd::move(pair.get<1>());
    }

    auto result = async::block_on(wait_for_completion(rstd::move(completion)));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_canceled());
}

TEST(AsyncCoro, CompletionReceiverDropClosesHandle) {
    auto completion_result = async::Completion<int>::make();
    ASSERT_TRUE(completion_result.is_ok());
    auto pair      = rstd::move(completion_result).unwrap_unchecked();
    auto completer = rstd::move(pair.get<1>());
    {
        auto completion = rstd::move(pair.get<0>());
    }

    EXPECT_TRUE(completer.is_closed());
    auto result = completer.complete(5);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err(), 5);
}

TEST(AsyncCoro, CompletionQueuePushCloseDrainsItems) {
    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair     = rstd::move(queue_result).unwrap_unchecked();
    auto queue    = rstd::move(pair.get<0>());
    auto producer = rstd::move(pair.get<1>());

    ASSERT_TRUE(producer.push(1).is_ok());
    ASSERT_TRUE(producer.push(2).is_ok());
    producer.close();

    auto result = async::block_on(drain_completion_queue(rstd::move(queue)));
    ASSERT_TRUE(result.is_ok());
    auto values = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(values.len(), 2u);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
}

TEST(AsyncCoro, CompletionQueueWakesCurrentThreadRuntimeFromExternalThread) {
    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair     = rstd::move(queue_result).unwrap_unchecked();
    auto queue    = rstd::move(pair.get<0>());
    auto producer = rstd::move(pair.get<1>());

    auto handle = thread::spawn([producer = rstd::move(producer)]() mutable {
        thread::sleep(time::Duration::from_millis(1));
        auto pushed = producer.push(7);
        if (pushed.is_err()) {
            return false;
        }
        producer.close();
        return true;
    }).unwrap();

    auto result = async::block_on(drain_completion_queue(rstd::move(queue)));
    ASSERT_TRUE(result.is_ok());
    auto values = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(values.len(), 1u);
    EXPECT_EQ(values[0], 7);

    EXPECT_TRUE(rstd::move(handle).join().unwrap());
}

TEST(AsyncCoro, CompletionQueueWakesSpawnedTaskFromExternalThread) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_io().build();
    auto runtime        = runtime_result.unwrap();

    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair     = rstd::move(queue_result).unwrap_unchecked();
    auto queue    = rstd::move(pair.get<0>());
    auto producer = rstd::move(pair.get<1>());

    auto handle = thread::spawn([producer = rstd::move(producer)]() mutable {
        thread::sleep(time::Duration::from_millis(1));
        auto pushed = producer.push(11);
        if (pushed.is_err()) {
            return false;
        }
        producer.close();
        return true;
    }).unwrap();

    auto result = runtime.block_on(join_spawned_queue_waiter(rstd::move(queue)));
    ASSERT_TRUE(result.is_ok());
    auto values = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(values.len(), 1u);
    EXPECT_EQ(values[0], 11);

    EXPECT_TRUE(rstd::move(handle).join().unwrap());
}

TEST(AsyncCoro, CompletionQueueHandleDropClosesReceiver) {
    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair  = rstd::move(queue_result).unwrap_unchecked();
    auto queue = rstd::move(pair.get<0>());
    {
        auto producer = rstd::move(pair.get<1>());
    }

    auto result = async::block_on(queue.next());
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(rstd::move(result).unwrap_unchecked().is_none());
}

TEST(AsyncCoro, CompletionQueueReceiverDropClosesHandle) {
    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair     = rstd::move(queue_result).unwrap_unchecked();
    auto producer = rstd::move(pair.get<1>());
    {
        auto queue = rstd::move(pair.get<0>());
    }

    EXPECT_TRUE(producer.is_closed());
    auto pushed = producer.push(13);
    ASSERT_TRUE(pushed.is_err());
    EXPECT_EQ(rstd::move(pushed).unwrap_err(), 13);
}

TEST(AsyncCoro, CompletionQueueClonedProducersCloseAfterLastDrop) {
    auto queue_result = async::CompletionQueue<int>::make();
    ASSERT_TRUE(queue_result.is_ok());
    auto pair      = rstd::move(queue_result).unwrap_unchecked();
    auto queue     = rstd::move(pair.get<0>());
    auto producer1 = rstd::move(pair.get<1>());
    auto producer2 = producer1.clone();

    producer1.drop();
    ASSERT_TRUE(producer2.push(17).is_ok());
    producer2.drop();

    auto result = async::block_on(drain_completion_queue(rstd::move(queue)));
    ASSERT_TRUE(result.is_ok());
    auto values = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(values.len(), 1u);
    EXPECT_EQ(values[0], 17);
}

TEST(AsyncCoro, MultiThreadRuntimeRunsManySpawnedTasks) {
    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(4).build();
    auto runtime        = runtime_result.unwrap();

    EXPECT_EQ(runtime.block_on(join_many_spawned_children()), 2016);
}

TEST(AsyncCoro, MultiThreadRuntimeSpawnRunsWhileCallerContinues) {
    auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(2).build();
    auto runtime        = runtime_result.unwrap();
    auto signal         = std::atomic<int> { 0 };

    auto handle = runtime.spawn(signal_after_yield(signal));

    EXPECT_TRUE(wait_for_signal(signal));
    auto result = runtime.block_on(rstd::move(handle));
    ASSERT_TRUE(result.is_ok());
}

TEST(AsyncCoro, MultiThreadRuntimeDetachedSpawnRunsWhileCallerContinues) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_time().build();
    auto runtime        = runtime_result.unwrap();
    auto signal         = std::atomic<int> { 0 };

    (void)runtime.spawn(signal_after_sleep(signal));

    EXPECT_TRUE(wait_for_signal(signal));
}

TEST(AsyncCoro, RuntimeHandleCloneSpawnsWhileCallerContinues) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_time().build();
    auto runtime        = runtime_result.unwrap();
    auto handle         = runtime.handle();
    auto cloned         = handle.clone();
    auto signal         = std::atomic<int> { 0 };

    (void)cloned.spawn(signal_after_sleep(signal));

    EXPECT_TRUE(wait_for_signal(signal));
}

TEST(AsyncCoro, MultiThreadRuntimeNamesWorkerThreads) {
    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(2).thread_name(rstd::string::String::make("async-worker"));

    auto runtime_result = builder.build();
    auto runtime        = runtime_result.unwrap();

    EXPECT_TRUE(runtime.block_on(join_spawned_thread_name_check()));
}

TEST(AsyncCoro, OneShotRoundtrip) {
    EXPECT_EQ(async::block_on(oneshot_roundtrip()), 33);
}

TEST(AsyncCoro, OneShotSenderCloseWakesReceiver) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    auto counts  = WakerCounts {};
    auto waker   = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx      = task::Context { waker };

    auto first = future::poll(rx, cx);
    EXPECT_TRUE(first.is_pending());
    EXPECT_EQ(counts.clones.load(), 1);

    tx.close();
    EXPECT_EQ(counts.wakes.load(), 1);

    auto second = future::poll(rx, cx);
    ASSERT_TRUE(second.is_ready());
    auto result = rstd::move(second).take();
    EXPECT_TRUE(result.is_err());
}

TEST(AsyncCoro, OneShotReceiverDropWakesSenderCancellation) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto counts  = WakerCounts {};
    auto waker   = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx      = task::Context { waker };

    {
        auto rx    = rstd::move(channel.get<1>());
        auto first = tx.poll_canceled(future::pin_mut(tx), cx);
        EXPECT_TRUE(first.is_pending());
        EXPECT_EQ(counts.clones.load(), 1);
    }

    EXPECT_EQ(counts.wakes.load(), 1);

    auto second = tx.poll_canceled(future::pin_mut(tx), cx);
    ASSERT_TRUE(second.is_ready());
    rstd::move(second).take();
}

TEST(AsyncCoro, OneShotSendFailsAfterReceiverDrop) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    {
        auto rx = rstd::move(channel.get<1>());
    }

    auto result = tx.send(55);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err(), 55);
}

TEST(AsyncCoro, OneShotReportsSenderDropAsCanceled) {
    EXPECT_TRUE(async::block_on(oneshot_sender_dropped()));
}

TEST(AsyncCoro, JoinPollsAllChildren) {
    int ready_polls  = 0;
    int pending_polls = 0;

    auto out = async::block_on(async::join(CountReadyInt { &ready_polls, 10 },
                                           CountPendingInt { &pending_polls, 20 }));

    EXPECT_EQ(out.get<0>(), 10);
    EXPECT_EQ(out.get<1>(), 20);
    EXPECT_EQ(ready_polls, 1);
    EXPECT_EQ(pending_polls, 2);
}

TEST(AsyncCoro, JoinSupportsVoidFuture) {
    auto out = async::block_on(async::join(async::yield_now(), ReadyInt {}));
    EXPECT_EQ(out.get<1>(), 7);
}

TEST(AsyncCoro, JoinAllPreservesOrder) {
    int polls[3] {};
    auto futures = Vec<IndexedPendingInt>::make();
    futures.push(IndexedPendingInt { polls, 0, 30 });
    futures.push(IndexedPendingInt { polls, 1, 10 });
    futures.push(IndexedPendingInt { polls, 2, 20 });

    auto out = async::block_on(async::join_all(rstd::move(futures)));

    ASSERT_EQ(out.len(), 3u);
    EXPECT_EQ(out[0], 30);
    EXPECT_EQ(out[1], 10);
    EXPECT_EQ(out[2], 20);
    EXPECT_EQ(polls[0], 2);
    EXPECT_EQ(polls[1], 2);
    EXPECT_EQ(polls[2], 2);
}

TEST(AsyncCoro, JoinAllHandlesEmptyAndVoid) {
    auto empty_futures = Vec<ReadyInt>::make();
    auto empty_out     = async::block_on(async::join_all(rstd::move(empty_futures)));
    EXPECT_TRUE(empty_out.is_empty());

    auto void_futures = Vec<async::YieldNow>::make();
    void_futures.push(async::yield_now());
    void_futures.push(async::yield_now());
    auto void_out = async::block_on(async::join_all(rstd::move(void_futures)));
    EXPECT_EQ(void_out.len(), 2u);
}

TEST(AsyncCoro, SelectReturnsLeftAndDropsLoser) {
    int drops = 0;

    auto out = async::block_on(async::select(ReadyInt {}, DropPendingInt { &drops }));

    ASSERT_TRUE(out.is_left());
    EXPECT_EQ(rstd::move(out).unwrap_left(), 7);
    EXPECT_EQ(drops, 1);
}

TEST(AsyncCoro, SelectReturnsRightWhenLeftPending) {
    int left_polls = 0;

    auto out = async::block_on(
        async::select(CountPendingInt { &left_polls, 11 }, ReadyInt {}));

    ASSERT_TRUE(out.is_right());
    EXPECT_EQ(rstd::move(out).unwrap_right(), 7);
    EXPECT_EQ(left_polls, 1);
}

TEST(AsyncCoro, RaceReturnsWinnerOutput) {
    int left_polls = 0;

    auto out = async::block_on(
        async::race(CountPendingInt { &left_polls, 11 }, ReadyInt {}));

    EXPECT_EQ(out, 7);
    EXPECT_EQ(left_polls, 1);
}

TEST(AsyncCoro, TimeoutReadyFutureWins) {
    auto result = async::block_on(
        async::timeout(ReadyInt {}, time::Duration::from_millis(0)));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 7);
}

TEST(AsyncCoro, TimeoutExpiresAndDropsFuture) {
    int drops = 0;
    auto result = async::block_on(
        async::timeout(DropPendingInt { &drops }, time::Duration::from_millis(1)));

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(drops, 1);
}

TEST(AsyncCoro, StreamLikePollNext) {
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx     = task::Context { waker };
    auto stream = CounterStream {};

    auto first = future::poll_next(stream, cx);
    ASSERT_TRUE(first.is_ready());
    auto first_value = rstd::move(first).take();
    ASSERT_TRUE(first_value.is_some());
    EXPECT_EQ(rstd::move(first_value).unwrap_unchecked(), 0);

    auto second = future::poll_next(stream, cx);
    ASSERT_TRUE(second.is_ready());
    auto second_value = rstd::move(second).take();
    ASSERT_TRUE(second_value.is_some());
    EXPECT_EQ(rstd::move(second_value).unwrap_unchecked(), 1);

    auto done = future::poll_next(stream, cx);
    ASSERT_TRUE(done.is_ready());
    EXPECT_TRUE(rstd::move(done).take().is_none());
}

TEST(AsyncCoro, AsyncIoProtocolsUseBytesAndIoResult) {
    u8 read_data[] { 'a', 'b', 'c' };
    auto stream = MockAsyncIo { read_data, 3 };
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx     = task::Context { waker };
    auto read_buf = bytes::BytesMut::make();

    auto read_pending = async::io::poll_read(stream, cx, read_buf);
    EXPECT_TRUE(read_pending.is_pending());
    EXPECT_EQ(counts.wake_refs.load(), 1);

    auto read_ready = async::io::poll_read(stream, cx, read_buf);
    ASSERT_TRUE(read_ready.is_ready());
    auto read_result = rstd::move(read_ready).take();
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(rstd::move(read_result).unwrap(), 3u);
    ASSERT_EQ(read_buf.len(), 3u);
    EXPECT_EQ(read_buf[0], 'a');
    EXPECT_EQ(read_buf[2], 'c');

    u8 write_data[] { 'x', 'y' };
    auto write_buf = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(write_data, 2));
    auto write_pending = async::io::poll_write(stream, cx, write_buf);
    EXPECT_TRUE(write_pending.is_pending());
    EXPECT_EQ(counts.wake_refs.load(), 2);

    auto write_ready = async::io::poll_write(stream, cx, write_buf);
    ASSERT_TRUE(write_ready.is_ready());
    auto write_result = rstd::move(write_ready).take();
    ASSERT_TRUE(write_result.is_ok());
    EXPECT_EQ(rstd::move(write_result).unwrap(), 2u);
    ASSERT_EQ(stream.written.len(), 2u);
    EXPECT_EQ(stream.written[0], 'x');
    EXPECT_EQ(stream.written[1], 'y');

    auto flush = async::io::poll_flush(stream, cx);
    ASSERT_TRUE(flush.is_ready());
    ASSERT_TRUE(rstd::move(flush).take().is_ok());
    EXPECT_TRUE(stream.flushed);

    auto shutdown = async::io::poll_shutdown(stream, cx);
    ASSERT_TRUE(shutdown.is_ready());
    ASSERT_TRUE(rstd::move(shutdown).take().is_ok());
    EXPECT_TRUE(stream.shutdown);
}

TEST(AsyncCoro, AsyncIoHelpersReadWriteFlushShutdown) {
    u8 read_data[] { 'a', 'b', 'c' };
    auto stream   = MockAsyncIo { read_data, 3 };
    auto read_buf = bytes::BytesMut::make();

    auto read_result = async::block_on(async::io::read(stream, read_buf));
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(rstd::move(read_result).unwrap(), 3u);
    ASSERT_EQ(read_buf.len(), 3u);
    EXPECT_EQ(read_buf[0], 'a');
    EXPECT_EQ(read_buf[2], 'c');

    u8 write_data[] { 'x', 'y' };
    auto write_buf = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(write_data, 2));
    auto write_result = async::block_on(async::io::write(stream, write_buf));
    ASSERT_TRUE(write_result.is_ok());
    EXPECT_EQ(rstd::move(write_result).unwrap(), 2u);
    ASSERT_EQ(stream.written.len(), 2u);
    EXPECT_EQ(stream.written[0], 'x');
    EXPECT_EQ(stream.written[1], 'y');

    auto flush_result = async::block_on(async::io::flush(stream));
    ASSERT_TRUE(flush_result.is_ok());
    EXPECT_TRUE(stream.flushed);

    auto shutdown_result = async::block_on(async::io::shutdown(stream));
    ASSERT_TRUE(shutdown_result.is_ok());
    EXPECT_TRUE(stream.shutdown);
}

TEST(AsyncCoro, AsyncIoReadExactReadsRequestedBytes) {
    u8 read_data[] { 'q', 'w', 'e' };
    auto stream   = MockAsyncIo { read_data, 3 };
    auto read_buf = bytes::BytesMut::make();

    auto result = async::block_on(async::io::read_exact(stream, read_buf, 3));

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(read_buf.len(), 3u);
    EXPECT_EQ(read_buf[0], 'q');
    EXPECT_EQ(read_buf[2], 'e');
}

TEST(AsyncCoro, AsyncIoReadExactReportsUnexpectedEof) {
    u8 read_data[] { 0 };
    auto stream          = MockAsyncIo { read_data, 0 };
    stream.read_pending  = false;
    auto read_buf        = bytes::BytesMut::make();

    auto result = async::block_on(async::io::read_exact(stream, read_buf, 1));

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::UnexpectedEof });
}

TEST(AsyncCoro, AsyncIoWriteAllWritesFullBuffer) {
    u8 read_data[] { 0 };
    auto stream = MockAsyncIo { read_data, 0 };
    u8 write_data[] { 'm', 'n', 'o' };
    auto write_buf = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(write_data, 3));

    auto result = async::block_on(async::io::write_all(stream, write_buf));

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(stream.written.len(), 3u);
    EXPECT_EQ(stream.written[0], 'm');
    EXPECT_EQ(stream.written[2], 'o');
}

TEST(AsyncCoro, AsyncIoWriteAllReportsWriteZero) {
    auto writer = ZeroAsyncWrite {};
    u8 write_data[] { 'z' };
    auto write_buf = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(write_data, 1));

    auto result = async::block_on(async::io::write_all(writer, write_buf));

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::WriteZero });
}

TEST(AsyncCoro, AsyncIoReadErrorUsesIoResult) {
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker::from_raw_parts(
        &counts,
        rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx     = task::Context { waker };
    auto reader = ErrorAsyncRead {};
    auto buf    = bytes::BytesMut::make();

    auto out = async::io::poll_read(reader, cx, buf);
    ASSERT_TRUE(out.is_ready());
    auto result = rstd::move(out).take();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
}

TEST(AsyncCoro, SleepWakesTask) {
    auto start = time::Instant::now();
    async::block_on(sleep_once());
    EXPECT_GE(start.elapsed().as_millis(), 5u);
}

TEST(AsyncCoro, SpawnedSleepWakesTask) {
    EXPECT_EQ(async::block_on(join_sleeping_child()), 17);
}

TEST(AsyncCoro, MultiThreadRuntimeSpawnedSleepWakesTask) {
    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(2).enable_time().build();
    auto runtime        = runtime_result.unwrap();

    EXPECT_EQ(runtime.block_on(join_spawned_sleeping_child()), 17);
}

TEST(AsyncCoro, MultiThreadRuntimeDropAbortsPendingTasks) {
    int polls = 0;
    auto handle = Option<async::JoinHandle<int>> {};

    {
        auto runtime_result = async::RuntimeBuilder::multi_thread().worker_threads(2).build();
        auto runtime        = runtime_result.unwrap();

        handle = Some(runtime.block_on(spawn_never_ready_child(polls)));
        ASSERT_TRUE(handle.is_some());
        EXPECT_FALSE((*handle).is_finished());
    }

    auto pending = rstd::move(handle).unwrap_unchecked();
    EXPECT_TRUE(pending.is_finished());

    auto joined = async::block_on(rstd::move(pending));
    ASSERT_TRUE(joined.is_err());
    EXPECT_TRUE(rstd::move(joined).unwrap_err().is_aborted());
}

TEST(AsyncCoro, RuntimeBuilderRejectsZeroWorkerThreads) {
    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(0);

    auto runtime = builder.build();

    EXPECT_TRUE(runtime.is_err());
    EXPECT_EQ(rstd::move(runtime).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
}

TEST(AsyncCoro, RuntimeBuilderDefaultsToDisabledDrivers) {
    auto runtime_result = async::RuntimeBuilder::current_thread().build();
    ASSERT_TRUE(runtime_result.is_ok());
    auto runtime = runtime_result.unwrap();

    EXPECT_FALSE(runtime.io_enabled());
    EXPECT_FALSE(runtime.time_enabled());
}

TEST(AsyncCoro, RuntimeBuilderEnableAllSetsDriverCapabilities) {
    auto runtime_result = async::RuntimeBuilder::multi_thread()
                              .worker_threads(2)
                              .enable_all()
                              .build();
    ASSERT_TRUE(runtime_result.is_ok());
    auto runtime = runtime_result.unwrap();

    EXPECT_TRUE(runtime.io_enabled());
    EXPECT_TRUE(runtime.time_enabled());
}

TEST(AsyncCoro, RuntimeBuilderEnableIndividualDriverCapabilities) {
    auto io_runtime_result = async::RuntimeBuilder::current_thread().enable_io().build();
    ASSERT_TRUE(io_runtime_result.is_ok());
    auto io_runtime = io_runtime_result.unwrap();

    EXPECT_TRUE(io_runtime.io_enabled());
    EXPECT_FALSE(io_runtime.time_enabled());

    auto time_runtime_result = async::RuntimeBuilder::current_thread().enable_time().build();
    ASSERT_TRUE(time_runtime_result.is_ok());
    auto time_runtime = time_runtime_result.unwrap();

    EXPECT_FALSE(time_runtime.io_enabled());
    EXPECT_TRUE(time_runtime.time_enabled());
}

TEST(AsyncCoro, RuntimeBuilderWithoutIoRejectsReadiness) {
    auto notify_result = async::Notify::make();
    ASSERT_TRUE(notify_result.is_ok());
    auto notify = rstd::move(notify_result).unwrap_unchecked();

    auto runtime_result = async::RuntimeBuilder::current_thread().build();
    ASSERT_TRUE(runtime_result.is_ok());
    auto runtime = runtime_result.unwrap();

    auto result = runtime.block_on(notify.notified());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::Unsupported });
}

TEST(AsyncCoro, PreludeExportsCoro) {
    EXPECT_EQ(async::block_on(prelude_coro_value()), 13);
}
