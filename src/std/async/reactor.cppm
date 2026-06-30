module;
#include <rstd/macro.hpp>

export module rstd:async.reactor;
export import :async.forward;
export import :io.error;
export import :time;
import :sys.fd;
import :sys.libc;
import :sync;
import :thread;
import rstd.alloc;

using namespace rstd;
using ::alloc::vec::Vec;
namespace libc = rstd::sys::libc;

namespace rstd::async
{

inline constexpr u64 WAKE_EVENT_ID  = 0;
inline constexpr u64 TIMER_EVENT_ID = u64(-1);

export struct Interest {
    u8 m_bits {};

    static constexpr u8 READABLE { 1 };
    static constexpr u8 WRITABLE { 2 };

    static constexpr auto readable() noexcept -> Interest { return Interest { READABLE }; }
    static constexpr auto writable() noexcept -> Interest { return Interest { WRITABLE }; }
    static constexpr auto read_write() noexcept -> Interest {
        return Interest { u8(READABLE | WRITABLE) };
    }

    constexpr auto is_readable() const noexcept -> bool { return (m_bits & READABLE) != 0; }
    constexpr auto is_writable() const noexcept -> bool { return (m_bits & WRITABLE) != 0; }
    constexpr auto is_empty() const noexcept -> bool { return m_bits == 0; }

    friend constexpr auto operator|(Interest a, Interest b) noexcept -> Interest {
        return Interest { u8(a.m_bits | b.m_bits) };
    }
};

export struct Ready {
    u8 m_bits {};

    static constexpr u8 READABLE { 1 };
    static constexpr u8 WRITABLE { 2 };
    static constexpr u8 READ_CLOSED { 4 };
    static constexpr u8 WRITE_CLOSED { 8 };
    static constexpr u8 ERROR { 16 };

    static constexpr auto readable() noexcept -> Ready { return Ready { READABLE }; }
    static constexpr auto writable() noexcept -> Ready { return Ready { WRITABLE }; }
    static constexpr auto read_closed() noexcept -> Ready { return Ready { READ_CLOSED }; }
    static constexpr auto write_closed() noexcept -> Ready { return Ready { WRITE_CLOSED }; }
    static constexpr auto error() noexcept -> Ready { return Ready { ERROR }; }

    constexpr auto is_readable() const noexcept -> bool { return (m_bits & READABLE) != 0; }
    constexpr auto is_writable() const noexcept -> bool { return (m_bits & WRITABLE) != 0; }
    constexpr auto is_read_closed() const noexcept -> bool { return (m_bits & READ_CLOSED) != 0; }
    constexpr auto is_write_closed() const noexcept -> bool {
        return (m_bits & WRITE_CLOSED) != 0;
    }
    constexpr auto is_error() const noexcept -> bool { return (m_bits & ERROR) != 0; }
    constexpr auto is_empty() const noexcept -> bool { return m_bits == 0; }

    constexpr auto for_interest(Interest interest) const noexcept -> Ready {
        auto bits = u8(0);
        if (interest.is_readable()) bits |= m_bits & (READABLE | READ_CLOSED | ERROR);
        if (interest.is_writable()) bits |= m_bits & (WRITABLE | WRITE_CLOSED | ERROR);
        return Ready { bits };
    }

    constexpr void remove(Ready ready) noexcept { m_bits &= ~ready.m_bits; }

    friend constexpr auto operator|(Ready a, Ready b) noexcept -> Ready {
        return Ready { u8(a.m_bits | b.m_bits) };
    }

    constexpr auto operator|=(Ready other) noexcept -> Ready& {
        m_bits |= other.m_bits;
        return *this;
    }
};

export struct ReadyEvent {
    Ready m_ready {};
    usize m_tick {};

    constexpr auto ready() const noexcept -> Ready { return m_ready; }
    constexpr auto tick() const noexcept -> usize { return m_tick; }

