module;
#include <rstd/macro.hpp>
export module rstd.alloc:sync;
export import rstd.core;
export import :alloc;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::mem::maybe_uninit::maybe_uninit_traits;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::ptr_::non_null::NonNull;
using rstd::sync::atomic::Atomic;
namespace mtp = rstd::mtp;
using namespace rstd::prelude;

constexpr usize ARC_MAX_REFCOUNT = usize::MAX / usize(2);

struct ArcHeader;
struct ArcAllocationOps {
    void (*release)(ArcHeader*) noexcept;
    ref<dyn<Allocator>> (*allocator)(ArcHeader*) noexcept;
    ArcHeader* (*allocator_owner)(ArcHeader*) noexcept;
    bool allocator_bound;
};
template<typename T, typename A>
struct ArcAllocatorAccess;

struct ArcHeader {
    Atomic<usize>           strong { usize(1) };
    Atomic<usize>           weak { usize(1) };
    const ArcAllocationOps* allocation;

    void inc_strong() {
        [[maybe_unused]]
        auto old = strong.fetch_add(usize(1), rstd::sync::atomic::Ordering::Relaxed);
        debug_assert(old < ARC_MAX_REFCOUNT);
    }

    void inc_weak() {
        [[maybe_unused]]
        auto old = weak.fetch_add(usize(1), rstd::sync::atomic::Ordering::Relaxed);
        debug_assert(old < ARC_MAX_REFCOUNT);
    }

    bool try_inc_strong() {
        usize current = strong.load(rstd::sync::atomic::Ordering::Acquire);
        while (current != usize()) {
            debug_assert(current < ARC_MAX_REFCOUNT);
            if (strong.compare_exchange_weak(current,
                                             current + usize(1),
                                             rstd::sync::atomic::Ordering::AcqRel,
                                             rstd::sync::atomic::Ordering::Acquire)) {
                return true;
            }
        }
        return false;
    }
};

export namespace alloc::sync
{
template<typename T>
class Arc;

/// Keeps an Arc's allocator alive without keeping its value alive.
class ArcAllocator {
    ArcHeader* header_ {};

    template<typename>
    friend class Arc;
    template<typename T, typename A>
    friend struct ::ArcAllocatorAccess;

    explicit ArcAllocator(ArcHeader* header) noexcept: header_(header) {
        if (header_) header_->inc_weak();
    }
    void reset() noexcept {
        auto* header = rstd::exchange(header_, nullptr);
        if (header &&
            header->weak.fetch_sub(usize(1), rstd::sync::atomic::Ordering::AcqRel) == usize(1)) {
            rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
            header->allocation->release(header);
        }
    }

public:
    ArcAllocator()                                       = default;
    ArcAllocator(const ArcAllocator&)                    = delete;
    auto operator=(const ArcAllocator&) -> ArcAllocator& = delete;
    ArcAllocator(ArcAllocator&& other) noexcept: header_(rstd::exchange(other.header_, nullptr)) {}
    auto operator=(ArcAllocator&& other) noexcept -> ArcAllocator& {
        if (this != &other) {
            reset();
            header_ = rstd::exchange(other.header_, nullptr);
        }
        return *this;
    }
    ~ArcAllocator() { reset(); }
    auto clone() const noexcept -> ArcAllocator { return ArcAllocator(header_); }
    auto as_ref() const noexcept [[clang::lifetimebound]] -> ref<dyn<Allocator>> {
        return header_ ? header_->allocation->allocator(header_)
                       : ::alloc::allocator_ref(::alloc::GLOBAL);
    }
    auto allocate(Layout layout) const -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->allocate(layout);
    }
    auto allocate_zeroed(Layout layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->allocate_zeroed(layout);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        as_ref()->deallocate(pointer, layout);
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->shrink(pointer, old_layout, new_layout);
    }
};
} // namespace alloc::sync

struct ArcAllocationLayout {
    Layout layout;
    usize  value_offset;
};

constexpr auto arc_allocation_layout(Layout value_layout) -> ArcAllocationLayout {
    usize value_offset {};
    auto  layout = Layout::make<ArcHeader>().extend(value_layout, value_offset).unwrap();
    return ArcAllocationLayout { .layout = layout.pad_to_align(), .value_offset = value_offset };
}

