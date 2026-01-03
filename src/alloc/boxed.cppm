export module rstd.boxed;
export import rstd.core;

using rstd::mem::manually_drop::ManuallyDrop;
using rstd::pin::Pin;
using rstd::ptr_::unique::Unique;
namespace rstd::boxed
{

export template<typename T>
class Box {
    Unique<T> m_ptr;

    Box(Unique<T>&& ptr) noexcept: m_ptr(rstd::move(ptr)) {}

public:
    ~Box() noexcept(noexcept(rstd::declval<Box>().reset())) { reset(); }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    Box(Box&& o) noexcept: m_ptr(rstd::move(o.m_ptr)) {}
    Box& operator=(Box&& o) noexcept(noexcept(rstd::declval<Box>().reset())) {
        if (this != &o) {
            reset();
            m_ptr = rstd::move(o.m_ptr);
        }
        return *this;
    }

    static auto make(param_t<T> in) -> Box
        requires Impled<T, Sized>
    {
        auto t = new T(rstd::param_forward<T>(in));
        return from_raw(t);
    }

    static auto pin(param_t<T> in) -> Pin<Box>
        requires Impled<T, Sized>
    {
        return Pin<Box>::make_unchecked(make(rstd::param_forward<T>(in)));
    }

    // Construct from a raw pointer (takes ownership)
    static Box from_raw(ptr<T> raw) noexcept { return { Unique<T>::make_unchecked(raw) }; }

    auto get() const noexcept -> T* { return m_ptr; }
    auto into_raw() noexcept -> ptr<T> {
        auto b = ManuallyDrop<>::make(rstd::move(*this));
        return b->m_ptr.as_ptr();
    }

    // Access
    // T&       operator*() noexcept { return *m_ptr.as_ptr(); }
    // const T& operator*() const noexcept { return *m_ptr.as_ptr(); }
    auto     operator->() noexcept { return m_ptr.as_ptr(); }
    auto     operator->() const noexcept { return m_ptr.as_ptr(); }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() noexcept(meta::nothrow_destructible<T>) {
        if (m_ptr != nullptr) {
            T* t = m_ptr.as_ptr();
            delete t;
            m_ptr = Unique<T>::make_unchecked(nullptr);
        }
    }
};
} // namespace rstd::boxed