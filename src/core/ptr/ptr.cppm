module;
#include <rstd/macro.hpp>
export module rstd.core:ptr.ptr;
import :num.types;
export import :ptr.metadata;
export import :ops.deref;
export import :cmp;

namespace rstd
{

/// An immutable raw pointer, analogous to Rust's `*const T`.
/// \tparam T The pointee type.
export template<typename T>
struct ptr;

/// A mutable raw pointer, analogous to Rust's `*mut T`.
/// \tparam T The pointee type.
export template<typename T>
struct mut_ptr;

namespace ptr_detail
{

template<typename T>
struct storage_type {
    using type = T;
};

template<>
struct storage_type<u8> {
    using type = byte;
};

template<typename T>
using storage_type_t = typename storage_type<mtp::rm_ext<T>>::type;

} // namespace ptr_detail

template<typename T, typename U>
auto from_raw_parts(U* self) noexcept -> T {
    if constexpr (requires { T::from_raw_parts(self->as_raw_ptr(), self->metadata()); }) {
        return T::from_raw_parts(self->as_raw_ptr(), self->metadata());
    } else {
        return T::from_raw_parts(self->as_raw_ptr());
    }
}

template<typename T, typename U, typename P>
auto from_raw_parts_override(U* self, P ptr) noexcept -> T {
    if constexpr (requires { self->metadata(); })
        return T::from_raw_parts(ptr, self->metadata());
    else
        return T::from_raw_parts(ptr);
}

/// CRTP base for reference-like pointer types (`ref`, `mut_ref`).
/// \tparam Self The derived type (CRTP).
/// \tparam T The pointee type.
/// \tparam Mutable Whether the reference is mutable.
export template<typename Self, typename T, bool Mutable>
struct ref_base {
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = mtp::cond<Mutable, storage_type, mtp::add_const<storage_type>>;

    constexpr auto operator<=>(const ref_base& other) const noexcept {
        return static_cast<Self const*>(this)->p <=> static_cast<Self const&>(other).p;
    }

    constexpr bool operator==(const ref_base& other) const noexcept {
        return static_cast<Self const*>(this)->p == static_cast<Self const&>(other).p;
    }

    constexpr bool operator==(const element_type& other) const noexcept {
        return static_cast<Self const*>(this)->get() == other;
    }

    constexpr auto as_ref() const noexcept -> ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ref<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> {
        return rstd::from_raw_parts<ptr<T>>(static_cast<Self const*>(this));
    }
    constexpr auto as_mut_ptr() const noexcept -> mut_ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ptr<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_raw_ptr() const noexcept -> value_type* {
        return static_cast<Self const*>(this)->p;
    }

    template<typename U>
    constexpr auto cast() const noexcept -> mut_ptr<U> {
        using Storage = ptr_detail::storage_type_t<U>;
        return mut_ptr<U>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()));
    }

    template<typename U>
    constexpr auto cast_array(usize len = usize()) const noexcept -> mut_ptr<U[]> {
        using Storage = ptr_detail::storage_type_t<U>;
        return mut_ptr<U[]>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()), len);
    }

    /// \name Normal
    /// @{
    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>)
    {
        return { .p = p };
    }
    /// @}

    /// \name Requires: DSTArray
    /// @{
    constexpr decltype(auto) operator[](usize i) const noexcept
        requires mtp::DSTArray<T>
    {
        return static_cast<Self const*>(this)->element_at(i);
    }

    constexpr auto len() const noexcept -> usize
        requires mtp::DSTArray<T>
    {
        return static_cast<Self const*>(this)->length;
    }

    constexpr auto is_empty() const noexcept -> bool
        requires mtp::DSTArray<T>
    {
        return len() == usize();
    }

    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]],
                                         usize       length) noexcept -> Self
        requires mtp::DSTArray<T> && mtp::is_aggregate<Self>
    {
        return { .p = p, .length = length };
    }

    constexpr auto metadata() const noexcept
        requires mtp::DSTArray<T>
    {
        return len();
    }
    /// @}
};

/// CRTP base for raw pointer types (`ptr`, `mut_ptr`).
/// \tparam Self The derived type (CRTP).
/// \tparam T The pointee type.
/// \tparam Mutable Whether the pointer is mutable.
export template<typename Self, typename T, bool Mutable>
struct ptr_base {
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = mtp::cond<Mutable, storage_type, mtp::add_const<storage_type>>;

