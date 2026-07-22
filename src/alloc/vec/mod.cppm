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
        if (capacity == usize()) return RawVec();
        auto layout = Layout::array<T>(capacity).unwrap();
        auto res    = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<T>();
        return { .ptr = NonNull<T>::make_unchecked(p), .cap = capacity };
    }

    /// Reallocates the storage to a new capacity.
    void grow(usize new_cap) {
        if (new_cap <= cap) return;

        auto new_layout = Layout::array<T>(new_cap).unwrap();

        if (cap == usize()) {
            auto res = as<Allocator>(::alloc::GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else {
            auto old_layout = Layout::array<T>(cap).unwrap();
            auto old_ptr    = ptr.as_mut_ptr();

            auto res =
                as<Allocator>(::alloc::GLOBAL).grow(old_ptr.as_raw_ptr(), old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        }
        cap = new_cap;
    }

    ~RawVec() {}

    void reset_ptr() {
        rstd::mem::fill(ptr, u8());
        cap = usize();
    }

    void drop() {
        if (! rstd::mem::all(ptr, u8())) {
            debug_assert(cap > usize());
            auto layout = Layout::array<T>(cap).unwrap();
            as<Allocator>(::alloc::GLOBAL).deallocate(ptr.as_raw_ptr(), layout);
        }
        reset_ptr();
    }
};

namespace alloc::vec
{

export template<typename T>
struct VecIntoIter;

/// A contiguous growable array type, analogous to Rust's `Vec<T>`.
/// \tparam T The element type, which must be `Sized`.
export template<typename T>
class Vec {
    RawVec<T> m_buf;
    usize     m_len;

    constexpr explicit Vec(RawVec<T> buf, usize len): m_buf(buf), m_len(len) {}

public:
    USE_TRAIT(Vec)

    using Target = T[];

    /// Creates an empty `Vec` with no allocation.
    constexpr Vec(): m_buf(), m_len() {}

    // no copy
    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    // move
    constexpr Vec(Self&& o) noexcept: m_buf(o.m_buf), m_len(o.m_len) {
        o.m_buf.reset_ptr();
        o.m_len = usize();
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
            o.m_len = usize();
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
    static auto with_capacity(usize capacity) -> Self {
        return Vec { RawVec<T>::with_capacity(capacity), usize() };
    }

    /// Copies raw bytes into owned `u8` objects.
    static auto copy_from_bytes(slice<byte> source) -> Self
        requires mtp::same_as<T, u8>
    {
        auto result = with_capacity(source.len());
        result.extend_from_bytes(source);
        return result;
    }

    /// Ensures that at least `additional` more elements can be inserted without reallocating.
    void reserve(usize additional) {
        auto required = m_len + additional;
        if (required <= m_buf.cap) return;

        auto new_cap = m_buf.cap == usize() ? usize(4) : m_buf.cap;
        while (new_cap < required) {
            new_cap *= usize(2);
        }
        m_buf.grow(new_cap);
    }

    /// Returns a slice containing the entire vector.
    /// \return An immutable `slice<T>` over all elements.
    constexpr auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<T> {
        if (m_len == usize()) return {};
        return slice<T>::from_raw_parts(m_buf.ptr.as_ptr().as_raw_ptr(), m_len);
    }

    /// Returns a mutable slice containing the entire vector.
    /// \return A mutable pointer to a slice over all elements.
    constexpr auto as_mut_slice() noexcept [[clang::lifetimebound]] -> mut_ptr<T[]> {
        if (m_len == usize()) return {};
        return mut_ptr<T[]>::from_raw_parts(m_buf.ptr.as_mut_ptr().as_raw_ptr(), m_len);
    }

    constexpr auto deref() const noexcept [[clang::lifetimebound]] -> ref<Target> {
        return as_slice();
    }

    constexpr auto deref_mut() noexcept [[clang::lifetimebound]] -> mut_ref<Target> {
        return as_mut_slice().as_mut_ref();
    }

    /// Returns a const pointer to the first element of the vector.
    /// \return A const pointer to the underlying buffer.
    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return m_buf.ptr.as_ptr();
    }

    /// Returns a mutable pointer to the first element of the vector.
    constexpr auto as_mut_ptr() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return m_buf.ptr.as_mut_ptr();
    }

    /// Returns the initialized contiguous storage as a raw pointer.
    constexpr auto data() noexcept [[clang::lifetimebound]] -> T* { return begin(); }

    /// Returns the initialized contiguous storage as a raw pointer.
    constexpr auto data() const noexcept [[clang::lifetimebound]] -> const T* { return begin(); }

    /// Returns writable spare capacity after the initialized range.
    ///
    /// The returned memory is uninitialized. After writing initialized values into it, callers must
    /// publish the written length with `set_len_unchecked`.
    constexpr auto spare_capacity_mut() noexcept [[clang::lifetimebound]] -> mut_ptr<T[]> {
        if (m_buf.cap == m_len) return {};
        return mut_ptr<T[]>::from_raw_parts(
            m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive(), m_buf.cap - m_len);
    }

    /// Sets the vector length without initializing or dropping elements.
    ///
    /// Callers must ensure that all elements in the new initialized range are valid.
    constexpr void set_len_unchecked(usize new_len) {
        if (new_len > m_buf.cap) {
            rstd::panic { "Vec::set_len_unchecked out of capacity" };
        }
        m_len = new_len;
    }

    /// Converts this `Vec` into a `Box<T[]>`, transferring ownership of all elements.
    /// \return A boxed slice containing the vector's elements.
    auto into_boxed_slice() noexcept -> Box<T[]> {
        auto length = m_len;
        auto layout = Layout::array<T>(length).unwrap();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto* raw     = res.unwrap_unchecked().template as_mut_ptr<T>().as_raw_ptr();
        auto* old_ptr = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (rstd::size_t index = 0; index < length.to_primitive(); ++index) {
            new (raw + index) T(rstd::move(old_ptr[index]));
            old_ptr[index].~T();
        }
        auto b = Box<T[]>::from_raw(mut_ptr<T[]>::from_raw_parts(raw, length));
        m_len  = usize();
        return b;
    }

    /// Constructs an element in-place at the back of the vector.
    template<typename... Args>
    constexpr T& emplace_back(Args&&... args) {
        if (m_len == m_buf.cap) {
            m_buf.grow(m_buf.cap == usize() ? usize(4) : m_buf.cap * usize(2));
        }
        auto* slot = m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive();
        new (slot) T(rstd::forward<Args>(args)...);
        ++m_len;
        return *slot;
    }

    /// Appends an element to the back of the vector by moving it.
    /// \param value The value to append.
    constexpr void push(T&& value) { (void)emplace_back(rstd::move(value)); }

    /// Removes the last element from the vector and returns it, or `None` if empty.
    /// \return An `Option<T>` containing the removed element.
    constexpr auto pop() -> Option<T> {
        if (m_len == usize()) {
            return None();
        } else {
            --m_len;
            T* p     = m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive();
            T  value = rstd::move(*p);
            p->~T();
            return Some(rstd::move(value));
        }
    }

    /// Appends a cloned copy of the element to the back of the vector.
    /// \param value The value to clone and append.
    constexpr void push_back(const T& value)
        requires Impled<T, Clone>
    {
        if (m_len == m_buf.cap) {
            m_buf.grow(m_buf.cap == usize() ? usize(4) : m_buf.cap * usize(2));
        }
        new (m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive())
            T(as<Clone>(value).clone());
        ++m_len;
    }

    /// Removes the last element from the vector, discarding it.
    constexpr void pop_back() { (void)pop(); }

    /// Appends a copy of all elements in `values`.
    void extend_from_slice(slice<T> values) {
        reserve(values.len());
        auto* p = m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive();
        for (rstd::size_t index = 0; index < values.len().to_primitive(); ++index) {
            new (p + index) T(values[usize(index)]);
        }
        m_len += values.len();
    }

    /// Appends a copy of `count` elements starting at `values`.
    void extend_from_slice(const T* values, usize count) {
        if (count == usize()) return;
        extend_from_slice(slice<T>::from_raw_parts(values, count));
    }

    /// Appends raw bytes as owned `u8` objects.
    void extend_from_bytes(slice<byte> values)
        requires mtp::same_as<T, u8>
    {
        reserve(values.len());
        for (u8 value : u8_values(values)) emplace_back(value);
    }

    /// Returns a mutable reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A mutable reference to the element.
    constexpr T& at(usize index) [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_mut_ptr().as_raw_ptr()[index.to_primitive()];
    }
    /// Returns a const reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A const reference to the element.
    constexpr const T& at(usize index) const [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_ptr().as_raw_ptr()[index.to_primitive()];
    }

    /// Indexes into the vector, panicking if out of bounds.
    constexpr T& operator[](usize index) [[clang::lifetimebound]] { return at(index); }
    /// Indexes into the vector (const), panicking if out of bounds.
    constexpr const T& operator[](usize index) const [[clang::lifetimebound]] { return at(index); }

    /// Returns the number of elements in the vector.
    /// \return The length of the vector.
    constexpr usize len() const { return m_len; }
    /// Returns the number of elements the vector can hold without reallocating.
    /// \return The current capacity.
    constexpr usize capacity() const { return m_buf.cap; }
    /// Returns `true` if the vector contains no elements.
    constexpr bool is_empty() const { return m_len == usize(); }

    auto clone() const -> Vec
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        auto result = Vec::with_capacity(m_len);
        for (rstd::size_t index = 0; index < m_len.to_primitive(); ++index) {
            result.push(rstd::as<rstd::clone::Clone>((*this)[usize(index)]).clone());
        }
        return result;
    }

    void clone_from(Vec& source)
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        *this = source.clone();
    }

    /// Clears the vector, destroying all elements but not deallocating memory.
    constexpr void clear() {
        auto* p = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (rstd::size_t index = 0; index < m_len.to_primitive(); ++index) {
            p[index].~T();
        }
        m_len = usize();
    }

    /// Shortens the vector, dropping elements after `new_len`.
    constexpr void truncate(usize new_len) {
        if (new_len >= m_len) return;
        auto* p = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (rstd::size_t index = new_len.to_primitive(); index < m_len.to_primitive(); ++index) {
            p[index].~T();
        }
        m_len = new_len;
    }

    /// Retains only the elements for which `predicate` returns true, preserving their order.
    template<typename F>
    constexpr void retain(F predicate) {
        auto*              values  = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        rstd::size_t       write   = 0;
        const rstd::size_t old_len = m_len.to_primitive();
        for (rstd::size_t read = 0; read < old_len; ++read) {
            if (predicate(static_cast<const T&>(values[read]))) {
                if (write != read) {
                    new (values + write) T(rstd::move(values[read]));
                    values[read].~T();
                }
                ++write;
            } else {
                values[read].~T();
            }
        }
        m_len = usize(write);
    }

    /// Resizes the vector to `new_len`, cloning `value` into newly-created slots.
    void resize(usize new_len, const T& value) {
        if (new_len <= m_len) {
            truncate(new_len);
            return;
        }

        auto old_len = m_len;
        reserve(new_len - m_len);
        auto* p = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (rstd::size_t index = old_len.to_primitive(); index < new_len.to_primitive(); ++index) {
            new (p + index) T(value);
        }
        m_len = new_len;
    }

    /// Removes and returns the element at the given index, shifting subsequent elements left.
    /// \param index The index of the element to remove.
    /// \return The removed element.
    constexpr T remove(usize index) {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        T     value = rstd::move(at(index));
        auto* p     = m_buf.ptr.as_mut_ptr().as_raw_ptr();
        for (rstd::size_t current = index.to_primitive(); current + 1 < m_len.to_primitive();
             ++current) {
            p[current] = rstd::move(p[current + 1]);
        }
        p[m_len.to_primitive() - 1].~T();
        --m_len;
        return value;
    }

    /// Returns a mutable iterator to the beginning.
    constexpr auto begin() noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_mut_ptr().as_raw_ptr();
    }
    /// Returns a mutable iterator to the end.
    constexpr auto end() noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_mut_ptr().as_raw_ptr() + m_len.to_primitive();
    }
    /// Returns a const iterator to the beginning.
    constexpr auto begin() const noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_ptr().as_raw_ptr();
    }
    /// Returns a const iterator to the end.
    constexpr auto end() const noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_ptr().as_raw_ptr() + m_len.to_primitive();
    }

    using IntoIter = VecIntoIter<T>;

    /// Returns an iterator over `&T`.
    auto iter() const [[clang::lifetimebound]] -> rstd::iter::SliceIter<T> {
        return { begin(), end() };
    }
    /// Returns an iterator over `&mut T`.
    auto iter_mut() [[clang::lifetimebound]] -> rstd::iter::SliceIterMut<T> {
        return { begin(), end() };
    }
    /// Consumes the vector, returning an iterator over owned `T`.
    auto into_iter() -> VecIntoIter<T> { return VecIntoIter<T>(rstd::move(*this)); }
};

