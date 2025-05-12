module;
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <limits>
#include <new>
#include <cassert>

export module rstd.rc;

namespace rstd::rc
{

namespace detail
{
template<typename Allocator, typename T>
using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

constexpr bool noexp { false };

void increase_count(std::size_t& count) {
    if (count == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("reference count overflow");
    }
    ++count;
}

template<typename T>
constexpr auto compute_alignment() -> std::size_t {
    return std::max(alignof(std::remove_extent_t<T>), alignof(std::size_t));
}

template<typename T>
[[nodiscard]] constexpr auto layout_for_value(std::size_t align) -> std::size_t {
    const auto size = sizeof(T);
    return (size + align - 1) & ~(align - 1);
}

template<typename T>
[[nodiscard]] constexpr auto layout_for_value() -> std::size_t {
    return layout_for_value<T>(alignof(T));
}

enum class DeleteType
{
    Value = 0,
    Self,
};

} // namespace detail

enum class StoragePolicy : std::uint32_t
{
    Embed = 0,
    Separate,
    SeparateWithDeleter
};

template<typename T>
struct alignas(std::size_t) RcInner {
    using value_t = std::remove_extent_t<T>;

    std::size_t strong { 1 };
    std::size_t weak { 1 };
    value_t*    value { nullptr };

    RcInner() noexcept {}
    // not need virtual here
    ~RcInner() = default;

    virtual void do_delete(detail::DeleteType t) {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            delete self->value;
        } else {
            delete self;
        }
    }

    void inc_strong() { detail::increase_count(strong); }
    void dec_strong() { --strong; }
    void inc_weak() { detail::increase_count(weak); }
    void dec_weak() { --weak; }
};
namespace detail
{

template<typename T>
using RcInnerConst = RcInner<std::add_const_t<T>>;

template<typename T, StoragePolicy P, typename ValueDeleter = void>
struct RcInnerImpl {
    static_assert(false);
};

template<typename T>
struct RcInnerImpl<T, StoragePolicy::Embed> : RcInnerConst<T> {
    static_assert(! std::is_array_v<T>);

    alignas(T) std::byte storage[sizeof(T)];

    RcInnerImpl() noexcept: RcInnerConst<T>() {}

    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            self->value->~T();
            self->value = nullptr;
        } else {
            delete self;
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        auto ptr    = new (storage) T(std::forward<Args>(args)...);
        this->value = ptr;
    }
};

template<typename T>
struct RcInnerImpl<T, StoragePolicy::Separate> : RcInnerConst<T> {
    RcInnerImpl() noexcept: RcInnerConst<T>() {}

    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            delete self->value;
            self->value = nullptr;
        } else {
            delete self;
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        auto ptr    = new T(std::forward<Args>(args)...);
        this->value = ptr;
    }
};

template<typename T>
struct RcInnerArrayImpl : RcInnerConst<T> {
    const std::size_t size;

    RcInnerArrayImpl(std::size_t n) noexcept: RcInnerConst<T>(), size(n) {}
};

template<typename T>
struct RcInnerImpl<T[], StoragePolicy::Separate> : RcInnerArrayImpl<T[]> {
    using value_t = std::remove_extent_t<T>;

    RcInnerImpl(std::size_t n) noexcept: RcInnerArrayImpl<T[]>(n) {}

    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            auto ptr = const_cast<std::remove_const_t<value_t>*>(self->value);
            for (std::size_t i = 0; i < this->size; i++) {
                (ptr + i)->~value_t();
            }
            ::operator delete[]((void*)ptr,
                                sizeof(value_t) * this->size,
                                std::align_val_t { std::alignment_of_v<value_t> });
            self->value = nullptr;
        } else {
            delete self;
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        auto* ptr   = static_cast<value_t*>(::operator new[](
            sizeof(value_t) * this->size, std::align_val_t { std::alignment_of_v<value_t> }));
        this->value = ptr;
        for (std::size_t i = 0; i < this->size; i++) {
            new (ptr + i) value_t(std::forward<Args>(args)...);
        }
    }
};

template<typename T, typename ValueDeleter>
struct RcInnerImpl<T, StoragePolicy::SeparateWithDeleter, ValueDeleter> : RcInnerConst<T> {
    ValueDeleter value_deletor;

    RcInnerImpl(T* p, ValueDeleter d): RcInnerConst<T>(), value_deletor(std::move(d)) {
        this->value = p;
    }

    void do_delete(detail::DeleteType t) override {
        auto self = this;

        if (t == detail::DeleteType::Value) {
            self->value_deletor(const_cast<std::remove_cv_t<T>*>(self->value));
            self->value = nullptr;
        } else {
            delete self;
        }
    }
};