    constexpr value_type* operator->() const noexcept
        requires(! mtp::same_as<element_type, u8>)
    {
        return static_cast<Self const*>(this)->p;
    }
    constexpr decltype(auto) operator*() const noexcept {
        return static_cast<Self const*>(this)->get();
    }

    constexpr auto operator<=>(const ptr_base& other) const noexcept {
        return static_cast<Self const*>(this)->p <=> static_cast<Self const&>(other).p;
    }

    constexpr bool operator==(const ptr_base& other) const noexcept {
        return static_cast<Self const*>(this)->p == static_cast<Self const&>(other).p;
    }

    constexpr bool operator==(const element_type& other) const noexcept {
        return static_cast<Self const*>(this)->get() == other;
    }
    constexpr bool operator==(std::nullptr_t) const noexcept {
        return static_cast<Self const*>(this)->p == nullptr;
    }

    constexpr auto as_ptr() const noexcept -> ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ptr<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_ref() const noexcept -> ref<T> {
        return rstd::from_raw_parts<ref<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_mut_ref() const noexcept -> mut_ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ref<T>>(static_cast<Self const*>(this));
    }

    constexpr operator value_type*() const noexcept { return static_cast<Self const*>(this)->p; }

    constexpr auto add(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = *static_cast<Self const*>(this);
        if (count != usize()) result.p += count.to_primitive();
        return result;
    }

    constexpr auto sub(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = *static_cast<Self const*>(this);
        if (count != usize()) result.p -= count.to_primitive();
        return result;
    }

    constexpr auto offset(isize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = *static_cast<Self const*>(this);
        if (count != isize()) result.p += count.to_primitive();
        return result;
    }

    constexpr auto operator+(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        return add(count);
    }

    constexpr auto operator-(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        return sub(count);
    }

    constexpr auto operator++() noexcept -> Self&
        requires(! mtp::DST<T>)
    {
        ++static_cast<Self*>(this)->p;
        return *static_cast<Self*>(this);
    }

    constexpr auto operator--() noexcept -> Self&
        requires(! mtp::DST<T>)
    {
        --static_cast<Self*>(this)->p;
        return *static_cast<Self*>(this);
    }

    constexpr auto distance_to(Self other) const noexcept -> ptrdiff_t
        requires(! mtp::DST<T>)
    {
        return other.p - static_cast<Self const*>(this)->p;
    }

    constexpr auto byte_add(usize bytes) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = *static_cast<Self const*>(this);
        if (bytes != usize()) {
            using RawByte = mtp::cond<Mutable, byte, mtp::add_const<byte>>;
            auto raw      = reinterpret_cast<RawByte*>(result.p) + bytes.to_primitive();
            result.p      = reinterpret_cast<value_type*>(raw);
        }
        return result;
    }

    constexpr auto byte_sub(usize bytes) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = *static_cast<Self const*>(this);
        if (bytes != usize()) {
            using RawByte = mtp::cond<Mutable, byte, mtp::add_const<byte>>;
            auto raw      = reinterpret_cast<RawByte*>(result.p) - bytes.to_primitive();
            result.p      = reinterpret_cast<value_type*>(raw);
        }
        return result;
    }

    constexpr void reset() noexcept {
        auto self = static_cast<Self*>(this);
        self->p   = nullptr;
        if constexpr (mtp::DSTArray<T>) {
            self->length = usize();
        }
    }

    constexpr auto as_raw_ptr() const noexcept -> value_type* {
        return static_cast<Self const*>(this)->p;
    }

    template<typename U>
    constexpr auto cast() const noexcept -> mut_ptr<U> {
        using Storage = ptr_detail::storage_type_t<U>;
        return mut_ptr<U>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()));
    }

    template<typename U>
    constexpr auto cast_array(usize len = usize()) const noexcept -> mut_ptr<U[]> {
        using Storage = ptr_detail::storage_type_t<U>;
        return mut_ptr<U[]>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()), len);
    }

    /// \name Normal
    /// @{
    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>) && mtp::is_aggregate<Self>
    {
        return { .p = p };
    }
    /// @}

    /// \name Requires: DSTArray
    /// @{
    constexpr decltype(auto) operator[](usize i) const noexcept
        requires mtp::DSTArray<T>
    {
        return static_cast<Self const*>(this)->element_at(i);
    }

    constexpr auto len() const noexcept
        requires mtp::DSTArray<T>
    {
        return static_cast<Self const*>(this)->length;
    }

    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]],
                                         usize       length) noexcept -> Self
        requires mtp::DSTArray<T> && mtp::is_aggregate<Self>
    {
        return { .p = p, .length = length };
    }

    constexpr auto metadata() const noexcept
        requires mtp::DSTArray<T>
    {
        return len();
    }
    /// @}
};

