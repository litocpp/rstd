module;
#include <rstd/macro.hpp>
export module rstd.core:ptr.ptr;
import :num.types;
import :mem.swap;
export import :ptr.metadata;
export import :ops.deref;
export import :cmp;
export import :marker;

namespace rstd::mem::maybe_uninit
{
export template<typename T>
class MaybeUninit;
}

using rstd::mem::maybe_uninit::MaybeUninit;

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

template<typename T>
inline constexpr bool byte_addressed = false;
template<typename T>
inline constexpr bool byte_addressed<MaybeUninit<T>> = true;

template<typename T>
using address_type_t = mtp::cond<byte_addressed<mtp::rm_ext<T>>, byte, storage_type_t<T>>;

template<typename T, typename P>
constexpr auto address(P* pointer) noexcept {
    using Address = mtp::cond<mtp::is_const<P>, const address_type_t<T>, address_type_t<T>>;
    if constexpr (byte_addressed<mtp::rm_ext<T>>) {
        [[unsafe]] { return reinterpret_cast<Address*>(pointer); }
    } else {
        return pointer;
    }
}

template<typename T, typename P>
constexpr auto element(P* pointer) noexcept {
    using Storage = mtp::cond<mtp::is_const<P>, const storage_type_t<T>, storage_type_t<T>>;
    if constexpr (byte_addressed<mtp::rm_ext<T>>) {
        [[unsafe]] { return reinterpret_cast<Storage*>(pointer); }
    } else {
        return pointer;
    }
}

template<typename T>
constexpr auto stride() noexcept -> rstd::size_t {
    if constexpr (byte_addressed<mtp::rm_ext<T>>)
        return sizeof(storage_type_t<T>);
    else
        return 1;
}

[[noreturn]]
void panic_empty_ref(source_location loc = source_location::current()) noexcept;

[[noreturn]]
void panic_slice_index(source_location loc = source_location::current()) noexcept;

template<typename View>
class SliceIterator {
    View  slice_;
    usize index_;

    friend View;

    constexpr SliceIterator(View slice, usize index) noexcept: slice_(slice), index_(index) {}

public:
    constexpr decltype(auto) operator*() const noexcept { return slice_[index_]; }

    constexpr auto operator++() noexcept -> SliceIterator& {
        if (index_ < slice_.len()) ++index_;
        return *this;
    }

    constexpr auto operator==(SliceIterator const& other) const noexcept -> bool {
        return index_ == other.index_ && slice_.len() == other.slice_.len() &&
               slice_.as_raw_ptr() == other.slice_.as_raw_ptr();
    }
};

} // namespace ptr_detail

template<typename T, typename U>
constexpr auto from_raw_parts(U const& self) noexcept -> T {
    [[unsafe]] {
        if constexpr (requires { T::from_raw_parts(self.as_raw_ptr(), self.metadata()); }) {
            return T::from_raw_parts(self.as_raw_ptr(), self.metadata());
        } else {
            return T::from_raw_parts(self.as_raw_ptr());
        }
    }
}

template<typename T, typename U, typename P>
constexpr auto from_raw_parts_override(U const& self, P ptr) noexcept -> T {
    if constexpr (requires { self.metadata(); })
        return T::from_raw_parts(ptr, self.metadata());
    else
        return T::from_raw_parts(ptr);
}

/// CRTP base for reference-like pointer types (`ref`, `mut_ref`).
/// \tparam Self The derived type (CRTP).
/// \tparam T The pointee type.
/// \tparam Mutable Whether the reference is mutable.
export template<typename Self, typename T, bool Mutable>
struct ref_base {
private:
    friend Self;

    constexpr ref_base() noexcept                                   = default;
    constexpr ref_base(ref_base const&) noexcept                    = default;
    constexpr auto operator=(ref_base const&) noexcept -> ref_base& = default;

    constexpr auto self() const noexcept -> Self const& {
        [[unsafe]] { return static_cast<Self const&>(*this); }
    }

public:
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = mtp::cond<Mutable, storage_type, mtp::add_const<storage_type>>;

    constexpr auto operator<=>(const ref_base& other) const noexcept {
        return self().p <=> other.self().p;
    }

    constexpr bool operator==(const ref_base& other) const noexcept {
        return self().p == other.self().p;
    }

    constexpr bool operator==(const element_type& other) const noexcept {
        return self().get() == other;
    }