template<typename T, typename Allocator, StoragePolicy P, typename ValueDeleter = void>
struct RcInnerAllocImpl {
    static_assert(false);
};

template<typename T, typename Allocator>
struct RcInnerAllocImpl<T, Allocator, StoragePolicy::Embed> : RcInnerImpl<T, StoragePolicy::Embed> {
    using base_t = RcInnerImpl<T, StoragePolicy::Embed>;
    Allocator allocator;

    RcInnerAllocImpl(Allocator a): base_t(), allocator(a) {}
    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            self->value->~T();
            self->value     = nullptr;
            self->has_value = false;
        } else {
            auto self_allocator =
                detail::rebind_alloc<Allocator, RcInnerAllocImpl>(self->allocator);
            self_allocator.deallocate(self, 1);
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        base_t::template allocate_value<Args...>(std::forward<Args>(args)...);
    }
};

template<typename T, typename Allocator>
struct RcInnerAllocImpl<T, Allocator, StoragePolicy::Separate>
    : RcInnerImpl<T, StoragePolicy::Separate> {
    static_assert(! std::is_array_v<T>);
    using base_t = RcInnerImpl<T, StoragePolicy::Separate>;
    Allocator allocator;

    RcInnerAllocImpl(Allocator a): base_t(), allocator(a) {}
    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            self->value->~T();
            self->allocator.deallocate(const_cast<std::remove_cv_t<T>*>(self->value), 1);
            self->value = nullptr;
        } else {
            auto self_allocator =
                detail::rebind_alloc<Allocator, RcInnerAllocImpl>(self->allocator);
            self_allocator.deallocate(self, 1);
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        auto* ptr = allocator.allocate(1);
        new (ptr) T(std::forward<Args>(args)...);
        this->value = ptr;
    }
};

template<typename T, typename Allocator>
struct RcInnerAllocImpl<T[], Allocator, StoragePolicy::Separate>
    : RcInnerImpl<T[], StoragePolicy::Separate> {
    using base_t  = RcInnerImpl<T[], StoragePolicy::Separate>;
    using value_t = std::remove_extent_t<T>;
    Allocator allocator;

    RcInnerAllocImpl(Allocator a, std::size_t n): base_t(n), allocator(a) {}
    void do_delete(detail::DeleteType t) override {
        auto self = this;
        if (t == detail::DeleteType::Value) {
            auto n = base_t::size;
            for (std::size_t i = 0; i < n; i++) {
                (self->value + i)->~value_t();
            }
            auto p = const_cast<std::remove_const_t<value_t>*>(self->value);
            self->allocator.deallocate(p, n);
            self->value = nullptr;
        } else {
            auto self_allocator =
                detail::rebind_alloc<Allocator, RcInnerAllocImpl>(self->allocator);
            self_allocator.deallocate(self, 1);
        }
    }

    template<typename... Args>
    void allocate_value(Args&&... args) {
        auto  n     = this->size;
        auto* ptr   = allocator.allocate(n);
        this->value = ptr;
        for (std::size_t i = 0; i < n; i++) {
            new (ptr + i) value_t(std::forward<Args>(args)...);
        }
    }
};

template<typename T, typename Allocator, typename ValueDeleter>
struct RcInnerAllocImpl<T, Allocator, StoragePolicy::SeparateWithDeleter, ValueDeleter>
    : RcInnerImpl<T, StoragePolicy::SeparateWithDeleter, ValueDeleter> {
    using base_t = RcInnerImpl<T, StoragePolicy::Separate>;
    Allocator allocator;

    RcInnerAllocImpl(Allocator a, T* p, ValueDeleter d): base_t(p, std::move(d)), allocator(a) {}

    void do_delete(detail::DeleteType t) override {
        auto self = this;

        if (t == detail::DeleteType::Value) {
            base_t::value_deletor(self->value);
            self->value = nullptr;
        } else {
            auto self_allocator =
                detail::rebind_alloc<Allocator, RcInnerAllocImpl>(self->allocator);
            self_allocator.deallocate(self, 1);
        }
    }
};

} // namespace detail

export template<typename T>
class Rc;

struct RcMakeHelper {
    template<typename T>
    static auto make_rc(Rc<T>::inner_t* inner) noexcept {
        return Rc<T>(inner);
    }
};

export template<typename T>
class Weak final {
    friend class Rc<T>;
    using inner_t = RcInner<std::add_const_t<T>>;
    inner_t* m_ptr;

    explicit Weak(inner_t* p) noexcept: m_ptr(p) {}

public:
    Weak() noexcept: m_ptr(nullptr) {}

    Weak(const Weak& other) noexcept: Weak(other.clone()) {}

    Weak(Weak&& other) noexcept: m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