template<typename T>
struct ref : ref_base<ref<T>, T, false> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(ref)

    using Target       = T;
    using storage_type = ptr_detail::storage_type_t<T>;

    storage_type const* p { nullptr };

    constexpr decltype(auto) get() const noexcept {
        if constexpr (mtp::same_as<T, u8>) {
            return u8::from_byte(*p);
        } else {
            return static_cast<T const&>(*p);
        }
    }

    constexpr auto deref() const noexcept -> ref<T> { return *this; }
};

template<mtp::DSTArray T>
struct ref<T> : ref_base<ref<T>, T, false> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(ref)

    using Target        = T;
    using element_type  = mtp::rm_ext<T>;
    using storage_type  = ptr_detail::storage_type_t<T>;
    using value_type    = element_type;
    using metadata_type = usize;

    storage_type const* p { nullptr };
    metadata_type       length;

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if constexpr (mtp::same_as<element_type, u8>) {
            return u8::from_byte(p[index.to_primitive()]);
        } else {
            return static_cast<element_type const&>(p[index.to_primitive()]);
        }
    }

    constexpr auto operator==(ref const& other) const
        noexcept(noexcept(element_at(usize()) == other.element_at(usize()))) -> bool
        requires requires { element_at(usize()) == other.element_at(usize()); }
    {
        if (length != other.length) return false;
        for (rstd::size_t index = 0; index < length.to_primitive(); ++index) {
            if (! (element_at(usize(index)) == other.element_at(usize(index)))) return false;
        }
        return true;
    }

    constexpr auto deref() const noexcept -> ref<T> { return *this; }

    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<element_type> {
        return ptr<element_type>::from_raw_parts(p);
    }

    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<element_type> {
        return begin().add(length);
    }
};

template<typename T>
struct mut_ref : ref_base<mut_ref<T>, T, true> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(mut_ref)

    using Target       = T;
    using storage_type = ptr_detail::storage_type_t<T>;

    storage_type* p { nullptr };

    constexpr auto get() const noexcept -> T const& { return *p; }
    constexpr auto get_mut() noexcept -> T& { return *p; }

    constexpr auto deref() const noexcept -> ref<T> { return this->as_ref(); }
    constexpr auto deref_mut() noexcept -> mut_ref<T> { return *this; }
};

template<>
struct mut_ref<u8> : ref_base<mut_ref<u8>, u8, true> {
    USE_TRAIT(mut_ref)

    using Target       = u8;
    using storage_type = byte;

    byte* p { nullptr };

    constexpr mut_ref() noexcept = default;
    constexpr explicit mut_ref(byte* value [[clang::lifetimebound]]) noexcept: p(value) {}
    constexpr mut_ref(mut_ref const&) noexcept = default;
    constexpr mut_ref(mut_ref&&) noexcept      = default;

    static constexpr auto from_raw_parts(byte* value [[clang::lifetimebound]]) noexcept -> mut_ref {
        return mut_ref(value);
    }

    constexpr auto get() const noexcept -> u8 { return u8::from_byte(*p); }
    constexpr auto get_mut() noexcept -> mut_ref { return *this; }

    constexpr auto operator=(u8 value) noexcept -> mut_ref& {
        *p = value.to_byte();
        return *this;
    }

    constexpr auto operator=(mut_ref const& other) noexcept -> mut_ref& {
        *p = *other.p;
        return *this;
    }

    constexpr auto operator=(mut_ref&& other) noexcept -> mut_ref& {
        return *this = static_cast<mut_ref const&>(other);
    }

    constexpr operator u8() const noexcept { return u8::from_byte(*p); }