    constexpr auto as_ref() const noexcept -> ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ref<T>>(self());
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> {
        return rstd::from_raw_parts<ptr<T>>(self());
    }
    constexpr auto as_mut_ptr() const noexcept -> mut_ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ptr<T>>(self());
    }

    constexpr auto as_raw_ptr() const noexcept -> value_type* {
        return ptr_detail::element<T>(self().p);
    }

    constexpr auto as_byte_ptr() const noexcept -> mtp::cond<Mutable, byte, const byte>* {
        [[unsafe]] { return reinterpret_cast<mtp::cond<Mutable, byte, const byte>*>(self().p); }
    }

    /// The storage covers count live aligned slots; no MaybeUninit array lifetime is required.
    [[unsafe]]
    static constexpr auto from_bytes(mtp::cond<Mutable, byte, const byte>* storage
                                     [[clang::lifetimebound]],
                                     usize count) noexcept -> Self
        requires mtp::DSTArray<T> && ptr_detail::byte_addressed<element_type>
    {
        Self result {};
        result.p      = storage;
        result.length = count;
        return result;
    }

    /// The storage covers one live aligned slot.
    [[unsafe]]
    static constexpr auto from_bytes(mtp::cond<Mutable, byte, const byte>* storage
                                     [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>) && ptr_detail::byte_addressed<element_type>
    {
        Self result {};
        result.p = storage;
        return result;
    }

    template<typename U>
    constexpr auto cast() const noexcept -> mut_ptr<U> {
        using Storage = ptr_detail::storage_type_t<U>;
        [[unsafe]] { return mut_ptr<U>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr())); }
    }

    template<typename U>
    constexpr auto cast_array(usize len = usize()) const noexcept -> mut_ptr<U[]> {
        using Storage = ptr_detail::storage_type_t<U>;
        [[unsafe]] {
            return mut_ptr<U[]>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()), len);
        }
    }

    /// \name Normal
    /// @{
    /// The pointee is live, initialized and suitably aligned for this borrow.
    [[unsafe]]
    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>)
    {
        Self result {};
        result.p = ptr_detail::address<T>(p);
        return result;
    }

    static constexpr auto from_ref(value_type& value [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>)
    {
        [[unsafe]] { return Self::from_raw_parts(rstd::addressof(value)); }
    }

    static auto from_ref(value_type&&) -> Self = delete;
    /// @}

    /// \name Requires: DSTArray
    /// @{
    constexpr decltype(auto) operator[](usize i) const noexcept
        requires mtp::DSTArray<T>
    {
        return self().element_at(i);
    }

    constexpr auto len() const noexcept -> usize
        requires mtp::DSTArray<T>
    {
        return self().length;
    }

    constexpr auto is_empty() const noexcept -> bool
        requires mtp::DSTArray<T>
    {
        return len() == usize();
    }

    constexpr auto subslice(usize start, usize count) const noexcept -> Self
        requires mtp::DSTArray<T>
    {
        if (! contains_range(start, count)) ptr_detail::panic_slice_index();
        if (count == usize()) return {};
        [[unsafe]] {
            if constexpr (Mutable)
                return get_unchecked_mut(start, count);
            else
                return get_unchecked(start, count);
        }
    }

    constexpr auto get(usize index) const noexcept -> Option<ref<element_type>>
        requires mtp::DSTArray<T>;
    /// Borrows count elements starting at start, or returns None if out of bounds.
    constexpr auto get(usize start, usize count) const noexcept -> Option<ref<T>>
        requires mtp::DSTArray<T>;
    constexpr auto get_mut(usize index) const noexcept -> Option<mut_ref<element_type>>
        requires Mutable && mtp::DSTArray<T>;
    constexpr auto get_mut(usize start, usize count) const noexcept -> Option<mut_ref<T>>
        requires Mutable && mtp::DSTArray<T>;

    /// The index must be less than len().
    [[unsafe]]
    constexpr auto get_unchecked(usize index) const noexcept -> ref<element_type>
        requires mtp::DSTArray<T>
    {
        [[unsafe]] { return offset_ptr(index).as_ref(); }
    }

    /// The index must be less than len().
    [[unsafe]]
    constexpr auto get_unchecked_mut(usize index) const noexcept -> mut_ref<element_type>
        requires Mutable && mtp::DSTArray<T>
    {
        [[unsafe]] { return offset_ptr(index).as_mut_ref(); }
    }

    /// Requires start <= len() and count <= len() - start.
    [[unsafe]]
    constexpr auto get_unchecked(usize start, usize count) const noexcept -> ref<T>
        requires mtp::DSTArray<T>
    {
        [[unsafe]] { return ref<T>::from_raw_parts(offset_ptr(start).as_raw_ptr(), count); }
    }

    /// Requires start <= len() and count <= len() - start.
    [[unsafe]]
    constexpr auto get_unchecked_mut(usize start, usize count) const noexcept -> mut_ref<T>
        requires Mutable && mtp::DSTArray<T>
    {
        [[unsafe]] { return mut_ref<T>::from_raw_parts(offset_ptr(start).as_raw_ptr(), count); }
    }

    // Endpoints borrow the backing storage, not the view object; empty views return None.
    constexpr auto first() const noexcept -> Option<ref<element_type>>
        requires mtp::DSTArray<T>;
    constexpr auto last() const noexcept -> Option<ref<element_type>>
        requires mtp::DSTArray<T>;
    constexpr auto first_mut() const noexcept -> Option<mut_ref<element_type>>
        requires Mutable && mtp::DSTArray<T>;
    constexpr auto last_mut() const noexcept -> Option<mut_ref<element_type>>
        requires Mutable && mtp::DSTArray<T>;

    /// The address and length describe a live initialized range for this borrow.
    [[unsafe]]
    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]],
                                         usize       length) noexcept -> Self
        requires mtp::DSTArray<T> && mtp::same_as<Self, mtp::cond<Mutable, mut_ref<T>, ref<T>>>
    {
        Self result {};
        result.p      = ptr_detail::address<T>(p);
        result.length = length;
        return result;
    }

    constexpr auto metadata() const noexcept
        requires mtp::DSTArray<T>
    {
        return len();
    }
    /// @}

