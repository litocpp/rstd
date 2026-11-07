module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <rstd/test/gtest.hpp>
#include <atomic>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

export module rstd:async.io_backend_tests;
#if RSTD_OS_LINUX
import :async.io_operation;
import :async.poll;
import :async.reactor;
import :async.runtime;
import :async.spawn;
import rstd.sys.pal.poll.types;
import :thread;
import :time;
import rstd.alloc;

using namespace rstd;
using ::alloc::boxed::Box;

struct ReactorFailureLeaf {
    int* drops;
    int  value;

    ReactorFailureLeaf(int& drops, int value): drops(&drops), value(value) {}
    ReactorFailureLeaf(const ReactorFailureLeaf&) = delete;
    ReactorFailureLeaf(ReactorFailureLeaf&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)), value(other.value) {}
    ~ReactorFailureLeaf() {
        if (drops) ++*drops;
    }
};

struct ReactorFailureContext {
    Box<dyn<error::Error>> inner;
};

namespace rstd
{
template<>
struct Impl<fmt::Display, ReactorFailureLeaf> : ImplBase<ReactorFailureLeaf> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "reactor leaf";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};
template<>
struct Impl<fmt::Debug, ReactorFailureLeaf> : Impl<fmt::Display, ReactorFailureLeaf> {};
template<>
struct Impl<error::Error, ReactorFailureLeaf> : DefaultInImpl<error::Error, ReactorFailureLeaf> {};

template<>
struct Impl<fmt::Display, ReactorFailureContext> : ImplBase<ReactorFailureContext> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "reactor context";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};
template<>
struct Impl<fmt::Debug, ReactorFailureContext> : Impl<fmt::Display, ReactorFailureContext> {};
template<>
struct Impl<error::Error, ReactorFailureContext> : ImplBase<ReactorFailureContext> {
    auto source() const noexcept [[clang::lifetimebound]] -> Option<error::ErrorRef> {
        return Some(self().inner.as_ref());
    }
};
} // namespace rstd