    constexpr auto is_readable() const noexcept -> bool { return m_ready.is_readable(); }
    constexpr auto is_writable() const noexcept -> bool { return m_ready.is_writable(); }
    constexpr auto is_read_closed() const noexcept -> bool { return m_ready.is_read_closed(); }
    constexpr auto is_write_closed() const noexcept -> bool { return m_ready.is_write_closed(); }
    constexpr auto is_error() const noexcept -> bool { return m_ready.is_error(); }
};

struct RegistrationState {
    sys::fd::RawFd      fd;
    usize               id {};
    Ready               ready {};
    usize               tick { 1 };
    Option<task::Waker> read_waker {};
    Option<task::Waker> write_waker {};
    usize               read_waiter_id {};
    usize               write_waiter_id {};
    usize               next_waiter_id { 1 };
    u32                 backend_events {};
    bool                backend_registered { false };
    bool                closed { false };

    RegistrationState(sys::fd::RawFd fd, usize id) : fd(fd), id(id) {}

    RegistrationState(const RegistrationState&)            = delete;
    auto operator=(const RegistrationState&) -> RegistrationState& = delete;
};

struct TimerEntry {
    usize               id {};
    time::Instant       deadline {};
    Option<task::Waker> waker {};

    TimerEntry(usize id, time::Instant deadline, task::Waker waker)
        : id(id), deadline(deadline), waker(Some(rstd::move(waker))) {}

