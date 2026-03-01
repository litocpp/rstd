module;
#include <rstd/macro.hpp>
export module rstd:alloc.boxed;
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
    USE_TRAIT(Box)

    ~Box()
    // noexcept(noexcept(rstd::declval<Box>().reset()))
    {
        reset();
    }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    auto clone() -> Self
        requires Impled<T, clone::Clone, Sized>
    {
        return make(as<clone::Clone>(as_ptr()).clone());
    }
    void clone_from(Self& source)
        requires requires(Box b) { b.clone(); }
    {
        *this = source.clone();
    }

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
        return from_raw(mut_ptr<T>::from_raw_parts(t));
    }

    template<typename U>
    static auto make(U&& in) -> Box
        requires(! Impled<T, Sized> && mtp::dyn_traits<T>::template Impled<U>)
    {
        auto t = new U(rstd::forward<U>(in));
        return from_raw(T::from_ptr(t));
    }

    static auto pin(param_t<T> in) -> Pin<Box>
        requires Impled<T, Sized>
    {
        return Pin<Box>::make_unchecked(make(rstd::param_forward<T>(in)));
    }

    // Construct from a raw pointer (takes ownership)
    static Box from_raw(mut_ptr<T> raw) noexcept { return { Unique<T>::make_unchecked(raw) }; }
    auto       into_raw(this Self&& self) noexcept -> mut_ptr<T> {
        auto b = ManuallyDrop<>::make(rstd::move(self));
        return b->m_ptr.as_mut_ptr();
    }

    auto get() noexcept -> mut_ptr<T>::value_type* {
        if (m_ptr) {
            return m_ptr.as_mut_ptr();
        } else {
            return nullptr;
        }
    }

    constexpr auto     operator->() noexcept { return m_ptr.as_mut_ptr(); }
    constexpr auto     operator->() const noexcept { return m_ptr.as_ptr(); }
    explicit constexpr operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() noexcept(mtp::destructible<T> || mtp::is_array_v<T>) {
        if (m_ptr != nullptr) {
            auto mptr = m_ptr.as_mut_ptr();
            if constexpr (requires { mptr.metadata()->deleter(mptr.as_raw_ptr()); }) {
                mptr.metadata()->deleter(mptr.as_raw_ptr());
            } else {
                rstd::default_delete<T> {}(mptr.as_raw_ptr());
            }
            m_ptr.reset();
        }
    }

    auto as_ref() const noexcept -> ref<T> {
        debug_assert(m_ptr != nullptr);
        return m_ptr.as_ptr().as_ref();
    }

    auto as_ptr() const noexcept -> ptr<T> {
        debug_assert(m_ptr != nullptr);
        return m_ptr.as_ptr();
    }
    auto as_mut_ptr() noexcept -> ptr<T> {
        debug_assert(m_ptr != nullptr);
        return m_ptr.as_mut_ptr();
    }

    // for T[]
    auto clone() -> Self
        requires mtp::is_array_v<T>
    {
        using V = mtp::remove_extent_t<T>;
        // auto old = as_ptr();
        // TODO
        auto p = mut_ptr<T>::from_raw_parts(new V[3] {}, 3);
        return from_raw(p);
    }
};
} // namespace rstd::alloc::boxed

using rstd::alloc::boxed::Box;
namespace rstd
{

template<typename A, mtp::same_as<convert::AsRef<const A>> T>
struct Impl<T, Box<A>> : ImplInClass<T, Box<A>> {};

template<typename A, mtp::same_as<clone::Clone> T>
    requires requires(Box<A> b) { b.clone(); }
struct Impl<T, Box<A>> : ImplInClass<T, Box<A>> {};

} // namespace rstd