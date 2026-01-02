export module rstd.alloc.sync;
export import rstd.core;

namespace rstd::sync
{

namespace detail
{

constexpr usize MAX_REFCOUNT = (rstd::numeric_limits<usize>::max)() / 2;

// Helper: "hard" abort when refcount overflows / invariant broken.
[[noreturn]] inline void abort_refcount_overflow() {
    // Prefer a project-wide abort facility if you have one.
    // Fallback: std::terminate().
    cppstd::terminate();
}

template<class Alloc>
struct alloc_traits {
    using traits = rstd::allocator_traits<Alloc>;

    template<class T>
    using rebind_alloc = typename traits::template rebind_alloc<T>;

    template<class T>
    static T* allocate_one(Alloc& a) {
        rebind_alloc<T> ra(a);
        return traits::allocate(ra, 1);
    }

    template<class T>
    static void deallocate_one(Alloc& a, T* p) noexcept {
        rebind_alloc<T> ra(a);
        traits::deallocate(ra, p, 1);
    }

    template<class T, class... Args>
    static void construct(Alloc& a, T* p, Args&&... args) {
        rebind_alloc<T> ra(a);
        traits::construct(ra, p, rstd::forward<Args>(args)...);
    }

    template<class T>
    static void destroy(Alloc& a, T* p) noexcept {
        rebind_alloc<T> ra(a);
        traits::destroy(ra, p);
    }
};

template<class T, class Alloc>
struct ArcInner {
    // strong == 0 => data already dropped.
    // weak counts include the implicit weak held by all strong pointers.
    Atomic<usize> strong { 1 };
    Atomic<usize> weak { 1 };

    // Store allocator for deallocation symmetry (like Rust's Arc<T, A>).
    [[no_unique_address]] Alloc alloc;

    T data;

    template<class... Args>
    ArcInner(Alloc a, Args&&... args): alloc(rstd::move(a)), data(rstd::forward<Args>(args)...) {}
};

template<class T, class Alloc>
inline void inc_strong(ArcInner<T, Alloc>* p) {
    auto old = p->strong.fetch_add(1, rstd::memory_order::relaxed);
    if (old >= MAX_REFCOUNT) abort_refcount_overflow();
}

template<class T, class Alloc>
inline void inc_weak(ArcInner<T, Alloc>* p) {
    auto old = p->weak.fetch_add(1, rstd::memory_order::relaxed);
    if (old >= MAX_REFCOUNT) abort_refcount_overflow();
}

template<class T, class Alloc>
inline bool try_inc_strong(ArcInner<T, Alloc>* p) {
    usize cur = p->strong.load(rstd::memory_order::acquire);
    while (cur != 0) {
        if (cur >= MAX_REFCOUNT) abort_refcount_overflow();
        if (p->strong.compare_exchange_weak(
                cur, cur + 1, rstd::memory_order::acq_rel, rstd::memory_order::acquire)) {
            return true;
        }
    }
    return false;
}

template<class T, class Alloc>
inline void drop_strong(ArcInner<T, Alloc>* p) noexcept {
    // If we were the last strong, destroy T and release the implicit weak.
    if (p->strong.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
        // Synchronize with readers of T through acquire on last release.
        rstd::atomic_thread_fence(rstd::memory_order::acquire);

        alloc_traits<Alloc>::destroy(p->alloc, rstd::addressof(p->data));

        // release the implicit weak held by strong pointers
        if (p->weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
            rstd::atomic_thread_fence(rstd::memory_order::acquire);
            Alloc a = rstd::move(p->alloc);
            alloc_traits<Alloc>::destroy(a, p); // destroy ArcInner itself (already destroyed data)
            alloc_traits<Alloc>::deallocate_one(a, p);
        }
    }
}

template<class T, class Alloc>
inline void drop_weak(ArcInner<T, Alloc>* p) noexcept {
    if (p->weak.fetch_sub(1, rstd::memory_order::acq_rel) == 1) {
        rstd::atomic_thread_fence(rstd::memory_order::acquire);
        Alloc a = rstd::move(p->alloc);
        // At this point, strong must be 0 and data already destroyed.
        alloc_traits<Alloc>::destroy(a, p);
        alloc_traits<Alloc>::deallocate_one(a, p);
    }
}

} // namespace detail

// Forward decls
export template<class T, class Alloc>
class Arc;
export template<class T, class Alloc>
class Weak;

export template<class T, class Alloc = rstd::allocator<byte>>
class Arc {
public:
    using element_type   = T;
    using allocator_type = Alloc;

    constexpr Arc() noexcept = default;

