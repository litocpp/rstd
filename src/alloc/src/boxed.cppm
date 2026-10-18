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
export template<typename T, typename A = ::alloc::Global>
class Box {
    static_assert(__is_nothrow_constructible(A, A&&), "allocator moves must not throw");
    RSTD_ATTR_NO_UNIQUE_ADDRESS A allocator_;
    Option<NonNull<T>>            m_ptr;
    struct StorageGuard {
        const A& allocator;
        void*    pointer;
        Layout   layout;
        ~StorageGuard() {
            if (pointer) as<Allocator>(allocator).deallocate(pointer, layout);
        }
    };

    [[noreturn]]
    static void panic_moved(rstd::source_location loc = rstd::source_location::current()) {
        rstd::panic_message("Box used after move", loc);
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
        if (layout.size != usize()) as<Allocator>(allocator_).deallocate(mptr.as_raw_ptr(), layout);
        m_ptr = Option<NonNull<T>> {};
    }

    constexpr explicit Box(NonNull<T> ptr, A allocator)
        : allocator_(rstd::move(allocator)), m_ptr(Some(ptr)) {
        if (! ptr) rstd::panic { "Box cannot be constructed from null" };
    }

public:
    USE_TRAIT(Box)

    ~Box() { drop(); }
    Box(const Box&) noexcept            = delete;
    Box& operator=(const Box&) noexcept = delete;

    /// Creates a new `Box` by cloning the contained value.
    /// \return A new `Box` with a cloned copy of the value.
    auto clone() const -> Self
        requires Impled<T, Clone, Sized> && rstd::alloc::CloneableAllocator<A>
    {
        static_cast<void>(checked_ptr());
        return clone_in(rstd::alloc::clone_allocator(allocator_));
    }
    /// Replaces the contents of this `Box` with a clone of the source.
    /// \param source The `Box` to clone from.
    void clone_from(const Self& source)
        requires requires(Box b) {
            b.clone();
            b = rstd::move(b);
        }
    {
        static_cast<void>(checked_ptr());
        *this = source.clone_in(rstd::alloc::clone_allocator(allocator_));
    }

    constexpr Box(Box&& o) noexcept(noexcept(A(rstd::move(o.allocator_))))
        : allocator_(rstd::move(o.allocator_)), m_ptr(Some(o.take_ptr())) {}
    Box& operator=(Box&& o) noexcept(noexcept(allocator_ = rstd::move(o.allocator_)))
        requires requires(A& a, A&& b) {
            { a = rstd::move(b) } noexcept;
        }
    {
        if (this != &o) {
            drop();
            allocator_ = rstd::move(o.allocator_);
            m_ptr      = Some(o.take_ptr());
        }
        return *this;
    }

    /// Allocates memory on the heap and constructs `T` in place with the given arguments.
    /// \tparam Args The constructor argument types.
    /// \param args The arguments forwarded to the constructor of `T`.
    /// Returns `AllocError` on allocation failure; constructor exceptions are not converted.
    template<typename... Args>
    static auto try_make_in(A allocator, Args&&... args) -> Result<Box, rstd::alloc::AllocError>
        requires Impled<T, Sized>
    {
        auto res = as<Allocator>(allocator).allocate(Layout::make<T>());
        if (res.is_err()) return Err(rstd::alloc::AllocError {});
        auto         p = res.unwrap_unchecked().template as_mut_ptr<T>();
        StorageGuard guard { allocator, p.as_raw_ptr(), Layout::make<T>() };
        rstd::ptr_::construct(p, rstd::forward<Args>(args)...);
        auto result   = from_raw_in(p, rstd::move(allocator));
        guard.pointer = nullptr;
        return Ok(rstd::move(result));
    }

    template<typename... Args>
    static auto make_in(A allocator, Args&&... args) -> Box
        requires Impled<T, Sized>
    {
        auto result = try_make_in(rstd::move(allocator), rstd::forward<Args>(args)...);
        if (result.is_err()) handle_alloc_error(Layout::make<T>());
        return result.unwrap_unchecked();
    }

