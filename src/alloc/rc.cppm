module;
#include <rstd/macro.hpp>
export module rstd.alloc:rc;
export import rstd.core;
export import :alloc;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;
namespace mtp = rstd::mtp;
using namespace rstd::prelude;

namespace alloc::rc
{

export enum class StoragePolicy : u32 { Embed = 0, Separate, SeparateWithDeleter };

export template<typename T>
class Rc;

export template<typename T>
class Weak;

export template<typename T>
class RcRaw;

} // namespace alloc::rc

template<typename AllocatorType, typename T>
using rc_rebind_alloc = typename mtp::allocator_traits<AllocatorType>::template rebind_alloc<T>;

constexpr bool RC_COPY_NOEXCEPT { false };

void rc_increase_count(usize& count) {
    if (count == rstd::numeric_limits<usize>::max()) {
        rstd::panic("reference count overflow");
    }
    ++count;
}

struct RcHeader;
using RcReleaseValue  = void (*)(RcHeader*, Layout) noexcept;
using RcReleaseHeader = void (*)(RcHeader*) noexcept;

struct RcHeader {
    usize strong { 1 };
    usize weak { 1 };
    voidp value { nullptr };

    bool            external_drop;
    RcReleaseValue  release_value;
    RcReleaseHeader release_header;

    RcHeader(voidp           value,
             bool            external_drop,
             RcReleaseValue  release_value,
             RcReleaseHeader release_header) noexcept
        : value(value),
          external_drop(external_drop),
          release_value(release_value),
          release_header(release_header) {}

    void inc_strong() { rc_increase_count(strong); }
    void inc_weak() { rc_increase_count(weak); }
};

template<typename T>
using RcTarget = mtp::rm_const<T>;

template<typename T, bool = mtp::DST<RcTarget<T>>>
struct RcData {
    RcHeader* header { nullptr };
};

template<typename T>
struct RcData<T, true> {
    using Metadata = decltype(rstd::declval<mut_ptr<RcTarget<T>>>().metadata());

    RcHeader* header { nullptr };
    Metadata  metadata {};
};

template<typename T>
auto rc_pointer(RcData<T> data) noexcept -> mut_ptr<RcTarget<T>> {
    using Target = RcTarget<T>;
    auto* value  = data.header == nullptr ? nullptr : data.header->value;
    if constexpr (mtp::DSTArray<Target>) {
        using Element = mtp::rm_ext<Target>;
        return mut_ptr<Target>::from_raw_parts(reinterpret_cast<Element*>(value), data.metadata);
    } else if constexpr (mtp::DST<Target>) {
        return mut_ptr<Target>::from_raw_parts(value, data.metadata);
    } else {
        return mut_ptr<Target>::from_raw_parts(static_cast<Target*>(value));
    }
}

template<typename T>
auto rc_data(RcHeader* header, mut_ptr<RcTarget<T>> pointer) noexcept -> RcData<T> {
    if constexpr (mtp::DST<RcTarget<T>>) {
        return RcData<T> { .header = header, .metadata = pointer.metadata() };
    } else {
        return RcData<T> { .header = header };
    }
}

auto rc_allocate(Layout layout) -> mut_ptr<u8> {
    auto result = rstd::as<Allocator>(::alloc::GLOBAL).allocate(layout);
    if (result.is_err()) ::alloc::handle_alloc_error(layout);
    return result.unwrap_unchecked().as_mut_ptr().template cast<u8>();
}

void rc_deallocate(voidp pointer, Layout layout) noexcept {
    auto allocation_pointer =
        NonNull<u8>::make_unchecked(mut_ptr<u8>::from_raw_parts(static_cast<u8*>(pointer)));
    rstd::as<Allocator>(::alloc::GLOBAL).deallocate(allocation_pointer, layout);
}

void rc_release_global_value(RcHeader* header, Layout layout) noexcept {
    rc_deallocate(header->value, layout);
}

void rc_release_global_header(RcHeader* header) noexcept {
    delete header;
}

void rc_retain_embedded_value(RcHeader*, Layout) noexcept {
}

template<typename T>
struct RcEmbeddedHeader : RcHeader {
    alignas(T) rstd::byte storage[sizeof(T)];