    // Copy == clone (increments strong)
    Arc(const Arc& other) noexcept: m_inner(other.m_inner) {
        if (m_inner) detail::inc_strong(m_inner);
    }
    Arc& operator=(const Arc& other) noexcept {
        if (this == &other) return *this;
        reset();
        m_inner = other.m_inner;
        if (m_inner) detail::inc_strong(m_inner);
        return *this;
    }

    // Move
    Arc(Arc&& other) noexcept: m_inner(rstd::exchange(other.m_inner, nullptr)) {}
    Arc& operator=(Arc&& other) noexcept {
        if (this == &other) return *this;
        reset();
        m_inner = rstd::exchange(other.m_inner, nullptr);
        return *this;
    }

    ~Arc() { reset(); }

    static Arc make(T value) { return make_in(Alloc {}, rstd::move(value)); }

    static Arc make_in(Alloc alloc, T value) {
        using Inner = detail::ArcInner<T, Alloc>;
        Alloc a     = alloc;

        Inner* p = nullptr;
        try {
            p = detail::alloc_traits<Alloc>::template allocate_one<Inner>(a);
            // Construct inner with strong=1, weak=1
            ::new (static_cast<void*>(p)) Inner(rstd::move(a), rstd::move(value));
        } catch (...) {
            if (p) detail::alloc_traits<Alloc>::template deallocate_one<Inner>(a, p);
            throw;
        }
        Arc out;
        out.m_inner = p;
        return out;
    }

    // ===== Observers =====
    T*       get() noexcept { return m_inner ? rstd::addressof(m_inner->data) : nullptr; }
    const T* get() const noexcept { return m_inner ? rstd::addressof(m_inner->data) : nullptr; }
    T&       operator*() noexcept { return *get(); }
    const T& operator*() const noexcept { return *get(); }
    T*       operator->() noexcept { return get(); }
    const T* operator->() const noexcept { return get(); }

    explicit operator bool() const noexcept { return m_inner != nullptr; }

    usize strong_count() const noexcept {
        return m_inner ? m_inner->strong.load(rstd::memory_order::acquire) : 0;
    }

    usize weak_count() const noexcept {
        // Rust's Arc::weak_count excludes the implicit weak if strong>0.
        if (! m_inner) return 0;
        auto w = m_inner->weak.load(rstd::memory_order::acquire);
        auto s = m_inner->strong.load(rstd::memory_order::acquire);
        if (s == 0) return w;
        return w > 0 ? (w - 1) : 0;
    }

    allocator_type allocator() const {
        if (! m_inner) return allocator_type {};
        return m_inner->alloc;
    }

    // Rust: Arc::downgrade
    Weak<T, Alloc> downgrade() const noexcept;

    // Rust: Arc::ptr_eq
    friend bool ptr_eq(const Arc& a, const Arc& b) noexcept { return a.m_inner == b.m_inner; }

    // Rust: Arc::as_ptr (raw pointer to T)
    const T* as_ptr() const noexcept { return get(); }

    // Rust: Arc::try_unwrap / into_inner style
    // If this is the only strong ref, move out T and drop allocation.
    // Returns true on success.
    bool try_unwrap(T& out) {
        if (! m_inner) return false;
        // Only one strong ref?
        if (m_inner->strong.load(rstd::memory_order::acquire) != 1) return false;

        // Attempt to drop strong to 0 with CAS to avoid races.
        usize expected = 1;
        if (! m_inner->strong.compare_exchange_strong(
                expected, 0, rstd::memory_order::acq_rel, rstd::memory_order::acquire)) {
            return false;
        }

        rstd::atomic_thread_fence(rstd::memory_order::acquire);

        // Move out data, destroy in-place
        out = rstd::move(m_inner->data);
        detail::alloc_traits<Alloc>::destroy(m_inner->alloc, rstd::addressof(m_inner->data));

        // release implicit weak + our Arc handle (strong already 0, we "consumed" Arc)
        m_inner->weak.fetch_sub(1, rstd::memory_order::acq_rel);

        // now drop the final weak (which should be 1 if there are no Weak refs)
        detail::drop_weak(m_inner);
        m_inner = nullptr;
        return true;
    }

    // ===== Raw interop (Rust: into_raw / from_raw) =====
    // These are intentionally low-level; the pointer must come from into_raw().
    const T* into_raw() noexcept {
        const T* p = as_ptr();
        m_inner    = nullptr; // leak ownership to raw
        return p;
    }

    static Arc from_raw(const T* p) noexcept {
        // ArcInner layout ensures data is a member; recover inner by offset.
        // This relies on ArcInner being standard layout with `data` last.
        using Inner    = detail::ArcInner<T, Alloc>;
        auto* data_ptr = const_cast<T*>(p);
        auto* inner    = reinterpret_cast<Inner*>(reinterpret_cast<unsigned char*>(data_ptr) -
                                               __builtin_offsetof(Inner, data));
        Arc   a;
        a.m_inner = inner;
        return a;
    }

