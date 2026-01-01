export module rstd.sys:sync.once_box;
export import rstd.core;
export import rstd.boxed;

using rstd::boxed::Box;
using rstd::pin::Pin;

export namespace rstd::sys::sync
{

template<typename T>
class OnceBox {
    rstd::atomic<T*> m_ptr;

    constexpr OnceBox() noexcept = delete;

public:
    ~OnceBox() { take(); }
    constexpr OnceBox(const OnceBox&) noexcept            = delete;
    constexpr OnceBox& operator=(const OnceBox&) noexcept = delete;

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
            return rstd::Some(Pin<Box<T>>::make_unchecked(Box<T>::from_raw(p)));
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