    RcEmbeddedHeader() noexcept
        : RcHeader(nullptr, false, &rc_retain_embedded_value, &RcEmbeddedHeader::release_self) {
        value = storage;
    }

    static void release_self(RcHeader* header) noexcept {
        delete static_cast<RcEmbeddedHeader*>(header);
    }
};

template<typename Element, typename AllocatorType>
struct RcAllocatorHeader : RcHeader {
    AllocatorType allocator;
    usize         count;

    RcAllocatorHeader(AllocatorType allocator, usize count)
        : RcHeader(nullptr,
                   false,
                   &RcAllocatorHeader::release_value_storage,
                   &RcAllocatorHeader::release_self),
          allocator(rstd::move(allocator)),
          count(count) {}

    static void release_value_storage(RcHeader* header, Layout) noexcept {
        auto* self = static_cast<RcAllocatorHeader*>(header);
        self->allocator.deallocate(static_cast<Element*>(self->value), self->count);
    }

    static void release_self(RcHeader* header) noexcept {
        auto* self      = static_cast<RcAllocatorHeader*>(header);
        auto  allocator = rc_rebind_alloc<AllocatorType, RcAllocatorHeader>(self->allocator);
        rstd::destroy_at(self);
        allocator.deallocate(self, 1);
    }
};

template<typename T, typename AllocatorType>
struct RcEmbeddedAllocatorHeader : RcHeader {
    AllocatorType allocator;
    alignas(T) rstd::byte storage[sizeof(T)];

    explicit RcEmbeddedAllocatorHeader(AllocatorType allocator)
        : RcHeader(nullptr,
                   false,
                   &rc_retain_embedded_value,
                   &RcEmbeddedAllocatorHeader::release_self),
          allocator(rstd::move(allocator)) {
        value = storage;
    }

    static void release_self(RcHeader* header) noexcept {
        auto* self     = static_cast<RcEmbeddedAllocatorHeader*>(header);
        auto allocator = rc_rebind_alloc<AllocatorType, RcEmbeddedAllocatorHeader>(self->allocator);
        rstd::destroy_at(self);
        allocator.deallocate(self, 1);
    }
};

template<typename T, typename Deleter>
struct RcDeleterHeader : RcHeader {
    Deleter deleter;

    RcDeleterHeader(T* pointer, Deleter deleter)
        : RcHeader(const_cast<mtp::rm_const<T>*>(pointer),
                   true,
                   &RcDeleterHeader::release_external_value,
                   &RcDeleterHeader::release_self),
          deleter(rstd::move(deleter)) {}

    static void release_external_value(RcHeader* header, Layout) noexcept {
        auto* self = static_cast<RcDeleterHeader*>(header);
        self->deleter(static_cast<T*>(self->value));
    }

    static void release_self(RcHeader* header) noexcept {
        delete static_cast<RcDeleterHeader*>(header);
    }
};

template<typename T, typename Deleter, typename AllocatorType>
struct RcDeleterAllocatorHeader : RcHeader {
    Deleter       deleter;
    AllocatorType allocator;

    RcDeleterAllocatorHeader(T* pointer, Deleter deleter, AllocatorType allocator)
        : RcHeader(const_cast<mtp::rm_const<T>*>(pointer),
                   true,
                   &RcDeleterAllocatorHeader::release_external_value,
                   &RcDeleterAllocatorHeader::release_self),
          deleter(rstd::move(deleter)),
          allocator(rstd::move(allocator)) {}

    static void release_external_value(RcHeader* header, Layout) noexcept {
        auto* self = static_cast<RcDeleterAllocatorHeader*>(header);
        self->deleter(static_cast<T*>(self->value));
    }

    static void release_self(RcHeader* header) noexcept {
        auto* self      = static_cast<RcDeleterAllocatorHeader*>(header);
        auto  allocator = rc_rebind_alloc<AllocatorType, RcDeleterAllocatorHeader>(self->allocator);
        rstd::destroy_at(self);
        allocator.deallocate(self, 1);
    }
};

template<typename T>
struct RcAllocation {
    RcHeader*  header;
    mut_ptr<T> pointer;
};

