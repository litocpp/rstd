module;
#include <rstd/macro.hpp>
export module rstd.alloc.sync;
export import rstd.core;

using rstd::mem::maybe_uninit::maybe_uninit_traits;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::pin::Pin;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::atomic_thread_fence;

namespace rstd::sync
{

namespace detail
{
constexpr usize MAX_REFCOUNT = (rstd::numeric_limits<usize>::max)() / 2;

enum class DeleteType
{
    Value = 0,
    Self,
};

using EmbedDeconstructor = void (*)(voidp);

struct ArcImplTrait {
    template<typename Self, typename = void>
    struct Api {
        using Trait = ArcImplTrait;
        auto data() noexcept -> voidp { return trait_call<0>(this); }
        void do_delete(DeleteType t, EmbedDeconstructor d) { trait_call<1>(this, t, d); }
    };

    template<typename F>
    using TCollect = TraitCollect<&F::data, &F::do_delete>;
};

} // namespace detail
enum class ArcStoragePolicy : u32
{
    Embed = 0,
    Separate,
    SeparateWithDeleter
};

template<class T>
struct ArcInner {
    Atomic<usize> strong { 1 };
    Atomic<usize> weak { 1 };

    Dyn<detail::ArcImplTrait> impl;

    ArcInner(Dyn<detail::ArcImplTrait> i): impl(i) {}

    auto data() noexcept -> ptr<T> {
        return ptr<T>::from_raw(rstd::launder(static_cast<T*>(impl.data())));
    }
    void do_delete(detail::DeleteType t, detail::EmbedDeconstructor de) {
        return impl.do_delete(t, de);
    }

    static void embed_deconstruct(voidp p) { rstd::destroy_at(static_cast<T*>(p)); }

    void inc_strong() {
        [[maybe_unused]] auto old = strong.fetch_add(1, rstd::memory_order::relaxed);
        debug_assert(old < detail::MAX_REFCOUNT);
    }

    void inc_weak() {
        [[maybe_unused]] auto old = weak.fetch_add(1, rstd::memory_order::relaxed);
        debug_assert(old < detail::MAX_REFCOUNT);
    }

    bool try_inc_strong() {
        usize cur = strong.load(rstd::memory_order::acquire);
        while (cur != 0) {
            debug_assert(cur < detail::MAX_REFCOUNT);
            if (strong.compare_exchange_weak(
                    cur, cur + 1, rstd::memory_order::acq_rel, rstd::memory_order::acquire)) {
                return true;
            }
        }
        return false;
    }

    void drop_strong() noexcept {
        // If we were the last strong, destroy T and release the implicit weak.
        if (strong.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
            // Synchronize with readers of T through acquire on last release.
            atomic_thread_fence(rstd::memory_order::acquire);

            do_delete(detail::DeleteType::Value, &embed_deconstruct);

            // release the implicit weak held by strong pointers
            if (weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
                atomic_thread_fence(rstd::memory_order::acquire);
                do_delete(detail::DeleteType::Self, &embed_deconstruct);
            }
        }
    }

    void drop_weak() noexcept {
        if (weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
            atomic_thread_fence(rstd::memory_order::acquire);
            do_delete(detail::DeleteType::Self, &embed_deconstruct);
        }
    }
};

template<typename T, ArcStoragePolicy P = ArcStoragePolicy::Embed>
struct ArcInnerImpl {
    static_assert(false);
};

template<typename T>
struct ArcInnerImpl<T, ArcStoragePolicy::Embed> : ArcInner<T> {
    static_assert(Impled<T, Sized>);

    alignas(T) rstd::byte storage[sizeof(T)];

    ArcInnerImpl();

    auto data() noexcept -> voidp { return &storage; }
    void do_delete(detail::DeleteType t, detail::EmbedDeconstructor deconstruct) {
        if (t == detail::DeleteType::Value) {
            deconstruct(&storage);
        } else {
            delete this;
        }
    }
};

template<typename T>
struct ArcInnerImpl<T, ArcStoragePolicy::Separate> : ArcInner<T> {
    ptr<T> p;

    ArcInnerImpl();

    auto data() noexcept -> voidp { return p; }
    void do_delete(detail::DeleteType t, detail::EmbedDeconstructor) {
        if (t == detail::DeleteType::Value) {
            delete rstd::addressof(*p);
        } else {
            delete this;
        }
    }
};

// Forward decls
export template<typename T>
class Arc;
export template<typename T>
class Weak;

} // namespace rstd::sync

