module rstd;
import :signal;
import :sys.signal;
import :sync;
import rstd.alloc;

namespace rstd::signal
{

namespace backend = rstd::sys::signal;

static auto error(io::ErrorKind::Entity kind) -> io::Error {
    return io::Error::from_kind(io::ErrorKind { kind });
}

struct RegistryFields {
    vec::Vec<sync::Arc<SubscriptionState>> subscribers =
        vec::Vec<sync::Arc<SubscriptionState>>::make();
    size_t            references[backend::capacity] {};
    Option<io::Error> failure;
};

static auto registry() -> sync::Mutex<RegistryFields>& {
    // The detached dispatcher and in-flight OS handlers have process lifetime.
    static auto* state = new sync::Mutex<RegistryFields>(RegistryFields {});
    return *state;
}

static auto refresh(SubscriptionFields& fields) -> bool {
    bool pending = false;
    for (auto& interest : fields.interests) {
        auto sequence = backend::sequence(interest.kind.as_raw().to_primitive());
        if (sequence != interest.sequence) {
            interest.sequence = sequence;
            interest.pending  = true;
        }
        pending |= interest.pending;
    }
    return pending;
}

static auto take_event(SubscriptionFields& fields) -> io::Result<Option<SignalKind>> {
    if (fields.closed) return Ok(None<SignalKind>());
    if (fields.error.is_some()) return Err(*fields.error);
    (void)refresh(fields);
    for (auto& interest : fields.interests) {
        if (interest.pending) {
            interest.pending = false;
            return Ok(Some(interest.kind));
        }
    }
    return Err(error(io::ErrorKind::WouldBlock));
}

static void dispatch(int native_error) {
    auto wakers = vec::Vec<task::Waker>::make();
    {
        auto entries = registry().lock().unwrap_unchecked();
        if (native_error != 0) {
            entries->failure = Some(io::Error::from_raw_os_error(i32(native_error)));
        }
        for (auto& state : entries->subscribers) {
            auto fields = state->fields.lock().unwrap_unchecked();
            if (entries->failure.is_some()) fields->error = entries->failure;
            if (fields->error.is_some() || refresh(*fields)) {
                if (fields->waker.is_some()) wakers.push(fields->waker.take().unwrap_unchecked());
                state->changed.notify_all();
            }
        }
    }
    for (auto& waker : wakers) rstd::move(waker).wake();
}

auto SignalKind::interrupt() noexcept -> SignalKind {
    return SignalKind { backend::interrupt_number() };
}
auto SignalKind::from_raw(i32 number) -> io::Result<SignalKind> {
    if (! backend::available()) return Err(error(io::ErrorKind::Unsupported));
    if (! backend::supported(number.to_primitive())) return Err(error(io::ErrorKind::InvalidInput));
    return Ok(SignalKind { number.to_primitive() });
}
static auto platform_kind(int number) -> io::Result<SignalKind> {
    if (number < 0) return Err(error(io::ErrorKind::Unsupported));
    return SignalKind::from_raw(i32(number));
}
auto unix::terminate() -> io::Result<SignalKind> {
    return platform_kind(backend::terminate_number());
}
auto unix::hangup() -> io::Result<SignalKind> {
    return platform_kind(backend::hangup_number());
}
auto unix::user1() -> io::Result<SignalKind> {
    return platform_kind(backend::user1_number());
}
auto unix::user2() -> io::Result<SignalKind> {
    return platform_kind(backend::user2_number());
}

auto subscribe(slice<SignalKind> kinds) -> io::Result<Subscription> {
    if (! backend::available() || ! backend::same_process())
        return Err(error(io::ErrorKind::Unsupported));
    if (kinds.is_empty()) return Err(error(io::ErrorKind::InvalidInput));
    auto state   = sync::Arc<SubscriptionState>::make();
    auto entries = registry().lock().unwrap_unchecked();
    if (entries->failure.is_some()) return Err(*entries->failure);
    auto fields = state->fields.lock().unwrap_unchecked();
    for (auto kind : kinds) {
        auto number = kind.as_raw().to_primitive();
        if (! backend::supported(number)) return Err(error(io::ErrorKind::InvalidInput));
        bool duplicate = false;
        for (auto& interest : fields->interests) duplicate |= interest.kind == kind;
        if (! duplicate) fields->interests.push(Interest { kind, backend::sequence(number) });
    }
    auto started = backend::start(dispatch);
    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
    for (auto& interest : fields->interests) {
        auto installed = backend::install(interest.kind.as_raw().to_primitive());
        if (installed.is_err()) {
            auto failure = rstd::move(installed).unwrap_err_unchecked();
            for (auto& rollback : fields->interests) {
                auto number = rollback.kind.as_raw().to_primitive();
                if (entries->references[number] == 0) {
                    auto restored = backend::restore(number);
                    if (restored.is_err()) failure = rstd::move(restored).unwrap_err_unchecked();
                }
            }
            return Err(rstd::move(failure));
        }
    }
    for (auto& interest : fields->interests)
        ++entries->references[interest.kind.as_raw().to_primitive()];
    entries->subscribers.push(state.clone());
    return Ok(Subscription { rstd::move(state) });
}

auto Subscription::close() const -> io::Result<empty> {
    if (! m_state) return Ok(empty {});
    if (! backend::same_process()) return Err(error(io::ErrorKind::Unsupported));
    auto waker   = Option<task::Waker> {};
    auto failure = Option<io::Error> {};
    {
        auto entries = registry().lock().unwrap_unchecked();
        auto fields  = m_state->fields.lock().unwrap_unchecked();
        if (! fields->closed) {
            fields->closed = true;
            waker          = fields->waker.take();
            for (auto& interest : fields->interests) {
                --entries->references[interest.kind.as_raw().to_primitive()];
                interest.pending = false;
            }
            for (size_t i = 0; i < entries->subscribers.len().to_primitive(); ++i) {
                if (entries->subscribers[usize(i)].as_ptr() == m_state.as_ptr()) {
                    (void)entries->subscribers.remove(usize(i));
                    break;
                }
            }
        }
        for (auto& interest : fields->interests) {
            auto number = interest.kind.as_raw().to_primitive();
            if (entries->references[number] == 0) {
                auto restored = backend::restore(number);
                if (restored.is_err()) failure = Some(rstd::move(restored).unwrap_err_unchecked());
            }
        }
        m_state->changed.notify_all();
    }
    if (waker.is_some()) rstd::move(*waker).wake();
    if (failure.is_some()) return Err(*failure);
    return Ok(empty {});
}

Subscription::~Subscription() {
    (void)close();
}
auto Subscription::operator=(Subscription&& other) noexcept -> Subscription& {
    if (this != &other) {
        (void)close();
        m_state = rstd::move(other.m_state);
    }
    return *this;
}

auto Subscription::receiver() const -> io::Result<Receiver> {
    if (! backend::same_process()) return Err(error(io::ErrorKind::Unsupported));
    if (! m_state) return Err(error(io::ErrorKind::NotConnected));
    auto fields = m_state->fields.lock().unwrap_unchecked();
    if (fields->receiving) return Err(error(io::ErrorKind::ResourceBusy));
    fields->receiving = true;
    return Ok(Receiver { m_state.clone() });
}
auto Subscription::try_recv() const -> io::Result<Option<SignalKind>> {
    auto lease = receiver();
    if (lease.is_err()) return Err(rstd::move(lease).unwrap_err_unchecked());
    return lease.unwrap_unchecked().try_recv();
}
auto Subscription::recv() const -> io::Result<Option<SignalKind>> {
    auto lease = receiver();
    if (lease.is_err()) return Err(rstd::move(lease).unwrap_err_unchecked());
    return lease.unwrap_unchecked().recv();
}

void Receiver::release() {
    if (! m_state || ! backend::same_process()) return;
    auto waker = Option<task::Waker> {};
    {
        auto fields       = m_state->fields.lock().unwrap_unchecked();
        fields->receiving = false;
        waker             = fields->waker.take();
    }
    m_state.reset();
}
Receiver::~Receiver() {
    release();
}
auto Receiver::operator=(Receiver&& other) noexcept -> Receiver& {
    if (this != &other) {
        release();
        m_state = rstd::move(other.m_state);
    }
    return *this;
}
auto Receiver::try_recv() -> io::Result<Option<SignalKind>> {
    if (! backend::same_process()) return Err(error(io::ErrorKind::Unsupported));
    if (! m_state) return Err(error(io::ErrorKind::NotConnected));
    auto fields = m_state->fields.lock().unwrap_unchecked();
    return take_event(*fields);
}
auto Receiver::recv() -> io::Result<Option<SignalKind>> {
    if (! backend::same_process()) return Err(error(io::ErrorKind::Unsupported));
    if (! m_state) return Err(error(io::ErrorKind::NotConnected));
    auto fields = m_state->fields.lock().unwrap_unchecked();
    for (;;) {
        auto result = take_event(*fields);
        if (result.is_ok() ||
            result.unwrap_err_unchecked().kind().code != io::ErrorKind::WouldBlock)
            return result;
        m_state->changed.wait(fields);
    }
}
auto Receiver::poll_recv(task::Context& cx) -> task::Poll<io::Result<Option<SignalKind>>> {
    using Poll = task::Poll<io::Result<Option<SignalKind>>>;
    if (! backend::same_process()) return Poll::Ready(Err(error(io::ErrorKind::Unsupported)));
    if (! m_state) return Poll::Ready(Err(error(io::ErrorKind::NotConnected)));
    auto next_waker = Some(cx.waker().clone());
    auto old_waker  = Option<task::Waker> {};
    auto result     = io::Result<Option<SignalKind>>(Err(error(io::ErrorKind::WouldBlock)));
    {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        result      = take_event(*fields);
        old_waker   = fields->waker.take();
        if (result.is_err() &&
            result.unwrap_err_unchecked().kind().code == io::ErrorKind::WouldBlock) {
            fields->waker = rstd::move(next_waker);
        }
    }
    if (result.is_err() && result.unwrap_err_unchecked().kind().code == io::ErrorKind::WouldBlock)
        return Poll::Pending();
    return Poll::Ready(rstd::move(result));
}

} // namespace rstd::signal