    TimerEntry(const TimerEntry&)                        = delete;
    auto operator=(const TimerEntry&) -> TimerEntry&     = delete;
    TimerEntry(TimerEntry&&) noexcept                    = default;
    auto operator=(TimerEntry&&) noexcept -> TimerEntry& = default;
};

struct ReactorState {
    Vec<sync::Arc<RegistrationState>> registrations {};
    Vec<TimerEntry>                   timers {};
    sys::fd::OwnedFd                  wake_read {};
    sys::fd::OwnedFd                  wake_write {};
    sys::fd::OwnedFd                  timer {};
    sys::fd::OwnedFd                  epoll {};
    usize                             next_registration_id { 1 };
    usize                             next_timer_id { 1 };
    bool                              stopped { false };
};

class Reactor {
    sync::Mutex<ReactorState> m_state;
    Option<thread::JoinHandle<void>> m_thread {};
    bool                      m_available { false };
    io::Error                 m_init_error {
        io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported })
    };

    static auto last_os_error() noexcept -> io::Error {
        return io::Error::from_raw_os_error(sys::io::last_os_error());
    }

    static auto duration_to_timespec(time::Duration duration) noexcept -> libc::timespec_t {
        return libc::timespec_t {
            .tv_sec  = static_cast<libc::time_t>(duration.as_secs()),
            .tv_nsec = static_cast<long>(duration.subsec_nanos()),
        };
    }

    static auto earliest_timer_locked(ReactorState& st) -> Option<time::Instant> {
        if (st.timers.is_empty()) return None();

        auto deadline = st.timers[0].deadline;
        for (usize i = 1; i < st.timers.len(); ++i) {
            if (st.timers[i].deadline < deadline) {
                deadline = st.timers[i].deadline;
            }
        }
        return Some(deadline);
    }

    auto update_timerfd_locked(ReactorState& st) -> io::Result<empty> {
#if RSTD_OS_LINUX
        if (! st.timer.is_open()) return Ok(empty {});

        auto spec = libc::itimerspec_t {};
        auto next = earliest_timer_locked(st);
        if (next.is_some()) {
            auto now      = time::Instant::now();
            auto deadline = rstd::move(next).unwrap_unchecked();
            auto delay    = deadline <= now ? time::Duration::from_nanos(1) : deadline - now;
            spec.it_value = duration_to_timespec(delay);
        }

        if (libc::timerfd_settime(st.timer.as_raw_fd(), 0, &spec, nullptr) < 0) {
            return Err(last_os_error());
        }
        return Ok(empty {});
#else
        (void)st;
        return Err(m_init_error);
#endif
    }

    static void collect_expired_timers_locked(ReactorState& st, Vec<task::Waker>& wakers) {
        auto now = time::Instant::now();
        for (usize i = 0; i < st.timers.len();) {
            if (st.timers[i].deadline <= now) {
                auto entry = st.timers.remove(i);
                if (entry.waker.is_some()) {
                    wakers.push(rstd::move(entry.waker).unwrap_unchecked());
                }
            } else {
                ++i;
            }
        }
    }

    auto init_backend() -> io::Result<empty> {
#if RSTD_OS_LINUX
        int epoll = libc::epoll_create1(libc::EPOLL_CLOEXEC);
        if (epoll < 0) {
            return Err(last_os_error());
        }
        auto epoll_fd = sys::fd::OwnedFd::from_raw_fd(epoll);

        int fds[2] {};
        if (libc::pipe2(fds, libc::O_NONBLOCK | libc::O_CLOEXEC) < 0) {
            return Err(last_os_error());
        }

        auto read_fd  = sys::fd::OwnedFd::from_raw_fd(fds[0]);
        auto write_fd = sys::fd::OwnedFd::from_raw_fd(fds[1]);

        int timer =
            libc::timerfd_create(libc::CLOCK_MONOTONIC, libc::TFD_NONBLOCK | libc::TFD_CLOEXEC);
        if (timer < 0) {
            return Err(last_os_error());
        }
        auto timer_fd = sys::fd::OwnedFd::from_raw_fd(timer);

        auto wake_event     = libc::epoll_event {};
        wake_event.events   = libc::EPOLLIN;
        wake_event.data.u64 = WAKE_EVENT_ID;
        if (libc::epoll_ctl(epoll, libc::EPOLL_CTL_ADD, fds[0], &wake_event) < 0) {
            return Err(last_os_error());
        }

        auto timer_event     = libc::epoll_event {};
        timer_event.events   = libc::EPOLLIN;
        timer_event.data.u64 = TIMER_EVENT_ID;
        if (libc::epoll_ctl(epoll, libc::EPOLL_CTL_ADD, timer, &timer_event) < 0) {
            return Err(last_os_error());
        }

        auto st        = m_state.lock().unwrap_unchecked();
        st->epoll      = rstd::move(epoll_fd);
        st->wake_read  = rstd::move(read_fd);
        st->wake_write = rstd::move(write_fd);
        st->timer      = rstd::move(timer_fd);
        return Ok(empty {});
#else
        return Err(m_init_error);
#endif
    }

    void notify() {
#if RSTD_OS_LINUX
        auto st = m_state.lock().unwrap_unchecked();
        if (! st->wake_write.is_open()) return;
        u8 byte = 1;
        (void)libc::write(st->wake_write.as_raw_fd(), &byte, 1);
#endif
    }

    void drain_wake_pipe(sys::fd::RawFd fd) {
#if RSTD_OS_LINUX
        u8 buf[64] {};
        while (libc::read(fd, buf, sizeof(buf)) > 0) {
        }
#else
        (void)fd;
#endif
    }

    void drain_timerfd(sys::fd::RawFd fd) {
#if RSTD_OS_LINUX
        u64 expirations {};
        while (libc::read(fd, &expirations, sizeof(expirations)) > 0) {
        }
#else
        (void)fd;
#endif
    }

    static auto backend_ready_bits(u32 events) noexcept -> Ready {
        auto ready = Ready {};
#if RSTD_OS_LINUX
        if ((events & libc::EPOLLIN) != 0) ready |= Ready::readable();
        if ((events & libc::EPOLLOUT) != 0) ready |= Ready::writable();
        if (libc::HAS_EPOLLRDHUP && (events & libc::EPOLLRDHUP) != 0) {
            ready |= Ready::read_closed();
        }
        if ((events & libc::EPOLLHUP) != 0) ready |= Ready::read_closed() | Ready::write_closed();
        if ((events & libc::EPOLLERR) != 0) ready |= Ready::error();
#else
        (void)events;
#endif
        return ready;
    }

    static auto backend_interest_events(RegistrationState& registration) noexcept -> u32 {
        u32 events = 0;
#if RSTD_OS_LINUX
        if (registration.read_waker.is_some()) events |= libc::EPOLLIN;
        if (registration.write_waker.is_some()) events |= libc::EPOLLOUT;
        if (libc::HAS_EPOLLRDHUP && registration.read_waker.is_some()) {
            events |= libc::EPOLLRDHUP;
        }
#endif
        return events;
    }

    auto update_backend_locked(ReactorState& st, RegistrationState& registration)
        -> io::Result<empty> {
#if RSTD_OS_LINUX
        auto events = registration.closed ? u32(0) : backend_interest_events(registration);
        if (events == registration.backend_events && registration.backend_registered) {
            return Ok(empty {});
        }

        if (events == 0) {
            if (registration.backend_registered) {
                if (libc::epoll_ctl(
                        st.epoll.as_raw_fd(), libc::EPOLL_CTL_DEL, registration.fd, nullptr) < 0) {
                    return Err(last_os_error());
                }
                registration.backend_registered = false;
                registration.backend_events     = 0;
            }
            return Ok(empty {});
        }

        auto event     = libc::epoll_event {};
        event.events   = events | libc::EPOLLERR | libc::EPOLLHUP;
        event.data.u64 = u64(registration.id);

        int op = registration.backend_registered ? libc::EPOLL_CTL_MOD : libc::EPOLL_CTL_ADD;
        if (libc::epoll_ctl(st.epoll.as_raw_fd(), op, registration.fd, &event) < 0) {
            return Err(last_os_error());
        }
        registration.backend_registered = true;
        registration.backend_events     = events;
        return Ok(empty {});
#else
        (void)st;
        (void)registration;
        return Err(m_init_error);
#endif
    }

    static auto find_registration_locked(ReactorState& st, usize id) -> RegistrationState* {
        for (usize i = 0; i < st.registrations.len(); ++i) {
            auto* registration = st.registrations[i].as_ptr().as_raw_ptr();
            if (registration->id == id) return registration;
        }
        return nullptr;
    }

