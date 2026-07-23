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

    static constexpr usize MIN_NON_ZERO_CAP =
        sizeof(T) == 1 ? usize(8) : (sizeof(T) <= 1024 ? usize(4) : usize(1));

    static auto with_capacity(usize capacity) -> RawVec {
        if (capacity == usize()) return RawVec();
        auto layout = Layout::array<T>(capacity).unwrap();
        auto res    = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<T>();
        return { .ptr = NonNull<T>::make_unchecked(p), .cap = capacity };
    }

    /// Reallocates the storage to a new capacity.
    void grow(usize new_cap, usize len) {
        if (new_cap <= cap) return;

        auto new_layout = Layout::array<T>(new_cap).unwrap();

        if (cap == usize()) {
            auto res = as<Allocator>(::alloc::GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else if constexpr (mtp::triv_copyable<T>) {
            auto old_layout = Layout::array<T>(cap).unwrap();
            auto old_ptr    = ptr.as_mut_ptr();

            auto res =
                as<Allocator>(::alloc::GLOBAL).grow(old_ptr.as_raw_ptr(), old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else {
            auto old_layout = Layout::array<T>(cap).unwrap();
            auto old_ptr    = ptr.as_mut_ptr().as_raw_ptr();
            auto res        = as<Allocator>(::alloc::GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<T>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                rstd::construct_at(new_ptr + index, rstd::move(old_ptr[index]));
                rstd::destroy_at(old_ptr + index);
            }
            as<Allocator>(::alloc::GLOBAL).deallocate(old_ptr, old_layout);
            ptr = NonNull<T>::make_unchecked(mut_ptr<T>::from_raw_parts(new_ptr));
        }
        cap = new_cap;
    }

    void shrink_to_fit(usize len) {
        if (len == cap) return;
        debug_assert(len < cap);

        auto old_layout = Layout::array<T>(cap).unwrap();
        auto old_ptr    = ptr.as_mut_ptr().as_raw_ptr();
        if (len == usize()) {
            as<Allocator>(::alloc::GLOBAL).deallocate(old_ptr, old_layout);
            reset_ptr();
            return;
        }

        auto new_layout = Layout::array<T>(len).unwrap();
        if constexpr (mtp::triv_copyable<T>) {
            auto res = as<Allocator>(::alloc::GLOBAL).shrink(old_ptr, old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else {
            auto res = as<Allocator>(::alloc::GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<T>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                rstd::construct_at(new_ptr + index, rstd::move(old_ptr[index]));
                rstd::destroy_at(old_ptr + index);
            }
            as<Allocator>(::alloc::GLOBAL).deallocate(old_ptr, old_layout);
            ptr = NonNull<T>::make_unchecked(mut_ptr<T>::from_raw_parts(new_ptr));
        }
        cap = len;
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

export template<typename T>
class Vec;

export template<typename T>
class SpareSlot {
    mut_ptr<T> slot_;

    constexpr explicit SpareSlot(mut_ptr<T> slot) noexcept: slot_(slot) {}

    template<typename>
    friend class SpareCapacity;

public:
    template<typename... Args>
    constexpr decltype(auto) write(Args&&... args) {
        rstd::ptr_::construct(slot_, rstd::forward<Args>(args)...);
        return slot_.as_mut_ref().get_mut();
    }
};

/// A writable view over uninitialized vector capacity.
export template<typename T>
class SpareCapacity {
    mut_ptr<T> pointer_;
    usize      length_;

    constexpr SpareCapacity(mut_ptr<T> pointer, usize length) noexcept
        : pointer_(pointer), length_(length) {}

    friend class Vec<T>;

public:
    constexpr SpareCapacity() noexcept = default;
    constexpr auto len() const noexcept -> usize { return length_; }
    constexpr auto is_empty() const noexcept -> bool { return length_ == usize(); }

    constexpr auto operator[](usize index) const -> SpareSlot<T> {
        if (index >= length_) rstd::panic { "Vec spare capacity index out of bounds" };
        return SpareSlot<T>(pointer_.add(index));
    }
};

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

    /// Takes ownership of a boxed slice without copying its elements.
    static auto from_boxed_slice(Box<T[]>&& values) noexcept -> Self {
        auto raw    = rstd::move(values).into_raw();
        auto length = raw.len();
        if (length == usize()) return {};

        auto pointer = mut_ptr<T>::from_raw_parts(raw.as_raw_ptr());
        return Vec { RawVec<T> { .ptr = NonNull<T>::make_unchecked(pointer), .cap = length },
                     length };
    }

    /// Clones a borrowed slice into a new vector.
    static auto from(slice<T> values) -> Self
        requires Impled<T, Clone>
    {
        auto result = with_capacity(values.len());
        result.extend_from_slice(values);
        return result;
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
        auto required_value = m_len.checked_add(additional);
        if (required_value.is_none()) rstd::panic { "Vec capacity overflow" };
        auto required = *required_value;
        if (required <= m_buf.cap) return;

        auto new_cap = m_buf.cap.saturating_mul(usize(2));
        if (new_cap < required) new_cap = required;
        if (new_cap < RawVec<T>::MIN_NON_ZERO_CAP) new_cap = RawVec<T>::MIN_NON_ZERO_CAP;
        m_buf.grow(new_cap, m_len);
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
    constexpr auto data() noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_mut_ptr().as_raw_ptr();
    }

    /// Returns the initialized contiguous storage as a raw pointer.
    constexpr auto data() const noexcept [[clang::lifetimebound]] {
        return m_buf.ptr.as_ptr().as_raw_ptr();
    }

    /// Returns writable spare capacity after the initialized range.
    ///
    /// The returned memory is uninitialized. After writing initialized values into it, callers must
    /// publish the written length with `set_len_unchecked`.
    constexpr auto spare_capacity_mut() noexcept [[clang::lifetimebound]] -> SpareCapacity<T> {
        if (m_buf.cap == m_len) return {};
        return SpareCapacity<T>(m_buf.ptr.as_mut_ptr().add(m_len), m_buf.cap - m_len);
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
        m_buf.shrink_to_fit(m_len);

        auto length = m_len;
        auto raw = length == usize() ? NonNull<T>::dangling().as_mut_ptr() : m_buf.ptr.as_mut_ptr();
        m_buf.reset_ptr();
        m_len = usize();
        return Box<T[]>::from_raw(mut_ptr<T[]>::from_raw_parts(raw.as_raw_ptr(), length));
    }

    /// Constructs an element in-place at the back of the vector.
    template<typename... Args>
    constexpr decltype(auto) emplace_back(Args&&... args) {
        if (m_len == m_buf.cap) reserve(usize(1));
        auto slot = m_buf.ptr.as_mut_ptr().add(m_len);
        rstd::ptr_::construct(slot, rstd::forward<Args>(args)...);
        ++m_len;
        return slot.as_mut_ref().get_mut();
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
            auto p     = m_buf.ptr.as_mut_ptr().add(m_len);
            T    value = rstd::ptr_::move_out(p);
            rstd::ptr_::destroy(p);
            return Some(rstd::move(value));
        }
    }

    /// Appends a cloned copy of the element to the back of the vector.
    /// \param value The value to clone and append.
    constexpr void push_back(const T& value)
        requires Impled<T, Clone>
    {
        if constexpr (mtp::same_as<T, u8>) {
            emplace_back(as<Clone>(value).clone());
        } else {
            extend_from_slice(slice<T>::from_raw_parts(rstd::addressof(value), usize(1)));
        }
    }

    /// Removes the last element from the vector, discarding it.
    constexpr void pop_back() { (void)pop(); }

    /// Appends a clone of all elements in `values`.
    void extend_from_slice(slice<T> values)
        requires Impled<T, Clone>
    {
        if (values.is_empty()) return;

        bool         source_is_self = false;
        rstd::size_t source_offset  = 0;
        if (m_len != usize()) {
            auto const buffer_address =
                reinterpret_cast<uintptr_t>(m_buf.ptr.as_ptr().as_raw_ptr());
            auto const source_address = reinterpret_cast<uintptr_t>(values.as_raw_ptr());
            if (source_address >= buffer_address) {
                auto const offset_bytes = source_address - buffer_address;
                using Storage           = typename mut_ptr<T>::storage_type;
                if (offset_bytes % sizeof(Storage) == 0) {
                    source_offset = offset_bytes / sizeof(Storage);
                    source_is_self =
                        source_offset <= m_len.to_primitive() &&
                        values.len().to_primitive() <= m_len.to_primitive() - source_offset;
                }
            }
        }

        reserve(values.len());

        if (source_is_self) {
            values = slice<T>::from_raw_parts(m_buf.ptr.as_ptr().as_raw_ptr() + source_offset,
                                              values.len());
        }

        auto destination = m_buf.ptr.as_mut_ptr().add(m_len);
        if constexpr (Impled<T, rstd::Copy>) {
            rstd::ptr_::copy_nonoverlapping(
                ptr<T>::from_raw_parts(values.as_raw_ptr()), destination, values.len());
            m_len += values.len();
        } else {
            for (rstd::size_t index = 0; index < values.len().to_primitive(); ++index) {
                rstd::ptr_::construct(destination.add(usize(index)),
                                      as<Clone>(values[usize(index)]).clone());
                ++m_len;
            }
        }
    }

    /// Appends a copy of `count` elements starting at `values`.
    void extend_from_slice(const T* values, usize count)
        requires Impled<T, Clone> && (! mtp::same_as<T, u8>)
    {
        if (count == usize()) return;
        extend_from_slice(slice<T>::from_raw_parts(values, count));
    }

    /// Appends raw bytes as owned `u8` objects.
    void extend_from_bytes(slice<byte> values)
        requires mtp::same_as<T, u8>
    {
        extend_from_slice(as_u8_slice(values));
    }

    /// Returns a mutable reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A mutable reference to the element.
    constexpr decltype(auto) at(usize index) [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_mut_ptr().add(index).get();
    }
    /// Returns a const reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A const reference to the element.
    constexpr decltype(auto) at(usize index) const [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr.as_ptr().add(index).get();
    }

    /// Indexes into the vector, panicking if out of bounds.
    constexpr decltype(auto) operator[](usize index) [[clang::lifetimebound]] { return at(index); }
    /// Indexes into the vector (const), panicking if out of bounds.
    constexpr decltype(auto) operator[](usize index) const [[clang::lifetimebound]] {
        return at(index);
    }

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
        result.extend_from_slice(as_slice());
        return result;
    }

    void clone_from(const Vec& source)
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        if (this == rstd::addressof(source)) return;

        truncate(source.len());
        auto const initialized = m_len;
        rstd::slice_::clone_from_slice(
            mut_ref<T[]>::from_raw_parts(m_buf.ptr.as_mut_ptr().as_raw_ptr(), initialized),
            slice<T>::from_raw_parts(source.data(), initialized));

        if (initialized < source.len()) {
            extend_from_slice(slice<T>::from_raw_parts(source.data() + initialized.to_primitive(),
                                                       source.len() - initialized));
        }
    }

    /// Clears the vector, destroying all elements but not deallocating memory.
    constexpr void clear() {
        auto p = m_buf.ptr.as_mut_ptr();
        for (rstd::size_t index = 0; index < m_len.to_primitive(); ++index) {
            rstd::ptr_::destroy(p.add(usize(index)));
        }
        m_len = usize();
    }

    /// Shortens the vector, dropping elements after `new_len`.
    constexpr void truncate(usize new_len) {
        if (new_len >= m_len) return;
        auto p = m_buf.ptr.as_mut_ptr();
        for (rstd::size_t index = new_len.to_primitive(); index < m_len.to_primitive(); ++index) {
            rstd::ptr_::destroy(p.add(usize(index)));
        }
        m_len = new_len;
    }

    /// Retains only the elements for which `predicate` returns true, preserving their order.
    template<typename F>
    constexpr void retain(F predicate) {
        auto               values  = m_buf.ptr.as_mut_ptr();
        rstd::size_t       write   = 0;
        const rstd::size_t old_len = m_len.to_primitive();
        for (rstd::size_t read = 0; read < old_len; ++read) {
            auto read_ptr = values.add(usize(read));
            if (predicate(read_ptr.as_ptr().get())) {
                if (write != read) {
                    rstd::ptr_::construct(values.add(usize(write)), rstd::ptr_::move_out(read_ptr));
                    rstd::ptr_::destroy(read_ptr);
                }
                ++write;
            } else {
                rstd::ptr_::destroy(read_ptr);
            }
        }
        m_len = usize(write);
    }

    /// Resizes the vector to `new_len`, cloning `value` into newly-created slots.
    void resize(usize new_len, const T& value)
        requires Impled<T, Clone>
    {
        if (new_len <= m_len) {
            truncate(new_len);
            return;
        }

        auto old_len = m_len;
        if constexpr (mtp::same_as<T, u8>) {
            auto source = as<Clone>(value).clone();
            reserve(new_len - m_len);
            auto p = m_buf.ptr.as_mut_ptr();
            for (rstd::size_t index = old_len.to_primitive(); index < new_len.to_primitive();
                 ++index) {
                rstd::ptr_::construct(p.add(usize(index)), source);
                ++m_len;
            }
            return;
        } else {
            bool         value_is_self = false;
            rstd::size_t value_offset  = 0;
            if (m_len != usize()) {
                auto const buffer_address =
                    reinterpret_cast<uintptr_t>(m_buf.ptr.as_ptr().as_raw_ptr());
                auto const value_address = reinterpret_cast<uintptr_t>(rstd::addressof(value));
                if (value_address >= buffer_address) {
                    auto const offset_bytes = value_address - buffer_address;
                    if (offset_bytes % sizeof(T) == 0) {
                        value_offset  = offset_bytes / sizeof(T);
                        value_is_self = value_offset < m_len.to_primitive();
                    }
                }
            }

            reserve(new_len - m_len);
            auto p = m_buf.ptr.as_mut_ptr();
            for (rstd::size_t index = old_len.to_primitive(); index < new_len.to_primitive();
                 ++index) {
                auto const* source =
                    value_is_self ? p.as_raw_ptr() + value_offset : rstd::addressof(value);
                rstd::ptr_::construct(p.add(usize(index)), as<Clone>(*source).clone());
                ++m_len;
            }
        }
    }

    /// Removes and returns the element at the given index, shifting subsequent elements left.
    /// \param index The index of the element to remove.
    /// \return The removed element.
    constexpr T remove(usize index) {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        auto p     = m_buf.ptr.as_mut_ptr();
        T    value = rstd::ptr_::move_out(p.add(index));
        for (rstd::size_t current = index.to_primitive(); current + 1 < m_len.to_primitive();
             ++current) {
            rstd::ptr_::write(p.add(usize(current)),
                              rstd::ptr_::move_out(p.add(usize(current + 1))));
        }
        rstd::ptr_::destroy(p.add(m_len - usize(1)));
        --m_len;
        return value;
    }

    /// Returns a mutable iterator to the beginning.
    constexpr auto begin() noexcept [[clang::lifetimebound]] -> mut_ptr<T> { return as_mut_ptr(); }
    /// Returns a mutable iterator to the end.
    constexpr auto end() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return as_mut_ptr().add(m_len);
    }
    /// Returns a const iterator to the beginning.
    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<T> { return as_ptr(); }
    /// Returns a const iterator to the end.
    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return as_ptr().add(m_len);
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
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;
    Vec<T>                vec;
    usize                 idx;

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

namespace details
{

template<typename T, typename I>
auto from_trusted_iter(I iterator) -> Vec<T> {
    auto hint  = rstd::as<rstd::iter::Iterator>(iterator).size_hint();
    auto upper = hint.template get<1>();
    if (upper.is_none()) rstd::panic { "Vec capacity overflow" };
    debug_assert(hint.template get<0>() == *upper);

    auto result = Vec<T>::with_capacity(*upper);
    for (auto item = rstd::as<rstd::iter::Iterator>(iterator).next(); item.is_some();
         item      = rstd::as<rstd::iter::Iterator>(iterator).next()) {
        result.push(rstd::move(*item));
    }
    return result;
}

template<typename T, typename I>
auto from_iter(I iterator) -> Vec<T> {
    if constexpr (rstd::Impled<I, rstd::iter::TrustedLen>) {
        return from_trusted_iter<T>(rstd::move(iterator));
    } else {
        auto first = rstd::as<rstd::iter::Iterator>(iterator).next();
        if (first.is_none()) return Vec<T>::make();

        auto lower =
            rstd::as<rstd::iter::Iterator>(iterator).size_hint().template get<0>().saturating_add(
                usize(1));
        auto initial_capacity =
            lower < RawVec<T>::MIN_NON_ZERO_CAP ? RawVec<T>::MIN_NON_ZERO_CAP : lower;
        auto result = Vec<T>::with_capacity(initial_capacity);
        result.push(rstd::move(*first));

        for (auto item = rstd::as<rstd::iter::Iterator>(iterator).next(); item.is_some();
             item      = rstd::as<rstd::iter::Iterator>(iterator).next()) {
            if (result.len() == result.capacity()) {
                auto additional = rstd::as<rstd::iter::Iterator>(iterator)
                                      .size_hint()
                                      .template get<0>()
                                      .saturating_add(usize(1));
                result.reserve(additional);
            }
            result.push(rstd::move(*item));
        }
        return result;
    }
}

template<typename T>
auto from_iter(VecIntoIter<T> iterator) -> Vec<T> {
    auto remaining = iterator.len();
    if (iterator.idx == usize()) return rstd::move(iterator.vec);

    if (remaining >= iterator.vec.capacity() / usize(2)) {
        auto pointer = iterator.vec.as_mut_ptr();
        for (auto index = usize(); index < iterator.idx; ++index) {
            rstd::ptr_::destroy(pointer.add(index));
        }
        for (auto index = usize(); index < remaining; ++index) {
            auto source      = pointer.add(iterator.idx + index);
            auto destination = pointer.add(index);
            auto value       = rstd::ptr_::move_out(source);
            rstd::ptr_::construct(destination, rstd::move(value));
            rstd::ptr_::destroy(source);
        }
        iterator.vec.set_len_unchecked(remaining);
        return rstd::move(iterator.vec);
    }

    auto result = Vec<T>::make();
    result.reserve(remaining);
    for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
        result.push(rstd::move(*item));
    }
    return result;
}

} // namespace details

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
        return ::alloc::vec::Vec<A>::from_boxed_slice(rstd::move(b));
    }
};

template<typename A, mtp::same_as<From<slice<A>>> T>
    requires Impled<A, clone::Clone>
struct Impl<T, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    static auto from(slice<A> values) -> ::alloc::vec::Vec<A> {
        return ::alloc::vec::Vec<A>::from(values);
    }
};

template<typename A>
struct Impl<iter::FromIterator<A>, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    template<typename It>
    static auto from_iter(It it) -> ::alloc::vec::Vec<A> {
        return ::alloc::vec::details::from_iter<A>(rstd::move(it));
    }
};

template<typename A>
struct Impl<iter::IntoIterator, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    using IntoIter = ::alloc::vec::VecIntoIter<A>;

    auto into_iter() -> IntoIter { return this->self().into_iter(); }
};

template<typename A>
struct Impl<iter::IntoIterator, ref<::alloc::vec::Vec<A>>> : ImplBase<ref<::alloc::vec::Vec<A>>> {
    using IntoIter = iter::SliceIter<A>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

template<typename A>
struct Impl<iter::IntoIterator, mut_ref<::alloc::vec::Vec<A>>>
    : ImplBase<mut_ref<::alloc::vec::Vec<A>>> {
    using IntoIter = iter::SliceIterMut<A>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter_mut(); }
};

} // namespace rstd
