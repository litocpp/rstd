module;
#include <rstd/macro.hpp>

export module rstd.alloc:vec;
export import :boxed;
export import :alloc;
export import rstd.core;

using ::alloc::boxed::Box;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;

using namespace rstd::prelude;

/// A low-level utility for managing the backing storage of a `Vec`.
/// It handles allocation and deallocation of raw memory via global allocator.
template<typename T>
struct RawVec {
    NonNull<T> ptr;
    usize      cap;

    static auto with_capacity(usize capacity) -> RawVec {
        if (capacity == 0) return RawVec();
        auto layout = Layout::array<T>(capacity).unwrap();
        auto res    = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().as_mut_ptr().template cast<T>();
        return { .ptr = NonNull<T>::make_unchecked(p), .cap = capacity };
    }

    /// Reallocates the storage to a new capacity.
    void grow(usize new_cap) {
        if (new_cap <= cap) return;

        auto new_layout = Layout::array<T>(new_cap).unwrap();

        if (cap == 0) {
            auto res = as<Allocator>(::alloc::GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr =
                NonNull<T>::make_unchecked(res.unwrap_unchecked().as_mut_ptr().template cast<T>());
        } else {
            auto old_layout = Layout::array<T>(cap).unwrap();
            auto old_ptr    = ptr.as_mut_ptr();

            auto res = as<Allocator>(::alloc::GLOBAL).grow(
                NonNull<u8>::make_unchecked(old_ptr.template cast<u8>()), old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            ptr =
                NonNull<T>::make_unchecked(res.unwrap_unchecked().as_mut_ptr().template cast<T>());
        }
        cap = new_cap;
    }

    ~RawVec() {}

    void reset_ptr() {
        rstd::mem::fill(ptr, 0);
        cap = 0;
    }

    void drop() {
        if (! rstd::mem::all(ptr, 0)) {
            debug_assert(cap > 0);
            auto layout = Layout::array<T>(cap).unwrap();
            as<Allocator>(::alloc::GLOBAL).deallocate(
                NonNull<u8>::make_unchecked(ptr.as_mut_ptr().template cast<u8>()), layout);
        }
        reset_ptr();
    }
};

namespace alloc::vec
{

/// A contiguous growable array type, analogous to Rust's `Vec<T>`.
/// \tparam T The element type, which must be `Sized`.
export template<typename T>
class Vec {
    RawVec<T> m_buf;
    usize     m_len;

    static_assert(Impled<T, Sized>);

    constexpr explicit Vec(RawVec<T> buf, usize len): m_buf(buf), m_len(len) {}

public:
    USE_TRAIT(Vec)

    /// Creates an empty `Vec` with no allocation.
    constexpr Vec(): m_buf(), m_len(0) {}

    // no copy
    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    // move
    constexpr Vec(Self&& o) noexcept: m_buf(o.m_buf), m_len(o.m_len) {
        o.m_buf.reset_ptr();
        o.m_len = 0;
    }
    constexpr Vec& operator=(Self&& o) noexcept {
        if (this != &o) {
            // clean
            clear();
            m_buf.drop();

            // assign
            m_buf = o.m_buf;
            m_len = o.m_len;

            // move
            o.m_buf.reset_ptr();
            o.m_len = 0;
        }
        return *this;
    }

    ~Vec() {
        clear();
        m_buf.drop();
    }

    /// Creates a new empty `Vec`.
    /// \return An empty `Vec`.
    static constexpr auto make() -> Self { return {}; }
    /// Creates a new empty `Vec` with at least the specified capacity.
    /// \param capacity The minimum number of elements the `Vec` can hold without reallocating.
    /// \return A `Vec` with preallocated capacity.
    static auto           with_capacity(usize capacity) -> Self {
        return Vec { RawVec<T>::with_capacity(capacity), 0 };
    }

    /// Returns a slice containing the entire vector.
    /// \return An immutable `slice<T>` over all elements.
    constexpr auto as_slice() const noexcept -> slice<T> {
        if (m_len == 0) return {};
        return slice<T>::from_raw_parts(m_buf.ptr.as_ptr().as_raw_ptr(), m_len);
    }

    /// Returns a mutable slice containing the entire vector.
    /// \return A mutable pointer to a slice over all elements.
    constexpr auto as_mut_slice() noexcept -> mut_ptr<T[]> {
        if (m_len == 0) return {};
        return mut_ptr<T[]>::from_raw_parts(m_buf.ptr.as_mut_ptr().as_raw_ptr(), m_len);
    }

    /// Returns a const pointer to the first element of the vector.
    /// \return A const pointer to the underlying buffer.
    constexpr auto as_ptr() const noexcept -> ptr<T> { return m_buf.ptr.as_ptr(); }

    /// Converts this `Vec` into a `Box<T[]>`, transferring ownership of all elements.
    /// \return A boxed slice containing the vector's elements.
    auto into_boxed_slice() noexcept -> Box<T[]> {
        auto length = m_len;
        auto layout = Layout::array<T>(length).unwrap();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto* raw     = reinterpret_cast<T*>(res.unwrap_unchecked().as_mut_ptr().as_raw_ptr());
        auto* old_ptr = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = 0; i < length; ++i) {
            new (raw + i) T(rstd::move(old_ptr[i]));
            old_ptr[i].~T();
        }
        auto b = Box<T[]>::from_raw(mut_ptr<T[]>::from_raw_parts(raw, length));
        m_len  = 0;
        return b;
    }

    /// Appends an element to the back of the vector by moving it.
    /// \param value The value to append.
    constexpr void push(T&& value) {
        if (m_len == m_buf.cap) {
            m_buf.grow(m_buf.cap == 0 ? 4 : m_buf.cap * 2);
        }
        new (m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len) T(rstd::move(value));
        m_len++;
    }

    /// Removes the last element from the vector and returns it, or `None` if empty.
    /// \return An `Option<T>` containing the removed element.
    constexpr auto pop() -> Option<T> {
        if (m_len == 0) {
            return None();
        } else {
            m_len--;
            T* p     = m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len;
            T  value = rstd::move(*p);
            p->~T();
            return Some(value);
        }
    }

    /// Appends a cloned copy of the element to the back of the vector.
    /// \param value The value to clone and append.
    constexpr void push_back(const T& value)
        requires Impled<T, Clone>
    {
        if (m_len == m_buf.cap) {
            m_buf.grow(m_buf.cap == 0 ? 4 : m_buf.cap * 2);
        }
        new (m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len) T(as<Clone>(value).clone());
        m_len++;
    }

    /// Removes the last element from the vector, discarding it.
    constexpr void pop_back() { (void)pop(); }

    /// Returns a mutable reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A mutable reference to the element.
    constexpr T& at(usize index) {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_mut_ptr().as_raw_ptr()[index];
    }
    /// Returns a const reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A const reference to the element.
    constexpr const T& at(usize index) const {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_ptr().as_raw_ptr()[index];
    }

    /// Indexes into the vector, panicking if out of bounds.
    constexpr T&       operator[](usize index) { return at(index); }
    /// Indexes into the vector (const), panicking if out of bounds.
    constexpr const T& operator[](usize index) const { return at(index); }

    /// Returns the number of elements in the vector.
    /// \return The length of the vector.
    constexpr usize len() const { return m_len; }
    /// Returns the number of elements the vector can hold without reallocating.
    /// \return The current capacity.
    constexpr usize capacity() const { return m_buf.cap; }
    /// Returns `true` if the vector contains no elements.
    constexpr bool  is_empty() const { return m_len == 0; }

    /// Clears the vector, destroying all elements but not deallocating memory.
    constexpr void clear() {
        auto* p = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = 0; i < m_len; ++i) {
            p[i].~T();
        }
        m_len = 0;
    }

    /// Removes and returns the element at the given index, shifting subsequent elements left.
    /// \param index The index of the element to remove.
    /// \return The removed element.
    constexpr T remove(usize index) {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        T     value = rstd::move(at(index));
        auto* p     = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = index; i < m_len - 1; ++i) {
            p[i] = rstd::move(p[i + 1]);
        }
        p[m_len - 1].~T();
        m_len--;
        return value;
    }

