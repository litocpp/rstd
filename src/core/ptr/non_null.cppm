module;
#include <rstd/macro.hpp>
export module rstd.core:ptr.non_null;
export import :marker;
export import :num;
export import :cmp;
export import :option;
export import :assert;

namespace rstd::ptr_::non_null
{

export template<typename T>
class NonNull;

template<typename T>
struct SizedNonNull {};

template<typename T>
    requires Impled<T, Sized>
struct SizedNonNull<T> {
    /// Creates a pointer with the given address
    static auto without_provenance(num::nonzero::NonZero<usize> addr) noexcept -> NonNull<T>;

    /// Creates a new `NonNull` that is dangling, but well-aligned.
    static auto dangling() noexcept -> NonNull<T>;
};

export template<typename T>
class NonNull : public SizedNonNull<T> {
public:
    using element_type    = T;
    using pointer_t       = mut_ptr<T>;
    using const_pointer_t = ptr<T>;

private:
    const_pointer_t m_ptr;

    constexpr NonNull(const_pointer_t p) noexcept: m_ptr(p) {}
    constexpr NonNull() = delete;

public:
    USE_TRAIT(NonNull)

    static constexpr auto make(pointer_t p) noexcept -> Option<NonNull> {
        if (p == nullptr) return {};
        return Some(NonNull(p));
    }

    static constexpr NonNull make_unchecked(pointer_t p) noexcept { return NonNull(p.as_ptr()); }

    constexpr auto as_ptr() const noexcept -> const_pointer_t { return m_ptr; }
    constexpr auto as_mut_ptr() noexcept -> pointer_t { return as_cast<pointer_t>(m_ptr); }

    constexpr explicit operator bool() const noexcept { return m_ptr != nullptr; }

    constexpr T& as_ref() const noexcept {
        assert(m_ptr != nullptr);
        return *m_ptr;
    }

    constexpr T& as_mut() const noexcept {
        assert(m_ptr != nullptr);
        return *m_ptr;
    }

    template<class U>
    constexpr NonNull<U> cast() const noexcept {
        static_assert(meta::convertible_to<pointer_t, U*> || meta::convertible_to<U*, pointer_t>,
                      "NonNull::cast: unsupported cast between pointer types");
        return NonNull<U>::new_unchecked(reinterpret_cast<U*>(m_ptr));
    }

    template<class U>
        requires(meta::convertible_to<U*, T*>)
    constexpr NonNull(const NonNull<U>& other) noexcept: m_ptr(other.as_ptr()) {
        assert(m_ptr != nullptr);
    }

    constexpr NonNull add(usize count) const noexcept {
        assert(m_ptr != nullptr);
        return NonNull::make_unchecked(m_ptr + count);
    }

    constexpr NonNull sub(usize count) const noexcept {
        assert(m_ptr != nullptr);
        return NonNull::make_unchecked(m_ptr - count);
    }

    constexpr NonNull offset(isize count) const noexcept {
        assert(m_ptr != nullptr);
        return NonNull::make_unchecked(m_ptr + count);
    }

    constexpr NonNull byte_add(usize bytes) const noexcept {
        assert(m_ptr != nullptr);
        auto* p = reinterpret_cast<byte*>(m_ptr) + bytes;
        return NonNull::make_unchecked(reinterpret_cast<pointer_t>(p));
    }

    constexpr NonNull byte_sub(usize bytes) const noexcept {
        assert(m_ptr != nullptr);
        auto* p = reinterpret_cast<byte*>(m_ptr) - bytes;
        return NonNull::make_unchecked(reinterpret_cast<pointer_t>(p));
    }

    template<class F>
    constexpr NonNull map_addr(F&& f) const noexcept {
        assert(m_ptr != nullptr);
        auto addr     = reinterpret_cast<rstd::nullptr_t>(m_ptr);
        auto new_addr = static_cast<rstd::nullptr_t>(rstd::invoke(rstd::forward<F>(f), addr));
        assert(new_addr != 0);
        return NonNull::make_unchecked(reinterpret_cast<pointer_t>(new_addr));
    }

    friend constexpr bool operator<=>(NonNull a, NonNull b) noexcept { return a.m_ptr <=> b.m_ptr; }
    friend constexpr bool operator==(NonNull a, NonNull b) noexcept { return a.m_ptr == b.m_ptr; }
    constexpr bool        operator==(pointer_t p) noexcept { return m_ptr == p; }
    constexpr bool        operator==(nullptr_t in) const noexcept { return m_ptr == in; }

    struct Hasher {
        usize operator()(NonNull p) const noexcept { return cppstd::hash<pointer_t> {}(p.m_ptr); }
    };
};

template<typename T>
    requires Impled<T, Sized>
auto SizedNonNull<T>::without_provenance(num::nonzero::NonZero<usize> addr) noexcept -> NonNull<T> {
    T* t = reinterpret_cast<T*>(addr);
    return { t };
}

template<typename T>
    requires Impled<T, Sized>
auto SizedNonNull<T>::dangling() noexcept -> NonNull<T> {
    return { nullptr };
}

} // namespace rstd::ptr_::non_null
