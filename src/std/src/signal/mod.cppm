export module rstd:signal;
export import :io;
import :sync;
import rstd.alloc;

namespace rstd::signal
{

/// An asynchronous OS signal. Native numbers are platform-specific.
export class SignalKind {
    int m_number;
    explicit constexpr SignalKind(int number): m_number(number) {}

public:
    static auto interrupt() noexcept -> SignalKind;
    /// Rejects uncatchable, synchronous fault, and realtime signals.
    /// Windows accepts only CTRL_C_EVENT (0), not C runtime signal numbers.
    static auto           from_raw(i32 number) -> io::Result<SignalKind>;
    auto                  as_raw() const noexcept -> i32 { return i32(m_number); }
    friend constexpr auto operator==(SignalKind, SignalKind) noexcept -> bool = default;
};

export namespace unix
{
auto terminate() -> io::Result<SignalKind>;
auto hangup() -> io::Result<SignalKind>;
auto user1() -> io::Result<SignalKind>;
auto user2() -> io::Result<SignalKind>;
} // namespace unix

struct Interest {
    SignalKind         kind;
    unsigned long long sequence {};
    bool               pending {};
};

struct SubscriptionFields {
    vec::Vec<Interest>  interests = vec::Vec<Interest>::make();
    Option<task::Waker> waker;
    Option<io::Error>   error;
    bool                closed {};
    bool                receiving {};
};

struct SubscriptionState {
    sync::Mutex<SubscriptionFields> fields { SubscriptionFields {} };
    sync::Condvar                   changed;
};

export class Subscription;

/// An exclusive receive lease. Dropping it cancels the wait, not the subscription.
/// Operations on a single lease must not run concurrently. Subscription::close()
/// may run concurrently and wakes a blocked receiver with None.
export class Receiver {
    sync::Arc<SubscriptionState> m_state;
    explicit Receiver(sync::Arc<SubscriptionState> state): m_state(rstd::move(state)) {}
    friend class Subscription;
    void release();

public:
    Receiver(const Receiver&)                    = delete;
    auto operator=(const Receiver&) -> Receiver& = delete;
    Receiver(Receiver&&) noexcept                = default;
    auto operator=(Receiver&& other) noexcept -> Receiver&;
    ~Receiver();

    /// None means closed; WouldBlock means open with no pending event.
    auto try_recv() -> io::Result<Option<SignalKind>>;
    auto recv() -> io::Result<Option<SignalKind>>;
    /// A pending poll registers the waker without consuming an event.
    auto poll_recv(task::Context& cx) -> task::Poll<io::Result<Option<SignalKind>>>;
};

/// Independent, move-only signal subscription. close() restores the previous OS
/// disposition on the last subscription and reports restoration errors.
/// Destruction attempts restoration but cannot report failure; use close() when
/// restoration must be checked. Receivers remain valid and observe closure.
export class Subscription {
    sync::Arc<SubscriptionState> m_state;
    explicit Subscription(sync::Arc<SubscriptionState> state): m_state(rstd::move(state)) {}
    friend auto subscribe(slice<SignalKind>) -> io::Result<Subscription>;

public:
    Subscription(const Subscription&)                    = delete;
    auto operator=(const Subscription&) -> Subscription& = delete;
    Subscription(Subscription&&) noexcept                = default;
    auto operator=(Subscription&& other) noexcept -> Subscription&;
    ~Subscription();

    /// Only one receiver may be active; another acquisition returns ResourceBusy.
    auto receiver() const -> io::Result<Receiver>;
    auto try_recv() const -> io::Result<Option<SignalKind>>;
    auto recv() const -> io::Result<Option<SignalKind>>;
    /// Wakes receivers with None, discards pending events, and allows retry on error.
    auto close() const -> io::Result<empty>;
};

/// Registration is effective before return. Repeated signals may be coalesced.
/// Concurrent external sigaction changes and use after fork without exec are unsupported.
/// Linux signals and Windows console Ctrl+C are supported. Thread signal masks are unchanged.
/// Windows adds/removes only its own handler; other handlers and the ignore flag are untouched.
/// Changing the attached console while subscribed is unsupported. A later handler may consume
/// Ctrl+C first. Close, logoff, shutdown and Ctrl+Break events are not intercepted.
/// A shared dispatcher, its wakeup descriptors/event, and fixed handler state remain
/// alive until process exit. No subscription or runtime is retained after closure.
/// Failed restoration retains an inactive handler safely; close() can retry. After a
/// failed registration rollback, subscribing and closing the same kind retries restoration.
/// \code
/// auto subscription = rstd::signal::subscribe(rstd::signal::SignalKind::interrupt()).unwrap();
/// auto event = subscription.recv();
/// auto restored = subscription.close();
/// \endcode
export auto        subscribe(slice<SignalKind> kinds) -> io::Result<Subscription>;
export inline auto subscribe(SignalKind kind) -> io::Result<Subscription> {
    return subscribe(slice<SignalKind>::from_raw_parts(&kind, usize(1)));
}

} // namespace rstd::signal