    // ===== Unimplemented Rust-only API surface =====
    // These are provided for parity; wire them to your project's equivalents.
    template<class U>
    static Arc<U, Alloc> map(Arc&& /*arc*/, U&& /*f*/) {
        static_assert(sizeof(U) == 0, "TODO: Arc::map port requires project-specific support.");
        cppstd::abort();
    }

    // Unique access helper (like get_mut in Rust)
    // Returns nullptr unless this is uniquely owned (strong==1 and no other refs).
    T* get_mut() noexcept {
        if (! m_inner) return nullptr;
        if (m_inner->strong.load(rstd::memory_order::acquire) != 1) return nullptr;
        if (weak_count() != 0) return nullptr;
        return rstd::addressof(m_inner->data);
    }

private:
    using Inner    = detail::ArcInner<T, Alloc>;
    Inner* m_inner = nullptr;

    void reset() noexcept {
        if (! m_inner) return;
        detail::drop_strong(m_inner);
        m_inner = nullptr;
    }

    friend class Weak<T, Alloc>;
};

export template<class T, class Alloc = rstd::allocator<byte>>
class Weak {
public:
    using element_type   = T;
    using allocator_type = Alloc;

    constexpr Weak() noexcept = default;

    Weak(const Weak& other) noexcept: m_inner(other.m_inner) {
        if (m_inner) detail::inc_weak(m_inner);
    }
    Weak& operator=(const Weak& other) noexcept {
        if (this == &other) return *this;
        reset();
        m_inner = other.m_inner;
        if (m_inner) detail::inc_weak(m_inner);
        return *this;
    }

    Weak(Weak&& other) noexcept: m_inner(rstd::exchange(other.m_inner, nullptr)) {}
    Weak& operator=(Weak&& other) noexcept {
        if (this == &other) return *this;
        reset();
        m_inner = rstd::exchange(other.m_inner, nullptr);
        return *this;
    }

    ~Weak() { reset(); }

    // Rust: Weak::upgrade
    Arc<T, Alloc> upgrade() const noexcept {
        Arc<T, Alloc> out;
        if (! m_inner) return out;
        if (! detail::try_inc_strong(m_inner)) return out;
        out.m_inner = m_inner;
        return out;
    }

    usize strong_count() const noexcept {
        return m_inner ? m_inner->strong.load(rstd::memory_order::acquire) : 0;
    }

    usize weak_count() const noexcept {
        return m_inner ? m_inner->weak.load(rstd::memory_order::acquire) : 0;
    }

    bool expired() const noexcept { return strong_count() == 0; }

    const T* as_ptr() const noexcept { return m_inner ? rstd::addressof(m_inner->data) : nullptr; }

private:
    using Inner    = detail::ArcInner<T, Alloc>;
    Inner* m_inner = nullptr;

    void reset() noexcept {
        if (! m_inner) return;
        detail::drop_weak(m_inner);
        m_inner = nullptr;
    }

    friend class Arc<T, Alloc>;
};

template<class T, class Alloc>
inline Weak<T, Alloc> Arc<T, Alloc>::downgrade() const noexcept {
    Weak<T, Alloc> w;
    if (! m_inner) return w;
    detail::inc_weak(m_inner);
    w.m_inner = m_inner;
    return w;
}

// UniqueArc: like Rust's UniqueArc (a uniquely-owned Arc that can be turned into Arc)
export template<class T, class Alloc = rstd::allocator<byte>>
class UniqueArc {
public:
    using element_type   = T;
    using allocator_type = Alloc;

    UniqueArc() = delete;

    static UniqueArc make(T value) { return make_in(Alloc {}, rstd::move(value)); }

    static UniqueArc make_in(Alloc alloc, T value) {
        UniqueArc u;
        u.m_arc = Arc<T, Alloc>::make_in(rstd::move(alloc), rstd::move(value));
        return u;
    }

    // Access to underlying mutable data (unique by construction; still check in debug)
    T& get_mut() noexcept {
        auto* p = m_arc.get_mut();
        assert(p && "UniqueArc invariant broken (not unique)");
        return *p;
    }

    // Convert into a regular Arc.
    Arc<T, Alloc> into_arc() && noexcept { return rstd::move(m_arc); }

    // Downgrade (creates a Weak) without cloning Arc.
    Weak<T, Alloc> downgrade() const noexcept { return m_arc.downgrade(); }

private:
    Arc<T, Alloc> m_arc {};
};

} // namespace rstd::sync
