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
import :async.runtime;
import :async.spawn;
import :sys.pal.poll.types;
import :thread;
import :time;
import rstd.alloc;

using namespace rstd;

namespace
{

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

} // namespace
#endif
