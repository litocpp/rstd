module;
#include <rstd/macro.hpp>
export module rstd.core:task;
export import :core;
export import :panicking;

namespace rstd::task
{

export template<typename T>
class Poll {
    alignas(T) rstd::byte storage[sizeof(T)];
    bool m_ready { false };

    constexpr auto ptr() noexcept -> T* {
        return rstd::launder(reinterpret_cast<T*>(storage));
    }

    constexpr auto ptr() const noexcept -> const T* {
        return rstd::launder(reinterpret_cast<const T*>(storage));
    }

    constexpr void drop_value() noexcept {
        if (m_ready) {
            rstd::destroy_at(ptr());
            m_ready = false;
        }
    }

    constexpr Poll() noexcept = default;

public:
    Poll(const Poll&)            = delete;
    Poll& operator=(const Poll&) = delete;

    constexpr Poll(Poll&& other) noexcept(mtp::noex_move<T>) : m_ready(other.m_ready) {
        if (m_ready) {
            rstd::construct_at(ptr(), rstd::move(*other.ptr()));
            other.drop_value();
        }
    }

    constexpr auto operator=(Poll&& other) noexcept(mtp::noex_move<T>) -> Poll& {
        if (this != &other) {
            drop_value();
            m_ready = other.m_ready;
            if (m_ready) {
                rstd::construct_at(ptr(), rstd::move(*other.ptr()));
                other.drop_value();
            }
        }
        return *this;
    }

    constexpr ~Poll() { drop_value(); }

    template<typename U = T>
    static constexpr auto Ready(U&& value) -> Poll {
        Poll out;
        rstd::construct_at(out.ptr(), rstd::forward<U>(value));
        out.m_ready = true;
        return out;
    }

    static constexpr auto Pending() noexcept -> Poll { return Poll {}; }

    constexpr auto is_ready() const noexcept -> bool { return m_ready; }
    constexpr auto is_pending() const noexcept -> bool { return ! m_ready; }

    constexpr auto value() & -> T& {
        if (! m_ready) {
            rstd::panic { "Poll::value called on Pending" };
        }
        return *ptr();
    }

    constexpr auto value() const& -> const T& {
        if (! m_ready) {
            rstd::panic { "Poll::value called on Pending" };
        }
        return *ptr();
    }

    constexpr auto take() && -> T {
        if (! m_ready) {
            rstd::panic { "Poll::take called on Pending" };
        }
        T out = rstd::move(*ptr());
        drop_value();
        return out;
    }
};

export template<typename T>
class Poll<T&> {
    T* m_value { nullptr };

    constexpr Poll() noexcept = default;

public:
    static constexpr auto Ready(T& value) noexcept -> Poll {
        Poll out;
        out.m_value = rstd::addressof(value);
        return out;
    }

    static constexpr auto Pending() noexcept -> Poll { return Poll {}; }

    constexpr auto is_ready() const noexcept -> bool { return m_value != nullptr; }
    constexpr auto is_pending() const noexcept -> bool { return m_value == nullptr; }

    constexpr auto value() const -> T& {
        if (m_value == nullptr) {
            rstd::panic { "Poll::value called on Pending" };
        }
        return *m_value;
    }

    constexpr auto take() && -> T& {
        if (m_value == nullptr) {
            rstd::panic { "Poll::take called on Pending" };
        }
        return *m_value;
    }
};

export template<>
class Poll<void> {
    bool m_ready { false };

    constexpr explicit Poll(bool ready) noexcept : m_ready(ready) {}

public:
    static constexpr auto Ready() noexcept -> Poll { return Poll { true }; }
    static constexpr auto Pending() noexcept -> Poll { return Poll { false }; }

    constexpr auto is_ready() const noexcept -> bool { return m_ready; }
    constexpr auto is_pending() const noexcept -> bool { return ! m_ready; }

    constexpr void take() && {
        if (! m_ready) {
            rstd::panic { "Poll::take called on Pending" };
        }
    }
};

export struct RawWakerVTable {
    voidp (*clone)(voidp);
    void (*wake)(voidp);
    void (*wake_by_ref)(voidp);
    void (*drop)(voidp);
};

export struct RawWaker {
    voidp data { nullptr };
    const RawWakerVTable* vtable { nullptr };
};

export class Waker {
    RawWaker raw {};

    explicit Waker(RawWaker raw) noexcept : raw(raw) {}

public:
    Waker() noexcept = default;

    Waker(const Waker&)            = delete;
    Waker& operator=(const Waker&) = delete;

    Waker(Waker&& other) noexcept : raw(rstd::exchange(other.raw, {})) {}

    auto operator=(Waker&& other) noexcept -> Waker& {
        if (this != &other) {
            reset();
            raw = rstd::exchange(other.raw, {});
        }
        return *this;
    }

    ~Waker() { reset(); }

    static auto from_raw(RawWaker raw) noexcept -> Waker { return Waker { raw }; }

    void reset() noexcept {
        if (raw.vtable != nullptr) {
            auto current = rstd::exchange(raw, {});
            current.vtable->drop(current.data);
        }
    }

    auto clone() const -> Waker {
        if (raw.vtable == nullptr) {
            return Waker {};
        }
        return Waker::from_raw(RawWaker { raw.vtable->clone(raw.data), raw.vtable });
    }

    void wake() && {
        if (raw.vtable != nullptr) {
            auto current = rstd::exchange(raw, {});
            current.vtable->wake(current.data);
        }
    }

    void wake_by_ref() const {
        if (raw.vtable != nullptr) {
            raw.vtable->wake_by_ref(raw.data);
        }
    }

    auto will_wake(const Waker& other) const noexcept -> bool {
        return raw.data == other.raw.data && raw.vtable == other.raw.vtable;
    }

    explicit operator bool() const noexcept { return raw.vtable != nullptr; }
};

export class Context {
    const Waker* m_waker;

public:
    explicit constexpr Context(const Waker& waker) noexcept : m_waker(rstd::addressof(waker)) {}

    constexpr auto waker() const noexcept -> const Waker& { return *m_waker; }
};

export template<typename T>
constexpr auto Ready(T&& value) {
    return Poll<mtp::rm_ref<T>>::Ready(rstd::forward<T>(value));
}

export constexpr auto Ready() noexcept {
    return Poll<void>::Ready();
}

export template<typename T = void>
constexpr auto Pending() noexcept {
    return Poll<T>::Pending();
}

} // namespace rstd::task

namespace rstd
{
export using task::Poll;
}