private:
    constexpr bool contains_range(usize start, usize count) const noexcept
        requires mtp::DSTArray<T>
    {
        return start <= len() && count <= len() - start;
    }

    [[unsafe]]
    constexpr auto offset_ptr(usize index) const noexcept
        requires mtp::DSTArray<T>
    {
        using Pointer = mtp::cond<Mutable, mut_ptr<element_type>, ptr<element_type>>;
        [[unsafe]] { return Pointer::from_raw_parts(as_raw_ptr()).add(index); }
    }
};

/// CRTP base for raw pointer types (`ptr`, `mut_ptr`).
/// \tparam Self The derived type (CRTP).
/// \tparam T The pointee type.
/// \tparam Mutable Whether the pointer is mutable.
export template<typename Self, typename T, bool Mutable>
struct ptr_base {
private:
    constexpr auto self() const noexcept -> Self const& {
        [[unsafe]] { return static_cast<Self const&>(*this); }
    }

    constexpr auto self() noexcept -> Self& {
        [[unsafe]] { return static_cast<Self&>(*this); }
    }

public:
    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = mtp::cond<Mutable, storage_type, mtp::add_const<storage_type>>;

    /// Accesses a member of a live, initialized and suitably aligned pointee.
    [[unsafe]]
    constexpr value_type* operator->() const noexcept
        requires(! mtp::same_as<element_type, u8>)
    {
        return ptr_detail::element<T>(self().p);
    }
    /// Dereferences a live, initialized and suitably aligned pointee.
    [[unsafe]]
    constexpr decltype(auto) operator*() const noexcept {
        [[unsafe]] { return self().get(); }
    }

    constexpr auto operator<=>(const ptr_base& other) const noexcept {
        return self().p <=> other.self().p;
    }

    constexpr bool operator==(const ptr_base& other) const noexcept {
        return self().p == other.self().p;
    }

    /// Compares the value of a live, initialized and suitably aligned pointee.
    [[unsafe]]
    constexpr bool operator==(const element_type& other) const noexcept {
        [[unsafe]] { return self().get() == other; }
    }
    constexpr bool operator==(std::nullptr_t) const noexcept { return self().p == nullptr; }

