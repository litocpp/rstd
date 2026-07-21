export module rstd:sync.mpmc.error;
export import rstd.core;
export import rstd.error;

namespace rstd::sync::mpmc
{

export template<typename T>
struct SendError {
    T value;

    auto into_inner() && -> T { return rstd::move(value); }

    bool operator==(const SendError&) const = default;
};

export struct RecvError {
    bool operator==(const RecvError&) const = default;
};

export enum class TryRecvError {
    Empty,
    Disconnected,
};

export enum class RecvTimeoutError {
    Timeout,
    Disconnected,
};

export template<typename T>
class TrySendError {
public:
    enum class Kind
    {
        Full,
        Disconnected,
    };

private:
    Kind kind_;
    T    value_;

    TrySendError(Kind kind, T value): kind_(kind), value_(rstd::move(value)) {}

public:
    static auto Full(T value) -> TrySendError {
        return TrySendError { Kind::Full, rstd::move(value) };
    }

    static auto Disconnected(T value) -> TrySendError {
        return TrySendError { Kind::Disconnected, rstd::move(value) };
    }

    auto kind() const -> Kind { return kind_; }
    bool is_full() const { return kind_ == Kind::Full; }
    bool is_disconnected() const { return kind_ == Kind::Disconnected; }

    auto value() & -> T& { return value_; }
    auto value() const& -> const T& { return value_; }
    auto into_inner() && -> T { return rstd::move(value_); }

    bool operator==(const TrySendError&) const = default;
};

export template<typename T>
class SendTimeoutError {
public:
    enum class Kind
    {
        Timeout,
        Disconnected,
    };

private:
    Kind kind_;
    T    value_;

    SendTimeoutError(Kind kind, T value): kind_(kind), value_(rstd::move(value)) {}

public:
    static auto Timeout(T value) -> SendTimeoutError {
        return SendTimeoutError { Kind::Timeout, rstd::move(value) };
    }

    static auto Disconnected(T value) -> SendTimeoutError {
        return SendTimeoutError { Kind::Disconnected, rstd::move(value) };
    }

    static auto from_send_error(SendError<T> error) -> SendTimeoutError {
        return Disconnected(rstd::move(error).into_inner());
    }

    auto kind() const -> Kind { return kind_; }
    bool is_timeout() const { return kind_ == Kind::Timeout; }
    bool is_disconnected() const { return kind_ == Kind::Disconnected; }

    auto value() & -> T& { return value_; }
    auto value() const& -> const T& { return value_; }
    auto into_inner() && -> T { return rstd::move(value_); }

    bool operator==(const SendTimeoutError&) const = default;
};

} // namespace rstd::sync::mpmc

namespace rstd
{

template<typename T>
struct Impl<fmt::Display, sync::mpmc::SendError<T>> : ImplBase<sync::mpmc::SendError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "sending on a closed channel";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::SendError<T>> : ImplBase<sync::mpmc::SendError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "SendError { .. }";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Display, sync::mpmc::TrySendError<T>> : ImplBase<sync::mpmc::TrySendError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self().is_full()) {
            constexpr char message[] = "sending on a full channel";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "sending on a closed channel";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::TrySendError<T>> : ImplBase<sync::mpmc::TrySendError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self().is_full()) {
            constexpr char message[] = "Full(..)";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "Disconnected(..)";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Display, sync::mpmc::SendTimeoutError<T>>
    : ImplBase<sync::mpmc::SendTimeoutError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self().is_timeout()) {
            constexpr char message[] = "timed out waiting on send operation";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "sending on a disconnected channel";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<fmt::Debug, sync::mpmc::SendTimeoutError<T>>
    : ImplBase<sync::mpmc::SendTimeoutError<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "SendTimeoutError(..)";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Display, sync::mpmc::RecvError> : ImplBase<sync::mpmc::RecvError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "receiving on a closed channel";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, sync::mpmc::RecvError> : ImplBase<sync::mpmc::RecvError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "RecvError";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Display, sync::mpmc::TryRecvError> : ImplBase<sync::mpmc::TryRecvError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == sync::mpmc::TryRecvError::Empty) {
            constexpr char message[] = "receiving on an empty channel";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "receiving on a closed channel";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, sync::mpmc::TryRecvError> : ImplBase<sync::mpmc::TryRecvError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == sync::mpmc::TryRecvError::Empty) {
            constexpr char message[] = "Empty";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "Disconnected";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Display, sync::mpmc::RecvTimeoutError> : ImplBase<sync::mpmc::RecvTimeoutError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == sync::mpmc::RecvTimeoutError::Timeout) {
            constexpr char message[] = "timed out waiting on channel";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "channel is empty and sending half is closed";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, sync::mpmc::RecvTimeoutError> : ImplBase<sync::mpmc::RecvTimeoutError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == sync::mpmc::RecvTimeoutError::Timeout) {
            constexpr char message[] = "Timeout";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "Disconnected";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<typename T>
struct Impl<error::Error, sync::mpmc::SendError<T>>
    : LinkClassMethod<error::Error, sync::mpmc::SendError<T>> {};

template<typename T>
struct Impl<error::Error, sync::mpmc::TrySendError<T>>
    : LinkClassMethod<error::Error, sync::mpmc::TrySendError<T>> {};

template<typename T>
struct Impl<error::Error, sync::mpmc::SendTimeoutError<T>>
    : LinkClassMethod<error::Error, sync::mpmc::SendTimeoutError<T>> {};

template<>
struct Impl<error::Error, sync::mpmc::RecvError>
    : LinkClassMethod<error::Error, sync::mpmc::RecvError> {};

template<>
struct Impl<error::Error, sync::mpmc::TryRecvError>
    : LinkClassMethod<error::Error, sync::mpmc::TryRecvError> {};

template<>
struct Impl<error::Error, sync::mpmc::RecvTimeoutError>
    : LinkClassMethod<error::Error, sync::mpmc::RecvTimeoutError> {};

} // namespace rstd