    /// Allocates memory on the heap for a dynamically-sized trait object.
    /// \tparam U The concrete type that implements the trait `T`.
    /// \param in The value to box as a trait object.
    /// Returns `AllocError` on allocation failure without moving from the input.
    template<typename U>
    static auto try_make_in(A allocator, U&& in) -> Result<Box, rstd::alloc::AllocError>
        requires(! Impled<T, Sized>) && Impled<mtp::rm_cvf<U>, Sized> &&
                mtp::dyn_traits<T>::template
    Impled<mtp::rm_cvf<U>> {
        using Concrete = mtp::rm_cvf<U>;
        auto layout    = Layout::make<Concrete>();
        auto res       = as<Allocator>(allocator).allocate(layout);
        if (res.is_err()) return Err(rstd::alloc::AllocError {});

        auto         p = res.unwrap_unchecked().template as_mut_ptr<Concrete>();
        StorageGuard guard { allocator, p.as_raw_ptr(), layout };
        new (p.as_raw_ptr()) Concrete(rstd::forward<U>(in));
        auto result   = from_raw_in(T::from_ptr(p), rstd::move(allocator));
        guard.pointer = nullptr;
        return Ok(rstd::move(result));
    }

    template<typename U>
    static auto make_in(A allocator, U&& in) -> Box
        requires(! Impled<T, Sized>) && Impled<mtp::rm_cvf<U>, Sized> &&
                mtp::dyn_traits<T>::template
    Impled<mtp::rm_cvf<U>> {
        auto result = try_make_in(rstd::move(allocator), rstd::forward<U>(in));
        if (result.is_err()) handle_alloc_error(Layout::make<mtp::rm_cvf<U>>());
        return result.unwrap_unchecked();
    }

    template<typename... Args>
    static auto try_make(Args&&... args) -> Result<Box, rstd::alloc::AllocError>
        requires mtp::same_as<A, ::alloc::Global> &&
                 requires { try_make_in(A {}, rstd::forward<Args>(args)...); }
    {
        return try_make_in(A {}, rstd::forward<Args>(args)...);
    }
    template<typename... Args>
    static auto make(Args&&... args) -> Box
        requires mtp::same_as<A, ::alloc::Global> &&
                 requires { try_make_in(A {}, rstd::forward<Args>(args)...); }
    {
        return make_in(A {}, rstd::forward<Args>(args)...);
    }
    auto allocator() const noexcept [[clang::lifetimebound]] -> const A& {
        static_cast<void>(checked_ptr());
        return allocator_;
    }

    template<typename B>
    auto try_clone_in(B allocator) const -> Result<Box<T, B>, rstd::alloc::AllocError>
        requires Impled<T, Clone, Sized>
    {
        auto const& value = *as_ptr();
        return Box<T, B>::try_make_in(rstd::move(allocator), as<Clone>(value).clone());
    }
    template<typename B>
    auto clone_in(B allocator) const -> Box<T, B>
        requires Impled<T, Clone, Sized>
    {
        auto const& value = *as_ptr();
        return Box<T, B>::make_in(rstd::move(allocator), as<Clone>(value).clone());
    }

    /// Constructs a `Box` from a raw mutable pointer.
    /// \param raw A non-null pointer that was previously obtained from `into_raw`.
    /// \return A `Box` that takes ownership of the pointed-to value.
    constexpr static Box from_raw(mut_ptr<T> raw) noexcept
        requires mtp::same_as<A, ::alloc::Global>
    {
        return from_raw_in(raw, A {});
    }
    /// The pointer must belong to this allocator, with a layout fitting T and its metadata.
    constexpr static auto from_raw_in(mut_ptr<T> raw, A allocator) -> Box {
        return Box { NonNull<T>::make_unchecked(raw), rstd::move(allocator) };
    }
    /// Transfers the allocation and its allocator. The pair does not destroy the payload.
    auto into_raw_with_allocator() && -> rstd::tuple<mut_ptr<T>, A> {
        return { take_ptr().as_mut_ptr(), rstd::move(allocator_) };
    }

    /// Consumes the `Box`, returning the wrapped raw pointer without deallocating.
    /// \return A mutable pointer to the heap-allocated value.
    constexpr auto into_raw() && noexcept -> mut_ptr<T>
        requires mtp::same_as<A, ::alloc::Global>
    {
        return take_ptr().as_mut_ptr();
    }

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