    constexpr auto as_ptr() const noexcept -> ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ptr<T>>(self());
    }

    /// Borrows a live, initialized and suitably aligned pointee or range.
    [[unsafe]]
    constexpr auto as_ref() const noexcept -> ref<T> {
        return rstd::from_raw_parts<ref<T>>(self());
    }

    /// Mutably borrows a live, initialized and suitably aligned pointee with exclusive access.
    [[unsafe]]
    constexpr auto as_mut_ref() const noexcept -> mut_ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ref<T>>(self());
    }

    constexpr operator value_type*() const noexcept { return ptr_detail::element<T>(self().p); }

    /// Offsets within the same array or to its one-past-the-end position.
    [[unsafe]]
    constexpr auto add(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = self();
        if (count != usize()) {
            [[unsafe]] { result.p += count.to_primitive() * ptr_detail::stride<T>(); }
        }
        return result;
    }

    /// Offsets within the same array or to its one-past-the-end position.
    [[unsafe]]
    constexpr auto sub(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = self();
        if (count != usize()) {
            [[unsafe]] { result.p -= count.to_primitive() * ptr_detail::stride<T>(); }
        }
        return result;
    }

    /// Offsets within the same array or to its one-past-the-end position.
    [[unsafe]]
    constexpr auto offset(isize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = self();
        if (count != isize()) {
            [[unsafe]] {
                result.p += count.to_primitive() * static_cast<ptrdiff_t>(ptr_detail::stride<T>());
            }
        }
        return result;
    }

    /// Uses the same range precondition as add().
    [[unsafe]]
    constexpr auto operator+(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        [[unsafe]] { return add(count); }
    }

    /// Uses the same range precondition as sub().
    [[unsafe]]
    constexpr auto operator-(usize count) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        [[unsafe]] { return sub(count); }
    }

    /// Uses the same range precondition as add().
    [[unsafe]]
    constexpr auto operator++() noexcept -> Self&
        requires(! mtp::DST<T>)
    {
        [[unsafe]] { self() = add(usize(1)); }
        return self();
    }

    /// Uses the same range precondition as sub().
    [[unsafe]]
    constexpr auto operator--() noexcept -> Self&
        requires(! mtp::DST<T>)
    {
        [[unsafe]] { self() = sub(usize(1)); }
        return self();
    }

    /// Unequal pointers must address the same array, and their difference must fit ptrdiff_t.
    [[unsafe]]
    constexpr auto distance_to(Self other) const noexcept -> ptrdiff_t
        requires(! mtp::DST<T>)
    {
        if (other.p == self().p) return 0;
        [[unsafe]] {
            return (other.p - self().p) / static_cast<ptrdiff_t>(ptr_detail::stride<T>());
        }
    }

    /// Offsets within the same byte storage or to its one-past-the-end position.
    [[unsafe]]
    constexpr auto byte_add(usize bytes) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = self();
        if (bytes != usize()) {
            using RawByte = mtp::cond<Mutable, byte, mtp::add_const<byte>>;
            [[unsafe]] {
                auto raw = reinterpret_cast<RawByte*>(result.p) + bytes.to_primitive();
                result.p = ptr_detail::address<T>(reinterpret_cast<value_type*>(raw));
            }
        }
        return result;
    }

    /// Offsets within the same byte storage or to its one-past-the-end position.
    [[unsafe]]
    constexpr auto byte_sub(usize bytes) const noexcept -> Self
        requires(! mtp::DST<T>)
    {
        auto result = self();
        if (bytes != usize()) {
            using RawByte = mtp::cond<Mutable, byte, mtp::add_const<byte>>;
            [[unsafe]] {
                auto raw = reinterpret_cast<RawByte*>(result.p) - bytes.to_primitive();
                result.p = ptr_detail::address<T>(reinterpret_cast<value_type*>(raw));
            }
        }
        return result;
    }

    constexpr void reset() noexcept {
        self().p = nullptr;
        if constexpr (mtp::DSTArray<T>) {
            self().length = usize();
        }
    }

    constexpr auto as_raw_ptr() const noexcept -> value_type* {
        return ptr_detail::element<T>(self().p);
    }

    constexpr auto as_byte_ptr() const noexcept -> mtp::cond<Mutable, byte, const byte>* {
        [[unsafe]] { return reinterpret_cast<mtp::cond<Mutable, byte, const byte>*>(self().p); }
    }

    /// The storage covers count aligned slots; no MaybeUninit array lifetime is required.
    static constexpr auto from_bytes(mtp::cond<Mutable, byte, const byte>* storage
                                     [[clang::lifetimebound]],
                                     usize count) noexcept -> Self
        requires mtp::DSTArray<T> && ptr_detail::byte_addressed<element_type>
    {
        return { .p = storage, .length = count };
    }

    static constexpr auto from_bytes(mtp::cond<Mutable, byte, const byte>* storage
                                     [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>) && ptr_detail::byte_addressed<element_type>
    {
        return { .p = storage };
    }

    template<typename U>
    constexpr auto cast() const noexcept -> mut_ptr<U> {
        using Storage = ptr_detail::storage_type_t<U>;
        [[unsafe]] { return mut_ptr<U>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr())); }
    }

    template<typename U>
    constexpr auto cast_array(usize len = usize()) const noexcept -> mut_ptr<U[]> {
        using Storage = ptr_detail::storage_type_t<U>;
        [[unsafe]] {
            return mut_ptr<U[]>::from_raw_parts(reinterpret_cast<Storage*>(as_raw_ptr()), len);
        }
    }

    /// \name Normal
    /// @{
    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]]) noexcept -> Self
        requires(! mtp::DST<T>) && mtp::is_aggregate<Self>
    {
        return { .p = ptr_detail::address<T>(p) };
    }
    /// @}

    /// \name Requires: DSTArray
    /// @{
    /// The pointer must address a live range containing the indexed element.
    [[unsafe]]
    constexpr decltype(auto) operator[](usize i) const noexcept
        requires mtp::DSTArray<T>
    {
        [[unsafe]] { return self().element_at(i); }
    }

    constexpr auto len() const noexcept
        requires mtp::DSTArray<T>
    {
        return self().length;
    }

    static constexpr auto from_raw_parts(value_type* p [[clang::lifetimebound]],
                                         usize       length) noexcept -> Self
        requires mtp::DSTArray<T> && mtp::is_aggregate<Self>
    {
        return { .p = ptr_detail::address<T>(p), .length = length };
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

    using storage_type = ptr_detail::storage_type_t<T>;

private:
    friend ref_base<ref<T>, T, false>;
    ptr_detail::address_type_t<T> const* p { nullptr };

public:
    constexpr decltype(auto) get() const noexcept {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] {
            if constexpr (mtp::same_as<T, u8>) {
                return u8::from_byte(*p);
            } else {
                return static_cast<T const&>(*ptr_detail::element<T>(p));
            }
        }
    }

    constexpr auto deref() const noexcept -> ref<T> {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        return *this;
    }
};

