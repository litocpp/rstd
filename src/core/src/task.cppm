export module rstd.core:task;
export import :core;
export import :panicking;

namespace rstd::task
{

export template<typename T>
class Poll {
    alignas(T) rstd::byte storage[sizeof(T)];
    bool m_ready { false };

    constexpr auto storage_ptr() noexcept -> T* {
        [[unsafe]] { return reinterpret_cast<T*>(storage); }
    }

    constexpr auto ptr() noexcept -> T* {
        [[unsafe]] { return rstd::launder(storage_ptr()); }
    }

    constexpr auto ptr() const noexcept -> const T* {
        [[unsafe]] { return rstd::launder(reinterpret_cast<const T*>(storage)); }
    }

    constexpr auto ready_value() noexcept -> T& {
        [[unsafe]] { return *ptr(); }
    }

    constexpr auto ready_value() const noexcept -> const T& {
        [[unsafe]] { return *ptr(); }
    }

    constexpr void drop_value() noexcept {
        if (m_ready) {
            [[unsafe]] { rstd::destroy_at(ptr()); }
            m_ready = false;
        }
    }

    constexpr Poll() noexcept = default;

public:
    Poll(const Poll&)            = delete;
    Poll& operator=(const Poll&) = delete;

    constexpr Poll(Poll&& other) noexcept(mtp::noex_move_v<T>) {
        if (other.m_ready) {
            [[unsafe]] { rstd::construct_at(storage_ptr(), rstd::move(other.ready_value())); }
            m_ready = true;
            other.drop_value();
        }
    }

    constexpr auto operator=(Poll&& other) noexcept(mtp::noex_move_v<T>) -> Poll& {
        if (this != &other) {
            drop_value();
            if (other.m_ready) {
                [[unsafe]] { rstd::construct_at(storage_ptr(), rstd::move(other.ready_value())); }
                m_ready = true;
                other.drop_value();
            }
        }
        return *this;
    }

    constexpr ~Poll() { drop_value(); }

    template<typename U = T>
    static constexpr auto Ready(U&& value) -> Poll {
        Poll out;
        [[unsafe]] { rstd::construct_at(out.storage_ptr(), rstd::forward<U>(value)); }
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
        return ready_value();
    }

    constexpr auto value() const& -> const T& {
        if (! m_ready) {
            rstd::panic { "Poll::value called on Pending" };
        }
        return ready_value();
    }

    constexpr auto take() && -> T {
        if (! m_ready) {
            rstd::panic { "Poll::take called on Pending" };
        }
        T out = rstd::move(ready_value());
        drop_value();
        return out;
    }
};

template<typename T>
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
        [[unsafe]] { return *m_value; }
    }

    constexpr auto take() && -> T& {
        if (m_value == nullptr) {
            rstd::panic { "Poll::take called on Pending" };
        }
        [[unsafe]] { return *m_value; }
    }
};

template<>
class Poll<void> {
    bool m_ready { false };

    constexpr explicit Poll(bool ready) noexcept: m_ready(ready) {}

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

export struct RawWaker;

export struct RawWakerVTable {
    using CloneFn = RawWaker (*)(voidp);
    using WakeFn  = void (*)(voidp);
    using DropFn  = void (*)(voidp);

    CloneFn clone;
    WakeFn  wake;
    WakeFn  wake_by_ref;
    DropFn  drop;

    static constexpr auto make(CloneFn clone, WakeFn wake, WakeFn wake_by_ref, DropFn drop)
        -> RawWakerVTable {
        return RawWakerVTable { clone, wake, wake_by_ref, drop };
    }
};

export struct RawWaker {
    voidp                 data { nullptr };
    const RawWakerVTable* vtable { nullptr };

    static constexpr auto from_raw_parts(voidp data, const RawWakerVTable* vtable) noexcept
        -> RawWaker {
        return RawWaker { data, vtable };
    }
};

inline auto noop_waker_clone(voidp) -> RawWaker;
inline void noop_waker_wake(voidp) {
}
inline void noop_waker_drop(voidp) {
}

inline const RawWakerVTable NOOP_WAKER_VTABLE {
    &noop_waker_clone,
    &noop_waker_wake,
    &noop_waker_wake,
    &noop_waker_drop,
};

inline auto noop_waker_clone(voidp) -> RawWaker {
    return RawWaker::from_raw_parts(nullptr, rstd::addressof(NOOP_WAKER_VTABLE));
}

export class Waker {
    RawWaker raw {};

    explicit Waker(RawWaker raw) noexcept: raw(raw) {}

public:
    Waker() noexcept = default;

    Waker(const Waker&)            = delete;
    Waker& operator=(const Waker&) = delete;

    Waker(Waker&& other) noexcept: raw(rstd::exchange(other.raw, {})) {}

    auto operator=(Waker&& other) noexcept -> Waker& {
        if (this != &other) {
            reset();
            raw = rstd::exchange(other.raw, {});
        }
        return *this;
    }

    ~Waker() { reset(); }

    /// Adopts one wake handle. A non-null vtable must remain valid for the handle and its clones.
    /// Callbacks must accept the associated data: clone returns a new owned handle, wake and drop
    /// each consume one handle, and wake_by_ref leaves it owned by the caller. Callbacks shared
    /// across threads must synchronize their access to the data.
    [[unsafe]]
    static auto from_raw(RawWaker raw) noexcept -> Waker {
        return Waker { raw };
    }

    /// Adopts one wake handle using the same callback and lifetime requirements as from_raw().
    [[unsafe]]
    static auto from_raw_parts(voidp data, const RawWakerVTable* vtable) noexcept -> Waker {
        [[unsafe]] { return from_raw(RawWaker::from_raw_parts(data, vtable)); }
    }

    static auto noop() noexcept -> const Waker& {
        [[unsafe]] {
            static const auto waker = from_raw_parts(nullptr, rstd::addressof(NOOP_WAKER_VTABLE));
            return waker;
        }
    }

    void reset() noexcept {
        if (raw.vtable != nullptr) {
            auto current = rstd::exchange(raw, {});
            [[unsafe]] { current.vtable->drop(current.data); }
        }
    }

    auto clone() const -> Waker {
        if (raw.vtable == nullptr) {
            return Waker {};
        }
        [[unsafe]] { return Waker::from_raw(raw.vtable->clone(raw.data)); }
    }

    void clone_from(const Waker& source) {
        if (! will_wake(source)) {
            auto cloned = source.clone();
            *this       = rstd::move(cloned);
        }
    }

    void wake() && {
        if (raw.vtable != nullptr) {
            auto current = rstd::exchange(raw, {});
            [[unsafe]] { current.vtable->wake(current.data); }
        }
    }

    void wake_by_ref() const {
        if (raw.vtable != nullptr) {
            [[unsafe]] { raw.vtable->wake_by_ref(raw.data); }
        }
    }

    auto will_wake(const Waker& other) const noexcept -> bool {
        return raw.data == other.raw.data && raw.vtable == other.raw.vtable;
    }

    auto data() const noexcept -> voidp { return raw.data; }
    auto vtable() const noexcept -> const RawWakerVTable* { return raw.vtable; }

    explicit operator bool() const noexcept { return raw.vtable != nullptr; }
};

export class Context {
    const Waker* m_waker;

public:
    explicit constexpr Context(const Waker& waker) noexcept: m_waker(rstd::addressof(waker)) {}

    static constexpr auto from_waker(const Waker& waker) noexcept -> Context {
        return Context { waker };
    }

    constexpr auto waker() const noexcept -> const Waker& {
        [[unsafe]] { return *m_waker; }
    }
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
