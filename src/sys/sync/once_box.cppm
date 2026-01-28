export module rstd.sys:sync.once_box;
export import rstd.core;
export import rstd.alloc;

using rstd::alloc::boxed::Box;
using rstd::pin::Pin;
using rstd::sync::atomic::Atomic;

export namespace rstd::sys::sync
{

template<typename T>
class OnceBox {
    Atomic<T*> m_ptr;

    constexpr OnceBox(T* p) noexcept: m_ptr(p) {}

public:
    ~OnceBox() { take(); }
    constexpr OnceBox(const OnceBox&) noexcept            = delete;
    constexpr OnceBox& operator=(const OnceBox&) noexcept = delete;
    OnceBox(OnceBox&& other) noexcept
        : m_ptr(other.m_ptr.exchange(nullptr, rstd::memory_order::acq_rel)) {}

    OnceBox& operator=(OnceBox&& other) noexcept {
        if (this != &other) {
            m_ptr.store(other.m_ptr.exchange(nullptr), rstd::memory_order::release);
        }
        return *this;
    }

    static OnceBox make() noexcept { return { nullptr }; }

    // Unsafe: caller must ensure the value is initialized and observed.
    auto get_unchecked() const noexcept -> Pin<T&> {
        return Pin<T&>::make_unchecked(*m_ptr.load(rstd::memory_order::relaxed));
    }

    template<typename F>
    auto get_or_init(F&& f) -> Pin<T&> {
        T* p = m_ptr.load(rstd::memory_order::acquire);
        if (p) {
            return Pin<T&>::make_unchecked(*p);
        }
        return initialize(rstd::forward<F>(f));
    }

    auto take() noexcept -> Option<Pin<Box<T>>> {
        T* p = m_ptr.exchange(nullptr, rstd::memory_order::relaxed);
        if (p) {
            return rstd::Some(Pin<Box<T>>::make_unchecked(Box<T>::from_raw(mut_ptr<T>::from_raw_parts(p))));

        }
        return {};
    }

    constexpr explicit operator bool() const noexcept { return m_ptr != nullptr; }
    T*                 operator->() { return m_ptr.load(rstd::memory_order::relaxed); }

private:
    template<typename F>
    auto initialize(F&& f) -> Pin<T&> {
        T* raw      = f().get_unchecked_mut().into_raw();
        T* expected = nullptr;
        if (m_ptr.compare_exchange_strong(
                expected, raw, rstd::memory_order::release, rstd::memory_order::acquire)) {
            return Pin<T&>::make_unchecked(*raw);
        } else {
            // Lost the race: destroy our allocation and use the winner's pointer.
            delete raw;
            return Pin<T&>::make_unchecked(*expected);
        }
    }
};

} // namespace rstd::sys::sync