namespace rstd
{
template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Arc>
struct Impl<T, Self> : Impl<T, default_tag<Self>> {
    auto clone() const -> Self;
};

template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Weak>
struct Impl<T, Self> : Impl<T, default_tag<Self>> {
    auto clone() const -> Self;
};

template<meta::same_as<sync::detail::ArcImplTrait> T, typename A, sync::ArcStoragePolicy P>
struct Impl<T, sync::ArcInnerImpl<A, P>> : ImplInClass<T, sync::ArcInnerImpl<A, P>> {};

template<typename T>
sync::ArcInnerImpl<T, sync::ArcStoragePolicy::Embed>::ArcInnerImpl()
    : ArcInner<T>(rstd::make_dyn<detail::ArcImplTrait>(this)) {}
template<typename T>
sync::ArcInnerImpl<T, sync::ArcStoragePolicy::Separate>::ArcInnerImpl()
    : ArcInner<T>(rstd::make_dyn<detail::ArcImplTrait>(this)) {}

} // namespace rstd

namespace rstd::sync
{
template<typename T>
struct ArcData {
    ArcInner<T>* inner { nullptr };
};

export template<typename T>
class ArcRaw {
    ArcInner<T>* inner;

    friend class Arc<T>;
    ArcRaw(auto t): inner(t) {}

public:
    auto as_ptr() const { return inner->data(); };
};

export template<typename T>
class Arc : public WithTrait<Arc<T>, clone::Clone> {
    ArcData<T> self;

    template<typename, typename>
    friend struct rstd::Impl;

    template<typename>
    friend class Weak;

    template<typename>
    friend class Arc;

    constexpr Arc(ArcData<T> s): self(s) {}

public:
    using element_type = T;

    // Copy
    constexpr Arc(const Arc&)            = delete;
    constexpr Arc& operator=(const Arc&) = delete;