    constexpr auto deref() const noexcept -> ref<u8> { return this->as_ref(); }
    constexpr auto deref_mut() noexcept -> mut_ref { return *this; }
};

template<mtp::DSTArray T>
struct mut_ref<T> : ref_base<mut_ref<T>, T, true> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(mut_ref)

    using Target       = T;
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = element_type;

    storage_type* p { nullptr };
    usize         length;

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if constexpr (mtp::same_as<element_type, u8>) {
            return mut_ref<u8>(p + index.to_primitive());
        } else {
            return static_cast<element_type&>(p[index.to_primitive()]);
        }
    }

    constexpr auto deref() const noexcept -> ref<T> { return this->as_ref(); }
    constexpr auto deref_mut() noexcept -> mut_ref<T> { return *this; }

    constexpr auto begin() noexcept [[clang::lifetimebound]] -> mut_ptr<element_type> {
        return mut_ptr<element_type>::from_raw_parts(p);
    }

    constexpr auto end() noexcept [[clang::lifetimebound]] -> mut_ptr<element_type> {
        return begin().add(length);
    }

    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<element_type> {
        return ptr<element_type>::from_raw_parts(p);
    }

    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<element_type> {
        return begin().add(length);
    }
};

template<typename T>
struct ptr : ptr_base<ptr<T>, T, false> {
    static_assert(! mtp::is_const<T>);
    using Self = ptr;

    using storage_type = ptr_detail::storage_type_t<T>;

    storage_type const* p { nullptr };

    constexpr decltype(auto) get() const noexcept {
        if constexpr (mtp::same_as<T, u8>) {
            return u8::from_byte(*p);
        } else {
            return static_cast<T const&>(*p);
        }
    }
};

template<mtp::DSTArray T>
struct ptr<T> : ptr_base<ptr<T>, T, false> {
    static_assert(! mtp::is_const<T>);
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = element_type;
    using Self         = ptr;

    storage_type const* p { nullptr };
    usize               length;

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if constexpr (mtp::same_as<element_type, u8>) {
            return u8::from_byte(p[index.to_primitive()]);
        } else {
            return static_cast<element_type const&>(p[index.to_primitive()]);
        }
    }
};

template<typename T>
struct mut_ptr : ptr_base<mut_ptr<T>, T, true> {
    static_assert(! mtp::is_const<T>);
    using Self = mut_ptr;

    using storage_type = ptr_detail::storage_type_t<T>;

    storage_type* p { nullptr };

    constexpr decltype(auto) get() const noexcept {
        if constexpr (mtp::same_as<T, u8>) {
            return mut_ref<u8>(p);
        } else {
            return static_cast<T&>(*p);
        }
    }
};

template<typename T>
    requires mtp::DSTArray<T>
struct mut_ptr<T> : ptr_base<mut_ptr<T>, T, true> {
    static_assert(! mtp::is_const<T>);
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = element_type;
    using Self         = mut_ptr;

    storage_type* p { nullptr };
    usize         length;

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if constexpr (mtp::same_as<element_type, u8>) {
            return mut_ref<u8>(p + index.to_primitive());
        } else {
            return static_cast<element_type&>(p[index.to_primitive()]);
        }
    }
};

/// A borrowed reference to a contiguous sequence of `T`, analogous to Rust's `&[T]`.
/// \tparam T The element type.
export template<typename T>
using slice = ref<T[]>;

} // namespace rstd

namespace rstd::ptr_
{

/// Destroys the pointee without deallocating its storage.
export template<typename T>
void drop_in_place(mut_ptr<T> pointer) noexcept {
    if constexpr (mtp::DSTArray<T>) {
        using Element = mtp::rm_ext<T>;
        if constexpr (! mtp::same_as<Element, u8>) {
            auto* data = pointer.as_raw_ptr();
            for (rstd::size_t i = 0; i < pointer.len().to_primitive(); ++i) {
                rstd::destroy_at(data + i);
            }
        }
    } else if constexpr (mtp::DST<T>) {
        pointer.metadata()->drop(pointer.as_raw_ptr());
    } else if constexpr (! mtp::same_as<T, u8>) {
        rstd::destroy_at(pointer.as_raw_ptr());
    }
}

} // namespace rstd::ptr_
