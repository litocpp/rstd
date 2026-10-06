#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

import rstd;
import rstd.test;

using namespace rstd;
namespace sig = rstd::signal;

static const char*      executable;
static int              expected_descriptors { -1 };
static std::atomic<int> fail_install {};
static std::atomic<int> native_changes[NSIG] {};

// Interpose the public libc entry point to inject installation failures and count changes.
extern "C" int sigaction(int number, const struct sigaction* action, struct sigaction* old)
#if ! RSTD_OS_MACOS
    noexcept
#endif
{
    using Function   = int (*)(int, const struct sigaction*, struct sigaction*);
    static auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "sigaction"));
    if (! real) ::_exit(122);
    if (action && number > 0 && number < NSIG) {
        ++native_changes[number];
        if (number == fail_install.load()) {
            errno = EIO;
            return -1;
        }
    }
    return real(number, action, old);
}

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (! (condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (false)

static void custom_handler(int) {
}

static void disposition(int number, void (*handler)(int)) {
    struct sigaction action {};
    action.sa_handler = handler;
    (void)sigemptyset(&action.sa_mask);
    if (::sigaction(number, &action, nullptr) != 0) ::_exit(120);
}

static auto is_disposition(int number, void (*handler)(int)) -> bool {
    struct sigaction action {};
    return ::sigaction(number, nullptr, &action) == 0 && action.sa_handler == handler;
}

static auto fd_count() -> int {
#if RSTD_OS_MACOS
    auto* dir = ::opendir("/dev/fd");
#else
    auto* dir = ::opendir("/proc/self/fd");
#endif
    if (! dir) return -1;
    int count = 0;
    while (::readdir(dir)) ++count;
    ::closedir(dir);
    return count;
}

struct MarkedRecv {
    using Output = async::signal::Recv::Output;
    async::signal::Recv future;
    std::atomic<bool>*  pending;
    auto                poll(mut_ref<MarkedRecv> self, task::Context& cx) -> task::Poll<Output> {
        auto result = future::poll(self->future, cx);
        if (result.is_pending()) self->pending->store(true);
        return result;
    }
};

struct Ready {
    using Output = int;
    auto poll(mut_ref<Ready>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(7);
    }
};

struct PausedWake {
    std::atomic<bool> entered {}, release {}, finished {};
};

static auto pause_clone(void* data) -> task::RawWaker;
static void pause_wake(void* data) {
    auto& state = *static_cast<PausedWake*>(data);
    state.entered.store(true);
    while (! state.release.load()) std::this_thread::yield();
    state.finished.store(true);
}
static void pause_drop(void*) {
}
static const task::RawWakerVTable pause_vtable { pause_clone, pause_wake, pause_wake, pause_drop };
static auto                       pause_clone(void* data) -> task::RawWaker {
    return task::RawWaker::from_raw_parts(data, &pause_vtable);
}

static auto child_case(const char* name) -> int {
    ::alarm(20);
    auto interrupt = sig::SignalKind::interrupt();
    if (std::strcmp(name, "initial") == 0) {
        CHECK(is_disposition(SIGINT, SIG_DFL));
        ::raise(SIGINT);
        return 1;
    }
    if (std::strcmp(name, "named") == 0) {
        sig::SignalKind kinds[] { sig::unix::child().unwrap(),
                                  sig::unix::window_change().unwrap(),
                                  sig::unix::pipe().unwrap(),
                                  sig::unix::quit().unwrap() };
        int             numbers[] { SIGCHLD, SIGWINCH, SIGPIPE, SIGQUIT };
        auto            subscription =
            sig::subscribe(slice<sig::SignalKind>::from_raw_parts(kinds, usize(4))).unwrap();
        for (int i = 0; i < 4; ++i) {
            CHECK(kinds[i].as_raw() == i32(numbers[i]));
            CHECK(kinds[i] == sig::SignalKind::from_raw(i32(numbers[i])).unwrap());
            CHECK(::raise(numbers[i]) == 0);
            CHECK(subscription.recv().unwrap().unwrap() == kinds[i]);
        }
        CHECK(subscription.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        CHECK(subscription.close().is_ok());
        return 0;
    }
    if (std::strcmp(name, "invalid") == 0) {
        CHECK(sig::SignalKind::from_raw(i32(SIGKILL)).is_err());
        CHECK(sig::SignalKind::from_raw(i32(SIGSTOP)).is_err());
        CHECK(sig::SignalKind::from_raw(i32(SIGSEGV)).is_err());
#ifdef SIGRTMIN
        CHECK(sig::SignalKind::from_raw(i32(SIGRTMIN)).is_err());
#endif
        CHECK(sig::SignalKind::from_raw(i32(NSIG)).is_err());
        CHECK(sig::SignalKind::from_raw(i32(-1)).is_err());
        CHECK(sig::subscribe(slice<sig::SignalKind> {}).is_err());
        disposition(SIGINT, custom_handler);
        auto result = sig::subscribe(interrupt);
        CHECK(result.is_err());
        CHECK(result.unwrap_err().kind().code == io::error::ErrorKind::ResourceBusy);
        CHECK(is_disposition(SIGINT, custom_handler));
        return 0;
    }
    if (std::strcmp(name, "partial_custom") == 0) {
        disposition(SIGINT, SIG_IGN);
        disposition(SIGTERM, custom_handler);
        sig::SignalKind kinds[] { interrupt, sig::unix::terminate().unwrap() };
        auto result = sig::subscribe(slice<sig::SignalKind>::from_raw_parts(kinds, usize(2)));
        CHECK(result.is_err());
        CHECK(result.unwrap_err().kind().code == io::error::ErrorKind::ResourceBusy);
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        CHECK(is_disposition(SIGTERM, custom_handler));
        auto changes = native_changes[SIGINT].load();
        CHECK(::raise(SIGINT) == 0);
        auto recovered = sig::subscribe(interrupt).unwrap();
        CHECK(recovered.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        CHECK(::raise(SIGINT) == 0);
        CHECK(recovered.recv().unwrap().unwrap() == interrupt);
        CHECK(recovered.close().is_ok());
        CHECK(native_changes[SIGINT].load() == changes);
        return 0;
    }
    if (std::strcmp(name, "partial_error") == 0) {
        disposition(SIGINT, SIG_IGN);
        disposition(SIGTERM, SIG_DFL);
        sig::SignalKind kinds[] { interrupt, sig::unix::terminate().unwrap() };
        fail_install.store(SIGTERM);
        auto result = sig::subscribe(slice<sig::SignalKind>::from_raw_parts(kinds, usize(2)));
        CHECK(result.is_err());
        CHECK(result.unwrap_err().raw_os_error().unwrap() == i32(EIO));
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        CHECK(is_disposition(SIGTERM, SIG_DFL));
        auto changes = native_changes[SIGINT].load();
        fail_install.store(0);
        auto recovered =
            sig::subscribe(slice<sig::SignalKind>::from_raw_parts(kinds, usize(2))).unwrap();
        CHECK(::raise(SIGINT) == 0);
        CHECK(recovered.recv().unwrap().unwrap() == interrupt);
        CHECK(::raise(SIGTERM) == 0);
        CHECK(recovered.recv().unwrap().unwrap() == kinds[1]);
        CHECK(recovered.close().is_ok());
        CHECK(native_changes[SIGINT].load() == changes);
        return 0;
    }
    if (std::strcmp(name, "default") == 0) {
        disposition(SIGINT, SIG_DFL);
        {
            auto subscription = sig::subscribe(interrupt).unwrap();
            CHECK(::raise(SIGINT) == 0);
            CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        }
        CHECK(! is_disposition(SIGINT, SIG_DFL));
        CHECK(::raise(SIGINT) == 0);
        auto subscription = sig::subscribe(interrupt).unwrap();
        CHECK(subscription.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        CHECK(::raise(SIGINT) == 0);
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        CHECK(native_changes[SIGINT].load() == 2);
        return 0;
    }
    if (std::strcmp(name, "ignored") == 0) {
        disposition(SIGINT, SIG_IGN);
        auto subscription = sig::subscribe(interrupt).unwrap();
        errno             = E2BIG;
        CHECK(::raise(SIGINT) == 0);
        CHECK(errno == E2BIG);
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        CHECK(subscription.close().is_ok());
        CHECK(subscription.close().is_ok());
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        CHECK(subscription.try_recv().unwrap().is_none());
        CHECK(::raise(SIGINT) == 0);
        return 0;
    }
    if (std::strcmp(name, "close_no_native") == 0) {
        disposition(SIGINT, SIG_IGN);
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto changes      = native_changes[SIGINT].load();
        fail_install.store(SIGINT);
        auto closed = subscription.close();
        CHECK(closed.is_ok());
        CHECK(subscription.recv().unwrap().is_none());
        CHECK(::raise(SIGINT) == 0);
        CHECK(subscription.close().is_ok());
        auto reopened = sig::subscribe(interrupt).unwrap();
        CHECK(reopened.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        CHECK(::raise(SIGINT) == 0);
        CHECK(reopened.recv().unwrap().unwrap() == interrupt);
        CHECK(reopened.close().is_ok());
        CHECK(native_changes[SIGINT].load() == changes);
        return 0;
    }
    if (std::strcmp(name, "handshake") == 0) {
        auto terminate    = sig::unix::terminate().unwrap();
        auto subscription = sig::subscribe(terminate).unwrap();
        CHECK(::write(STDOUT_FILENO, "R", 1) == 1);
        CHECK(subscription.recv().unwrap().unwrap() == terminate);
        return 0;
    }
    if (std::strcmp(name, "fanout") == 0) {
        auto first = sig::subscribe(interrupt).unwrap();
        CHECK(::raise(SIGINT) == 0);
        auto second = sig::subscribe(interrupt).unwrap();
        CHECK(second.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        CHECK(first.recv().unwrap().unwrap() == interrupt);
        CHECK(::raise(SIGINT) == 0);
        CHECK(first.recv().unwrap().unwrap() == interrupt);
        CHECK(second.recv().unwrap().unwrap() == interrupt);
        CHECK(first.close().is_ok());
        CHECK(::raise(SIGINT) == 0);
        CHECK(second.recv().unwrap().unwrap() == interrupt);
        CHECK(first.recv().unwrap().is_none());
        return 0;
    }
    if (std::strcmp(name, "close") == 0) {
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto receiver     = subscription.receiver().unwrap();
        CHECK(subscription.receiver().unwrap_err().kind().code ==
              io::error::ErrorKind::ResourceBusy);
        std::atomic<bool> started {};
        std::atomic<bool> closed {};
        auto thread = std::thread([lease = rstd::move(receiver), &started, &closed]() mutable {
            started.store(true);
            closed.store(lease.recv().unwrap().is_none());
        });
        while (! started.load()) std::this_thread::yield();
        CHECK(subscription.close().is_ok());
        thread.join();
        CHECK(closed.load());
        CHECK(subscription.receiver().is_ok());
        return 0;
    }
    if (std::strcmp(name, "burst") == 0) {
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto before       = fd_count();
        for (int i = 0; i < 20000; ++i) CHECK(::raise(SIGINT) == 0);
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        CHECK(subscription.try_recv().unwrap_err().kind().code == io::error::ErrorKind::WouldBlock);
        for (int i = 0; i < 500; ++i) {
            auto other = sig::subscribe(interrupt).unwrap();
            CHECK(::raise(SIGINT) == 0);
            CHECK(other.recv().unwrap().unwrap() == interrupt);
        }
        CHECK(fd_count() == before);
        return 0;
    }
    if (std::strcmp(name, "saturated") == 0) {
        auto       subscription = sig::subscribe(interrupt).unwrap();
        auto       receiver     = subscription.receiver().unwrap();
        PausedWake paused;
        auto       waker = task::Waker::from_raw(pause_clone(&paused));
        auto       cx    = task::Context::from_waker(waker);
        CHECK(receiver.poll_recv(cx).is_pending());
        CHECK(::raise(SIGINT) == 0);
        while (! paused.entered.load()) std::this_thread::yield();
        for (int i = 0; i < 100000; ++i) CHECK(::raise(SIGINT) == 0);
        CHECK(receiver.try_recv().unwrap().unwrap() == interrupt);
        paused.release.store(true);
        while (! paused.finished.load()) std::this_thread::yield();
        CHECK(::raise(SIGINT) == 0);
        CHECK(receiver.recv().unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "churn") == 0) {
        disposition(SIGINT, SIG_IGN);
        auto seed = sig::subscribe(interrupt).unwrap();
        CHECK(seed.close().is_ok());
        auto              before = fd_count();
        std::atomic<bool> running { true };
        auto              sender = std::thread([&] {
            while (running.load()) ::raise(SIGINT);
        });
        for (int i = 0; i < 1000; ++i) {
            auto subscription = sig::subscribe(interrupt).unwrap();
            CHECK(subscription.recv().unwrap().unwrap() == interrupt);
            CHECK(subscription.close().is_ok());
        }
        running.store(false);
        sender.join();
        CHECK(fd_count() == before);
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        CHECK(native_changes[SIGINT].load() == 2);
        return 0;
    }
    if (std::strcmp(name, "cancel") == 0) {
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto cx           = task::Context::from_waker(task::Waker::noop());
        {
            auto future = async::signal::recv(subscription).unwrap();
            CHECK(future::poll(future, cx).is_pending());
            CHECK(::raise(SIGINT) == 0);
            auto selected = async::select(Ready {}, rstd::move(future));
            auto result   = future::poll(selected, cx);
            CHECK(result.is_ready());
            CHECK(rstd::move(result).take().is_left());
        }
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        auto future = async::signal::recv(subscription).unwrap();
        CHECK(future::poll(future, cx).is_pending());
        CHECK(subscription.close().is_ok());
        auto result = future::poll(future, cx);
        CHECK(result.is_ready());
        CHECK(rstd::move(result).take().unwrap().is_none());
        return 0;
    }
    if (std::strcmp(name, "ctrl_c") == 0) {
        disposition(SIGINT, SIG_IGN);
        auto future = async::signal::ctrl_c().unwrap();
        CHECK(::raise(SIGINT) == 0);
        auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
        CHECK(runtime.block_on(rstd::move(future)).unwrap().unwrap() == interrupt);
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        {
            auto canceled = async::signal::ctrl_c().unwrap();
            auto cx       = task::Context::from_waker(task::Waker::noop());
            CHECK(future::poll(canceled, cx).is_pending());
        }
        CHECK(! is_disposition(SIGINT, SIG_IGN));
        CHECK(native_changes[SIGINT].load() == 2);
        return 0;
    }
    if (std::strcmp(name, "ctrl_c_install_error") == 0) {
        disposition(SIGINT, SIG_IGN);
        fail_install.store(SIGINT);
        auto result = async::signal::ctrl_c();
        CHECK(result.is_err());
        CHECK(result.unwrap_err().raw_os_error().unwrap() == i32(EIO));
        CHECK(is_disposition(SIGINT, SIG_IGN));
        fail_install.store(0);
        auto recovered = async::signal::ctrl_c().unwrap();
        CHECK(::raise(SIGINT) == 0);
        auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
        CHECK(runtime.block_on(rstd::move(recovered)).unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "runtime") == 0) {
        auto              subscription = sig::subscribe(interrupt).unwrap();
        std::atomic<bool> pending {};
        auto              sender  = std::thread([&] {
            while (! pending.load()) std::this_thread::yield();
            ::raise(SIGINT);
        });
        auto              runtime = async::RuntimeBuilder::current_thread().build().unwrap();
        auto              result =
            runtime.block_on(MarkedRecv { async::signal::recv(subscription).unwrap(), &pending });
        sender.join();
        CHECK(result.unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "runtimes") == 0) {
        auto              first       = sig::subscribe(interrupt).unwrap();
        auto              second      = sig::subscribe(interrupt).unwrap();
        auto              synchronous = sig::subscribe(interrupt).unwrap();
        std::atomic<bool> p1 {}, p2 {};
        auto              other =
            async::RuntimeBuilder::multi_thread().worker_threads(usize(1)).build().unwrap();
        auto second_task = other.spawn(MarkedRecv { async::signal::recv(second).unwrap(), &p2 });
        {
            auto runtime =
                async::RuntimeBuilder::multi_thread().worker_threads(usize(1)).build().unwrap();
            auto first_task =
                runtime.spawn(MarkedRecv { async::signal::recv(first).unwrap(), &p1 });
            while (! p1.load() || ! p2.load()) std::this_thread::yield();
        }
        CHECK(first.receiver().is_ok());
        CHECK(::raise(SIGINT) == 0);
        CHECK(other.block_on(rstd::move(second_task)).unwrap().unwrap().unwrap() == interrupt);
        CHECK(first.recv().unwrap().unwrap() == interrupt);
        CHECK(synchronous.recv().unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "exec_child") == 0) {
        CHECK(is_disposition(SIGINT, SIG_DFL));
        CHECK(fd_count() == expected_descriptors);
        auto subscription = sig::subscribe(interrupt).unwrap();
        CHECK(::raise(SIGINT) == 0);
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "exec") == 0) {
        disposition(SIGINT, SIG_DFL);
        char descriptors[32];
        std::snprintf(descriptors, sizeof(descriptors), "%d", fd_count());
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto child        = ::fork();
        CHECK(child >= 0);
        if (child == 0) {
            ::execl(executable, executable, "--signal-child", "exec_child", descriptors, nullptr);
            ::_exit(123);
        }
        int status;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(::raise(SIGINT) == 0);
        CHECK(subscription.recv().unwrap().unwrap() == interrupt);
        return 0;
    }
    if (std::strcmp(name, "fork") == 0) {
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto child        = ::fork();
        CHECK(child >= 0);
        if (child == 0) {
            auto result = sig::subscribe(interrupt);
            ::_exit(result.is_err() &&
                            result.unwrap_err().kind().code == io::error::ErrorKind::Unsupported
                        ? 0
                        : 1);
        }
        int status;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        return 0;
    }
    return 2;
}

static auto run_child(const char* name, int expected_signal = 0, int send_signal = 0) -> bool {
    int ready[2];
    if (::pipe(ready) != 0) return false;
    auto pid = ::fork();
    if (pid == 0) {
        ::close(ready[0]);
        if (send_signal != 0) ::dup2(ready[1], STDOUT_FILENO);
        ::close(ready[1]);
        ::signal(SIGINT, SIG_DFL);
        ::execl(executable, executable, "--signal-child", name, nullptr);
        ::_exit(121);
    }
    ::close(ready[1]);
    if (pid < 0) {
        ::close(ready[0]);
        return false;
    }
    bool handshake = true;
    if (send_signal != 0) {
        struct pollfd fd { ready[0], POLLIN, 0 };
        char          value {};
        handshake = ::poll(&fd, 1, 10000) == 1 && ::read(ready[0], &value, 1) == 1 && value == 'R';
        ::kill(pid, handshake ? send_signal : SIGKILL);
    }
    ::close(ready[0]);
    int   status;
    pid_t waited;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != pid || ! handshake) return false;
    if (expected_signal != 0) return WIFSIGNALED(status) && WTERMSIG(status) == expected_signal;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

TEST(Signal, ImportPreservesDefault) {
    EXPECT_TRUE(run_child("initial", SIGINT));
}
TEST(Signal, NamedUnixSignals) {
    EXPECT_TRUE(run_child("named"));
}
TEST(Signal, RejectsInvalidAndCustomHandlers) {
    EXPECT_TRUE(run_child("invalid"));
}
TEST(Signal, PartialRegistrationPreservesCustomHandler) {
    EXPECT_TRUE(run_child("partial_custom"));
}
TEST(Signal, PartialInstallationCanBeRetried) {
    EXPECT_TRUE(run_child("partial_error"));
}
TEST(Signal, HandlerSurvivesDropWithoutReplayingEvents) {
    EXPECT_TRUE(run_child("default"));
}
TEST(Signal, CloseKeepsHandlerAndDiscardsEvents) {
    EXPECT_TRUE(run_child("ignored"));
}
TEST(Signal, CloseAndResubscribeDoNotChangeNativeHandler) {
    EXPECT_TRUE(run_child("close_no_native"));
}
TEST(Signal, ExternalTerminationAfterHandshake) {
    EXPECT_TRUE(run_child("handshake", 0, SIGTERM));
}
TEST(Signal, IndependentSubscriptionsAndBoundaries) {
    EXPECT_TRUE(run_child("fanout"));
}
TEST(Signal, ExclusiveReceiverAndCloseWakeup) {
    EXPECT_TRUE(run_child("close"));
}
TEST(Signal, CoalescingAndBoundedDescriptors) {
    EXPECT_TRUE(run_child("burst"));
}
TEST(Signal, SaturatedWakePipeKeepsPendingEvent) {
    EXPECT_TRUE(run_child("saturated"));
}
TEST(Signal, ConcurrentLastCloseAndRegistration) {
    EXPECT_TRUE(run_child("churn"));
}
TEST(Signal, RejectsForkWithoutExec) {
    EXPECT_TRUE(run_child("fork"));
}
TEST(Signal, ExecResetsHandlerAndClosesDescriptors) {
    EXPECT_TRUE(run_child("exec"));
}
TEST(AsyncSignal, CancelDoesNotConsume) {
    EXPECT_TRUE(run_child("cancel"));
}
TEST(AsyncSignal, CtrlCRegistersBeforePollAndKeepsHandler) {
    EXPECT_TRUE(run_child("ctrl_c"));
}
TEST(AsyncSignal, CtrlCReportsInstallationErrorBeforePoll) {
    EXPECT_TRUE(run_child("ctrl_c_install_error"));
}
TEST(AsyncSignal, CurrentThreadWakeup) {
    EXPECT_TRUE(run_child("runtime"));
}
TEST(AsyncSignal, ShutdownPreservesOtherReceivers) {
    EXPECT_TRUE(run_child("runtimes"));
}

int main(int argc, char** argv) {
    executable = argv[0];
    if (argc >= 3 && std::strcmp(argv[1], "--signal-child") == 0) {
        if (argc == 4) expected_descriptors = std::atoi(argv[3]);
        return child_case(argv[2]);
    }
    rstd::env::args_init(argc, argv);
    return rstd::test::run_registered().to_primitive();
}