template<typename T, typename... Args>
auto rc_allocate_separate_value(Args&&... args) -> RcAllocation<T> {
    auto layout  = Layout::make<T>();
    auto pointer = rc_allocate(layout).template cast<T>();
    rstd::construct_at(pointer.as_raw_ptr(), rstd::forward<Args>(args)...);
    auto* header = new RcHeader(
        pointer.as_raw_ptr(), false, &rc_release_global_value, &rc_release_global_header);
    return RcAllocation<T> { .header = header, .pointer = pointer };
}

template<typename T, typename... Args>
auto rc_allocate_embedded_value(Args&&... args) -> RcAllocation<T> {
    auto* header  = new RcEmbeddedHeader<T>();
    auto  pointer = mut_ptr<T>::from_raw_parts(static_cast<T*>(header->value));
    rstd::construct_at(pointer.as_raw_ptr(), rstd::forward<Args>(args)...);
    return RcAllocation<T> { .header = header, .pointer = pointer };
}

template<typename T, alloc::rc::StoragePolicy Policy, typename... Args>
auto rc_allocate_value(Args&&... args) -> RcAllocation<T> {
    if constexpr (Policy == alloc::rc::StoragePolicy::Separate) {
        return rc_allocate_separate_value<T>(rstd::forward<Args>(args)...);
    } else if constexpr (Policy == alloc::rc::StoragePolicy::Embed) {
        return rc_allocate_embedded_value<T>(rstd::forward<Args>(args)...);
    } else {
        static_assert(Policy != Policy);
    }
}

template<typename Element, typename Value>
auto rc_allocate_array(usize count, Value const& initial) -> RcAllocation<Element[]> {
    auto layout  = Layout::array<Element>(count).unwrap();
    auto pointer = rc_allocate(layout).template cast_array<Element>(count);
    for (usize index = 0; index < count; ++index) {
        rstd::construct_at(pointer.as_raw_ptr() + index, initial);
    }
    auto* header = new RcHeader(
        pointer.as_raw_ptr(), false, &rc_release_global_value, &rc_release_global_header);
    return RcAllocation<Element[]> { .header = header, .pointer = pointer };
}

template<typename T, typename AllocatorType, typename... Args>
auto rc_allocate_separate_value_with(AllocatorType allocator, Args&&... args) -> RcAllocation<T> {
    using Header           = RcAllocatorHeader<T, AllocatorType>;
    auto  header_allocator = rc_rebind_alloc<AllocatorType, Header>(allocator);
    auto* header           = header_allocator.allocate(1);
    rstd::construct_at(header, allocator, usize(1));
    auto* value   = header->allocator.allocate(1);
    header->value = value;
    rstd::construct_at(value, rstd::forward<Args>(args)...);
    return RcAllocation<T> {
        .header  = header,
        .pointer = mut_ptr<T>::from_raw_parts(value),
    };
}

template<typename T, typename AllocatorType, typename... Args>
auto rc_allocate_embedded_value_with(AllocatorType allocator, Args&&... args) -> RcAllocation<T> {
    using Header           = RcEmbeddedAllocatorHeader<T, AllocatorType>;
    auto  header_allocator = rc_rebind_alloc<AllocatorType, Header>(allocator);
    auto* header           = header_allocator.allocate(1);
    rstd::construct_at(header, allocator);
    auto pointer = mut_ptr<T>::from_raw_parts(static_cast<T*>(header->value));
    rstd::construct_at(pointer.as_raw_ptr(), rstd::forward<Args>(args)...);
    return RcAllocation<T> { .header = header, .pointer = pointer };
}

template<typename T, alloc::rc::StoragePolicy Policy, typename AllocatorType, typename... Args>
auto rc_allocate_value_with(AllocatorType allocator, Args&&... args) -> RcAllocation<T> {
    if constexpr (Policy == alloc::rc::StoragePolicy::Separate) {
        return rc_allocate_separate_value_with<T>(allocator, rstd::forward<Args>(args)...);
    } else if constexpr (Policy == alloc::rc::StoragePolicy::Embed) {
        return rc_allocate_embedded_value_with<T>(allocator, rstd::forward<Args>(args)...);
    } else {
        static_assert(Policy != Policy);
    }
}