    // Move
    constexpr Arc(Arc&& other) noexcept: self({ rstd::exchange(other.self, {}) }) {}
    constexpr Arc& operator=(Arc&& other) noexcept {
        if (this != &other) {
            reset();
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    ~Arc() { reset(); }

    void reset() {
        if (self.inner) {
            self.inner->drop_strong();
            self.inner = nullptr;
        }
    }

    /// Constructs a new `Arc<T>`.
    ///
    /// This is the primary way to create an Arc. The value is moved into
    /// a new heap allocation and wrapped in an Arc.
    static auto make(param_t<T> value) -> Arc {
        auto inner = new ArcInnerImpl<T, ArcStoragePolicy::Embed>;
        rstd::construct_at(reinterpret_cast<T*>(&(inner->storage)), rstd::param_forward<T>(value));
        return { ArcData<T> { .inner = inner } };
    }

    /// Creates a new `Arc` containing an uninitialized value.
    ///
    /// The returned Arc contains a `MaybeUninit<T>` that must be initialized
    /// before calling `assume_init()` to convert to `Arc<T>`.
    ///
    /// # Example
    /// ```cpp
    /// auto arc = Arc<int>::make_uninit();
    /// // Initialize the value
    /// Arc::get_mut(arc).unwrap().write(42);
    /// auto initialized = arc.assume_init();
    /// ```
    static auto make_uninit() -> Arc<MaybeUninit<T>>
        requires Impled<T, Sized>
    {
        return Arc<MaybeUninit<T>>::make(MaybeUninit<T>::uninit());
    }

    /// Creates a new `Pin<Arc<T>>`. If `T` does not implement `Unpin`, then
    /// the value will be pinned in memory and unable to be moved.
    ///
    /// This is useful for types that must not be moved after creation,
    /// such as self-referential structures.
    static auto pin(param_t<T> value) -> Pin<Arc<T>> {
        return Pin<Arc<T>>::make_unchecked(Arc::make(rstd::param_forward<T>(value)));
    }

    static Arc from_raw(ArcRaw<T> r) noexcept { return { ArcData<T> { .inner = r.inner } }; }

    auto assume_init()
        requires(! meta::same_as<typename maybe_uninit_traits<T>::value_type, void>)
    {
        using V    = maybe_uninit_traits<T>::value_type;
        auto inner = rstd::launder(reinterpret_cast<ArcInner<V>*>(self.inner));
        self.inner = nullptr;
        return Arc<V> { { .inner = inner } };
    }

    ref<T>       operator*() noexcept { return self.inner->data().as_ref(); }
    ref<const T> operator*() const noexcept { return self.inner->data().to_ref(); }

    T*       operator->() noexcept { return self.inner->data(); }
    T*       operator->() const noexcept { return self.inner->data(); }
    explicit operator bool() const noexcept { return self.inner != nullptr; }

    usize strong_count() const noexcept {
        return self.inner ? self.inner->strong.load(rstd::memory_order::acquire) : 0;
    }

    usize weak_count() const noexcept {
        // Rust's Arc::weak_count excludes the implicit weak if strong>0.
        if (! self.inner) return 0;
        auto w = self.inner->weak.load(rstd::memory_order::acquire);
        auto s = self.inner->strong.load(rstd::memory_order::acquire);
        if (s == 0) return w;
        return w > 0 ? (w - 1) : 0;
    }

    // Rust: Arc::downgrade
    Weak<T> downgrade() const noexcept;

    // Rust: Arc::as_ptr (raw pointer to T)
    auto as_ptr() const noexcept { return self.inner->data(); }

    /// Returns true if the two `Arc`s point to the same allocation
    /// (not just values that compare as equal).
    static bool ptr_eq(const Arc& a, const Arc& b) noexcept { return a.self.inner == b.self.inner; }

    /// Returns true if this is the only `Arc` or `Weak` pointer to the allocation.
    static bool is_unique(const Arc& arc) noexcept {
        return arc.self.inner && arc.self.inner->strong.load(rstd::memory_order::acquire) == 1 &&
               arc.self.inner->weak.load(rstd::memory_order::acquire) == 1;
    }

    /// Returns a mutable reference to the inner value if there are no other
    /// `Arc` or `Weak` pointers to the same allocation.
    static auto get_mut(Arc& arc) noexcept -> Option<ref<T>> {
        if (is_unique(arc)) {
            return Some(arc.self.inner->data().as_ref());
        }
        return None();
    }

    // Rust: Arc::try_unwrap / into_inner style
    auto try_unwrap() -> Result<T, Arc>
        requires Impled<T, Sized>
    {
        do {
            if (self.inner == nullptr) break;
            // Attempt to drop strong to 0 with CAS to avoid races.
            usize expected = 1;
            if (! self.inner->strong.compare_exchange_strong(
                    expected, 0, rstd::memory_order::relaxed, rstd::memory_order::relaxed)) {
                break;
            }
            atomic_thread_fence(rstd::memory_order::acquire);
            auto w = Weak<T> { self };
            self   = {};
            return Ok(rstd::move(*(w.self.inner->data())));
        } while (0);
        return Err(rstd::move(*this));
    }

    // ===== Raw interop (Rust: into_raw / from_raw) =====
    // These are intentionally low-level; the pointer must come from into_raw().
    auto into_raw() noexcept {
        auto inner = self.inner;
        self       = {}; // leak ownership to raw
        return ArcRaw<T> { inner };
    }
};

export template<class T>
class Weak : public WithTrait<Weak<T>, clone::Clone> {
    ArcData<T> self;

    template<typename, typename>
    friend struct rstd::Impl;
    friend class Arc<T>;

    constexpr Weak(ArcData<T> s) noexcept: self(s) {}

public:
    using element_type = T;

    // Copy
    constexpr Weak(const Weak& other)            = delete;
    constexpr Weak& operator=(const Weak& other) = delete;

    // Move
    constexpr Weak(Weak&& other) noexcept: self(rstd::exchange(other.self, {})) {}
    constexpr Weak& operator=(Weak&& other) noexcept {
        if (this != &other) {
            reset();
            self = rstd::exchange(other.self, {});
        }
        return *this;
    }

    ~Weak() { reset(); }

    /// Constructs a new `Weak<T>`, without allocating any memory.
    /// Calling `upgrade()` on the return value always gives `None`.
    static constexpr auto make() noexcept -> Weak {
        return Weak { ArcData<T> { .inner = nullptr } };
    }

    void reset() {
        if (self.inner) {
            self.inner->drop_weak();
            self.inner = nullptr;
        }
    }

    auto upgrade() const noexcept -> Arc<T> {
        if (self.inner && self.inner->try_inc_strong()) {
            return { self };
        }
        return { {} };
    }

    auto strong_count() const noexcept -> usize {
        return self.inner ? self.inner->strong.load(rstd::memory_order::acquire) : 0;
    }

    auto weak_count() const noexcept -> usize {
        return self.inner ? self.inner->weak.load(rstd::memory_order::acquire) : 0;
    }

    bool expired() const noexcept { return strong_count() == 0; }

    auto as_ptr() const noexcept { return self.inner->data(); }
};

template<class T>
auto Arc<T>::downgrade() const noexcept -> Weak<T> {
    Weak<T> out { self };
    if (self.inner) {
        self.inner->inc_weak();
    }
    return out;
}

} // namespace rstd::sync

namespace rstd
{
template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Arc>
auto Impl<T, Self>::clone() const -> Self {
    auto& s = this->self();
    if (s.self.inner) {
        s.self.inner->inc_strong();
    }
    return { s.self };
}

template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Weak>
auto Impl<T, Self>::clone() const -> Self {
    auto& s = this->self();
    if (s.self.inner) {
        s.self.inner->inc_weak();
    }
    return { s.self };
}

} // namespace rstd