    constexpr auto begin() noexcept [[clang::lifetimebound]] -> mut_ptr<mtp::rm_ext<T>>
        requires mtp::is_array<T>
    {
        return mut_ptr<mtp::rm_ext<T>>::from_raw_parts(as_mut_ptr().as_raw_ptr());
    }

    constexpr auto end() noexcept [[clang::lifetimebound]] -> mut_ptr<mtp::rm_ext<T>>
        requires mtp::is_array<T>
    {
        return begin().add(as_ptr().len());
    }

    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<mtp::rm_ext<T>>
        requires mtp::is_array<T>
    {
        return ptr<mtp::rm_ext<T>>::from_raw_parts(as_ptr().as_raw_ptr());
    }

    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<mtp::rm_ext<T>>
        requires mtp::is_array<T>
    {
        return begin().add(as_ptr().len());
    }

    /// Transfers a sized value to a trait object without moving or reallocating the payload.
    template<typename D>
    auto into_dyn() && -> Box<D, A>
        requires Impled<T, Sized> && (! Impled<D, Sized>) && mtp::dyn_traits<D>::template
    Impled<T> {
        auto pointer = take_ptr().as_mut_ptr();
        return Box<D, A>::from_raw_in(D::from_ptr(pointer), rstd::move(allocator_));
    }

    /// Downcasts a boxed `Any` value to its concrete type.
    template<typename U>
    auto downcast() && -> Result<Box<U, A>, Box>
        requires mtp::same_as<T, rstd::dyn<rstd::any::Any>>
    {
        if (! rstd::any::is<U>(as_ref())) return Err(rstd::move(*this));

        auto raw      = take_ptr().as_mut_ptr();
        using Storage = typename mut_ptr<U>::storage_type;
        auto concrete = mut_ptr<U>::from_raw_parts(static_cast<Storage*>(raw.as_raw_ptr()));
        return Ok(Box<U, A>::from_raw_in(concrete, rstd::move(allocator_)));
    }

    /// Downcasts a boxed `Error` value to its concrete error type.
    template<typename U>
    auto downcast() && -> Result<Box<U, A>, Box>
        requires mtp::same_as<T, rstd::dyn<rstd::error::Error>> && Impled<U, rstd::error::Error>
    {
        if (! rstd::error::is<U>(as_ref())) return Err(rstd::move(*this));

        auto raw      = take_ptr().as_mut_ptr();
        using Storage = typename mut_ptr<U>::storage_type;
        auto concrete = mut_ptr<U>::from_raw_parts(static_cast<Storage*>(raw.as_raw_ptr()));
        return Ok(Box<U, A>::from_raw_in(concrete, rstd::move(allocator_)));
    }

    template<typename B>
    auto try_clone_in(B allocator) const -> Result<Box<T, B>, rstd::alloc::AllocError>
        requires mtp::is_array<T> && Impled<mtp::rm_ext<T>, Clone>
    {
        using V     = mtp::rm_ext<T>;
        auto old    = as_ptr();
        auto length = old.len();
        auto layout = Layout::array<V>(length).unwrap();
        if (length == usize()) {
            auto raw = NonNull<V>::dangling().as_mut_ptr();
            return Ok(Box<T, B>::from_raw_in(mut_ptr<T>::from_raw_parts(raw.as_raw_ptr(), length),
                                             rstd::move(allocator)));
        }
        auto res = as<Allocator>(allocator).allocate(layout);
        if (res.is_err()) return Err(rstd::alloc::AllocError {});
        auto raw = res.unwrap_unchecked().template as_mut_ptr<V>();
        struct Guard {
            B&         allocator;
            mut_ptr<V> pointer;
            Layout     layout;
            usize      initialized {};
            bool       active { true };
            ~Guard() {
                if (! active) return;
                for (usize i; i < initialized; ++i) rstd::ptr_::destroy(pointer.add(i));
                as<Allocator>(allocator).deallocate(pointer.as_raw_ptr(), layout);
            }
        } guard { allocator, raw, layout };
        for (usize i; i < length; ++i) {
            auto const& value = old[i];
            rstd::ptr_::construct(raw.add(i), as<Clone>(value).clone());
            ++guard.initialized;
        }
        auto result  = Box<T, B>::from_raw_in(mut_ptr<T>::from_raw_parts(raw.as_raw_ptr(), length),
                                              rstd::move(allocator));
        guard.active = false;
        return Ok(rstd::move(result));
    }
    template<typename B>
    auto clone_in(B allocator) const -> Box<T, B>
        requires mtp::is_array<T> && Impled<mtp::rm_ext<T>, Clone>
    {
        auto result = try_clone_in(rstd::move(allocator));
        if (result.is_err()) handle_alloc_error(Layout::for_value(as_ptr()));
        return result.unwrap_unchecked();
    }
    auto clone() const -> Self
        requires mtp::is_array<T> && Impled<mtp::rm_ext<T>, Clone> &&
                 rstd::alloc::CloneableAllocator<A>
    {
        static_cast<void>(checked_ptr());
        return clone_in(rstd::alloc::clone_allocator(allocator_));
    }
};

