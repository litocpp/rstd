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

constexpr usize ARC_MAX_REFCOUNT = rstd::numeric_limits<usize>::max() / 2;

struct ArcHeader {
    Atomic<usize> strong { 1 };
    Atomic<usize> weak { 1 };

    void inc_strong() {
        [[maybe_unused]]
        auto old = strong.fetch_add(1, rstd::sync::atomic::Ordering::Relaxed);
        debug_assert(old < ARC_MAX_REFCOUNT);
    }

    void inc_weak() {
        [[maybe_unused]]
        auto old = weak.fetch_add(1, rstd::sync::atomic::Ordering::Relaxed);
        debug_assert(old < ARC_MAX_REFCOUNT);
    }

    bool try_inc_strong() {
        usize current = strong.load(rstd::sync::atomic::Ordering::Acquire);
        while (current != 0) {
            debug_assert(current < ARC_MAX_REFCOUNT);
            if (strong.compare_exchange_weak(current,
                                             current + 1,
                                             rstd::sync::atomic::Ordering::AcqRel,
                                             rstd::sync::atomic::Ordering::Acquire)) {
                return true;
            }
        }
        return false;
    }
};

struct ArcAllocationLayout {
    Layout layout;
    usize  value_offset;
};

constexpr auto arc_allocation_layout(Layout value_layout) -> ArcAllocationLayout {
    usize value_offset = 0;
    auto  layout       = Layout::make<ArcHeader>().extend(value_layout, value_offset).unwrap();
    return ArcAllocationLayout { .layout = layout.pad_to_align(), .value_offset = value_offset };
}

template<typename T>
auto arc_allocation_layout(mut_ptr<T> pointer) -> ArcAllocationLayout {
    return arc_allocation_layout(Layout::for_value(pointer.as_ptr()));
}

template<typename T>
auto arc_header(mut_ptr<T> pointer) noexcept -> ArcHeader* {
    auto  allocation = arc_allocation_layout(pointer);
    auto* value      = reinterpret_cast<u8*>(pointer.as_raw_ptr());
    return rstd::launder(reinterpret_cast<ArcHeader*>(value - allocation.value_offset));
}

template<typename T, typename... Args>
auto arc_allocate_value(Args&&... args) -> mut_ptr<T> {
    auto value_layout = Layout::make<T>();
    auto allocation   = arc_allocation_layout(value_layout);
    auto result       = rstd::as<Allocator>(::alloc::GLOBAL).allocate(allocation.layout);
    if (result.is_err()) ::alloc::handle_alloc_error(allocation.layout);

    auto* base   = result.unwrap_unchecked().as_mut_ptr().as_raw_ptr();
    auto* header = rstd::construct_at(reinterpret_cast<ArcHeader*>(base));
    (void)header;
    auto* value = reinterpret_cast<T*>(base + allocation.value_offset);
    rstd::construct_at(value, rstd::forward<Args>(args)...);
    return mut_ptr<T>::from_raw_parts(value);
}

template<typename T>
void arc_deallocate(mut_ptr<T> pointer) noexcept {
    auto  allocation = arc_allocation_layout(pointer);
    auto* header     = arc_header(pointer);
    rstd::destroy_at(header);
    auto allocation_pointer =
        NonNull<u8>::make_unchecked(mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(header)));
    rstd::as<Allocator>(::alloc::GLOBAL).deallocate(allocation_pointer, allocation.layout);
}

template<typename T>
void arc_drop_weak(mut_ptr<T> pointer) noexcept {
    auto* header = arc_header(pointer);
    if (header->weak.fetch_sub(1, rstd::sync::atomic::Ordering::AcqRel) == 1) {
        rstd::sync::atomic::fence(rstd::sync::atomic::Ordering::Acquire);
        arc_deallocate(pointer);
    }
}

template<typename T>
void arc_drop_strong(mut_ptr<T> pointer) noexcept {
    auto* header = arc_header(pointer);
    if (header->strong.fetch_sub(1, rstd::sync::atomic::Ordering::AcqRel) == 1) {
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

    using Target       = T;
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

    template<typename... Args>
    static auto make(Args&&... args) -> Arc
        requires Impled<T, Sized>
    {
        return Arc { ArcData<T> {
            .pointer = arc_allocate_value<T>(rstd::forward<Args>(args)...),
        } };
    }

    template<typename U>
    static auto make(U&& value) -> Arc
        requires(! Impled<T, Sized>) && Impled<mtp::rm_cvf<U>, Sized> &&
                mtp::dyn_traits<T>::template
    Impled<mtp::rm_cvf<U>> {
        using Concrete = mtp::rm_cvf<U>;
        auto pointer   = arc_allocate_value<Concrete>(rstd::forward<U>(value));
        return Arc { ArcData<T> { .pointer = T::from_ptr(pointer.as_raw_ptr()) } };
    }

    static auto make_uninit() -> Arc<MaybeUninit<T>>
        requires Impled<T, Sized>
    {
        return Arc<MaybeUninit<T>>::make(MaybeUninit<T>::uninit());
    }

    static auto from_raw(ArcRaw<T> raw) noexcept -> Arc {
        return Arc { ArcData<T> { .pointer = raw.into_ptr() } };
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
                   : 0;
    }

    usize weak_count() const noexcept {
        if (self.pointer == nullptr) return 0;
        auto* header = arc_header(self.pointer);
        auto  weak   = header->weak.load(rstd::sync::atomic::Ordering::Acquire);
        auto  strong = header->strong.load(rstd::sync::atomic::Ordering::Acquire);
        if (strong == 0) return weak;
        return weak > 0 ? weak - 1 : 0;
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
        return header->strong.load(rstd::sync::atomic::Ordering::Acquire) == 1 &&
               header->weak.load(rstd::sync::atomic::Ordering::Acquire) == 1;
    }

    auto get_mut() const noexcept -> Option<mut_ref<T>> {
        if (is_unique(*this)) return Some(self.pointer.as_mut_ref());
        return None();
    }

    auto try_unwrap() -> Result<T, Arc>
        requires Impled<T, Sized>
    {
        if (self.pointer != nullptr) {
            auto* header   = arc_header(self.pointer);
            usize expected = 1;
            if (header->strong.compare_exchange_strong(expected,
                                                       0,
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
                   : 0;
    }

    auto weak_count() const noexcept -> usize {
        return self.pointer != nullptr
                   ? arc_header(self.pointer)->weak.load(rstd::sync::atomic::Ordering::Acquire)
                   : 0;
    }

    bool expired() const noexcept { return strong_count() == 0; }

    auto as_ptr() const noexcept -> mut_ptr<T> { return self.pointer; }
};

template<typename T>
auto Arc<T>::downgrade() const noexcept -> Weak<T> {
    Weak<T> result { self };
    if (self.pointer != nullptr) arc_header(self.pointer)->inc_weak();
    return result;
}

} // namespace alloc::sync