template<typename T>
auto arc_allocation_layout(mut_ptr<T> pointer) -> ArcAllocationLayout {
    return arc_allocation_layout(Layout::for_value(pointer.as_ptr()));
}

template<typename T>
auto arc_header(mut_ptr<T> pointer) noexcept -> ArcHeader* {
    auto  allocation = arc_allocation_layout(pointer);
    auto* value      = reinterpret_cast<rstd::byte*>(pointer.as_raw_ptr());
    return rstd::launder(
        reinterpret_cast<ArcHeader*>(value - allocation.value_offset.to_primitive()));
}

template<typename T, typename A>
struct ArcAllocatorAccess {
    static auto stored(ArcHeader* header) noexcept -> A& {
        usize offset;
        (void)arc_allocation_layout(Layout::make<T>())
            .layout.extend(Layout::make<A>(), offset)
            .unwrap();
        return *rstd::launder(
            reinterpret_cast<A*>(reinterpret_cast<rstd::byte*>(header) + offset.to_primitive()));
    }
    static auto allocator(ArcHeader* header) noexcept -> ref<dyn<Allocator>> {
        if constexpr (mtp::same_as<A, ::alloc::Global>)
            return ::alloc::allocator_ref(::alloc::GLOBAL);
        else if constexpr (mtp::same_as<A, ::alloc::sync::ArcAllocator>)
            return stored(header).as_ref();
        else
            return ::alloc::allocator_ref(stored(header));
    }
    static auto owner(ArcHeader* header) noexcept -> ArcHeader* {
        if constexpr (mtp::same_as<A, ::alloc::Global>)
            return nullptr;
        else if constexpr (mtp::same_as<A, ::alloc::sync::ArcAllocator>)
            return stored(header).header_;
        else
            return header;
    }
};

template<typename T, typename A>
void arc_release_allocation(ArcHeader* header) noexcept {
    auto allocation = arc_allocation_layout(Layout::make<T>());
    if constexpr (mtp::same_as<A, ::alloc::Global>) {
        rstd::destroy_at(header);
        rstd::as<Allocator>(::alloc::GLOBAL).deallocate(header, allocation.layout);
    } else {
        usize allocator_offset {};
        auto  layout =
            allocation.layout.extend(Layout::make<A>(), allocator_offset).unwrap().pad_to_align();
        auto* stored    = rstd::launder(reinterpret_cast<A*>(reinterpret_cast<rstd::byte*>(header) +
                                                             allocator_offset.to_primitive()));
        auto  allocator = rstd::move(*stored);
        rstd::destroy_at(stored);
        rstd::destroy_at(header);
        rstd::as<Allocator>(allocator).deallocate(header, layout);
    }
}

template<typename T, bool AllocatorBound = false, typename A, typename F>
auto arc_try_allocate_value_with(A allocator, F&& initialize)
    -> Result<mut_ptr<T>, rstd::alloc::AllocError> {
    static_assert(__is_nothrow_constructible(A, A&&), "allocator moves must not throw");
    auto  allocation = arc_allocation_layout(Layout::make<T>());
    usize allocator_offset {};
    auto  layout = allocation.layout;
    if constexpr (! mtp::same_as<A, ::alloc::Global>) {
        auto checked_layout = layout.extend(Layout::make<A>(), allocator_offset);
        if (checked_layout.is_none()) return Err(rstd::alloc::AllocError {});
        layout = checked_layout.unwrap_unchecked().pad_to_align();
    }
    auto result = rstd::as<Allocator>(allocator).allocate(layout);
    if (result.is_err()) return Err(rstd::alloc::AllocError {});

    auto* base   = static_cast<rstd::byte*>(result.unwrap_unchecked().pointer);
    auto* header = rstd::construct_at(reinterpret_cast<ArcHeader*>(base));
    static constexpr ArcAllocationOps operations { &arc_release_allocation<T, A>,
                                                   &ArcAllocatorAccess<T, A>::allocator,
                                                   &ArcAllocatorAccess<T, A>::owner,
                                                   AllocatorBound };
    header->allocation = &operations;
    if constexpr (! mtp::same_as<A, ::alloc::Global>) {
        rstd::construct_at(reinterpret_cast<A*>(base + allocator_offset.to_primitive()),
                           rstd::move(allocator));
    }
    struct Guard {
        ArcHeader* header;
        ~Guard() {
            if (header) header->allocation->release(header);
        }
    } guard { header };
    auto* value = reinterpret_cast<T*>(base + allocation.value_offset.to_primitive());
    initialize(value, header->allocation->allocator(header));
    guard.header = nullptr;
    return Ok(mut_ptr<T>::from_raw_parts(value));
}

