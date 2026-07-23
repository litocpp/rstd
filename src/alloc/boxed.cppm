module;
#include <rstd/macro.hpp>
export module rstd.alloc:boxed;
export import rstd.core;
export import :alloc;

using alloc::handle_alloc_error;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;
namespace mtp = rstd::mtp;
using namespace rstd::prelude;

namespace alloc::boxed
{

/// A pointer type that uniquely owns a heap allocation of type `T`.
/// A moved-from `Box` may only be destroyed or assigned to. Any other use panics.
/// \tparam T The type of the value stored on the heap.
export template<typename T>
class Box {
    Option<NonNull<T>> m_ptr;

    [[noreturn]]
    static void panic_moved() {
        rstd::panic { "Box used after move" };
    }

    [[nodiscard]]
    constexpr auto checked_ptr() const noexcept -> NonNull<T> {
        if (m_ptr.is_none()) panic_moved();
        return *m_ptr;
    }

    [[nodiscard]]
    constexpr auto take_ptr() noexcept -> NonNull<T> {
        auto ptr = checked_ptr();
        m_ptr    = Option<NonNull<T>> {};
        return ptr;
    }

    void drop() noexcept {
        if (m_ptr.is_none()) return;

        auto mptr   = checked_ptr().as_mut_ptr();
        auto layout = Layout::for_value(mptr.as_ptr());
        rstd::ptr_::drop_in_place(mptr);
        as<Allocator>(GLOBAL).deallocate(mptr.as_raw_ptr(), layout);
        m_ptr = Option<NonNull<T>> {};
    }

    constexpr explicit Box(NonNull<T> ptr) noexcept: m_ptr(Some(ptr)) {
        if (! ptr) rstd::panic { "Box cannot be constructed from null" };
    }

public:
    USE_TRAIT(Box)

    using Target = T;

    ~Box() { drop(); }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    /// Creates a new `Box` by cloning the contained value.
    /// \return A new `Box` with a cloned copy of the value.
    auto clone() const -> Self
        requires Impled<T, Clone, Sized>
    {
        return make(as<Clone>(*as_ptr()).clone());
    }
    /// Replaces the contents of this `Box` with a clone of the source.
    /// \param source The `Box` to clone from.
    void clone_from(const Self& source)
        requires requires(Box b) { b.clone(); }
    {
        static_cast<void>(checked_ptr());
        *this = source.clone();
    }

    constexpr Box(Box&& o) noexcept: Box(o.take_ptr()) {}
    Box& operator=(Box&& o) noexcept {
        if (this != &o) {
            auto ptr = o.take_ptr();
            drop();
            m_ptr = Some(ptr);
        }
        return *this;
    }

    /// Allocates memory on the heap and constructs `T` in place with the given arguments.
    /// \tparam Args The constructor argument types.
    /// \param args The arguments forwarded to the constructor of `T`.
    /// \return A `Box` owning the newly allocated value.
    template<typename... Args>
    static auto make(Args&&... args) -> Box
        requires Impled<T, Sized>
    {
        auto layout = Layout::make<T>();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<T>();
        rstd::ptr_::construct(p, rstd::forward<Args>(args)...);
        return from_raw(p);
    }

    /// Allocates memory on the heap for a dynamically-sized trait object.
    /// \tparam U The concrete type that implements the trait `T`.
    /// \param in The value to box as a trait object.
    /// \return A `Box` owning the trait object.
    template<typename U>
    static auto make(U&& in) -> Box
        requires(! Impled<T, Sized> && mtp::dyn_traits<T>::template Impled<U>)
    {
        auto layout = Layout::make<U>();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<U>();
        new (p.as_raw_ptr()) U(rstd::forward<U>(in));
        return from_raw(T::from_ptr(p.as_raw_ptr()));
    }

    /// Constructs a `Box` from a raw mutable pointer.
    /// \param raw A non-null pointer that was previously obtained from `into_raw`.
    /// \return A `Box` that takes ownership of the pointed-to value.
    constexpr static Box from_raw(mut_ptr<T> raw) noexcept {
        return Box { NonNull<T>::make_unchecked(raw) };
    }

    /// Consumes the `Box`, returning the wrapped raw pointer without deallocating.
    /// \return A mutable pointer to the heap-allocated value.
    constexpr auto into_raw() && noexcept -> mut_ptr<T> { return take_ptr().as_mut_ptr(); }

    /// Returns a raw pointer to the contained value.
    /// \return A non-null raw pointer to the heap-allocated value.
    constexpr auto get() noexcept [[clang::lifetimebound]] {
        return checked_ptr().as_mut_ptr().as_raw_ptr();
    }

    /// Returns an immutable borrow of the contained value.
    constexpr auto deref() const noexcept [[clang::lifetimebound]] -> ref<T> { return as_ref(); }
    /// Returns a mutable borrow of the contained value.
    constexpr auto deref_mut() noexcept [[clang::lifetimebound]] -> mut_ref<T> {
        return checked_ptr().as_mut_ptr().as_mut_ref();
    }

    /// Returns an immutable reference to the contained value.
    /// \return A `ref<T>` to the boxed value.
    constexpr auto as_ref() const noexcept [[clang::lifetimebound]] -> ref<T> {
        return checked_ptr().as_ptr().as_ref();
    }

    /// Returns a const pointer to the contained value.
    /// \return A `ptr<T>` to the boxed value.
    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return checked_ptr().as_ptr();
    }
    /// Returns a mutable pointer to the contained value.
    /// \return A `mut_ptr<T>` to the boxed value.
    constexpr auto as_mut_ptr() const noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return checked_ptr().as_mut_ptr();
    }

    /// Downcasts a boxed `Any` value to its concrete type.
    template<typename U>
    auto downcast() && -> Result<Box<U>, Box>
        requires mtp::same_as<T, rstd::dyn<rstd::any::Any>>
    {
        if (! rstd::any::is<U>(as_ref())) return Err(rstd::move(*this));

        auto raw      = rstd::move(*this).into_raw();
        auto concrete = mut_ptr<U>::from_raw_parts(static_cast<U*>(raw.as_raw_ptr()));
        return Ok(Box<U>::from_raw(concrete));
    }

    /// Creates a new `Box` by cloning all elements of the contained array.
    /// \return A new `Box` owning a cloned copy of the array.
    auto clone() const -> Self
        requires mtp::is_array<T>
    {
        using V     = mtp::rm_ext<T>;
        auto old    = as_ptr();
        auto length = old.len();
        auto layout = Layout::array<V>(length).unwrap();

        auto res = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto raw = res.unwrap_unchecked().template as_mut_ptr<V>();
        for (rstd::size_t index = 0; index < length.to_primitive(); ++index) {
            rstd::ptr_::construct(raw.add(usize(index)), old[usize(index)]);
        }
        auto p = mut_ptr<T>::from_raw_parts(raw.as_raw_ptr(), length);
        return from_raw(p);
    }
};
} // namespace alloc::boxed

using ::alloc::boxed::Box;
namespace rstd
{

} // namespace rstd