    ~Weak() {
        if (m_ptr) {
            m_ptr->dec_weak();
            if (m_ptr->weak == 0) {
                m_ptr->do_delete(detail::DeleteType::Self);
            }
        }
    }

    auto clone() const noexcept -> Weak {
        if (m_ptr) m_ptr->inc_weak();
        return Weak(m_ptr);
    }

    auto upgrade() const -> std::optional<Rc<T>> {
        if (! m_ptr || m_ptr->strong == 0) return std::nullopt;
        m_ptr->inc_strong();
        return { RcMakeHelper::make_rc<T>(m_ptr) };
    }

    auto strong_count() const -> std::size_t { return m_ptr ? m_ptr->strong : 0; }

    auto weak_count() const -> std::size_t { return m_ptr ? m_ptr->weak - 1 : 0; }
};

template<typename T>
class RcBase {
protected:
    using inner_t = RcInner<std::add_const_t<T>>;
    inner_t* m_ptr;

    explicit RcBase(inner_t* p) noexcept: m_ptr(p) {}

    auto inner() const { return m_ptr; }
    auto inner() { return m_ptr; }

    auto drop() {
        if (m_ptr) {
            m_ptr->dec_strong();
            if (m_ptr->strong == 0) {
                m_ptr->do_delete(detail::DeleteType::Value);
                m_ptr->dec_weak();
                if (m_ptr->weak == 0) {
                    m_ptr->do_delete(detail::DeleteType::Self);
                }
            }
        }
    }

    template<typename Deleter>
    static auto allocate_inner(T* p, Deleter&& d) -> inner_t* {
        return new detail::RcInnerImpl<T, StoragePolicy::SeparateWithDeleter, Deleter>(
            p, std::move(d));
    }

    template<typename Deleter, typename Allocator>
    static auto allocate_inner(T* p, Allocator alloc, Deleter&& d) -> inner_t* {
        using inner_t =
            detail::RcInnerAllocImpl<T, Allocator, StoragePolicy::SeparateWithDeleter, Deleter>;
        auto self_allocator = detail::rebind_alloc<Allocator, inner_t>(alloc);
        auto mem            = (std::byte*)self_allocator.allocate(1);
        return new (mem) inner_t(alloc, p, std::move(d));
    }

public:
    using value_t       = std::remove_extent_t<T>;
    using const_value_t = std::add_const_t<std::remove_extent_t<T>>;

    auto strong_count() const -> std::size_t { return m_ptr ? m_ptr->strong : 0; }

    auto weak_count() const -> std::size_t { return m_ptr ? m_ptr->weak - 1 : 0; }

    auto is_unique() const -> bool { return strong_count() == 1 && weak_count() == 0; }

    auto size() const -> std::size_t {
        if (m_ptr) {
            if constexpr (std::is_array_v<T>) {
                auto p = static_cast<const detail::RcInnerArrayImpl<T>*>(m_ptr);
                return p->size;
            } else {
                return 1;
            }
        } else {
            return 0;
        }
    }

    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() {
        drop();
        m_ptr = nullptr;
    }
    auto get() const noexcept -> const_value_t* {
        auto p = this->inner();
        return p ? p->value : nullptr;
    }

    auto operator*() const noexcept -> const_value_t& {
        auto p = this->inner();
        return *(p->value);
    }
    auto operator->() const noexcept -> const_value_t* { return get(); }
};

template<typename T>
class RcAdaptor : public RcBase<T> {
protected:
    using inner_t = RcBase<T>::inner_t;
    explicit RcAdaptor(inner_t* p) noexcept: RcBase<T>(p) {}

public:
    using value_t       = RcBase<T>::value_t;
    using const_value_t = RcBase<T>::const_value_t;
    auto to_const() const -> Rc<const T> {
        this->m_ptr->inc_strong();
        return RcMakeHelper::make_rc<const T>(this->m_ptr);
    }
    auto get() noexcept -> value_t* {
        auto p = this->inner();
        return p ? const_cast<value_t*>(p->value) : nullptr;
    }
    auto operator*() noexcept -> value_t& {
        auto p = this->inner();
        return *const_cast<value_t*>(p->value);
    }
    auto operator->() noexcept -> value_t* { return get(); }

    auto get() const noexcept -> const_value_t* {
        auto p = this->inner();
        return p ? p->value : nullptr;
    }
    auto operator*() const noexcept -> const_value_t& {
        auto p = this->inner();
        return *(p->value);
    }
    auto operator->() const noexcept -> const_value_t* { return get(); }
};
template<typename F>
class RcAdaptor<const F> : public RcBase<const F> {
    using T = const F;

protected:
    using inner_t = RcBase<T>::inner_t;
    explicit RcAdaptor(inner_t* p) noexcept: RcBase<T>(p) {}

public:
    using const_value_t = RcBase<T>::const_value_t;
    auto get() const noexcept -> const_value_t* {
        auto p = this->inner();
        return p ? p->value : nullptr;
    }
    auto operator*() const noexcept -> const_value_t& {
        auto p = this->inner();
        return *(p->value);
    }
    auto operator->() const noexcept -> const_value_t* { return get(); }
};