#if RSTD_OS_LINUX
    void wake_ready(Vec<libc::epoll_event>& events, int count) {
        auto wakers = Vec<task::Waker>::make();
        {
            auto st = m_state.lock().unwrap_unchecked();
            for (int i = 0; i < count; ++i) {
                auto id = usize(events[usize(i)].data.u64);
                if (id == 0) continue;

                auto* registration = find_registration_locked(*st, id);
                if (registration == nullptr || registration->closed) continue;

                auto ready = backend_ready_bits(events[usize(i)].events);
                if (ready.is_empty()) continue;

                registration->ready |= ready;
                ++registration->tick;
                if ((ready.is_readable() || ready.is_read_closed() || ready.is_error()) &&
                    registration->read_waker.is_some()) {
                    wakers.push(rstd::move(registration->read_waker.take()).unwrap_unchecked());
                    registration->read_waiter_id = 0;
                }
                if ((ready.is_writable() || ready.is_write_closed() || ready.is_error()) &&
                    registration->write_waker.is_some()) {
                    wakers.push(rstd::move(registration->write_waker.take()).unwrap_unchecked());
                    registration->write_waiter_id = 0;
                }
                (void)update_backend_locked(*st, *registration);
            }
        }

        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
    }

    void wake_expired_timers() {
        auto wakers = Vec<task::Waker>::make();
        {
            auto st = m_state.lock().unwrap_unchecked();
            collect_expired_timers_locked(*st, wakers);
            (void)update_timerfd_locked(*st);
        }

        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
    }
#endif

