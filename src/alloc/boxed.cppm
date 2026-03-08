module;
#include <rstd/macro.hpp>
export module rstd.alloc:boxed;
export import rstd.core;
export import :alloc;

using alloc::handle_alloc_error;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::mem::manually_drop::ManuallyDrop;
using rstd::pin::Pin;
using rstd::ptr_::non_null::NonNull;
namespace mtp = rstd::mtp;
using namespace rstd::prelude;

namespace alloc::boxed
{

export template<typename T>
class Box {
    NonNull<T> m_ptr;

    constexpr explicit Box(NonNull<T> ptr) noexcept: m_ptr(ptr) {}

public:
    USE_TRAIT(Box)

    ~Box() { reset(); }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    auto clone() -> Self
        requires Impled<T, Clone, Sized>
    {
        return make(as<Clone>(as_ptr()).clone());
    }
    void clone_from(Self& source)
        requires requires(Box b) { b.clone(); }
    {
        *this = source.clone();
    }

    constexpr Box(Box&& o) noexcept: m_ptr(o.m_ptr) { rstd::mem::fill(o.m_ptr, 0); }
    Box& operator=(Box&& o) noexcept {
        if (this != &o) {
            // clean
            reset();
            // assign
            m_ptr = o.m_ptr;
            // move
            rstd::mem::fill(o.m_ptr, 0);
        }
        return *this;
    }

    template<typename... Args>
    static auto make(Args&&... args) -> Box
        requires Impled<T, Sized>
    {
        auto layout = Layout::make<T>();
        auto res    = GLOBAL.allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().as_mut_ptr().template cast<T>();
        new (p.as_raw_ptr()) T(rstd::forward<Args>(args)...);
        return from_raw(p);
    }

    template<typename U>
    static auto make(U&& in) -> Box
        requires(! Impled<T, Sized> && mtp::dyn_traits<T>::template Impled<U>)
    {
        auto layout = Layout::make<U>();
        auto res    = GLOBAL.allocate(layout);
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

    constexpr static Box from_raw(mut_ptr<T> raw) noexcept {
        return Box { NonNull<T>::make_unchecked(raw) };
    }

    constexpr auto into_raw(this Self&& self) noexcept -> mut_ptr<T> {
        auto b = ManuallyDrop<>::make(rstd::move(self));
        return b->m_ptr.as_mut_ptr();
    }

    constexpr auto get() noexcept -> mut_ptr<T>::value_type* {
        return m_ptr.as_mut_ptr().as_raw_ptr();
    }

    constexpr auto     operator->() noexcept { return m_ptr.as_mut_ptr(); }
    constexpr auto     operator->() const noexcept { return m_ptr.as_ptr(); }
    explicit constexpr operator bool() const noexcept { return ! rstd::mem::all(m_ptr, 0); }

    void reset() noexcept {
        if (! rstd::mem::all(m_ptr, 0)) {
            auto mptr         = m_ptr.as_mut_ptr();
            auto raw_non_null = NonNull<u8>::make_unchecked(
                mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(mptr.as_raw_ptr())));
            Option<Layout> layout {};

            if constexpr (mtp::is_array_v<T>) {
                using V   = mtp::remove_extent_t<T>;
                usize len = mptr.len();
                auto* p   = reinterpret_cast<V*>(mptr.as_raw_ptr());
                for (usize i = 0; i < len; ++i) {
                    p[i].~V();
                }
                layout = Some(Layout::array<V>(len).unwrap());
            } else if constexpr (requires { mptr.metadata()->drop; }) {
                auto const* meta = mptr.metadata();
                meta->drop(mptr.as_raw_ptr());
                layout = Some(Layout::from_size_align(meta->size, meta->align).unwrap());
            } else {
                mptr.as_raw_ptr()->~T();
                layout = Some(Layout::make<T>());
            }
            GLOBAL.deallocate(raw_non_null, layout.unwrap());
            rstd::mem::fill(m_ptr, 0);
        }
    }

    constexpr auto as_ref() const noexcept -> ref<T> { return m_ptr.as_ptr().as_ref(); }

    constexpr auto as_ptr() const noexcept -> ptr<T> { return m_ptr.as_ptr(); }
    constexpr auto as_mut_ptr() const noexcept -> mut_ptr<T> { return m_ptr.as_mut_ptr(); }

    auto clone() -> Self
        requires mtp::is_array_v<T>
    {
        using V     = mtp::remove_extent_t<T>;
        auto old    = as_ptr();
        auto length = old.len();
        auto layout = Layout::array<V>(length).unwrap();

        auto res = GLOBAL.allocate(layout);
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

template<typename A, mtp::same_as<AsRef<const A>> T>
struct Impl<T, ::alloc::boxed::Box<A>> : LinkClassMethod<T, ::alloc::boxed::Box<A>> {};

template<typename A, mtp::same_as<Clone> T>
    requires requires(::alloc::boxed::Box<A> b) { b.clone(); }
struct Impl<T, ::alloc::boxed::Box<A>> : LinkClassMethod<T, ::alloc::boxed::Box<A>> {};

} // namespace rstd