template<mtp::DSTArray T>
struct ref<T> : ref_base<ref<T>, T, false> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(ref)

    using element_type  = mtp::rm_ext<T>;
    using storage_type  = ptr_detail::storage_type_t<T>;
    using value_type    = element_type;
    using metadata_type = usize;

private:
    friend ref_base<ref<T>, T, false>;
    ptr_detail::address_type_t<T> const* p { nullptr };
    metadata_type                        length {};

public:
    template<rstd::size_t N, typename U>
    static constexpr auto from_array(U (&values [[clang::lifetimebound]])[N]) noexcept -> ref
        requires mtp::same_as<mtp::rm_const<U>, storage_type> &&
                 (! ptr_detail::byte_addressed<element_type>)
    {
        [[unsafe]] { return ref::from_raw_parts(values, usize(N)); }
    }

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if (index >= length) ptr_detail::panic_slice_index();
        [[unsafe]] {
            if constexpr (mtp::same_as<element_type, u8>) {
                return u8::from_byte(p[index.to_primitive()]);
            } else {
                return static_cast<element_type const&>(
                    *ptr_detail::element<T>(p + index.to_primitive() * ptr_detail::stride<T>()));
            }
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

    constexpr auto begin() const noexcept { return ptr_detail::SliceIterator<ref>(*this, usize()); }

    constexpr auto end() const noexcept { return ptr_detail::SliceIterator<ref>(*this, length); }
};

template<typename T>
struct mut_ref : ref_base<mut_ref<T>, T, true> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(mut_ref)

    using storage_type = ptr_detail::storage_type_t<T>;

private:
    friend ref_base<mut_ref<T>, T, true>;
    ptr_detail::address_type_t<T>* p { nullptr };

public:
    constexpr auto get() const noexcept -> T const& {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] { return *ptr_detail::element<T>(p); }
    }
    constexpr auto get_mut() noexcept -> T& {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] { return *ptr_detail::element<T>(p); }
    }

    constexpr auto deref() const noexcept -> ref<T> {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        return this->as_ref();
    }
    constexpr auto deref_mut() noexcept -> mut_ref<T> {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        return *this;
    }
};