template<typename T, typename A, typename... Args>
auto arc_try_allocate_value_in(A allocator, Args&&... args)
    -> Result<mut_ptr<T>, rstd::alloc::AllocError> {
    return arc_try_allocate_value_with<T>(
        rstd::move(allocator), [&](T* value, ref<dyn<Allocator>>) {
            rstd::construct_at(value, rstd::forward<Args>(args)...);
        });
}

template<typename T>
void arc_deallocate(mut_ptr<T> pointer) noexcept {
    auto* header = arc_header(pointer);
    header->allocation->release(header);
}

template<typename T>
void arc_drop_weak(mut_ptr<T> pointer) noexcept {
    auto* header = arc_header(pointer);
    if (header->weak.fetch_sub(usize(1), rstd::sync::atomic::Ordering::AcqRel) == usize(1)) {
        rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
        arc_deallocate(pointer);
    }
}

template<typename T>
void arc_drop_strong(mut_ptr<T> pointer) noexcept {
    auto* header = arc_header(pointer);
    if (header->strong.fetch_sub(usize(1), rstd::sync::atomic::Ordering::AcqRel) == usize(1)) {
        rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
        rstd::ptr_::drop_in_place(pointer);
        arc_drop_weak(pointer);
    }
}

template<typename T>
struct ArcData {
    mut_ptr<T> pointer {};
};

namespace alloc::sync
{

/// A thread-safe reference-counting pointer, analogous to Rust's `Arc<T>`.
export template<typename T>
class Arc;

/// A non-owning weak reference to an `Arc` allocation.
export template<typename T>
class Weak;

/// An owning raw representation used for low-level interop.
export template<typename T>
class ArcRaw {
    mut_ptr<T> pointer {};

    friend class Arc<T>;
    constexpr explicit ArcRaw(mut_ptr<T> pointer) noexcept: pointer(pointer) {}

public:
    auto as_ptr() const noexcept -> mut_ptr<T> { return pointer; }

    auto into_ptr() noexcept -> mut_ptr<T> {
        auto result = pointer;
        pointer.reset();
        return result;
    }

    static auto from_ptr(mut_ptr<T> pointer) noexcept -> ArcRaw { return ArcRaw { pointer }; }

    auto into_raw() noexcept -> voidp
        requires Impled<T, Sized>
    {
        return static_cast<voidp>(into_ptr().as_raw_ptr());
    }

    static auto from_raw(voidp pointer) noexcept -> ArcRaw
        requires Impled<T, Sized>
    {
        return from_ptr(mut_ptr<T>::from_raw_parts(static_cast<T*>(pointer)));
    }
};

/// A thread-safe reference-counting pointer, analogous to Rust's `Arc<T>`.
export template<typename T>
class Arc : public DefaultInClass<Arc<T>, Clone> {
    ArcData<T> self;

    template<typename>
    friend class Weak;

    template<typename>
    friend class Arc;

    constexpr explicit Arc(ArcData<T> data) noexcept: self(data) {}

public:
    USE_TRAIT(Arc)

    using element_type = T;

    constexpr Arc(const Arc&)            = delete;
    constexpr Arc& operator=(const Arc&) = delete;