template<typename T>
class Rc final : public RcAdaptor<T> {
    friend struct RcMakeHelper;
    using inner_t = RcBase<T>::inner_t;
    explicit Rc(inner_t* p) noexcept: RcAdaptor<T>(p) {}

public:
    Rc(): Rc((inner_t*)nullptr) {}
    ~Rc() { RcBase<T>::drop(); }

    Rc(const Rc& other) noexcept(detail::noexp): Rc(other.clone()) {}
    Rc& operator=(const Rc& other) noexcept(detail::noexp) {
        if (this != &other) {
            Rc(other.clone()).swap(*this);
        }
        return *this;
    }

    Rc(Rc&& other) noexcept: Rc(other.m_ptr) { other.m_ptr = nullptr; }
    Rc& operator=(Rc&& other) noexcept {
        Rc(std::move(other)).swap(*this);
        return *this;
    }

    template<typename U>
        requires std::is_const_v<T> && std::same_as<std::remove_cv_t<T>, U>
    Rc(const Rc<U>& o): Rc(o.to_const()) {}

    explicit Rc(T* p): Rc(p, std::default_delete<T>()) {}
    template<typename Deleter>
    Rc(T* p, Deleter&& d): Rc(RcBase<T>::allocate_inner(p, std::move(d))) {}

    template<typename Deleter, typename Allocator>
    Rc(T* p, Deleter&& d, Allocator alloc): Rc(RcBase<T>::allocate_inner(p, alloc, std::move(d))) {}

    auto downgrade() const -> Weak<T> {
        this->m_ptr->inc_weak();
        return Weak<T>(this->m_ptr);
    }
    auto clone() const noexcept(detail::noexp) -> Rc {
        if (this->m_ptr) this->m_ptr->inc_strong();
        return Rc(this->m_ptr);
    }

    void swap(Rc& other) noexcept { std::swap(this->m_ptr, other.m_ptr); }
};

export template<typename T, StoragePolicy Sp = StoragePolicy::Separate, typename... Args>
    requires(! std::is_array_v<T>)
auto make_rc(Args&&... args) -> Rc<T> {
    auto inner = new detail::RcInnerImpl<T, Sp>();
    inner->allocate_value(std::forward<Args>(args)...);
    return RcMakeHelper::make_rc<T>(inner);
}

export template<typename T, StoragePolicy Sp = StoragePolicy::Separate, typename... Args>
    requires std::is_array_v<T>
auto make_rc(std::size_t n, const typename Rc<T>::const_value_t& init) -> Rc<T> {
    auto inner = new detail::RcInnerImpl<T, Sp>(n);
    inner->allocate_value(init);
    return RcMakeHelper::make_rc<T>(inner);
}

export template<typename T, StoragePolicy Sp = StoragePolicy::Separate, typename Allocator,
                typename... Args>
    requires(! std::is_array_v<T>)
auto allocate_make_rc(const Allocator& alloc, Args&&... args) -> Rc<T> {
    using inner_t       = detail::RcInnerAllocImpl<T, Allocator, Sp>;
    auto self_allocator = detail::rebind_alloc<Allocator, inner_t>(alloc);

    auto mem   = (std::byte*)self_allocator.allocate(1);
    auto inner = new (mem) inner_t(alloc);
    inner->allocate_value(std::forward<Args>(args)...);
    return RcMakeHelper::make_rc<T>(inner);
}
export template<typename T, StoragePolicy Sp = StoragePolicy::Separate, typename Allocator>
    requires std::is_array_v<T>
auto allocate_make_rc(const Allocator& alloc, std::size_t n, typename Rc<T>::const_value_t& t)
    -> Rc<T> {
    using inner_t       = detail::RcInnerAllocImpl<T, Allocator, Sp>;
    auto self_allocator = detail::rebind_alloc<Allocator, inner_t>(alloc);

    auto mem   = (std::byte*)self_allocator.allocate(1);
    auto inner = new (mem) inner_t(alloc, n);
    inner->allocate_value(t);
    return RcMakeHelper::make_rc<T>(inner);
}

// Non-member functions
export template<typename T>
void swap(Rc<T>& lhs, Rc<T>& rhs) noexcept {
    lhs.swap(rhs);
}
} // namespace rstd::rc

namespace rstd
{
export template<typename T>
using Rc = rc::Rc<T>;
}