template<>
struct mut_ref<u8> : ref_base<mut_ref<u8>, u8, true> {
    USE_TRAIT(mut_ref)

    using storage_type = byte;

private:
    friend ref_base<mut_ref<u8>, u8, true>;
    byte* p { nullptr };

public:
    constexpr mut_ref() noexcept = default;
    /// The storage must contain a live byte for the duration of this borrow.
    [[unsafe]]
    constexpr explicit mut_ref(byte* value [[clang::lifetimebound]]) noexcept
        : p(value) {}
    constexpr mut_ref(mut_ref const&) noexcept = default;
    constexpr mut_ref(mut_ref&&) noexcept      = default;

    /// The storage must contain a live byte for the duration of this borrow.
    [[unsafe]]
    static constexpr auto from_raw_parts(byte* value [[clang::lifetimebound]]) noexcept -> mut_ref {
        [[unsafe]] { return mut_ref(value); }
    }

    constexpr auto get() const noexcept -> u8 {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] { return u8::from_byte(*p); }
    }
    constexpr auto get_mut() noexcept -> mut_ref {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        return *this;
    }

    constexpr auto operator=(u8 value) noexcept -> mut_ref& {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] { *p = value.to_byte(); }
        return *this;
    }

    constexpr auto operator=(mut_ref const& other) noexcept -> mut_ref& {
        if (p == nullptr || other.p == nullptr) ptr_detail::panic_empty_ref();
        [[unsafe]] { *p = *other.p; }
        return *this;
    }

    constexpr auto operator=(mut_ref&& other) noexcept -> mut_ref& {
        return *this = static_cast<mut_ref const&>(other);
    }

    constexpr operator u8() const noexcept { return get(); }

    constexpr auto deref() const noexcept -> ref<u8> {
        if (p == nullptr) ptr_detail::panic_empty_ref();
        return this->as_ref();
    }
    constexpr auto deref_mut() noexcept -> mut_ref { return get_mut(); }
};

template<mtp::DSTArray T>
struct mut_ref<T> : ref_base<mut_ref<T>, T, true> {
    static_assert(! mtp::is_const<T>);

    USE_TRAIT(mut_ref)

    using element_type = mtp::rm_ext<T>;
    using storage_type = ptr_detail::storage_type_t<T>;
    using value_type   = element_type;

private:
    friend ref_base<mut_ref<T>, T, true>;
    ptr_detail::address_type_t<T>* p { nullptr };
    usize                          length {};

public:
    template<rstd::size_t N, typename U>
    static constexpr auto from_array(U (&values [[clang::lifetimebound]])[N]) noexcept -> mut_ref
        requires mtp::same_as<U, storage_type> && (! ptr_detail::byte_addressed<element_type>)
    {
        [[unsafe]] { return mut_ref::from_raw_parts(values, usize(N)); }
    }

    constexpr decltype(auto) element_at(usize index) const noexcept {
        if (index >= length) ptr_detail::panic_slice_index();
        [[unsafe]] {
            if constexpr (mtp::same_as<element_type, u8>) {
                return mut_ref<u8>(p + index.to_primitive());
            } else {
                return static_cast<element_type&>(
                    *ptr_detail::element<T>(p + index.to_primitive() * ptr_detail::stride<T>()));
            }
        }
    }

    constexpr auto deref() const noexcept -> ref<T> { return this->as_ref(); }
    constexpr auto deref_mut() noexcept -> mut_ref<T> { return *this; }

    constexpr auto begin() noexcept { return ptr_detail::SliceIterator<mut_ref>(*this, usize()); }

    constexpr auto end() noexcept { return ptr_detail::SliceIterator<mut_ref>(*this, length); }

    constexpr auto begin() const noexcept { return this->as_ref().begin(); }
    constexpr auto end() const noexcept { return this->as_ref().end(); }
};

template<typename T>
struct Impl<ops::Deref, ref<T>> : ImplBase<ref<T>> {
    using Target = T;

    constexpr auto deref() const noexcept -> ref<Target> { return this->self().deref(); }
};

template<typename T>
struct Impl<ops::Deref, mut_ref<T>> : ImplBase<mut_ref<T>> {
    using Target = T;

    constexpr auto deref() const noexcept -> ref<Target> { return this->self().deref(); }
};

template<typename T>
struct Impl<ops::DerefMut, mut_ref<T>> : ImplBase<mut_ref<T>> {
    constexpr auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<mut_ref<T>>> {
        return this->self().deref_mut();
    }
};

