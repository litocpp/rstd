export module rstd:sync.mpmc;
export import :sync.mpmc.error;
export import :time;
export import rstd.core;
import :sync.mpmc.detail;

namespace rstd::sync::mpmc
{

enum class ChannelFlavor
{
    Array,
    List,
    Zero,
};

export template<typename T>
class Receiver;

export template<typename T>
class Iter;

export template<typename T>
class TryIter;

export template<typename T>
class IntoIter;

export template<typename T>
class Sender {
    detail::Sender<Box<detail::Channel<T>>>     inner_array;
    detail::Sender<Box<detail::ListChannel<T>>> inner_list;
    detail::Sender<Box<detail::ZeroChannel<T>>> inner_zero;
    ChannelFlavor                               flavor;

    void release() {
        switch (flavor) {
        case ChannelFlavor::Array:
            inner_array.release([](auto* channel) {
                (*channel)->disconnect_senders();
            });
            break;
        case ChannelFlavor::List:
            inner_list.release([](auto* channel) {
                (*channel)->disconnect_senders();
            });
            break;
        case ChannelFlavor::Zero:
            inner_zero.release([](auto* channel) {
                (*channel)->disconnect();
            });
            break;
        }
    }

public:
    explicit Sender(detail::Sender<Box<detail::Channel<T>>> inner)
        : inner_array(rstd::move(inner)), flavor(ChannelFlavor::Array) {}

    explicit Sender(detail::Sender<Box<detail::ListChannel<T>>> inner)
        : inner_list(rstd::move(inner)), flavor(ChannelFlavor::List) {}

    explicit Sender(detail::Sender<Box<detail::ZeroChannel<T>>> inner)
        : inner_zero(rstd::move(inner)), flavor(ChannelFlavor::Zero) {}

    auto try_send(T msg) const -> Result<empty, TrySendError<T>> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->try_send(rstd::move(msg));
        case ChannelFlavor::List: return inner_list->try_send(rstd::move(msg));
        case ChannelFlavor::Zero: return inner_zero->try_send(rstd::move(msg));
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto send(T msg) const -> Result<empty, SendError<T>> {
        Result<empty, SendTimeoutError<T>> result = [&] {
            switch (flavor) {
            case ChannelFlavor::Array: return inner_array->send(rstd::move(msg), None());
            case ChannelFlavor::List: return inner_list->send(rstd::move(msg), None());
            case ChannelFlavor::Zero: return inner_zero->send(rstd::move(msg), None());
            }
            rstd::panic { "invalid channel sender flavor" };
        }();
        if (result.is_ok()) return Ok(empty {});
        return Err(SendError<T> { rstd::move(result).unwrap_err_unchecked().into_inner() });
    }

    auto send_timeout(T msg, time::Duration timeout) const -> Result<empty, SendTimeoutError<T>> {
        auto deadline = time::Instant::now().checked_add(timeout);
        if (deadline.is_some()) return send_deadline(rstd::move(msg), *deadline);

        auto result = send(rstd::move(msg));
        if (result.is_ok()) return Ok(empty {});
        return Err(SendTimeoutError<T>::from_send_error(rstd::move(result).unwrap_err_unchecked()));
    }

    auto send_deadline(T msg, time::Instant deadline) const -> Result<empty, SendTimeoutError<T>> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->send(rstd::move(msg), Some(deadline));
        case ChannelFlavor::List: return inner_list->send(rstd::move(msg), Some(deadline));
        case ChannelFlavor::Zero: return inner_zero->send(rstd::move(msg), Some(deadline));
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto is_empty() const -> bool {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->is_empty();
        case ChannelFlavor::List: return inner_list->is_empty();
        case ChannelFlavor::Zero: return inner_zero->is_empty();
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto is_full() const -> bool {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->is_full();
        case ChannelFlavor::List: return inner_list->is_full();
        case ChannelFlavor::Zero: return inner_zero->is_full();
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto len() const -> usize {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->len();
        case ChannelFlavor::List: return inner_list->len();
        case ChannelFlavor::Zero: return inner_zero->len();
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto capacity() const -> Option<usize> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->capacity();
        case ChannelFlavor::List: return inner_list->capacity();
        case ChannelFlavor::Zero: return inner_zero->capacity();
        }
        rstd::panic { "invalid channel sender flavor" };
    }

    auto same_channel(const Sender& other) const -> bool {
        if (flavor != other.flavor) return false;
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array == other.inner_array;
        case ChannelFlavor::List: return inner_list == other.inner_list;
        case ChannelFlavor::Zero: return inner_zero == other.inner_zero;
        }
        return false;
    }

    ~Sender() { release(); }

    Sender(const Sender& other): flavor(other.flavor) {
        switch (flavor) {
        case ChannelFlavor::Array: inner_array = other.inner_array.acquire(); break;
        case ChannelFlavor::List: inner_list = other.inner_list.acquire(); break;
        case ChannelFlavor::Zero: inner_zero = other.inner_zero.acquire(); break;
        }
    }