public:
    Reactor() : m_state(ReactorState {}) {
        auto backend = init_backend();
        if (backend.is_err()) {
            m_init_error = rstd::move(backend).unwrap_err_unchecked();
            return;
        }

        auto spawned = thread::spawn([this] {
            run();
        });
        if (spawned.is_err()) {
            m_init_error = rstd::move(spawned).unwrap_err_unchecked();
            return;
        }

        m_thread.insert(rstd::move(spawned).unwrap_unchecked());
        m_available = true;
    }

    ~Reactor() {
        {
            auto st     = m_state.lock().unwrap_unchecked();
            st->stopped = true;
        }
        notify();
        if (m_thread.is_some()) {
            auto handle = rstd::move(m_thread.take()).unwrap_unchecked();
            (void)rstd::move(handle).join();
        }
    }

    auto register_fd(sys::fd::RawFd fd) -> io::Result<sync::Arc<RegistrationState>> {
        if (! m_available) return Err(io::Error { m_init_error });

        auto registration = [&] {
            auto st           = m_state.lock().unwrap_unchecked();
            auto id           = st->next_registration_id++;
            auto registration = sync::Arc<RegistrationState>::make(fd, id);
            st->registrations.push(registration.clone());
            return registration;
        }();
        notify();
        return Ok(rstd::move(registration));
    }

    auto add_timer(time::Instant deadline, task::Waker waker) -> io::Result<usize> {
        if (! m_available) return Err(io::Error { m_init_error });

        auto wakers = Vec<task::Waker>::make();
        auto id     = usize {};
        {
            auto st = m_state.lock().unwrap_unchecked();
            id      = st->next_timer_id++;
            st->timers.push(TimerEntry { id, deadline, rstd::move(waker) });
            collect_expired_timers_locked(*st, wakers);
            auto updated = update_timerfd_locked(*st);
            if (updated.is_err()) return Err(rstd::move(updated).unwrap_err_unchecked());
        }

        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
        return Ok(id);
    }

    auto update_timer(usize id, task::Waker waker) -> io::Result<empty> {
        if (! m_available) return Err(io::Error { m_init_error });

        auto wakers = Vec<task::Waker>::make();
        {
            auto st = m_state.lock().unwrap_unchecked();
            for (usize i = 0; i < st->timers.len(); ++i) {
                if (st->timers[i].id == id) {
                    st->timers[i].waker = Some(rstd::move(waker));
                    collect_expired_timers_locked(*st, wakers);
                    auto updated = update_timerfd_locked(*st);
                    if (updated.is_err()) return Err(rstd::move(updated).unwrap_err_unchecked());
                    break;
                }
            }
        }

        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
        return Ok(empty {});
    }

    void cancel_timer(usize id) {
        {
            auto st = m_state.lock().unwrap_unchecked();
            for (usize i = 0; i < st->timers.len(); ++i) {
                if (st->timers[i].id == id) {
                    st->timers.remove(i);
                    (void)update_timerfd_locked(*st);
                    return;
                }
            }
        }
    }

    void deregister(sync::Arc<RegistrationState>& registration) {
        auto wakers = Vec<task::Waker>::make();
        {
            auto st = m_state.lock().unwrap_unchecked();
            for (usize i = 0; i < st->registrations.len(); ++i) {
                if (sync::Arc<RegistrationState>::ptr_eq(st->registrations[i], registration)) {
                    st->registrations.remove(i);
                    break;
                }
            }

            registration->closed = true;
            (void)update_backend_locked(*st, *registration.as_ptr().as_raw_ptr());
            if (registration->read_waker.is_some()) {
                wakers.push(rstd::move(registration->read_waker.take()).unwrap_unchecked());
            }
            if (registration->write_waker.is_some()) {
                wakers.push(rstd::move(registration->write_waker.take()).unwrap_unchecked());
            }
            registration->read_waiter_id  = 0;
            registration->write_waiter_id = 0;
        }

        notify();
        while (! wakers.is_empty()) {
            rstd::move(wakers.pop().unwrap_unchecked()).wake();
        }
    }

    auto poll_readiness(sync::Arc<RegistrationState>& registration,
                        task::Context&                cx,
                        Interest                      interest,
                        usize&                        waiter_id)
        -> task::Poll<io::Result<ReadyEvent>> {
        if (interest.is_empty()) {
            return task::Poll<io::Result<ReadyEvent>>::Ready(Ok(ReadyEvent {}));
        }

        {
            auto st    = m_state.lock().unwrap_unchecked();
            auto ready = registration->ready.for_interest(interest);
            if (! ready.is_empty()) {
                return task::Poll<io::Result<ReadyEvent>>::Ready(
                    Ok(ReadyEvent { ready, registration->tick }));
            }

            if (registration->closed) {
                return task::Poll<io::Result<ReadyEvent>>::Ready(Err(io::Error::from_kind(
                    io::ErrorKind { io::ErrorKind::NotConnected })));
            }

            if (waiter_id == 0) {
                waiter_id = registration->next_waiter_id++;
            }
            if (interest.is_readable()) {
                registration->read_waker     = Some(cx.waker().clone());
                registration->read_waiter_id = waiter_id;
            }
            if (interest.is_writable()) {
                registration->write_waker     = Some(cx.waker().clone());
                registration->write_waiter_id = waiter_id;
            }

            auto update = update_backend_locked(*st, *registration.as_ptr().as_raw_ptr());
            if (update.is_err()) {
                return task::Poll<io::Result<ReadyEvent>>::Ready(
                    Err(rstd::move(update).unwrap_err_unchecked()));
            }
            (void)st;
        }

        notify();
        return task::Poll<io::Result<ReadyEvent>>::Pending();
    }

    void clear_readiness(sync::Arc<RegistrationState>& registration, Ready ready) {
        auto st = m_state.lock().unwrap_unchecked();
        registration->ready.remove(ready);
        (void)st;
    }

    void clear_readiness(sync::Arc<RegistrationState>& registration, ReadyEvent event) {
        auto st = m_state.lock().unwrap_unchecked();
        if (registration->tick == event.tick()) {
            registration->ready.remove(event.ready());
        }
        (void)st;
    }

    void clear_waker(sync::Arc<RegistrationState>& registration, Interest interest, usize waiter_id) {
        if (waiter_id == 0) return;

        auto st = m_state.lock().unwrap_unchecked();
        if (interest.is_readable() && registration->read_waiter_id == waiter_id) {
            registration->read_waker     = None();
            registration->read_waiter_id = 0;
        }
        if (interest.is_writable() && registration->write_waiter_id == waiter_id) {
            registration->write_waker     = None();
            registration->write_waiter_id = 0;
        }
        (void)update_backend_locked(*st, *registration.as_ptr().as_raw_ptr());
    }

    void run() {
#if RSTD_OS_LINUX
        auto events = Vec<libc::epoll_event>::make();
        events.resize(64, libc::epoll_event {});

        while (true) {
            sys::fd::RawFd epoll_fd {};
            sys::fd::RawFd wake_fd {};
            sys::fd::RawFd timer_fd {};

            {
                auto st = m_state.lock().unwrap_unchecked();
                if (st->stopped) break;
                epoll_fd = st->epoll.as_raw_fd();
                wake_fd  = st->wake_read.as_raw_fd();
                timer_fd = st->timer.as_raw_fd();
            }

            int rc = libc::epoll_wait(epoll_fd, events.data(), int(events.len()), -1);
            if (rc < 0) {
                if (libc::get_errno() == libc::EINTR) continue;
                continue;
            }

            for (int i = 0; i < rc; ++i) {
                if (events[usize(i)].data.u64 == WAKE_EVENT_ID) {
                    drain_wake_pipe(wake_fd);
                } else if (events[usize(i)].data.u64 == TIMER_EVENT_ID) {
                    drain_timerfd(timer_fd);
                    wake_expired_timers();
                }
            }
            wake_ready(events, rc);
        }
#endif
    }
};