template<typename T>
struct ptr : ptr_base<ptr<T>, T, false> {
    static_assert(! mtp::is_const<T>);
    using Self = ptr;

    using storage_type = ptr_detail::storage_type_t<T>;

    ptr_detail::address_type_t<T> const* p { nullptr };

    /// Dereferences a live, initialized and suitably aligned pointee.
    [[unsafe]]
    constexpr decltype(auto) get() const noexcept {
        [[unsafe]] {
            if constexpr (mtp::same_as<T, u8>) {
                return u8::from_byte(*p);
            } else {
                return static_cast<T const&>(*ptr_detail::element<T>(p));
            }
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

    ptr_detail::address_type_t<T> const* p { nullptr };
    usize                                length;

    /// The pointer must address a live range containing the indexed element.
    [[unsafe]]
    constexpr decltype(auto) element_at(usize index) const noexcept {
        [[unsafe]] {
            if constexpr (mtp::same_as<element_type, u8>) {
                return u8::from_byte(p[index.to_primitive()]);
            } else {
                return static_cast<element_type const&>(
                    *ptr_detail::element<T>(p + index.to_primitive() * ptr_detail::stride<T>()));
            }
        }
    }
};

template<typename T>
struct mut_ptr : ptr_base<mut_ptr<T>, T, true> {
    static_assert(! mtp::is_const<T>);
    using Self = mut_ptr;

    using storage_type = ptr_detail::storage_type_t<T>;

    ptr_detail::address_type_t<T>* p { nullptr };

    /// Mutably borrows a live, initialized and suitably aligned pointee with exclusive access.
    [[unsafe]]
    constexpr decltype(auto) get() const noexcept {
        [[unsafe]] {
            if constexpr (mtp::same_as<T, u8>) {
                return mut_ref<u8>(p);
            } else {
                return static_cast<T&>(*ptr_detail::element<T>(p));
            }
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

    ptr_detail::address_type_t<T>* p { nullptr };
    usize                          length;

    /// The pointer must address a live range containing the element with exclusive access.
    [[unsafe]]
    constexpr decltype(auto) element_at(usize index) const noexcept {
        [[unsafe]] {
            if constexpr (mtp::same_as<element_type, u8>) {
                return mut_ref<u8>(p + index.to_primitive());
            } else {
                return static_cast<element_type&>(
                    *ptr_detail::element<T>(p + index.to_primitive() * ptr_detail::stride<T>()));
            }
        }
    }
};

/// A borrowed reference to a contiguous sequence of `T`, analogous to Rust's `&[T]`.
/// \tparam T The element type.
export template<typename T>
using slice = ref<T[]>;

template<typename T>
struct Impl<Copy, ptr<T>> {};

template<typename T>
struct Impl<Copy, mut_ptr<T>> {};

template<typename T>
struct Impl<Copy, ref<T>> {};

} // namespace rstd

namespace rstd::ptr_
{

/// Swaps initialized elements in non-overlapping storage, or does nothing for equal pointers.
/// Moves and destructors must not throw.
export template<typename T>
    requires(! mtp::DST<T>) && mtp::init<T, T&&>
[[unsafe]]
constexpr void swap(mut_ptr<T> a, mut_ptr<T> b) noexcept {
    if (a.as_raw_ptr() == b.as_raw_ptr()) return;
    [[unsafe]] { rstd::mem::swap(*a.as_raw_ptr(), *b.as_raw_ptr()); }
}

/// Destroys a live pointee without deallocating its storage. It must be dropped only once.
export template<typename T>
[[unsafe]]
void drop_in_place(mut_ptr<T> pointer) noexcept {
    if constexpr (mtp::DSTArray<T>) {
        using Element = mtp::rm_ext<T>;
        if constexpr (! mtp::triv_drop<Element>) {
            auto* data = pointer.as_raw_ptr();
            for (rstd::size_t i = 0; i < pointer.len().to_primitive(); ++i) {
                [[unsafe]] { rstd::destroy_at(data + i); }
            }
        }
    } else if constexpr (mtp::DST<T>) {
        [[unsafe]] { pointer.metadata()->drop(pointer.as_raw_ptr()); }
    } else if constexpr (! mtp::triv_drop<T>) {
        [[unsafe]] { rstd::destroy_at(pointer.as_raw_ptr()); }
    }
}

} // namespace rstd::ptr_