template<typename Element, typename AllocatorType, typename Value>
auto rc_allocate_array_with(AllocatorType allocator, usize count, Value const& initial)
    -> RcAllocation<Element[]> {
    using Header           = RcAllocatorHeader<Element, AllocatorType>;
    auto  header_allocator = rc_rebind_alloc<AllocatorType, Header>(allocator);
    auto* header           = header_allocator.allocate(1);
    rstd::construct_at(header, allocator, count);
    auto* value   = header->allocator.allocate(count);
    header->value = value;
    for (usize index = 0; index < count; ++index) {
        rstd::construct_at(value + index, initial);
    }
    return RcAllocation<Element[]> {
        .header  = header,
        .pointer = mut_ptr<Element[]>::from_raw_parts(value, count),
    };
}

template<typename T, typename Deleter>
auto rc_external_data(T* pointer, Deleter&& deleter) -> RcData<T> {
    using StoredDeleter = mtp::rm_cvf<Deleter>;
    auto* header = new RcDeleterHeader<T, StoredDeleter>(pointer, rstd::forward<Deleter>(deleter));
    auto  value_pointer = mut_ptr<RcTarget<T>>::from_raw_parts(const_cast<RcTarget<T>*>(pointer));
    return rc_data<T>(header, value_pointer);
}

template<typename T, typename Deleter, typename AllocatorType>
auto rc_external_data(T* pointer, Deleter&& deleter, AllocatorType allocator) -> RcData<T> {
    using StoredDeleter    = mtp::rm_cvf<Deleter>;
    using Header           = RcDeleterAllocatorHeader<T, StoredDeleter, AllocatorType>;
    auto  header_allocator = rc_rebind_alloc<AllocatorType, Header>(allocator);
    auto* header           = header_allocator.allocate(1);
    rstd::construct_at(header, pointer, rstd::forward<Deleter>(deleter), rstd::move(allocator));
    auto value_pointer = mut_ptr<RcTarget<T>>::from_raw_parts(const_cast<RcTarget<T>*>(pointer));
    return rc_data<T>(header, value_pointer);
}

template<typename T>
void rc_drop_strong(RcData<T> data) noexcept {
    auto* header = data.header;
    if (header == nullptr) return;
    --header->strong;
    if (header->strong != 0) return;

    auto pointer = rc_pointer(data);
    auto layout  = Layout::for_value(pointer.as_ptr());
    if (! header->external_drop) {
        rstd::ptr_::drop_in_place(pointer);
    }
    header->release_value(header, layout);

    --header->weak;
    if (header->weak == 0) {
        header->release_header(header);
    }
}

template<typename T>
void rc_drop_weak(RcData<T> data) noexcept {
    auto* header = data.header;
    if (header == nullptr) return;
    --header->weak;
    if (header->weak == 0) {
        header->release_header(header);
    }
}

struct RcMakeHelper {
    template<typename T>
    static auto make(RcData<T> data) noexcept -> alloc::rc::Rc<T> {
        return alloc::rc::Rc<T>(data);
    }
};

namespace alloc::rc
{

export template<typename T>
class RcRaw {
    RcData<T> self;

    friend class Rc<T>;
    explicit RcRaw(RcData<T> data) noexcept: self(data) {}

    auto take() noexcept -> RcData<T> { return rstd::exchange(self, {}); }

public:
    using Target = RcTarget<T>;

    RcRaw(const RcRaw&)            = delete;
    RcRaw& operator=(const RcRaw&) = delete;

    RcRaw(RcRaw&& other) noexcept: self(other.take()) {}
    RcRaw& operator=(RcRaw&&) = delete;

    auto as_ptr() const noexcept -> mut_ptr<Target> { return rc_pointer(self); }
};

export template<typename T>
class Weak final {
    RcData<T> self;

    friend class Rc<T>;
    explicit Weak(RcData<T> data) noexcept: self(data) {}

public:
    using Target = RcTarget<T>;

    Weak() noexcept = default;
    Weak(const Weak& other) noexcept: Weak(other.clone()) {}
    Weak& operator=(const Weak& other) noexcept {
        if (this != &other) {
            auto replacement = other.clone();
            rstd::swap(self, replacement.self);
        }
        return *this;
    }

