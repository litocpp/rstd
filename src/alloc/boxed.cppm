module;
#include <rstd/macro.hpp>
export module rstd.alloc.boxed;
export import rstd.core;

using rstd::mem::manually_drop::ManuallyDrop;
using rstd::pin::Pin;
using rstd::ptr_::unique::Unique;
namespace rstd::alloc::boxed
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
        return from_raw(ptr<T>::from_raw(t));
    }

    static auto pin(param_t<T> in) -> Pin<Box>
        requires Impled<T, Sized>
    {
        return Pin<Box>::make_unchecked(make(rstd::param_forward<T>(in)));
    }

    // Construct from a raw pointer (takes ownership)
    static Box from_raw(ptr<T> raw) noexcept { return { Unique<T>::make_unchecked(raw) }; }

    auto get() noexcept -> ptr<T>::value_type* { return m_ptr.as_mut_ptr(); }
    auto into_raw() noexcept -> ptr<T> {
        auto b = ManuallyDrop<>::make(rstd::move(*this));
        return b->m_ptr.as_mut_ptr();
    }

    auto     operator->() noexcept { return m_ptr.as_mut_ptr(); }
    auto     operator->() const noexcept { return m_ptr.as_ptr(); }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() noexcept(meta::destructible<T> || meta::is_array_v<T>) {
        if (m_ptr != nullptr) {
            ptr<T> t = m_ptr.as_mut_ptr();
            rstd::default_delete<T> {}(&*t);
            m_ptr = Unique<T>::make_unchecked({ .p = nullptr });
        }
    }

    auto as_ref() const noexcept -> ref<const T> {
        debug_assert(m_ptr != nullptr);
        return ref<const T>::from_raw(m_ptr.as_ptr());
    }

    auto as_ptr() const noexcept -> ptr<const T> {
        debug_assert(m_ptr != nullptr);
        return m_ptr.as_ptr();
    }
    auto as_mut_ptr() noexcept -> ptr<T> {
        debug_assert(m_ptr != nullptr);
        return m_ptr.as_mut_ptr();
    }
};
} // namespace rstd::alloc::boxed

using rstd::alloc::boxed::Box;
namespace rstd
{

template<typename A, meta::same_as<convert::AsRef<const A>> T>
struct Impl<T, Box<A>> : ImplInClass<T, Box<A>> {};

} // namespace rstd