auto global_reactor() -> Reactor& {
    static auto reactor = Reactor {};
    return reactor;
}

export class Registration {
    sync::Arc<RegistrationState> m_state;

    explicit Registration(sync::Arc<RegistrationState> state) : m_state(rstd::move(state)) {}

public:
    Registration(const Registration&)            = delete;
    auto operator=(const Registration&) -> Registration& = delete;

    Registration(Registration&& other) noexcept : m_state(rstd::move(other.m_state)) {}

    auto operator=(Registration&& other) noexcept -> Registration& {
        if (this != &other) {
            reset();
            m_state = rstd::move(other.m_state);
        }
        return *this;
    }

    ~Registration() { reset(); }

    static auto register_fd(sys::fd::RawFd fd) -> io::Result<Registration> {
        auto state = global_reactor().register_fd(fd);
        if (state.is_err()) return Err(rstd::move(state).unwrap_err_unchecked());
        return Ok(Registration { rstd::move(state).unwrap_unchecked() });
    }

    void reset() {
        if (m_state) {
            global_reactor().deregister(m_state);
            m_state.reset();
        }
    }

    auto poll_readiness(task::Context& cx, Interest interest, usize& waiter_id)
        -> task::Poll<io::Result<ReadyEvent>> {
        return global_reactor().poll_readiness(m_state, cx, interest, waiter_id);
    }