    constexpr Arc(Arc&& other) noexcept: self(rstd::exchange(other.self, {})) {}
    constexpr Arc& operator=(Arc&& other) noexcept {
        if (this != &other) {
            reset();
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    ~Arc() { reset(); }

    auto clone() const -> Arc {
        if (self.pointer != nullptr) arc_header(self.pointer)->inc_strong();
        return Arc { self };
    }

    void reset() noexcept {
        if (self.pointer != nullptr) {
            arc_drop_strong(self.pointer);
            self.pointer.reset();
        }
    }

    /// The allocator must support concurrent operations and final release on another thread.
    template<typename A, typename... Args>
    static auto try_make_in(A allocator, Args&&... args) -> Result<Arc, rstd::alloc::AllocError>
        requires Impled<T, Sized> && Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator>
    {
        auto result =
            arc_try_allocate_value_in<T>(rstd::move(allocator), rstd::forward<Args>(args)...);
        if (result.is_err()) return Err(rstd::alloc::AllocError {});
        return Ok(Arc { ArcData<T> { .pointer = result.unwrap_unchecked() } });
    }

    /// The factory borrows the allocator at its final address in the control block.
    /// The borrow remains valid until the value is destroyed; it must not escape that lifetime.
    /// Values created this way cannot be extracted with try_unwrap.
    template<typename A, typename F>
    static auto try_make_in_with(A allocator, F factory) -> Result<Arc, rstd::alloc::AllocError>
        requires Impled<T, Sized> && Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator> &&
                 requires(ref<dyn<Allocator>> borrowed) { T(factory(borrowed)); }
    {
        auto result = arc_try_allocate_value_with<T, true>(
            rstd::move(allocator), [&](T* value, ref<dyn<Allocator>> borrowed) {
                rstd::construct_at(value, factory(borrowed));
            });
        if (result.is_err()) return Err(rstd::alloc::AllocError {});
        return Ok(Arc { ArcData<T> { .pointer = result.unwrap_unchecked() } });
    }

    template<typename A, typename F>
    static auto make_in_with(A allocator, F factory) -> Arc
        requires requires { try_make_in_with(rstd::move(allocator), rstd::move(factory)); }
    {
        auto result = try_make_in_with(rstd::move(allocator), rstd::move(factory));
        if (result.is_err()) rstd::panic { "Arc allocation failed" };
        return result.unwrap_unchecked();
    }

    auto allocator_owner() const noexcept -> ArcAllocator {
        if (self.pointer == nullptr) return {};
        auto* header = arc_header(self.pointer);
        return ArcAllocator(header->allocation->allocator_owner(header));
    }

    /// Allocation failure leaves the input untouched.
    template<typename A, typename U>
    static auto try_make_in(A allocator, U&& value) -> Result<Arc, rstd::alloc::AllocError>
        requires Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator> && (! Impled<T, Sized>) &&
                 Impled<mtp::rm_cvf<U>, Sized> && mtp::dyn_traits<T>::template
    Impled<mtp::rm_cvf<U>> {
        using Concrete = mtp::rm_cvf<U>;
        auto result =
            arc_try_allocate_value_in<Concrete>(rstd::move(allocator), rstd::forward<U>(value));
        if (result.is_err()) return Err(rstd::alloc::AllocError {});
        return Ok(
            Arc { ArcData<T> { .pointer = T::from_ptr(result.unwrap_unchecked().as_raw_ptr()) } });
    }

    template<typename A, typename... Args>
    static auto make_in(A allocator, Args&&... args) -> Arc
        requires requires { try_make_in(rstd::move(allocator), rstd::forward<Args>(args)...); }
    {
        auto result = try_make_in(rstd::move(allocator), rstd::forward<Args>(args)...);
        if (result.is_err()) rstd::panic { "Arc allocation failed" };
        return result.unwrap_unchecked();
    }

    template<typename... Args>
    static auto try_make(Args&&... args) -> Result<Arc, rstd::alloc::AllocError>
        requires requires { try_make_in(::alloc::Global {}, rstd::forward<Args>(args)...); }
    {
        return try_make_in(::alloc::Global {}, rstd::forward<Args>(args)...);
    }

    template<typename... Args>
    static auto make(Args&&... args) -> Arc
        requires requires { try_make_in(::alloc::Global {}, rstd::forward<Args>(args)...); }
    {
        return make_in(::alloc::Global {}, rstd::forward<Args>(args)...);
    }

    template<typename A>
    static auto try_make_uninit_in(A allocator)
        -> Result<Arc<MaybeUninit<T>>, rstd::alloc::AllocError>
        requires Impled<T, Sized> && Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator>
    {
        return Arc<MaybeUninit<T>>::try_make_in(rstd::move(allocator), MaybeUninit<T>::uninit());
    }

    template<typename A>
    static auto make_uninit_in(A allocator) -> Arc<MaybeUninit<T>>
        requires Impled<T, Sized> && Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator>
    {
        return Arc<MaybeUninit<T>>::make_in(rstd::move(allocator), MaybeUninit<T>::uninit());
    }

    static auto make_uninit() -> Arc<MaybeUninit<T>>
        requires Impled<T, Sized>
    {
        return make_uninit_in(::alloc::Global {});
    }

    static auto from_raw(ArcRaw<T> raw) noexcept -> Arc {
        return Arc { ArcData<T> { .pointer = raw.into_ptr() } };
    }

    /// The pointer must come from this Arc type and remain protected by a strong owner.
    /// Adds one strong ownership token, which must be released or adopted with from_raw.
    static void increment_strong_count(mut_ptr<T> pointer) noexcept {
        arc_header(pointer)->inc_strong();
    }

    /// Consumes one strong ownership token. The value may be destroyed by this call.
    static void decrement_strong_count(mut_ptr<T> pointer) noexcept { arc_drop_strong(pointer); }

    /// The pointer must remain protected by a strong owner throughout the query.
    static auto strong_count_from_raw(mut_ptr<T> pointer) noexcept -> usize {
        return arc_header(pointer)->strong.load(rstd::sync::atomic::Ordering::Acquire);
    }

    /// Borrows a live Arc pointer and retains its allocator without retaining its value.
    static auto allocator_owner_from_raw(mut_ptr<T> pointer) noexcept -> ArcAllocator {
        auto* header = arc_header(pointer);
        return ArcAllocator(header->allocation->allocator_owner(header));
    }

    auto assume_init()
        requires(! mtp::same_as<typename maybe_uninit_traits<T>::value_type, void>)
    {
        using Value  = maybe_uninit_traits<T>::value_type;
        auto pointer = self.pointer.template cast<Value>();
        self.pointer.reset();
        return Arc<Value> { ArcData<Value> { .pointer = pointer } };
    }

    auto deref() const noexcept -> ref<T> { return self.pointer.as_ref(); }
    auto deref_mut() const noexcept -> mut_ref<T> { return self.pointer.as_mut_ref(); }

    explicit operator bool() const noexcept { return self.pointer != nullptr; }

    usize strong_count() const noexcept {
        return self.pointer != nullptr
                   ? arc_header(self.pointer)->strong.load(rstd::sync::atomic::Ordering::Acquire)
                   : usize();
    }

    usize weak_count() const noexcept {
        if (self.pointer == nullptr) return usize();
        auto* header = arc_header(self.pointer);
        auto  weak   = header->weak.load(rstd::sync::atomic::Ordering::Acquire);
        auto  strong = header->strong.load(rstd::sync::atomic::Ordering::Acquire);
        if (strong == usize()) return weak;
        return weak > usize() ? weak - usize(1) : usize();
    }

    auto downgrade() const noexcept -> Weak<T>;

    auto as_ptr() const noexcept -> mut_ptr<T> {
        if (self.pointer == nullptr) [[unlikely]] {
            rstd::panic { "Arc::as_ptr called on an empty Arc" };
        }
        return self.pointer;
    }

    static bool ptr_eq(const Arc& left, const Arc& right) noexcept {
        return left.self.pointer.as_raw_ptr() == right.self.pointer.as_raw_ptr();
    }

    static bool is_unique(const Arc& arc) noexcept {
        if (arc.self.pointer == nullptr) return false;
        auto* header = arc_header(arc.self.pointer);
        return header->strong.load(rstd::sync::atomic::Ordering::Acquire) == usize(1) &&
               header->weak.load(rstd::sync::atomic::Ordering::Acquire) == usize(1);
    }

    auto get_mut() const noexcept -> Option<mut_ref<T>> {
        if (is_unique(*this)) return Some(self.pointer.as_mut_ref());
        return None();
    }

    auto try_unwrap() -> Result<T, Arc>
        requires Impled<T, Sized>
    {
        if (self.pointer != nullptr) {
            auto* header = arc_header(self.pointer);
            if (header->allocation->allocator_bound) return Err(rstd::move(*this));
            usize expected(rstd::size_t(1));
            if (header->strong.compare_exchange_strong(expected,
                                                       usize(),
                                                       rstd::sync::atomic::Ordering::Relaxed,
                                                       rstd::sync::atomic::Ordering::Relaxed)) {
                rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
                auto pointer = self.pointer;
                self.pointer.reset();
                T value = rstd::move(*pointer);
                rstd::ptr_::drop_in_place(pointer);
                arc_drop_weak(pointer);
                return Ok(rstd::move(value));
            }
        }
        return Err(rstd::move(*this));
    }

    auto into_raw() noexcept -> ArcRaw<T> {
        auto pointer = self.pointer;
        self.pointer.reset();
        return ArcRaw<T>::from_ptr(pointer);
    }
};

/// A non-owning weak reference to an `Arc` allocation.
export template<typename T>
class Weak : public DefaultInClass<Weak<T>, Clone> {
    ArcData<T> self;

    template<typename, typename>
    friend struct rstd::Impl;
    friend class Arc<T>;

    constexpr explicit Weak(ArcData<T> data) noexcept: self(data) {}

public:
    using element_type = T;

    constexpr Weak(const Weak&)            = delete;
    constexpr Weak& operator=(const Weak&) = delete;

    constexpr Weak(Weak&& other) noexcept: self(rstd::exchange(other.self, {})) {}
    constexpr Weak& operator=(Weak&& other) noexcept {
        if (this != &other) {
            reset();
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    ~Weak() { reset(); }

    auto clone() const -> Weak {
        if (self.pointer != nullptr) arc_header(self.pointer)->inc_weak();
        return Weak { self };
    }

    static constexpr auto make() noexcept -> Weak { return Weak { ArcData<T> {} }; }

    void reset() noexcept {
        if (self.pointer != nullptr) {
            arc_drop_weak(self.pointer);
            self.pointer.reset();
        }
    }

    auto upgrade() const noexcept -> Arc<T> {
        if (self.pointer != nullptr && arc_header(self.pointer)->try_inc_strong()) {
            return Arc<T> { self };
        }
        return Arc<T> { ArcData<T> {} };
    }

    auto strong_count() const noexcept -> usize {
        return self.pointer != nullptr
                   ? arc_header(self.pointer)->strong.load(rstd::sync::atomic::Ordering::Acquire)
                   : usize();
    }

    auto weak_count() const noexcept -> usize {
        return self.pointer != nullptr
                   ? arc_header(self.pointer)->weak.load(rstd::sync::atomic::Ordering::Acquire)
                   : usize();
    }

    bool expired() const noexcept { return strong_count() == usize(); }

    auto as_ptr() const noexcept -> mut_ptr<T> { return self.pointer; }
};

template<typename T>
auto Arc<T>::downgrade() const noexcept -> Weak<T> {
    Weak<T> result { self };
    if (self.pointer != nullptr) arc_header(self.pointer)->inc_weak();
    return result;
}

} // namespace alloc::sync

namespace rstd
{

template<typename T>
struct Impl<ops::Deref, ::alloc::sync::Arc<T>> : ImplBase<::alloc::sync::Arc<T>> {
    using Target = T;

    auto deref() const noexcept -> ref<Target> { return this->self().deref(); }
};

template<typename T>
struct Impl<ops::DerefMut, ::alloc::sync::Arc<T>> : ImplBase<::alloc::sync::Arc<T>> {
    auto deref_mut() const noexcept -> mut_ref<ops::deref_target_t<::alloc::sync::Arc<T>>> {
        return this->self().deref_mut();
    }
};

} // namespace rstd

namespace rstd
{
template<typename A>
    requires(Impled<A, alloc::Allocator> || mtp::same_as<A, dyn<alloc::Allocator>>)
struct Impl<alloc::Allocator, ::alloc::sync::Arc<A>> : ImplBase<::alloc::sync::Arc<A>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).allocate(layout);
        else
            return allocator->allocate(layout);
    }
    auto allocate_zeroed(alloc::Layout layout) const
        -> Result<alloc::Allocation, alloc::AllocError> {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).allocate_zeroed(layout);
        else
            return allocator->allocate_zeroed(layout);
    }
    auto deallocate(void* pointer, alloc::Layout layout) const noexcept -> void {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).deallocate(pointer, layout);
        else
            return allocator->deallocate(pointer, layout);
    }
    auto grow(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const
        -> Result<alloc::Allocation, alloc::AllocError> {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).grow(pointer, old_layout, new_layout);
        else
            return allocator->grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const
        -> Result<alloc::Allocation, alloc::AllocError> {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).grow_zeroed(pointer, old_layout, new_layout);
        else
            return allocator->grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const
        -> Result<alloc::Allocation, alloc::AllocError> {
        auto allocator = this->self().deref();
        if constexpr (Impled<A, Sized>)
            return as<alloc::Allocator>(*allocator).shrink(pointer, old_layout, new_layout);
        else
            return allocator->shrink(pointer, old_layout, new_layout);
    }
};
template<typename A>
    requires(Impled<A, alloc::Allocator> || mtp::same_as<A, dyn<alloc::Allocator>>)
struct Impl<alloc::AllocatorClone, ::alloc::sync::Arc<A>> {};
template<typename A>
    requires Impled<A, alloc::ThreadSafeAllocator>
struct Impl<alloc::ThreadSafeAllocator, ::alloc::sync::Arc<A>> {};
} // namespace rstd

