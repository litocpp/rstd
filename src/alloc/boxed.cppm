export module rstd.boxed;
export import rstd.core;

using rstd::pin::Pin;
namespace rstd::boxed
{

export template<typename T>
class Box {
    T* m_ptr;

    Box() noexcept = default;

public:
    ~Box() noexcept(noexcept(rstd::declval<Box>().reset())) { reset(); }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    Box(Box&& o) noexcept: m_ptr(o.m_ptr) { o.m_ptr = nullptr; }
    Box& operator=(Box&& o) noexcept(noexcept(rstd::declval<Box>().reset())) {
        if (this != &o) {
            reset();
            m_ptr   = o.m_ptr;
            o.m_ptr = nullptr;
        }
        return *this;
    }

    static Box make(param_t<T> in) {
        Box b;
        b.m_ptr = new T(rstd::param_forward<T>(in));
        return b;
    }

    static Pin<Box> pin(param_t<T> in) {
        return Pin<Box>::make_unchecked(make(rstd::param_forward<T>(in)));
    }

    // Construct from a raw pointer (takes ownership)
    static Box from_raw(T* raw) noexcept {
        Box b;
        b.m_ptr = raw;
        return b;
    }

    auto get() const noexcept -> T* { return m_ptr; }
    auto into_raw() noexcept -> T* {
        auto ptr = m_ptr;
        // take
        m_ptr = nullptr;
        return ptr;
    }

    // Access
    T&       operator*() noexcept { return *m_ptr; }
    const T& operator*() const noexcept { return *m_ptr; }
    T*       operator->() noexcept { return m_ptr; }
    const T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() noexcept(meta::nothrow_destructible<T>) {
        if (m_ptr != nullptr) {
            delete m_ptr;
            m_ptr = nullptr;
        }
    }
};

} // namespace rstd::boxed