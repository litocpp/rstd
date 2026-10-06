export module rstd:async.signal;
export import :signal;
export import :async.forward;

namespace rstd::async::signal
{

/// Canceling this future releases its receive lease without consuming pending signals.
export class Recv {
    Option<rstd::signal::Subscription> m_owned;
    Option<rstd::signal::Receiver>     m_receiver;

public:
    using Output = io::Result<Option<rstd::signal::SignalKind>>;

    explicit Recv(rstd::signal::Receiver             receiver,
                  Option<rstd::signal::Subscription> owned = None())
        : m_owned(rstd::move(owned)), m_receiver(Some(rstd::move(receiver))) {}
    Recv(const Recv&)                    = delete;
    auto operator=(const Recv&) -> Recv& = delete;
    Recv(Recv&&) noexcept                = default;
    auto operator=(Recv&& other) noexcept -> Recv& {
        if (this != &other) {
            m_receiver = None();
            m_owned    = rstd::move(other.m_owned);
            m_receiver = rstd::move(other.m_receiver);
        }
        return *this;
    }

    auto poll(mut_ref<Recv> self, task::Context& cx) -> task::Poll<Output> {
        if (self->m_receiver.is_none()) rstd::panic { "signal receive polled after completion" };
        auto result = self->m_receiver->poll_recv(cx);
        if (result.is_ready()) {
            self->m_receiver = None();
            if (self->m_owned.is_some()) {
                auto closed   = self->m_owned->close();
                self->m_owned = None();
                if (closed.is_err())
                    return task::Poll<Output>::Ready(
                        Err(rstd::move(closed).unwrap_err_unchecked()));
            }
        }
        return result;
    }
};

export inline auto recv(const rstd::signal::Subscription& subscription) -> io::Result<Recv> {
    auto receiver = subscription.receiver();
    if (receiver.is_err()) return Err(rstd::move(receiver).unwrap_err_unchecked());
    return Ok(Recv { rstd::move(receiver).unwrap_unchecked() });
}

/// Installs the subscription before returning, even before the first poll.
/// Completion and cancellation release this subscription, not the process-wide handler.
/// \code
/// auto pending = rstd::async::signal::ctrl_c().unwrap();
/// auto runtime = rstd::async::RuntimeBuilder::current_thread().build().unwrap();
/// auto event = runtime.block_on(rstd::move(pending));
/// \endcode
export inline auto ctrl_c() -> io::Result<Recv> {
    auto subscription = rstd::signal::subscribe(rstd::signal::SignalKind::interrupt());
    if (subscription.is_err()) return Err(rstd::move(subscription).unwrap_err_unchecked());
    auto owned    = rstd::move(subscription).unwrap_unchecked();
    auto receiver = owned.receiver();
    if (receiver.is_err()) return Err(rstd::move(receiver).unwrap_err_unchecked());
    return Ok(Recv { rstd::move(receiver).unwrap_unchecked(), Some(rstd::move(owned)) });
}

} // namespace rstd::async::signal