export namespace alloc::sync
{
/// Erases the allocator type only after checking concurrent operation and cross-thread release.
class SharedAllocator {
    Option<Arc<rstd::dyn<rstd::alloc::Allocator>>> owner_;
    explicit SharedAllocator(Arc<rstd::dyn<rstd::alloc::Allocator>> owner)
        : owner_(Some(rstd::move(owner))) {}

public:
    SharedAllocator()                                              = default;
    SharedAllocator(const SharedAllocator&)                        = delete;
    auto operator=(const SharedAllocator&) -> SharedAllocator&     = delete;
    SharedAllocator(SharedAllocator&&) noexcept                    = default;
    auto operator=(SharedAllocator&&) noexcept -> SharedAllocator& = default;

    template<typename A>
        requires rstd::Impled<A, rstd::alloc::Allocator, rstd::alloc::ThreadSafeAllocator>
    static auto try_make(A allocator) -> Result<SharedAllocator, rstd::alloc::AllocError> {
        auto owner = Arc<rstd::dyn<rstd::alloc::Allocator>>::try_make(rstd::move(allocator));
        if (owner.is_err()) return Err(rstd::alloc::AllocError {});
        return Ok(SharedAllocator(owner.unwrap_unchecked()));
    }
    template<typename A>
        requires rstd::Impled<A, rstd::alloc::Allocator, rstd::alloc::ThreadSafeAllocator>
    static auto make(A allocator) -> SharedAllocator {
        auto result = try_make(rstd::move(allocator));
        if (result.is_err()) rstd::panic { "shared allocator allocation failed" };
        return result.unwrap_unchecked();
    }
    auto clone() const -> SharedAllocator {
        return owner_.is_some() ? SharedAllocator(owner_->clone()) : SharedAllocator {};
    }
    auto as_ref() const noexcept [[clang::lifetimebound]]
    -> ref<rstd::dyn<rstd::alloc::Allocator>> {
        return owner_.is_some() ? owner_->deref() : ::alloc::allocator_ref(::alloc::GLOBAL);
    }
    auto allocate(Layout layout) const -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->allocate(layout);
    }
    auto allocate_zeroed(Layout layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->allocate_zeroed(layout);
    }
    auto deallocate(void* pointer, Layout layout) const noexcept -> void {
        return as_ref()->deallocate(pointer, layout);
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as_ref()->shrink(pointer, old_layout, new_layout);
    }
};
} // namespace alloc::sync
template<>
struct rstd::Impl<rstd::alloc::AllocatorClone, ::alloc::sync::SharedAllocator> {};
template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, ::alloc::sync::SharedAllocator> {};

template<>
struct rstd::Impl<rstd::clone::Clone, ::alloc::sync::SharedAllocator>
    : rstd::DefaultInImpl<rstd::clone::Clone, ::alloc::sync::SharedAllocator> {
    auto clone() const -> ::alloc::sync::SharedAllocator { return this->self().clone(); }
};

template<>
struct rstd::Impl<rstd::alloc::AllocatorClone, ::alloc::sync::ArcAllocator> {};
template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, ::alloc::sync::ArcAllocator> {};
template<>
struct rstd::Impl<rstd::clone::Clone, ::alloc::sync::ArcAllocator>
    : rstd::DefaultInImpl<rstd::clone::Clone, ::alloc::sync::ArcAllocator> {
    auto clone() const -> ::alloc::sync::ArcAllocator { return this->self().clone(); }
};