namespace
{

extern const task::RawWakerVTable READINESS_WAKER_VTABLE;

auto readiness_waker_clone(voidp data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(data, rstd::addressof(READINESS_WAKER_VTABLE));
}

void readiness_waker_wake(voidp data) {
    ++*static_cast<int*>(data);
}

void readiness_waker_drop(voidp) {
}

const task::RawWakerVTable READINESS_WAKER_VTABLE {
    &readiness_waker_clone,
    &readiness_waker_wake,
    &readiness_waker_wake,
    &readiness_waker_drop,
};

auto readiness_waker(int& wakes) -> task::Waker {
    return task::Waker::from_raw(readiness_waker_clone(rstd::addressof(wakes)));
}

struct OrderedWake {
    async::RegistrationState* state;
    int*                      count;
    int*                      order;
    bool*                     unlocked;
    int                       value;
};

extern const task::RawWakerVTable ORDERED_WAKER_VTABLE;

auto ordered_waker_clone(voidp data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(data, rstd::addressof(ORDERED_WAKER_VTABLE));
}

void ordered_waker_wake(voidp data) {
    auto& record                    = *static_cast<OrderedWake*>(data);
    auto  fields                    = record.state->fields.try_lock();
    *record.unlocked                = *record.unlocked && fields.is_ok();
    record.order[(*record.count)++] = record.value;
}

const task::RawWakerVTable ORDERED_WAKER_VTABLE {
    &ordered_waker_clone,
    &ordered_waker_wake,
    &ordered_waker_wake,
    &readiness_waker_drop,
};

TEST(RstdAsyncReactor, ConcurrentReadinessWakesWriteThenReadOutsideLock) {
    auto        state = async::RegistrationArc::make(os::fd::INVALID_RAW_FD);
    int         count {}, order[2] {};
    bool        unlocked = true;
    OrderedWake read { state.deref_mut().as_raw_ptr(), &count, order, &unlocked, 1 };
    OrderedWake write { state.deref_mut().as_raw_ptr(), &count, order, &unlocked, 2 };
    {
        auto fields         = state->fields.lock().unwrap();
        fields->read_waker  = Some(task::Waker::from_raw(ordered_waker_clone(&read)));
        fields->write_waker = Some(task::Waker::from_raw(ordered_waker_clone(&write)));
    }
    auto key = async::RegistrationKey { u32(), u32(1) };
    async::handle_registration_event(
        state,
        async::PollEventData::readiness(key, async::Ready::readable() | async::Ready::writable()));
    ASSERT_EQ(count, 2);
    EXPECT_EQ(order[0], 2);
    EXPECT_EQ(order[1], 1);
    EXPECT_TRUE(unlocked);
}

TEST(RstdAsyncReactor, ReadinessTakesOnlyMatchingWaitersBeforeNotifying) {
    auto state = async::RegistrationArc::make(os::fd::INVALID_RAW_FD);
    int  read_wakes {}, write_wakes {};
    {
        auto fields             = state->fields.lock().unwrap();
        fields->read_waker      = Some(readiness_waker(read_wakes));
        fields->write_waker     = Some(readiness_waker(write_wakes));
        fields->read_waiter_id  = usize(1);
        fields->write_waiter_id = usize(2);
    }
    auto key = async::RegistrationKey { u32(), u32(1) };
    async::handle_registration_event(
        state, async::PollEventData::readiness(key, async::Ready::readable()));
    EXPECT_EQ(read_wakes, 1);
    EXPECT_EQ(write_wakes, 0);
    {
        auto fields = state->fields.lock().unwrap();
        EXPECT_TRUE(fields->read_waker.is_none());
        EXPECT_EQ(fields->read_waiter_id, usize());
        EXPECT_TRUE(fields->write_waker.is_some());
        EXPECT_EQ(fields->write_waiter_id, usize(2));
        EXPECT_FALSE(async::registration_interest(*fields).is_readable());
        EXPECT_TRUE(async::registration_interest(*fields).is_writable());
        EXPECT_TRUE(fields->ready.is_readable());
    }
    async::handle_registration_event(
        state, async::PollEventData::readiness(key, async::Ready::writable()));
    EXPECT_EQ(read_wakes, 1);
    EXPECT_EQ(write_wakes, 1);
    {
        auto fields = state->fields.lock().unwrap();
        EXPECT_TRUE(fields->write_waker.is_none());
        EXPECT_EQ(fields->write_waiter_id, usize());
        EXPECT_TRUE(async::registration_interest(*fields).is_empty());
    }
    async::handle_registration_event(
        state, async::PollEventData::readiness(key, async::Ready::writable()));
    EXPECT_EQ(write_wakes, 1);
}

void verify_readiness_close(bool failure) {
    auto state = async::RegistrationArc::make(os::fd::INVALID_RAW_FD);
    int  read_wakes {}, write_wakes {};
    {
        auto fields             = state->fields.lock().unwrap();
        fields->read_waker      = Some(readiness_waker(read_wakes));
        fields->write_waker     = Some(readiness_waker(write_wakes));
        fields->read_waiter_id  = usize(1);
        fields->write_waiter_id = usize(2);
    }
    if (failure)
        async::fail_registration(state,
                                 io::Error::from_kind(io::ErrorKind { io::ErrorKind::BrokenPipe }));
    else
        async::begin_registration_deregister(state);
    EXPECT_EQ(read_wakes, 1);
    EXPECT_EQ(write_wakes, 1);
    {
        auto fields = state->fields.lock().unwrap();
        EXPECT_TRUE(fields->closed);
        EXPECT_TRUE(fields->read_waker.is_none());
        EXPECT_TRUE(fields->write_waker.is_none());
        EXPECT_EQ(fields->read_waiter_id, usize());
        EXPECT_EQ(fields->write_waiter_id, usize());
        EXPECT_TRUE(async::registration_interest(*fields).is_empty());
    }
    async::begin_registration_deregister(state);
    EXPECT_EQ(read_wakes, 1);
    EXPECT_EQ(write_wakes, 1);
}

TEST(RstdAsyncReactor, FailureTakesWaitersBeforeNotifying) {
    verify_readiness_close(true);
}

TEST(RstdAsyncReactor, DeregisterTakesWaitersBeforeNotifying) {
    verify_readiness_close(false);
}

void expect_reactor_custom_failure(const io::Error& failure, int& drops) {
    EXPECT_EQ(failure.kind(), io::ErrorKind { io::ErrorKind::BrokenPipe });
    auto context = error::downcast_ref<ReactorFailureContext>(failure.get_ref().unwrap());
    ASSERT_TRUE(context.is_some());
    auto source = dyn<error::Error>::from_ref(failure)->source();
    ASSERT_TRUE(source.is_some());
    auto leaf = error::downcast_ref<ReactorFailureLeaf>(source.unwrap());
    ASSERT_TRUE(leaf.is_some());
    EXPECT_EQ((*leaf)->value, 41);
    EXPECT_EQ((*leaf)->drops, &drops);
    EXPECT_EQ(context.unwrap()->inner.as_ref().as_raw_ptr(), leaf.unwrap().as_raw_ptr());
    EXPECT_EQ(drops, 0);
}

TEST(RstdAsyncReactor, ReadinessObserversLeaveCustomFailureForOwner) {
    int               drops {};
    Option<io::Error> owned;
    {
        auto state = async::RegistrationArc::make(os::fd::INVALID_RAW_FD);
        async::fail_registration(
            state,
            io::Error::with_source(
                io::ErrorKind { io::ErrorKind::BrokenPipe },
                Box<dyn<error::Error>>::make(ReactorFailureContext {
                    Box<dyn<error::Error>>::make(ReactorFailureLeaf(drops, 41)) })));
        auto first_ready = async::try_registration_readiness(state, async::Interest::readable());
        ASSERT_TRUE(first_ready.is_some());
        auto first_result = first_ready.unwrap();
        ASSERT_TRUE(first_result.is_err());
        EXPECT_EQ(first_result.as_ref().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
        auto second_ready = async::try_registration_readiness(state, async::Interest::writable());
        ASSERT_TRUE(second_ready.is_some());
        auto second_result = second_ready.unwrap();
        ASSERT_TRUE(second_result.is_err());
        EXPECT_EQ(second_result.as_ref().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
        auto& waker = task::Waker::noop();
        auto  cx    = task::Context { waker };
        usize waiter;
        auto  readiness =
            async::poll_registration_readiness(state, cx, async::Interest::readable(), waiter);
        ASSERT_TRUE(readiness.is_ready());
        auto result = rstd::move(readiness).take();
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.as_ref().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
        async::begin_registration_deregister(state);
        owned = async::take_registration_error(state);
        ASSERT_TRUE(owned.is_some());
        EXPECT_TRUE(async::take_registration_error(state).is_none());
        expect_reactor_custom_failure(*owned, drops);
    }
    expect_reactor_custom_failure(*owned, drops);
    owned = None();
    EXPECT_EQ(drops, 1);
}

void verify_reactor_simple_failure(io::Error failure) {
    auto kind    = failure.kind();
    auto code    = failure.raw_os_error();
    auto message = failure.static_message();
    auto state   = async::RegistrationArc::make(os::fd::INVALID_RAW_FD);
    async::fail_registration(state, rstd::move(failure));
    auto expect_failure = [&](const io::Error& reported) {
        EXPECT_EQ(reported.kind(), kind);
        auto reported_code = reported.raw_os_error();
        EXPECT_EQ(reported_code.is_some(), code.is_some());
        if (code.is_some() && reported_code.is_some()) EXPECT_EQ(*reported_code, *code);
        EXPECT_EQ(reported.static_message(), message);
        EXPECT_TRUE(reported.get_ref().is_none());
        EXPECT_TRUE(dyn<error::Error>::from_ref(reported)->source().is_none());
    };
    for (auto index = 0; index < 2; ++index) {
        auto ready = async::try_registration_readiness(state, async::Interest::readable());
        ASSERT_TRUE(ready.is_some());
        auto result = ready.unwrap();
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.as_ref().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
    }
    auto& waker = task::Waker::noop();
    auto  cx    = task::Context { waker };
    usize waiter;
    auto ready = async::poll_registration_readiness(state, cx, async::Interest::writable(), waiter);
    ASSERT_TRUE(ready.is_ready());
    auto result = rstd::move(ready).take();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.as_ref().unwrap_err().kind(), io::ErrorKind { io::ErrorKind::NotConnected });
    auto owned = async::take_registration_error(state);
    ASSERT_TRUE(owned.is_some());
    expect_failure(*owned);
    EXPECT_TRUE(async::take_registration_error(state).is_none());
}

TEST(RstdAsyncReactor, OwnerTakesOriginalFailureRepresentationOnce) {
    static constexpr char message[] = "reactor message";
    io::Error             failures[] {
        io::Error::from_raw_os_error(i32(32)),
        io::Error::make_const(io::ErrorKind { io::ErrorKind::BrokenPipe }, message),
        io::Error::from_kind(io::ErrorKind { io::ErrorKind::TimedOut }),
    };
    for (auto& failure : failures) verify_reactor_simple_failure(rstd::move(failure));
}

TEST(RstdAsyncReactor, TimerFailureMovesOnceAndLateEventsCannotReplaceIt) {
    int  drops {};
    auto state = async::TimerArc::make(
        WorkerHandle::make(RuntimeWorkerId {}), async::TimerKey {}, task::Waker::noop().clone());
    async::handle_timer_event(
        state,
        async::PollEventData::backend_error(
            async::TimerKey {},
            io::Error::with_source(
                io::ErrorKind { io::ErrorKind::BrokenPipe },
                Box<dyn<error::Error>>::make(ReactorFailureContext {
                    Box<dyn<error::Error>>::make(ReactorFailureLeaf(drops, 41)) }))));
    async::handle_timer_event(
        state,
        async::PollEventData::backend_error(
            async::TimerKey {}, io::Error::from_kind(io::ErrorKind { io::ErrorKind::Other })));
    {
        auto owned = async::update_timer_waker(state, task::Waker::noop().clone());
        ASSERT_TRUE(owned.is_err());
        expect_reactor_custom_failure(owned.as_ref().unwrap_err(), drops);
        auto again = async::update_timer_waker(state, task::Waker::noop().clone());
        ASSERT_TRUE(again.is_err());
        EXPECT_EQ(again.as_ref().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
    }
    EXPECT_EQ(drops, 1);
}

struct CapturedEvent {
    Option<async::PollEventData> value {};
};

extern const async::RawPollEventOwnerVTable CAPTURE_OWNER_VTABLE;

auto capture_owner_clone(voidp data) -> async::RawPollEventOwner {
    return async::RawPollEventOwner::from_raw_parts(data, rstd::addressof(CAPTURE_OWNER_VTABLE));
}

void capture_owner_dispatch(voidp data, async::PollEventData event) {
    static_cast<CapturedEvent*>(data)->value = Some(rstd::move(event));
}

void capture_owner_drop(voidp) {
}

const async::RawPollEventOwnerVTable CAPTURE_OWNER_VTABLE {
    &capture_owner_clone,
    &capture_owner_dispatch,
    &capture_owner_drop,
};

auto capture_owner(CapturedEvent& capture) -> async::PollEventOwner {
    return async::PollEventOwner::from_raw(async::RawPollEventOwner::from_raw_parts(
        rstd::addressof(capture), rstd::addressof(CAPTURE_OWNER_VTABLE)));
}

void dispatch_all(async::PollBatch batch);

TEST(RstdAsyncIoBackend, HeldPollBatchSurvivesLargeShutdownBatch) {
    auto initialized = async::Poll::init(async::IoBackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(initialized.is_ok());
    auto          poll = rstd::move(initialized).unwrap_unchecked();
    CapturedEvent captures[260] {};
    for (rstd::uint32_t i = 0; i < 130; ++i) {
        auto key    = async::TimerKey { u32(i), u32(1) };
        auto result = async::Poll::apply(
            poll.state,
            async::PollCommand::arm_timer(key, time::Instant::now(), capture_owner(captures[i])));
        ASSERT_EQ(result.status(), async::PollApplyStatus::Accepted);
    }
    auto held = async::Poll::poll(poll.state, async::PollTimeout::Immediate);
    ASSERT_TRUE(held.is_ok());
    ASSERT_EQ(held->len(), usize(130));
    for (rstd::uint32_t i = 130; i < 260; ++i) {
        auto key    = async::TimerKey { u32(i), u32(1) };
        auto result = async::Poll::apply(
            poll.state,
            async::PollCommand::arm_timer(key,
                                          time::Instant::now() + time::Duration::from_secs(u64(60)),
                                          capture_owner(captures[i])));
        ASSERT_EQ(result.status(), async::PollApplyStatus::Accepted);
    }
    auto closing = async::Poll::shutdown(poll.state);
    ASSERT_EQ(closing.len(), usize(130));
    dispatch_all(rstd::move(closing));
    dispatch_all(rstd::move(held).unwrap_unchecked());
    for (rstd::uint32_t i = 0; i < 260; ++i) {
        ASSERT_TRUE(captures[i].value.is_some());
        EXPECT_EQ(captures[i].value->timer_key(), (async::TimerKey { u32(i), u32(1) }));
        EXPECT_EQ(captures[i].value->kind(),
                  i < 130 ? async::PollEventKind::Timer : async::PollEventKind::BackendError);
    }
}

auto reserve_operation(async::PollState& state, async::PollEventOwner owner, u32 fresh_slot = u32())
    -> async::OperationKey {
    if (async::Poll::has_recycled_operation(state)) {
        return async::Poll::reserve_recycled_operation(state, rstd::move(owner));
    }
    auto key = async::OperationKey { fresh_slot, u32(1) };
    EXPECT_TRUE(async::Poll::reserve_fresh_operation(state, key, rstd::move(owner)));
    return key;
}

void dispatch_all(async::PollBatch batch) {
    while (! batch.is_empty()) {
        rstd::move(batch.pop_front()).unwrap_unchecked().dispatch();
    }
}

auto read_one(os::fd::RawFd fd) -> async::coro<io::Result<async::IoCompletion>> {
    auto source = async::CompletionSource::socket(os::fd::BorrowedFd::borrow_raw(fd));
    co_return co_await async::IoOperation::read(source, usize(1));
}

auto read_file_one(os::fd::RawFd fd) -> async::coro<io::Result<async::IoCompletion>> {
    auto source = async::CompletionSource::file(os::fd::BorrowedFd::borrow_raw(fd));
    co_return co_await async::IoOperation::read(source, usize(1));
}

void verify_runtime_file_backend(async::IoBackendPreference preference,
                                 bool                       expect_blocking_thread) {
    auto builder = async::RuntimeBuilder::current_thread();
    builder.enable_io();
    async::RuntimeBuilderConfigAccess::set_io_backend(builder, preference);
    auto built = builder.build();
    if (built.is_err()) {
        GTEST_SKIP() << "requested runtime backend is unavailable";
    }
    auto runtime = rstd::move(built).unwrap_unchecked();
    EXPECT_EQ(async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime), usize());

    auto raw_fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(raw_fd, 0);
    auto file   = os::fd::OwnedFd::from_raw_fd(raw_fd);
    auto result = runtime.block_on(read_file_one(file.as_raw_fd()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime) != usize(),
              expect_blocking_thread);
}

void verify_runtime_backend(async::IoBackendPreference preference) {
    int sockets[2] = { -1, -1 };
    ASSERT_EQ(::socketpair(AF_UNIX, ::SOCK_STREAM | ::SOCK_NONBLOCK | ::SOCK_CLOEXEC, 0, sockets),
              0);
    auto reader = os::fd::OwnedFd::from_raw_fd(sockets[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(sockets[1]);
    ASSERT_EQ(::send(writer.as_raw_fd(), "w", 1, ::MSG_NOSIGNAL), 1);

    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(usize(1));
    builder.enable_io();
    async::RuntimeBuilderConfigAccess::set_io_backend(builder, preference);
    auto built = builder.build();
    if (built.is_err()) {
        GTEST_SKIP() << "requested runtime backend is unavailable";
    }
    auto runtime = rstd::move(built).unwrap_unchecked();
    auto result  = runtime.block_on(read_one(reader.as_raw_fd()));

    ASSERT_TRUE(result.is_ok());
    auto completion = rstd::move(result).unwrap_unchecked();
    EXPECT_EQ(completion.transferred(), usize(1));
    EXPECT_EQ(completion.data()[usize()], u8('w'));
}

void verify_socket_completion(async::IoBackendPreference preference, bool expect_file) {
    auto initialized = async::Poll::init(preference);
    if (initialized.is_err()) {
        GTEST_SKIP() << "requested backend is unavailable";
    }
    auto poll         = rstd::move(initialized).unwrap_unchecked();
    auto capabilities = async::Poll::capabilities(poll.state);
    ASSERT_TRUE(capabilities.contains(async::PollCapability::SocketCompletion));
    EXPECT_EQ(capabilities.contains(async::PollCapability::FileCompletion), expect_file);

    int sockets[2] = { -1, -1 };
    ASSERT_EQ(::socketpair(AF_UNIX, ::SOCK_STREAM | ::SOCK_NONBLOCK | ::SOCK_CLOEXEC, 0, sockets),
              0);
    auto reader = os::fd::OwnedFd::from_raw_fd(sockets[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(sockets[1]);

    char          byte {};
    CapturedEvent capture {};
    auto operation = async::PollOperation::read_socket(reader.as_raw_fd(), &byte, usize(1));
    operation.set_source_key(async::SourceKey { u32(), u32(1) });
    auto operation_key = reserve_operation(poll.state, capture_owner(capture));
    auto applied       = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(operation_key, rstd::move(operation)));
    ASSERT_EQ(applied.status(), async::PollApplyStatus::Accepted);

    auto pending = async::Poll::poll(poll.state, async::PollTimeout::Immediate);
    ASSERT_TRUE(pending.is_ok());
    dispatch_all(rstd::move(pending).unwrap_unchecked());
    ASSERT_TRUE(capture.value.is_none());
    ASSERT_EQ(::send(writer.as_raw_fd(), "b", 1, ::MSG_NOSIGNAL), 1);

    auto completed = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(completed.is_ok());
    dispatch_all(rstd::move(completed).unwrap_unchecked());
    ASSERT_TRUE(capture.value.is_some());
    ASSERT_EQ(capture.value->kind(), async::PollEventKind::Completion);
    auto record = capture.value->take_completion();
    ASSERT_FALSE(record.is_error());
    EXPECT_EQ(record.result(), isize(1));
    EXPECT_EQ(byte, 'b');

    auto released = async::Poll::apply(poll.state,
                                       async::PollCommand::release_completion_source(
                                           async::SourceKey { u32(), u32(1) }, reader.as_raw_fd()));
    EXPECT_EQ(released.status(), async::PollApplyStatus::Accepted);
    ASSERT_TRUE(released.has_event());
    released.take_event().dispatch();

    ASSERT_EQ(::send(writer.as_raw_fd(), "c", 1, ::MSG_NOSIGNAL), 1);
    CapturedEvent second_capture {};
    auto second_operation = async::PollOperation::read_socket(reader.as_raw_fd(), &byte, usize(1));
    second_operation.set_source_key(async::SourceKey { u32(1), u32(1) });
    auto second_key = reserve_operation(poll.state, capture_owner(second_capture), u32(1));
    EXPECT_EQ(second_key.slot, operation_key.slot);
    EXPECT_NE(second_key.generation, operation_key.generation);
    auto stale_cancel =
        async::Poll::apply(poll.state, async::PollCommand::cancel_operation(operation_key));
    ASSERT_EQ(stale_cancel.status(), async::PollApplyStatus::Accepted);
    auto second_applied = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(second_key, rstd::move(second_operation)));
    ASSERT_EQ(second_applied.status(), async::PollApplyStatus::Accepted);
    auto second_completed = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(second_completed.is_ok());
    dispatch_all(rstd::move(second_completed).unwrap_unchecked());
    ASSERT_TRUE(second_capture.value.is_some());
    EXPECT_EQ(byte, 'c');
    dispatch_all(async::Poll::shutdown(poll.state));
}

void verify_cancellation(async::IoBackendPreference preference) {
    auto initialized = async::Poll::init(preference);
    if (initialized.is_err()) {
        GTEST_SKIP() << "requested backend is unavailable";
    }
    auto poll = rstd::move(initialized).unwrap_unchecked();

    int sockets[2] = { -1, -1 };
    ASSERT_EQ(::socketpair(AF_UNIX, ::SOCK_STREAM | ::SOCK_NONBLOCK | ::SOCK_CLOEXEC, 0, sockets),
              0);
    auto reader = os::fd::OwnedFd::from_raw_fd(sockets[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(sockets[1]);

    char          byte {};
    CapturedEvent capture {};
    auto operation = async::PollOperation::read_socket(reader.as_raw_fd(), &byte, usize(1));
    operation.set_source_key(async::SourceKey { u32(), u32(1) });
    auto operation_key = reserve_operation(poll.state, capture_owner(capture));
    auto submitted     = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(operation_key, rstd::move(operation)));
    ASSERT_EQ(submitted.status(), async::PollApplyStatus::Accepted);

    auto canceled =
        async::Poll::apply(poll.state, async::PollCommand::cancel_operation(operation_key));
    ASSERT_EQ(canceled.status(), async::PollApplyStatus::Accepted);

    auto completed = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(completed.is_ok());
    dispatch_all(rstd::move(completed).unwrap_unchecked());
    ASSERT_TRUE(capture.value.is_some());
    ASSERT_EQ(capture.value->kind(), async::PollEventKind::Completion);
    EXPECT_TRUE(capture.value->take_completion().is_error());

    auto released = async::Poll::apply(poll.state,
                                       async::PollCommand::release_completion_source(
                                           async::SourceKey { u32(), u32(1) }, reader.as_raw_fd()));
    EXPECT_EQ(released.status(), async::PollApplyStatus::Accepted);
    dispatch_all(async::Poll::shutdown(poll.state));
}

void verify_cancel_before_submit(async::IoBackendPreference preference) {
    auto initialized = async::Poll::init(preference);
    if (initialized.is_err()) {
        GTEST_SKIP() << "requested backend is unavailable";
    }
    auto poll = rstd::move(initialized).unwrap_unchecked();

    int sockets[2] = { -1, -1 };
    ASSERT_EQ(::socketpair(AF_UNIX, ::SOCK_STREAM | ::SOCK_NONBLOCK | ::SOCK_CLOEXEC, 0, sockets),
              0);
    auto reader = os::fd::OwnedFd::from_raw_fd(sockets[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(sockets[1]);
    (void)writer;

    char          byte {};
    CapturedEvent capture {};
    auto operation = async::PollOperation::read_socket(reader.as_raw_fd(), &byte, usize(1));
    operation.set_source_key(async::SourceKey { u32(), u32(1) });
    auto operation_key = reserve_operation(poll.state, capture_owner(capture));

    auto canceled =
        async::Poll::apply(poll.state, async::PollCommand::cancel_operation(operation_key));
    ASSERT_EQ(canceled.status(), async::PollApplyStatus::Accepted);
    auto submitted = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(operation_key, rstd::move(operation)));
    ASSERT_EQ(submitted.status(), async::PollApplyStatus::Accepted);

    auto completed = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(completed.is_ok());
    dispatch_all(rstd::move(completed).unwrap_unchecked());
    ASSERT_TRUE(capture.value.is_some());
    ASSERT_EQ(capture.value->kind(), async::PollEventKind::Completion);
    EXPECT_TRUE(capture.value->take_completion().is_error());

    auto released = async::Poll::apply(poll.state,
                                       async::PollCommand::release_completion_source(
                                           async::SourceKey { u32(), u32(1) }, reader.as_raw_fd()));
    EXPECT_EQ(released.status(), async::PollApplyStatus::Accepted);
    dispatch_all(async::Poll::shutdown(poll.state));
}

TEST(RstdAsyncIoBackend, NativeCompletionContract) {
    verify_socket_completion(async::IoBackendPreference::NativeCompletionRequired, true);
}

TEST(RstdAsyncIoBackend, ReadinessEmulationContract) {
    verify_socket_completion(async::IoBackendPreference::ReadinessEmulationRequired, false);
}

TEST(RstdAsyncIoBackend, RuntimeUsesNativeCompletionPreference) {
    verify_runtime_backend(async::IoBackendPreference::NativeCompletionRequired);
}

TEST(RstdAsyncIoBackend, RuntimeUsesReadinessEmulationPreference) {
    verify_runtime_backend(async::IoBackendPreference::ReadinessEmulationRequired);
}

TEST(RstdAsyncIoBackend, NativeFileCompletionKeepsBlockingPoolLazy) {
    verify_runtime_file_backend(async::IoBackendPreference::NativeCompletionRequired, false);
}

TEST(RstdAsyncIoBackend, ReadinessFileCompletionStartsSharedBlockingPool) {
    verify_runtime_file_backend(async::IoBackendPreference::ReadinessEmulationRequired, true);
}

TEST(RstdAsyncBlockingPool, GrowsToConfiguredLimit) {
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(2)).build().unwrap();
    auto entered = std::atomic<size_t> { 0 };
    auto release = std::atomic<bool> { false };
    auto jobs    = Vec<async::JoinHandle<void>>::make();
    for (auto index = usize(); index < usize(4); ++index) {
        auto submitted = runtime.spawn_blocking([&] {
            entered.fetch_add(1, std::memory_order_release);
            while (! release.load(std::memory_order_acquire)) hint::spin_loop();
        });
        ASSERT_TRUE(submitted.is_ok());
        jobs.push(rstd::move(submitted).unwrap_unchecked());
    }

    while (entered.load(std::memory_order_acquire) != 2) hint::spin_loop();
    EXPECT_EQ(async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime), usize(2));
    release.store(true, std::memory_order_release);
    while (! jobs.is_empty()) {
        EXPECT_TRUE(runtime.block_on(rstd::move(jobs.pop()).unwrap_unchecked()).is_ok());
    }
}

TEST(RstdAsyncBlockingPool, IdleWorkerExpiresAndRestarts) {
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    async::RuntimeBuilderConfigAccess::set_blocking_keep_alive(runtime,
                                                               time::Duration::from_millis(u64(2)));
    auto first = runtime.spawn_blocking([] {
        return thread::current().id();
    });
    ASSERT_TRUE(first.is_ok());
    auto first_result = runtime.block_on(rstd::move(first).unwrap_unchecked());
    ASSERT_TRUE(first_result.is_ok());

    auto started = time::Instant::now();
    while (async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime) != usize() &&
           started.elapsed() < time::Duration::from_secs(u64(1))) {
        thread::yield_now();
    }
    ASSERT_EQ(async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime), usize());

    auto second = runtime.spawn_blocking([] {
        return thread::current().id();
    });
    ASSERT_TRUE(second.is_ok());
    auto second_result = runtime.block_on(rstd::move(second).unwrap_unchecked());
    ASSERT_TRUE(second_result.is_ok());
    EXPECT_NE(rstd::move(first_result).unwrap_unchecked(),
              rstd::move(second_result).unwrap_unchecked());
}

TEST(RstdAsyncBlockingPool, FirstWorkerSpawnFailureRejectsSubmission) {
    auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
    async::RuntimeBuilderConfigAccess::fail_next_blocking_spawn(runtime);
    auto ran       = false;
    auto submitted = runtime.spawn_blocking([&] {
        ran = true;
    });
    ASSERT_TRUE(submitted.is_err());
    EXPECT_EQ(rstd::move(submitted).unwrap_err_unchecked().kind(),
              io::ErrorKind { io::ErrorKind::Other });
    EXPECT_FALSE(ran);
    EXPECT_EQ(async::RuntimeBuilderConfigAccess::blocking_thread_count(runtime), usize());

    auto rejected = runtime.spawn_blocking([] {
    });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rstd::move(rejected).unwrap_err_unchecked().kind(),
              io::ErrorKind { io::ErrorKind::NotConnected });
}

TEST(RstdAsyncIoBackend, NativeCancellationContract) {
    verify_cancellation(async::IoBackendPreference::NativeCompletionRequired);
}

TEST(RstdAsyncIoBackend, ReadinessEmulationCancellationContract) {
    verify_cancellation(async::IoBackendPreference::ReadinessEmulationRequired);
}

TEST(RstdAsyncIoBackend, NativeCancelBeforeSubmitContract) {
    verify_cancel_before_submit(async::IoBackendPreference::NativeCompletionRequired);
}

TEST(RstdAsyncIoBackend, ReadinessEmulationCancelBeforeSubmitContract) {
    verify_cancel_before_submit(async::IoBackendPreference::ReadinessEmulationRequired);
}

TEST(RstdAsyncIoBackend, FreshOperationSlotsAcceptOutOfOrderRemoteKeys) {
    auto initialized = async::Poll::init();
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap_unchecked();

    CapturedEvent first {};
    CapturedEvent second {};
    auto          key0 = async::OperationKey { u32(), u32(1) };
    auto          key1 = async::OperationKey { u32(1), u32(1) };
    EXPECT_TRUE(async::Poll::reserve_fresh_operation(poll.state, key1, capture_owner(second)));
    EXPECT_TRUE(async::Poll::reserve_fresh_operation(poll.state, key0, capture_owner(first)));
    EXPECT_TRUE(async::Poll::abandon_operation(poll.state, key0).is_some());
    EXPECT_TRUE(async::Poll::abandon_operation(poll.state, key1).is_some());

    dispatch_all(async::Poll::shutdown(poll.state));
}

TEST(RstdAsyncIoBackend, OperationSlotFreeListReusesGenerationAndRetiresMaximum) {
    auto initialized = async::Poll::init();
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap_unchecked();

    CapturedEvent first {};
    CapturedEvent second {};
    auto          key0 = async::OperationKey { u32(), u32(1) };
    auto          key1 = async::OperationKey { u32(1), u32(1) };
    ASSERT_TRUE(async::Poll::reserve_fresh_operation(poll.state, key0, capture_owner(first)));
    ASSERT_TRUE(async::Poll::reserve_fresh_operation(poll.state, key1, capture_owner(second)));
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, key0).is_some());
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, key1).is_some());

    auto reused1 = async::Poll::reserve_recycled_operation(poll.state, capture_owner(second));
    auto reused0 = async::Poll::reserve_recycled_operation(poll.state, capture_owner(first));
    EXPECT_EQ(reused1, (async::OperationKey { u32(1), u32(2) }));
    EXPECT_EQ(reused0, (async::OperationKey { u32(), u32(2) }));
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, reused1).is_some());
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, reused0).is_some());

    auto maximum = async::OperationKey { u32(2), u32::MAX };
    ASSERT_TRUE(async::Poll::reserve_fresh_operation(poll.state, maximum, capture_owner(first)));
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, maximum).is_some());
    auto next0 = async::Poll::reserve_recycled_operation(poll.state, capture_owner(first));
    auto next1 = async::Poll::reserve_recycled_operation(poll.state, capture_owner(second));
    EXPECT_NE(next0.slot, maximum.slot);
    EXPECT_NE(next1.slot, maximum.slot);
    EXPECT_FALSE(async::Poll::has_recycled_operation(poll.state));
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, next0).is_some());
    ASSERT_TRUE(async::Poll::abandon_operation(poll.state, next1).is_some());

    dispatch_all(async::Poll::shutdown(poll.state));
}

