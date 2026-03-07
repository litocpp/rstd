module;
#include <rstd/macro.hpp>
export module rstd.alloc:boxed;
export import rstd.core;
export import :alloc;

using alloc::Allocator;
using alloc::handle_alloc_error;

using rstd::alloc::Layout;
using rstd::mem::manually_drop::ManuallyDrop;
using rstd::pin::Pin;
using rstd::ptr_::non_null::NonNull;
using rstd::ptr_::unique::Unique;
using namespace rstd;

namespace alloc::boxed
{

export template<typename T>
class Box {
    Unique<T> m_ptr;

    Box(Unique<T>&& ptr) noexcept: m_ptr(rstd::move(ptr)) {}

public:
    USE_TRAIT(Box)

    ~Box() { reset(); }
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
    Box& operator=(Box&& o) noexcept {
        if (this != &o) {
            reset();
            m_ptr = rstd::move(o.m_ptr);
        }
        return *this;
    }

    template<typename... Args>
    static auto make(Args&&... args) -> Box
        requires Impled<T, Sized>
    {
        auto layout = Layout::new_<T>();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().as_mut_ptr().template cast<T>();
        new (p.as_raw_ptr()) T(rstd::forward<Args>(args)...);
        return from_raw(p);
    }

    template<typename U>
    static auto make(U&& in) -> Box
        requires(! Impled<T, Sized> && mtp::dyn_traits<T>::template Impled<U>)
    {
        auto layout = Layout::new_<U>();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().as_mut_ptr().template cast<U>();
        new (p.as_raw_ptr()) U(rstd::forward<U>(in));
        return from_raw(T::from_ptr(p.as_raw_ptr()));
    }

    static auto pin(T&& in) -> Pin<Box>
        requires Impled<T, Sized>
    {
        return Pin<Box>::make_unchecked(make(rstd::forward<T>(in)));
    }

    static Box from_raw(mut_ptr<T> raw) noexcept { return { Unique<T>::make_unchecked(raw) }; }

    auto into_raw(this Self&& self) noexcept -> mut_ptr<T> {
        auto b = ManuallyDrop<>::make(rstd::move(self));
        return b->m_ptr.as_mut_ptr();
    }

    auto get() noexcept -> mut_ptr<T>::value_type* {
        if (m_ptr) {
            return m_ptr.as_mut_ptr().as_raw_ptr();
        } else {
            return nullptr;
        }
    }

    constexpr auto     operator->() noexcept { return m_ptr.as_mut_ptr(); }
    constexpr auto     operator->() const noexcept { return m_ptr.as_ptr(); }
    explicit constexpr operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() noexcept {
        if (m_ptr != nullptr) {
            auto mptr = m_ptr.as_mut_ptr();
            if constexpr (mtp::is_array_v<T>) {
                using V   = mtp::remove_extent_t<T>;
                usize len = mptr.len();
                auto* p   = reinterpret_cast<V*>(mptr.as_raw_ptr());
                for (usize i = 0; i < len; ++i) {
                    p[i].~V();
                }
                auto layout = Layout::array<V>(len).unwrap();
                as<Allocator>(GLOBAL).deallocate(
                    NonNull<u8>::make_unchecked(
                        mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(mptr.as_raw_ptr()))),
                    layout);
            } else if constexpr (requires { mptr.metadata()->deleter(mptr.as_raw_ptr()); }) {
                mptr.metadata()->deleter(mptr.as_raw_ptr());
            } else {
                mptr.as_raw_ptr()->~T();
                auto layout = Layout::new_<T>();
                as<Allocator>(GLOBAL).deallocate(
                    NonNull<u8>::make_unchecked(
                        mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(mptr.as_raw_ptr()))),
                    layout);
            }
            m_ptr.reset();
        }
    }

    auto as_ref() const noexcept -> ref<T> { return m_ptr.as_ptr().as_ref(); }

    auto as_ptr() const noexcept -> ptr<T> { return m_ptr.as_ptr(); }
    auto as_mut_ptr() const noexcept -> mut_ptr<T> { return m_ptr.as_mut_ptr(); }

    auto clone() -> Self
        requires mtp::is_array_v<T>
    {
        using V     = mtp::remove_extent_t<T>;
        auto old    = as_ptr();
        auto length = old.len();
        auto layout = Layout::array<V>(length).unwrap();

        auto res = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto* raw = static_cast<V*>(res.unwrap_unchecked().as_mut_ptr().as_raw_ptr());
        for (usize i = 0; i < length; ++i) {
            new (raw + i) V(old[i]);
        }
        auto p = mut_ptr<T>::from_raw_parts(raw, length);
        return from_raw(p);
    }
};
} // namespace alloc::boxed

using ::alloc::boxed::Box;
namespace rstd
{

template<typename A, mtp::same_as<convert::AsRef<const A>> T>
struct Impl<T, ::alloc::boxed::Box<A>> : ImplInClass<T, ::alloc::boxed::Box<A>> {};

template<typename A, mtp::same_as<clone::Clone> T>
    requires requires(::alloc::boxed::Box<A> b) { b.clone(); }
struct Impl<T, ::alloc::boxed::Box<A>> : ImplInClass<T, ::alloc::boxed::Box<A>> {};

} // namespace rstd
