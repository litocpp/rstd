module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

module rstd;
import :sys.signal;
import :io;

namespace rstd::sys::signal
{

#if RSTD_OS_LINUX
static_assert(__atomic_always_lock_free(sizeof(unsigned long long), nullptr));
static_assert(__atomic_always_lock_free(sizeof(int), nullptr));

// In-flight handlers may outlive sigaction restoration. These never change address
// or close after publication, and contain no subscription or runtime ownership.
static unsigned long long sequences[capacity] {};
static int                process_id {};
static int                pipe_read { -1 };
static int                pipe_write { -1 };
static bool               installed[capacity] {};
static struct sigaction   previous[capacity] {};
static void (*dispatch_events)(int) {};

static auto os_error(int error) -> rstd::io::Error {
    return rstd::io::Error::from_raw_os_error(i32(error));
}

static void handle_signal(int number) {
    int saved_errno = errno;
    __atomic_fetch_add(&sequences[number], 1ULL, __ATOMIC_SEQ_CST);
    unsigned char byte = 1;
    while (::write(__atomic_load_n(&pipe_write, __ATOMIC_RELAXED), &byte, 1) < 0 &&
           errno == EINTR) {
    }
    errno = saved_errno;
}

static auto dispatch_loop(void*) -> void* {
    for (;;) {
        struct pollfd fd { pipe_read, POLLIN, 0 };
        int           result;
        do {
            result = ::poll(&fd, 1, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            dispatch_events(result < 0 ? errno : EIO);
            return nullptr;
        }
        unsigned char buffer[256];
        // A bounded drain lets delivery progress even under a continuous signal load.
        auto count = ::read(pipe_read, buffer, sizeof(buffer));
        if (count < 0 && errno != EAGAIN && errno != EINTR) {
            dispatch_events(errno);
            return nullptr;
        }
        dispatch_events(0);
    }
}

auto available() noexcept -> bool {
    return true;
}
auto same_process() noexcept -> bool {
    auto owner = __atomic_load_n(&process_id, __ATOMIC_ACQUIRE);
    return owner == 0 || owner == ::getpid();
}
auto supported(int number) noexcept -> bool {
    if (number <= 0 || number >= capacity || number >= SIGRTMIN) return false;
    switch (number) {
    case SIGKILL:
    case SIGSTOP:
    case SIGSEGV:
    case SIGBUS:
    case SIGILL:
    case SIGFPE:
    case SIGABRT:
    case SIGTRAP:
    case SIGSYS: return false;
    default: {
        struct sigaction action {};
        return number < NSIG && ::sigaction(number, nullptr, &action) == 0;
    }
    }
}
auto interrupt_number() noexcept -> int {
    return SIGINT;
}
auto terminate_number() noexcept -> int {
    return SIGTERM;
}
auto hangup_number() noexcept -> int {
    return SIGHUP;
}
auto user1_number() noexcept -> int {
    return SIGUSR1;
}
auto user2_number() noexcept -> int {
    return SIGUSR2;
}
auto sequence(int number) noexcept -> unsigned long long {
    return __atomic_load_n(&sequences[number], __ATOMIC_SEQ_CST);
}

auto start(void (*dispatch)(int)) -> rstd::io::Result<empty> {
    if (pipe_read >= 0) return Ok(empty {});
    int fds[2];
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) < 0) return Err(os_error(errno));
    pthread_attr_t attr;
    auto           error = ::pthread_attr_init(&attr);
    if (error == 0) {
        error = ::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (error == 0) {
            pipe_read = fds[0];
            __atomic_store_n(&pipe_write, fds[1], __ATOMIC_RELAXED);
            dispatch_events = dispatch;
            pthread_t thread;
            error = ::pthread_create(&thread, &attr, dispatch_loop, nullptr);
        }
        (void)::pthread_attr_destroy(&attr);
    }
    if (error != 0) {
        pipe_read = -1;
        __atomic_store_n(&pipe_write, -1, __ATOMIC_RELAXED);
        ::close(fds[0]);
        ::close(fds[1]);
        return Err(os_error(error));
    }
    __atomic_store_n(&process_id, static_cast<int>(::getpid()), __ATOMIC_RELEASE);
    return Ok(empty {});
}

auto install(int number) -> rstd::io::Result<empty> {
    if (installed[number]) return Ok(empty {});
    struct sigaction old {};
    if (::sigaction(number, nullptr, &old) < 0) return Err(os_error(errno));
    if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
        return Err(
            rstd::io::Error::new_const(rstd::io::ErrorKind { rstd::io::ErrorKind::ResourceBusy },
                                       "signal already has a custom handler"));
    }
    struct sigaction action {};
    action.sa_handler = handle_signal;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (::sigaction(number, &action, nullptr) < 0) return Err(os_error(errno));
    previous[number]  = old;
    installed[number] = true;
    return Ok(empty {});
}

auto restore(int number) -> rstd::io::Result<empty> {
    if (! installed[number]) return Ok(empty {});
    if (::sigaction(number, &previous[number], nullptr) < 0) return Err(os_error(errno));
    installed[number] = false;
    return Ok(empty {});
}
#else
auto available() noexcept -> bool {
    return false;
}
auto same_process() noexcept -> bool {
    return true;
}
auto supported(int) noexcept -> bool {
    return false;
}
auto interrupt_number() noexcept -> int {
    return -1;
}
auto terminate_number() noexcept -> int {
    return -1;
}
auto hangup_number() noexcept -> int {
    return -1;
}
auto user1_number() noexcept -> int {
    return -1;
}
auto user2_number() noexcept -> int {
    return -1;
}
auto sequence(int) noexcept -> unsigned long long {
    return 0;
}
auto start(void (*)(int)) -> rstd::io::Result<empty> {
    return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
}
auto install(int) -> rstd::io::Result<empty> {
    return start(nullptr);
}
auto restore(int) -> rstd::io::Result<empty> {
    return start(nullptr);
}
#endif

} // namespace rstd::sys::signal