    Weak(Weak&& other) noexcept: self(rstd::exchange(other.self, {})) {}
    Weak& operator=(Weak&& other) noexcept {
        if (this != &other) {
            rc_drop_weak(self);
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    ~Weak() { rc_drop_weak(self); }

    auto clone() const noexcept -> Weak {
        if (self.header != nullptr) self.header->inc_weak();
        return Weak(self);
    }

    auto upgrade() const -> Option<Rc<T>> {
        if (self.header == nullptr || self.header->strong == 0) return None();
        self.header->inc_strong();
        return Some(RcMakeHelper::make<T>(self));
    }

    auto strong_count() const noexcept -> usize {
        return self.header == nullptr ? 0 : self.header->strong;
    }

    auto weak_count() const noexcept -> usize {
        if (self.header == nullptr) return 0;
        auto weak = self.header->weak;
        return self.header->strong == 0 ? weak : weak - 1;
    }

    bool expired() const noexcept { return strong_count() == 0; }

    auto as_ptr() const noexcept -> mut_ptr<Target> { return rc_pointer(self); }
};

export template<typename T>
class Rc final : public DefaultInClass<Rc<T>, Clone> {
    RcData<T> self;

    friend struct ::RcMakeHelper;
    template<typename>
    friend class Rc;

    explicit Rc(RcData<T> data) noexcept: self(data) {}

public:
    USE_TRAIT(Rc)

    using Target        = RcTarget<T>;
    using value_t       = mtp::rm_ext<Target>;
    using const_value_t = mtp::add_const<value_t>;

    Rc() noexcept = default;
    ~Rc() { rc_drop_strong(self); }

    Rc(const Rc& other) noexcept(RC_COPY_NOEXCEPT): Rc(other.clone()) {}
    Rc& operator=(const Rc& other) noexcept(RC_COPY_NOEXCEPT) {
        if (this != &other) {
            auto replacement = other.clone();
            swap(replacement);
        }
        return *this;
    }

    Rc(Rc&& other) noexcept: self(rstd::exchange(other.self, {})) {}
    Rc& operator=(Rc&& other) noexcept {
        if (this != &other) {
            rc_drop_strong(self);
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    template<typename U>
        requires mtp::is_const<T> && mtp::same_as<mtp::rm_const<T>, U>
    Rc(const Rc<U>& other): self(rc_data<T>(other.self.header, rc_pointer(other.self))) {
        if (self.header != nullptr) self.header->inc_strong();
    }

    explicit Rc(T* pointer)
        requires Impled<Target, Sized>
        : Rc(pointer, mtp::default_delete<T>()) {}

    template<typename Deleter>
    Rc(T* pointer, Deleter&& deleter)
        requires Impled<Target, Sized>
        : self(rc_external_data(pointer, rstd::forward<Deleter>(deleter))) {}

    template<typename Deleter, typename AllocatorType>
    Rc(T* pointer, Deleter&& deleter, AllocatorType allocator)
        requires Impled<Target, Sized>
        : self(rc_external_data(pointer, rstd::forward<Deleter>(deleter), rstd::move(allocator))) {}

    auto clone() const noexcept(RC_COPY_NOEXCEPT) -> Rc {
        if (self.header != nullptr) self.header->inc_strong();
        return Rc(self);
    }

    void reset() noexcept {
        rc_drop_strong(self);
        self = {};
    }

    auto downgrade() const noexcept -> Weak<T> {
        if (self.header != nullptr) self.header->inc_weak();
        return Weak<T>(self);
    }

    auto strong_count() const noexcept -> usize {
        return self.header == nullptr ? 0 : self.header->strong;
    }

    auto weak_count() const noexcept -> usize {
        if (self.header == nullptr) return 0;
        auto weak = self.header->weak;
        return self.header->strong == 0 ? weak : weak - 1;
    }

    auto is_unique() const noexcept -> bool {
        return self.header != nullptr && self.header->strong == 1 && self.header->weak == 1;
    }

    auto size() const noexcept -> usize {
        if (self.header == nullptr) return 0;
        if constexpr (mtp::DSTArray<Target>) {
            return self.metadata;
        } else {
            return 1;
        }
    }

    explicit operator bool() const noexcept { return self.header != nullptr; }

    auto as_ptr() const noexcept -> mut_ptr<Target> { return rc_pointer(self); }

    auto get() noexcept -> value_t*
        requires(! mtp::is_const<T>) && (! mtp::DST<Target> || mtp::DSTArray<Target>)
    {
        return self.header == nullptr ? nullptr : as_ptr().as_raw_ptr();
    }

    auto get() const noexcept -> const_value_t*
        requires(! mtp::DST<Target> || mtp::DSTArray<Target>)
    {
        return self.header == nullptr ? nullptr : as_ptr().as_raw_ptr();
    }

    auto deref() const noexcept -> ref<Target> { return as_ptr().as_ref(); }

    auto to_const() const -> Rc<const T>
        requires(! mtp::is_const<T>) && Impled<Target, Sized>
    {
        if (self.header != nullptr) self.header->inc_strong();
        return RcMakeHelper::make<const T>(rc_data<const T>(self.header, rc_pointer(self)));
    }

    static bool ptr_eq(const Rc& left, const Rc& right) noexcept {
        return left.self.header == right.self.header;
    }

    static auto from_raw(RcRaw<T> raw) noexcept -> Rc { return Rc(raw.take()); }

    auto into_raw() noexcept -> RcRaw<T> { return RcRaw<T>(rstd::exchange(self, {})); }

    void swap(Rc& other) noexcept { rstd::swap(self, other.self); }
};

export template<typename T, StoragePolicy Policy = StoragePolicy::Separate, typename... Args>
    requires Impled<T, Sized>
auto make_rc(Args&&... args) -> Rc<T> {
    auto allocation = rc_allocate_value<T, Policy>(rstd::forward<Args>(args)...);
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, allocation.pointer));
}

export template<typename T, StoragePolicy Policy = StoragePolicy::Separate>
    requires mtp::DSTArray<T> && (Policy == StoragePolicy::Separate)
auto make_rc(usize count, typename Rc<T>::const_value_t const& initial) -> Rc<T> {
    using Element   = mtp::rm_ext<T>;
    auto allocation = rc_allocate_array<Element>(count, initial);
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, allocation.pointer));
}

export template<typename T, StoragePolicy Policy = StoragePolicy::Separate, typename U>
    requires mtp::DST<T> && (! mtp::DSTArray<T>) && Impled<mtp::rm_cvf<U>, Sized> &&
             mtp::dyn_traits<T>::template
Impled<mtp::rm_cvf<U>> auto make_rc(U&& value) -> Rc<T> {
    using Concrete  = mtp::rm_cvf<U>;
    auto allocation = rc_allocate_value<Concrete, Policy>(rstd::forward<U>(value));
    auto pointer    = T::from_ptr(allocation.pointer.as_raw_ptr());
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, pointer));
}