export template<typename T, typename A = ::alloc::Global>
class BoxIntoIter : public rstd::DefaultInClass<BoxIntoIter<T, A>, rstd::iter::Iterator> {
    Box<T[], A> values;
    usize       front;
    usize       back;

public:
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    explicit BoxIntoIter(Box<T[], A> source)
        : values(rstd::move(source)), front(), back(values.as_ptr().len()) {}

    auto next() -> Option<Item> {
        if (front == back) return None();
        auto element = values.begin().add(front++);
        return Some(T(rstd::move(*element)));
    }

    auto next_back() -> Option<Item> {
        if (front == back) return None();
        auto element = values.begin().add(--back);
        return Some(T(rstd::move(*element)));
    }

    auto size_hint() const -> rstd::iter::SizeHint {
        auto length = len();
        return { length, Some(length) };
    }

    auto len() const -> usize { return back - front; }
};
} // namespace alloc::boxed

using ::alloc::boxed::Box;
namespace rstd
{

template<typename T, typename A>
struct Impl<ops::Deref, ::alloc::boxed::Box<T, A>> : ImplBase<::alloc::boxed::Box<T, A>> {
    using Target = T;

    constexpr auto deref() const noexcept -> ref<Target> { return this->self().deref(); }
};

template<typename T, typename A>
struct Impl<ops::DerefMut, ::alloc::boxed::Box<T, A>> : ImplBase<::alloc::boxed::Box<T, A>> {
    constexpr auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<::alloc::boxed::Box<T, A>>> {
        return this->self().deref_mut();
    }
};

template<typename T, typename A>
struct Impl<iter::IntoIterator, ::alloc::boxed::Box<T[], A>>
    : ImplBase<::alloc::boxed::Box<T[], A>> {
    using IntoIter = ::alloc::boxed::BoxIntoIter<T, A>;

    auto into_iter() -> IntoIter { return IntoIter(rstd::move(this->self())); }
};

template<typename T, typename A>
struct Impl<iter::IntoIterator, ref<::alloc::boxed::Box<T[], A>>>
    : ImplBase<ref<::alloc::boxed::Box<T[], A>>> {
    using IntoIter = iter::SliceIter<T>;

    auto into_iter() -> IntoIter {
        auto& source = *this->self();
        return { source.begin(), source.end() };
    }
};

template<typename T, typename A>
struct Impl<iter::IntoIterator, mut_ref<::alloc::boxed::Box<T[], A>>>
    : ImplBase<mut_ref<::alloc::boxed::Box<T[], A>>> {
    using IntoIter = iter::SliceIterMut<T>;

    auto into_iter() -> IntoIter {
        auto& source = *this->self();
        return { source.begin(), source.end() };
    }
};

template<typename A>
struct Impl<fmt::Display, ::alloc::boxed::Box<dyn<error::Error>, A>>
    : ImplBase<::alloc::boxed::Box<dyn<error::Error>, A>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto value = this->self().as_ref();
        return as<fmt::Display>(value).fmt(formatter);
    }
};

template<typename A>
struct Impl<fmt::Debug, ::alloc::boxed::Box<dyn<error::Error>, A>>
    : ImplBase<::alloc::boxed::Box<dyn<error::Error>, A>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto value = this->self().as_ref();
        return as<fmt::Debug>(value).fmt(formatter);
    }
};

} // namespace rstd