/// Owning iterator over a `Vec<T>`, yielding elements by value.
export template<typename T>
struct VecIntoIter : rstd::DefaultInClass<VecIntoIter<T>, rstd::iter::Iterator> {
    using Item = T;
    Vec<T> vec;
    usize  idx;

    explicit VecIntoIter(Vec<T> v): vec(rstd::move(v)), idx() {}

    auto next() -> rstd::Option<Item> {
        if (idx >= vec.len()) return rstd::None();
        T v = rstd::move(vec[idx]);
        ++idx;
        return rstd::Some(rstd::move(v));
    }

    auto next_back() -> rstd::Option<Item> {
        if (idx >= vec.len()) return rstd::None();
        return vec.pop();
    }

    auto size_hint() const -> rstd::iter::SizeHint {
        usize n = vec.len() - idx;
        return { n, rstd::Some(n) };
    }

    auto len() const -> usize { return vec.len() - idx; }
};

} // namespace alloc::vec

namespace rstd
{
template<typename U, mtp::same_as<cmp::PartialEq<::alloc::vec::Vec<U>>> T>
struct Impl<T, ::alloc::vec::Vec<U>> : DefaultInImpl<T, ::alloc::vec::Vec<U>> {
    auto eq(const ::alloc::vec::Vec<U>& other) const noexcept -> bool {
        if (this->self().len() != other.len()) return false;
        for (rstd::size_t index = 0; index < this->self().len().to_primitive(); ++index) {
            if (! (this->self()[usize(index)] == other[usize(index)])) return false;
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
        for (rstd::size_t index = 0; index != len.to_primitive(); ++index) {
            vec.push(rstd::move(ptr[usize(index)]));
        }
        return vec;
    }
};

// collect<Vec<A>>() builds a Vec by draining any iterator of A.
template<typename A>
struct Impl<iter::FromIterator<A>, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    template<typename It>
    static auto from_iter(It it) -> ::alloc::vec::Vec<A> {
        auto vec = ::alloc::vec::Vec<A>::make();
        for (auto x = it.next(); x.is_some(); x = it.next()) vec.push(rstd::move(*x));
        return vec;
    }
};

template<typename A>
struct Impl<iter::IntoIterator, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    auto into_iter() -> ::alloc::vec::VecIntoIter<A> { return this->self().into_iter(); }
};

} // namespace rstd