    auto operator=(const Sender& other) -> Sender& {
        if (this != &other) {
            release();
            flavor = other.flavor;
            switch (flavor) {
            case ChannelFlavor::Array: inner_array = other.inner_array.acquire(); break;
            case ChannelFlavor::List: inner_list = other.inner_list.acquire(); break;
            case ChannelFlavor::Zero: inner_zero = other.inner_zero.acquire(); break;
            }
        }
        return *this;
    }

    Sender(Sender&& other) noexcept
        : inner_array(rstd::move(other.inner_array)),
          inner_list(rstd::move(other.inner_list)),
          inner_zero(rstd::move(other.inner_zero)),
          flavor(other.flavor) {}

    auto operator=(Sender&& other) noexcept -> Sender& {
        if (this != &other) {
            release();
            inner_array = rstd::move(other.inner_array);
            inner_list  = rstd::move(other.inner_list);
            inner_zero  = rstd::move(other.inner_zero);
            flavor      = other.flavor;
        }
        return *this;
    }
};

export template<typename T>
class Receiver {
    detail::Receiver<Box<detail::Channel<T>>>     inner_array;
    detail::Receiver<Box<detail::ListChannel<T>>> inner_list;
    detail::Receiver<Box<detail::ZeroChannel<T>>> inner_zero;
    ChannelFlavor                                 flavor;

    void release() {
        switch (flavor) {
        case ChannelFlavor::Array:
            inner_array.release([](auto* channel) {
                (*channel)->disconnect_receivers();
            });
            break;
        case ChannelFlavor::List:
            inner_list.release([](auto* channel) {
                (*channel)->disconnect_receivers();
            });
            break;
        case ChannelFlavor::Zero:
            inner_zero.release([](auto* channel) {
                (*channel)->disconnect();
            });
            break;
        }
    }

public:
    explicit Receiver(detail::Receiver<Box<detail::Channel<T>>> inner)
        : inner_array(rstd::move(inner)), flavor(ChannelFlavor::Array) {}

    explicit Receiver(detail::Receiver<Box<detail::ListChannel<T>>> inner)
        : inner_list(rstd::move(inner)), flavor(ChannelFlavor::List) {}

    explicit Receiver(detail::Receiver<Box<detail::ZeroChannel<T>>> inner)
        : inner_zero(rstd::move(inner)), flavor(ChannelFlavor::Zero) {}

    auto try_recv() const -> Result<T, TryRecvError> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->try_recv();
        case ChannelFlavor::List: return inner_list->try_recv();
        case ChannelFlavor::Zero: return inner_zero->try_recv();
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto recv() const -> Result<T, RecvError> {
        Result<T, RecvTimeoutError> result = [&] {
            switch (flavor) {
            case ChannelFlavor::Array: return inner_array->recv(None());
            case ChannelFlavor::List: return inner_list->recv(None());
            case ChannelFlavor::Zero: return inner_zero->recv(None());
            }
            rstd::panic { "invalid channel receiver flavor" };
        }();
        if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
        return Err(RecvError {});
    }

    auto recv_timeout(time::Duration timeout) const -> Result<T, RecvTimeoutError> {
        auto deadline = time::Instant::now().checked_add(timeout);
        if (deadline.is_some()) return recv_deadline(*deadline);

        auto result = recv();
        if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
        return Err(RecvTimeoutError::Disconnected);
    }