export template<typename T,
                StoragePolicy Policy = StoragePolicy::Separate,
                typename AllocatorType,
                typename... Args>
    requires Impled<T, Sized>
auto allocate_make_rc(AllocatorType const& allocator, Args&&... args) -> Rc<T> {
    auto allocation = rc_allocate_value_with<T, Policy>(allocator, rstd::forward<Args>(args)...);
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, allocation.pointer));
}

export template<typename T, StoragePolicy Policy = StoragePolicy::Separate, typename AllocatorType>
    requires mtp::DSTArray<T> && (Policy == StoragePolicy::Separate)
auto allocate_make_rc(AllocatorType const&                 allocator,
                      usize                                count,
                      typename Rc<T>::const_value_t const& initial) -> Rc<T> {
    using Element   = mtp::rm_ext<T>;
    auto allocation = rc_allocate_array_with<Element>(allocator, count, initial);
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, allocation.pointer));
}

export template<typename T,
                StoragePolicy Policy = StoragePolicy::Separate,
                typename AllocatorType,
                typename U>
    requires mtp::DST<T> && (! mtp::DSTArray<T>) && Impled<mtp::rm_cvf<U>, Sized> &&
             mtp::dyn_traits<T>::template
Impled<mtp::rm_cvf<U>> auto allocate_make_rc(AllocatorType const& allocator, U&& value) -> Rc<T> {
    using Concrete  = mtp::rm_cvf<U>;
    auto allocation = rc_allocate_value_with<Concrete, Policy>(allocator, rstd::forward<U>(value));
    auto pointer    = T::from_ptr(allocation.pointer.as_raw_ptr());
    return RcMakeHelper::make<T>(rc_data<T>(allocation.header, pointer));
}

export template<typename T>
void swap(Rc<T>& left, Rc<T>& right) noexcept {
    left.swap(right);
}

} // namespace alloc::rc
