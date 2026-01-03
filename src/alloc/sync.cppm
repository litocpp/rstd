module;
#include <rstd/assert.hpp>
export module rstd.alloc.sync;
export import rstd.core;

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

    virtual ~ArcInner()                            = default;
    virtual ptr<T> data() noexcept                 = 0;
    virtual void   do_delete(detail::DeleteType t) = 0;

    void inc_strong() {
        auto old = strong.fetch_add(1, rstd::memory_order::relaxed);
        assert(old < detail::MAX_REFCOUNT);
    }

    void inc_weak() {
        auto old = weak.fetch_add(1, rstd::memory_order::relaxed);
        assert(old < detail::MAX_REFCOUNT);
    }

    bool try_inc_strong() {
        usize cur = strong.load(rstd::memory_order::acquire);
        while (cur != 0) {
            assert(cur < detail::MAX_REFCOUNT);
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
            rstd::atomic_thread_fence(rstd::memory_order::acquire);

            do_delete(detail::DeleteType::Value);

            // release the implicit weak held by strong pointers
            if (weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
                rstd::atomic_thread_fence(rstd::memory_order::acquire);
                do_delete(detail::DeleteType::Self);
            }
        }
    }

    void drop_weak() noexcept {
        if (weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
            rstd::atomic_thread_fence(rstd::memory_order::acquire);
            do_delete(detail::DeleteType::Self);
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

    auto data() noexcept -> ptr<T> override { return reinterpret_cast<T*>(&storage); }
    void do_delete(detail::DeleteType t) override {
        if (t == detail::DeleteType::Value) {
            rstd::destroy_at(reinterpret_cast<T*>(&storage));
        } else {
            delete this;
        }
    }
};

template<typename T>
struct ArcInnerImpl<T, ArcStoragePolicy::Separate> : ArcInner<T> {
    ptr<T> p;

    auto data() noexcept -> ptr<T> override { return p; }
    void do_delete(detail::DeleteType t) override {
        if (t == detail::DeleteType::Value) {
            delete &*p;
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
export template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Arc>
struct Impl<T, Self> : Impl<T, Def<Self>> {
    auto clone() const -> Self;
};

export template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> && meta::special_of<Self, rstd::sync::Weak>
struct Impl<T, Self> : Impl<T, Def<Self>> {
    auto clone() const -> Self;
};
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
    friend class Weak<T>;

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

    static auto make(param_t<T> value) -> Arc {
        auto inner = new ArcInnerImpl<T, ArcStoragePolicy::Embed>;
        rstd::construct_at(reinterpret_cast<T*>(&(inner->storage)), rstd::param_forward<T>(value));
        return { ArcData<T> { .inner = inner } };
    }

    static Arc from_raw(ArcRaw<T> r) noexcept { return { ArcData<T> { .inner = r.inner } }; }

    // ===== Observers =====
    ref<T>       operator*() noexcept { return self.inner->data().to_ref(); }
    ref<const T> operator*() const noexcept { return self.inner->data().to_ref(); }

    auto     operator->() noexcept { return self.inner->data(); }
    auto     operator->() const noexcept { return self.inner->data(); }
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
            rstd::atomic_thread_fence(rstd::memory_order::acquire);
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