    auto recv_deadline(time::Instant deadline) const -> Result<T, RecvTimeoutError> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->recv(Some(deadline));
        case ChannelFlavor::List: return inner_list->recv(Some(deadline));
        case ChannelFlavor::Zero: return inner_zero->recv(Some(deadline));
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto is_empty() const -> bool {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->is_empty();
        case ChannelFlavor::List: return inner_list->is_empty();
        case ChannelFlavor::Zero: return inner_zero->is_empty();
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto is_full() const -> bool {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->is_full();
        case ChannelFlavor::List: return inner_list->is_full();
        case ChannelFlavor::Zero: return inner_zero->is_full();
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto len() const -> usize {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->len();
        case ChannelFlavor::List: return inner_list->len();
        case ChannelFlavor::Zero: return inner_zero->len();
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto capacity() const -> Option<usize> {
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array->capacity();
        case ChannelFlavor::List: return inner_list->capacity();
        case ChannelFlavor::Zero: return inner_zero->capacity();
        }
        rstd::panic { "invalid channel receiver flavor" };
    }

    auto same_channel(const Receiver& other) const -> bool {
        if (flavor != other.flavor) return false;
        switch (flavor) {
        case ChannelFlavor::Array: return inner_array == other.inner_array;
        case ChannelFlavor::List: return inner_list == other.inner_list;
        case ChannelFlavor::Zero: return inner_zero == other.inner_zero;
        }
        return false;
    }

    auto iter() const -> Iter<T>;
    auto try_iter() const -> TryIter<T>;
    auto into_iter() const& -> Iter<T>;
    auto into_iter() && -> IntoIter<T>;

    ~Receiver() { release(); }

    Receiver(const Receiver& other): flavor(other.flavor) {
        switch (flavor) {
        case ChannelFlavor::Array: inner_array = other.inner_array.acquire(); break;
        case ChannelFlavor::List: inner_list = other.inner_list.acquire(); break;
        case ChannelFlavor::Zero: inner_zero = other.inner_zero.acquire(); break;
        }
    }

    auto operator=(const Receiver& other) -> Receiver& {
        if (this != &other) {
            release();
            flavor = other.flavor;
            switch (flavor) {
            case ChannelFlavor::Array: inner_array = other.inner_array.acquire(); break;
            case ChannelFlavor::List: inner_list = other.inner_list.acquire(); break;
            case ChannelFlavor::Zero: inner_zero = other.inner_zero.acquire(); break;
            }
        }
        return *this;
    }

    Receiver(Receiver&& other) noexcept
        : inner_array(rstd::move(other.inner_array)),
          inner_list(rstd::move(other.inner_list)),
          inner_zero(rstd::move(other.inner_zero)),
          flavor(other.flavor) {}

    auto operator=(Receiver&& other) noexcept -> Receiver& {
        if (this != &other) {
            release();
            inner_array = rstd::move(other.inner_array);
            inner_list  = rstd::move(other.inner_list);
            inner_zero  = rstd::move(other.inner_zero);
            flavor      = other.flavor;
        }
        return *this;
    }
};

export template<typename T>
class Iter : public DefaultInClass<Iter<T>, rstd::iter::Iterator> {
    const Receiver<T>* receiver;

public:
    using Item = T;

    explicit Iter(const Receiver<T>* receiver): receiver(receiver) {}

    auto next() -> Option<T> {
        auto result = receiver->recv();
        if (result.is_err()) return None();
        return Some(rstd::move(result).unwrap_unchecked());
    }
};

export template<typename T>
class TryIter : public DefaultInClass<TryIter<T>, rstd::iter::Iterator> {
    const Receiver<T>* receiver;

public:
    using Item = T;

    explicit TryIter(const Receiver<T>* receiver): receiver(receiver) {}

    auto next() -> Option<T> {
        auto result = receiver->try_recv();
        if (result.is_err()) return None();
        return Some(rstd::move(result).unwrap_unchecked());
    }
};

export template<typename T>
class IntoIter : public DefaultInClass<IntoIter<T>, rstd::iter::Iterator> {
    Receiver<T> receiver;

public:
    using Item = T;

    explicit IntoIter(Receiver<T> receiver): receiver(rstd::move(receiver)) {}

    auto next() -> Option<T> {
        auto result = receiver.recv();
        if (result.is_err()) return None();
        return Some(rstd::move(result).unwrap_unchecked());
    }
};

template<typename T>
auto Receiver<T>::iter() const -> Iter<T> {
    return Iter<T> { this };
}

template<typename T>
auto Receiver<T>::try_iter() const -> TryIter<T> {
    return TryIter<T> { this };
}

template<typename T>
auto Receiver<T>::into_iter() const& -> Iter<T> {
    return iter();
}

template<typename T>
auto Receiver<T>::into_iter() && -> IntoIter<T> {
    return IntoIter<T> { rstd::move(*this) };
}

export template<typename T>
auto channel() -> rstd::tuple<Sender<T>, Receiver<T>> {
    auto channel        = detail::ListChannel<T>::make();
    auto [sender, recv] = detail::new_counter(rstd::move(channel));
    return { Sender<T>(rstd::move(sender)), Receiver<T>(rstd::move(recv)) };
}

export template<typename T>
auto sync_channel(usize capacity) -> rstd::tuple<Sender<T>, Receiver<T>> {
    if (capacity == usize()) {
        auto channel        = detail::ZeroChannel<T>::make();
        auto [sender, recv] = detail::new_counter(rstd::move(channel));
        return { Sender<T>(rstd::move(sender)), Receiver<T>(rstd::move(recv)) };
    }

    auto channel        = detail::Channel<T>::with_capacity(capacity);
    auto [sender, recv] = detail::new_counter(rstd::move(channel));
    return { Sender<T>(rstd::move(sender)), Receiver<T>(rstd::move(recv)) };
}

} // namespace rstd::sync::mpmc

namespace rstd
{

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::Sender<T>> : ImplBase<sync::mpmc::Sender<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "Sender { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::Receiver<T>> : ImplBase<sync::mpmc::Receiver<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "Receiver { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::Iter<T>> : ImplBase<sync::mpmc::Iter<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "Iter { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::TryIter<T>> : ImplBase<sync::mpmc::TryIter<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "TryIter { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::IntoIter<T>> : ImplBase<sync::mpmc::IntoIter<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "IntoIter { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

} // namespace rstd