    /// Returns a mutable iterator to the beginning.
    constexpr auto begin() noexcept { return m_buf.ptr.as_mut_ptr().as_raw_ptr(); }
    /// Returns a mutable iterator to the end.
    constexpr auto end() noexcept { return m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len; }
    /// Returns a const iterator to the beginning.
    constexpr auto begin() const noexcept { return m_buf.ptr.as_ptr().as_raw_ptr(); }
    /// Returns a const iterator to the end.
    constexpr auto end() const noexcept { return m_buf.ptr.as_ptr().as_raw_ptr() + m_len; }
};

} // namespace alloc::vec

namespace rstd
{
template<typename U, mtp::same_as<cmp::PartialEq<::alloc::vec::Vec<U>>> T>
struct Impl<T, ::alloc::vec::Vec<U>> : ImplBase<default_tag<::alloc::vec::Vec<U>>> {
    auto eq(const ::alloc::vec::Vec<U>& other) const noexcept -> bool {
        if (this->self().len() != other.len()) return false;
        for (usize i = 0; i < this->self().len(); ++i) {
            if (! (this->self()[i] == other[i])) return false;
        }
        return true;
    }
};

template<typename A, mtp::same_as<From<::alloc::boxed::Box<A[]>>> T>
struct Impl<T, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    static auto from(::alloc::boxed::Box<A[]> b) -> ::alloc::vec::Vec<A> {
        auto ptr = b.as_mut_ptr();
        auto len = ptr.len();
        auto vec = ::alloc::vec::Vec<A>::with_capacity(len);
        for (usize i = 0; i != len; ++i) {
            vec.push(rstd::move(ptr[i]));
        }
        return vec;
    }
};

} // namespace rstd