    void clear_readiness(Ready ready) {
        if (m_state) {
            global_reactor().clear_readiness(m_state, ready);
        }
    }

    void clear_readiness(ReadyEvent event) {
        if (m_state) {
            global_reactor().clear_readiness(m_state, event);
        }
    }

    void clear_waker(Interest interest, usize waiter_id) {
        if (m_state) {
            global_reactor().clear_waker(m_state, interest, waiter_id);
        }
    }
};

export class TimerRegistration {
    Option<usize> m_id {};

    explicit TimerRegistration(usize id): m_id(Some(id)) {}

public:
    TimerRegistration(const TimerRegistration&)                    = delete;
    auto operator=(const TimerRegistration&) -> TimerRegistration& = delete;

    TimerRegistration(TimerRegistration&& other) noexcept: m_id(other.m_id.take()) {}

    auto operator=(TimerRegistration&& other) noexcept -> TimerRegistration& {
        if (this != &other) {
            reset();
            m_id = other.m_id.take();
        }
        return *this;
    }

    ~TimerRegistration() { reset(); }

    static auto register_deadline(time::Instant deadline, task::Waker waker)
        -> io::Result<TimerRegistration> {
        auto id = global_reactor().add_timer(deadline, rstd::move(waker));
        if (id.is_err()) return Err(rstd::move(id).unwrap_err_unchecked());
        return Ok(TimerRegistration { rstd::move(id).unwrap_unchecked() });
    }

    auto update_waker(task::Waker waker) -> io::Result<empty> {
        if (m_id.is_none()) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        }
        return global_reactor().update_timer(*m_id, rstd::move(waker));
    }

    void reset() {
        if (m_id.is_some()) {
            global_reactor().cancel_timer(rstd::move(m_id).unwrap_unchecked());
            m_id = None();
        }
    }
};

export class ReadinessFuture {
    Registration* m_registration {};
    Interest      m_interest {};
    usize         m_waiter_id {};
    bool          m_started { false };

public:
    using Output = io::Result<ReadyEvent>;

    ReadinessFuture(Registration& registration, Interest interest)
        : m_registration(rstd::addressof(registration)), m_interest(interest) {}

    ReadinessFuture(const ReadinessFuture&)            = delete;
    auto operator=(const ReadinessFuture&) -> ReadinessFuture& = delete;

    ReadinessFuture(ReadinessFuture&& other) noexcept
        : m_registration(rstd::exchange(other.m_registration, nullptr)),
          m_interest(other.m_interest),
          m_waiter_id(rstd::exchange(other.m_waiter_id, 0)),
          m_started(rstd::exchange(other.m_started, false)) {}

    auto operator=(ReadinessFuture&& other) noexcept -> ReadinessFuture& {
        if (this != &other) {
            cancel();
            m_registration = rstd::exchange(other.m_registration, nullptr);
            m_interest = other.m_interest;
            m_waiter_id = rstd::exchange(other.m_waiter_id, 0);
            m_started = rstd::exchange(other.m_started, false);
        }
        return *this;
    }

    ~ReadinessFuture() { cancel(); }

    auto poll(pin::Pin<mut_ref<ReadinessFuture>> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        future.m_started = true;
        auto result = future.m_registration->poll_readiness(cx, future.m_interest, future.m_waiter_id);
        if (result.is_ready()) {
            future.m_waiter_id = 0;
            future.m_started = false;
        }
        return result;
    }

private:
    void cancel() {
        if (m_started && m_registration != nullptr && m_waiter_id != 0) {
            m_registration->clear_waker(m_interest, m_waiter_id);
        }
        m_waiter_id = 0;
        m_started = false;
    }
};

} // namespace rstd::async