TEST(RstdAsyncIoBackend, PalBatchMovesOnlyInitializedEvents) {
    namespace pal = rstd::sys::pal::poll;
    auto batch    = pal::Batch {};
    for (rstd::size_t i = 0; i < pal::Batch::capacity(); ++i) {
        batch.push(pal::Event::completion(u64(i + 1), isize(i), u32()));
    }
    ASSERT_EQ(batch.len(), usize(pal::Batch::capacity()));

    auto moved = rstd::move(batch);
    EXPECT_TRUE(batch.is_empty());
    ASSERT_EQ(moved.len(), usize(pal::Batch::capacity()));
    for (rstd::size_t i = 0; i < pal::Batch::capacity(); ++i) {
        EXPECT_EQ(moved[usize(i)].kind(), pal::EventKind::Completion);
        EXPECT_EQ(moved[usize(i)].operation_key(), u64(i + 1));
    }
}

TEST(RstdAsyncIoBackend, ReadinessEmulationSharesExplicitReadinessSource) {
    auto initialized = async::Poll::init(async::IoBackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap_unchecked();

    int sockets[2] = { -1, -1 };
    ASSERT_EQ(::socketpair(AF_UNIX, ::SOCK_STREAM | ::SOCK_NONBLOCK | ::SOCK_CLOEXEC, 0, sockets),
              0);
    auto reader = os::fd::OwnedFd::from_raw_fd(sockets[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(sockets[1]);

    auto          readiness_key = async::RegistrationKey { u32(), u32(1) };
    CapturedEvent readiness_capture {};
    auto          registered =
        async::Poll::apply(poll.state,
                           async::PollCommand::register_source(readiness_key,
                                                               reader.as_raw_fd(),
                                                               async::Interest::readable(),
                                                               capture_owner(readiness_capture)));
    ASSERT_EQ(registered.status(), async::PollApplyStatus::Accepted);

    char          byte {};
    CapturedEvent completion_capture {};
    auto operation = async::PollOperation::read_socket(reader.as_raw_fd(), &byte, usize(1));
    operation.set_source_key(async::SourceKey { u32(), u32(1) });
    auto operation_key = reserve_operation(poll.state, capture_owner(completion_capture));
    auto submitted     = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(operation_key, rstd::move(operation)));
    ASSERT_EQ(submitted.status(), async::PollApplyStatus::Accepted);
    ASSERT_EQ(::send(writer.as_raw_fd(), "r", 1, ::MSG_NOSIGNAL), 1);

    auto completed = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(completed.is_ok());
    dispatch_all(rstd::move(completed).unwrap_unchecked());
    ASSERT_TRUE(readiness_capture.value.is_some());
    EXPECT_EQ(readiness_capture.value->kind(), async::PollEventKind::Readiness);
    ASSERT_TRUE(completion_capture.value.is_some());
    EXPECT_EQ(completion_capture.value->kind(), async::PollEventKind::Completion);
    EXPECT_EQ(byte, 'r');

    auto deregistered = async::Poll::apply(
        poll.state, async::PollCommand::deregister_source(readiness_key, async::PollEventOwner {}));
    ASSERT_EQ(deregistered.status(), async::PollApplyStatus::Accepted);
    ASSERT_TRUE(deregistered.has_event());
    deregistered.take_event().dispatch();
    auto released = async::Poll::apply(poll.state,
                                       async::PollCommand::release_completion_source(
                                           async::SourceKey { u32(), u32(1) }, reader.as_raw_fd()));
    EXPECT_EQ(released.status(), async::PollApplyStatus::Accepted);
    dispatch_all(async::Poll::shutdown(poll.state));
}

void verify_closed_readiness_reuse(bool reuse_before_deregister) {
    auto initialized = async::Poll::init(async::IoBackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap();
    int  fds[2];
    ASSERT_EQ(::pipe2(fds, O_NONBLOCK | O_CLOEXEC), 0);
    auto          old_fd = fds[0];
    auto          reader = Some(os::fd::OwnedFd::from_raw_fd(fds[0]));
    auto          writer = os::fd::OwnedFd::from_raw_fd(fds[1]);
    auto          key    = async::RegistrationKey { u32(), u32(1) };
    CapturedEvent capture {};
    auto          registered =
        async::Poll::apply(poll.state,
                           async::PollCommand::register_source(
                               key, old_fd, async::Interest::readable(), capture_owner(capture)));
    ASSERT_EQ(registered.status(), async::PollApplyStatus::Accepted);
    reader                  = None();
    auto replacement_reader = None<os::fd::OwnedFd>();
    auto replacement_writer = None<os::fd::OwnedFd>();
    auto replace            = [&] {
        if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) return false;
        replacement_reader = Some(os::fd::OwnedFd::from_raw_fd(fds[0]));
        replacement_writer = Some(os::fd::OwnedFd::from_raw_fd(fds[1]));
        return fds[0] == old_fd;
    };
    if (reuse_before_deregister) ASSERT_TRUE(replace());
    auto removed = async::Poll::apply(
        poll.state, async::PollCommand::deregister_source(key, capture_owner(capture)));
    ASSERT_EQ(removed.status(), async::PollApplyStatus::Accepted);
    ASSERT_TRUE(removed.has_event());
    removed.take_event().dispatch();
    ASSERT_TRUE(capture.value.is_some());
    EXPECT_FALSE(capture.value->has_backend_error());
    if (! reuse_before_deregister) ASSERT_TRUE(replace());
    capture.value    = None();
    auto replacement = async::Poll::apply(
        poll.state,
        async::PollCommand::register_source(async::RegistrationKey { u32(), u32(2) },
                                            old_fd,
                                            async::Interest::readable(),
                                            capture_owner(capture)));
    ASSERT_EQ(replacement.status(), async::PollApplyStatus::Accepted);
    ASSERT_EQ(::write(replacement_writer->as_raw_fd(), "r", 1), 1);
    auto ready = async::Poll::poll(poll.state, async::PollTimeout::Infinite);
    ASSERT_TRUE(ready.is_ok());
    dispatch_all(rstd::move(ready).unwrap());
    ASSERT_TRUE(capture.value.is_some());
    EXPECT_EQ(capture.value->kind(), async::PollEventKind::Readiness);
    dispatch_all(async::Poll::shutdown(poll.state));
}

TEST(RstdAsyncIoBackend, ClosedReadinessDescriptorCanBeReused) {
    verify_closed_readiness_reuse(false);
}

TEST(RstdAsyncIoBackend, ReusedDescriptorBeforeReadinessDeregister) {
    verify_closed_readiness_reuse(true);
}

TEST(RstdAsyncIoBackend, ReadinessEmulationRejectsFileOperations) {
    auto initialized = async::Poll::init(async::IoBackendPreference::ReadinessEmulationRequired);
    ASSERT_TRUE(initialized.is_ok());
    auto poll = rstd::move(initialized).unwrap_unchecked();

    int fds[2] = { -1, -1 };
    ASSERT_EQ(::pipe2(fds, O_NONBLOCK | O_CLOEXEC), 0);
    auto reader = os::fd::OwnedFd::from_raw_fd(fds[0]);
    auto writer = os::fd::OwnedFd::from_raw_fd(fds[1]);
    char byte {};
    auto operation = async::PollOperation::read(reader.as_raw_fd(), &byte, usize(1));
    operation.set_source_key(async::SourceKey { u32(), u32(1) });
    CapturedEvent capture {};
    auto          operation_key = reserve_operation(poll.state, capture_owner(capture));
    auto          applied       = async::Poll::apply(
        poll.state, async::PollCommand::submit_operation(operation_key, rstd::move(operation)));

    EXPECT_EQ(applied.status(), async::PollApplyStatus::Accepted);
    ASSERT_TRUE(applied.has_event());
    applied.take_event().dispatch();
    ASSERT_TRUE(capture.value.is_some());
    EXPECT_EQ(capture.value->kind(), async::PollEventKind::BackendError);
    dispatch_all(async::Poll::shutdown(poll.state));
}

TEST(RstdAsyncIoBackend, ReadCompletionRejectsMoreThanRequested) {
    auto source = async::CompletionSource::file(os::fd::BorrowedFd::borrow_raw(0));
    auto state  = async::IoOperationState(rstd::move(source), usize(3));
    auto result = state.finish(async::PollCompletion::success(isize(4)));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(state.payload.as<async::PollOperationKind::Read>().buffer.len(), usize());
}

} // namespace
#endif
