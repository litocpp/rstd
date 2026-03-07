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
}

namespace rstd::option::detail
{

template<typename T>
struct option_store<ptr_::non_null::NonNull<T>> {
protected:
    using union_value_t       = ptr_::non_null::NonNull<T>;
    using union_const_value_t = ptr_::non_null::NonNull<T> const;

    constexpr auto _ptr() const noexcept -> union_const_value_t* {
        return reinterpret_cast<union_const_value_t*>(m_storage);
    }
    constexpr auto _ptr() noexcept -> union_value_t* {
        return reinterpret_cast<union_value_t*>(m_storage);
    }

    template<typename V>
    constexpr void _construct_val(V&& val) {
        auto v = union_value_t(rstd::forward<V>(val));
        rstd::memcpy(m_storage, &v, sizeof(union_value_t));
    }
    template<typename V>
    constexpr void _assign_val(V&& val) {
        _construct_val(rstd::forward<V>(val));
    }
    constexpr void _assign_none() {
        rstd::memset(m_storage, 0, sizeof(union_value_t));
    }

public:
    constexpr auto is_some() const noexcept -> bool {
        for (usize i = 0; i < sizeof(union_value_t); i++) {
            if (m_storage[i] != rstd::byte(0)) {
                return true;
            }
        }
        return false;
    }

private:
    alignas(union_value_t) rstd::byte m_storage[sizeof(union_value_t)] {};
};

} // namespace rstd::option::detail

namespace rstd::ptr_::non_null
{

template<typename T>
class NonNull {
public:
    using element_type    = T;
    using pointer_t       = mut_ptr<T>;
    using const_pointer_t = ptr<T>;

private:
    pointer_t m_ptr;

    explicit constexpr NonNull(pointer_t p) noexcept: m_ptr(p) {}
    constexpr NonNull() = delete;

public:
    friend struct rstd::option::detail::option_store<NonNull>;
    constexpr NonNull(const NonNull&) noexcept            = default;
    constexpr NonNull& operator=(const NonNull&) noexcept = default;
    constexpr NonNull(NonNull&&) noexcept                 = default;
    constexpr NonNull& operator=(NonNull&&) noexcept      = default;

    USE_TRAIT(NonNull)

    /// \name T: Sized
    /// @{
    static auto without_provenance(num::nonzero::NonZero<usize> addr) noexcept -> NonNull<T>
        requires Impled<T, Sized>
    {
        T* t = reinterpret_cast<T*>(addr.get());
        return { mut_ptr<T>::from_raw_parts(t) };
    }

    static auto dangling() noexcept -> NonNull<T>
        requires Impled<T, Sized>
    {
        return { mut_ptr<T>::from_raw_parts(reinterpret_cast<T*>(alignof(T))) };
    }
    /// @}

    /// \name T: ?Sized
    /// @{
    static constexpr auto make(pointer_t p) noexcept -> Option<NonNull> {
        if (p == nullptr) return {};
        return Some(NonNull(p));
    }

    static constexpr auto make_unchecked(pointer_t p) noexcept -> NonNull { return NonNull(p); }

    constexpr auto as_ptr() const noexcept -> const_pointer_t { return m_ptr.as_ptr(); }
    constexpr auto as_mut_ptr() const noexcept -> pointer_t { return m_ptr; }

    constexpr explicit operator bool() const noexcept { return m_ptr != nullptr; }

    constexpr auto as_ref() const noexcept { return m_ptr.as_ref(); }

    constexpr auto as_mut() const noexcept { return m_ptr.as_mut_ref(); }
    /// @}

    template<class U>
    constexpr auto cast() const noexcept {
        return NonNull<U>::make_unchecked(m_ptr.template cast<U>());
    }

    template<class U>
        requires(mtp::convertible_to<mut_ptr<U>, mut_ptr<T>>)
    constexpr NonNull(const NonNull<U>& other) noexcept: m_ptr(other.as_mut_ptr()) {}

    constexpr NonNull add(usize count) const noexcept {
        return NonNull::make_unchecked(m_ptr + count);
    }

    constexpr NonNull sub(usize count) const noexcept {
        return NonNull::make_unchecked(m_ptr - count);
    }

    constexpr NonNull offset(isize count) const noexcept {
        return NonNull::make_unchecked(m_ptr + count);
    }

    constexpr NonNull byte_add(usize bytes) const noexcept {
        return NonNull::make_unchecked(m_ptr.byte_add(bytes));
    }

    constexpr NonNull byte_sub(usize bytes) const noexcept {
        return NonNull::make_unchecked(m_ptr.byte_sub(bytes));
    }

    friend constexpr bool operator<=>(NonNull a, NonNull b) noexcept { return a.m_ptr <=> b.m_ptr; }
    friend constexpr bool operator==(NonNull a, NonNull b) noexcept { return a.m_ptr == b.m_ptr; }
    constexpr bool        operator==(pointer_t p) noexcept { return m_ptr == p; }
    constexpr bool        operator==(nullptr_t in) const noexcept { return m_ptr == in; }

    struct Hasher {
        usize operator()(NonNull p) const noexcept { return cppstd::hash<pointer_t> {}(p.m_ptr); }
    };
};

} // namespace rstd::ptr_::non_null
