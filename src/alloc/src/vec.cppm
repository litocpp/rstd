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

export namespace alloc::collections
{
enum class TryReserveError
{
    CapacityOverflow,
    AllocError
};
}

using alloc::collections::TryReserveError;

/// A low-level utility for managing the backing storage of a `Vec`.
template<typename T, typename A>
struct RawVec {
    NonNull<T>                    ptr;
    usize                         cap;
    Layout                        allocation_layout;
    RSTD_ATTR_NO_UNIQUE_ADDRESS A allocator;

    static constexpr usize MIN_NON_ZERO_CAP =
        sizeof(typename mut_ptr<T>::storage_type) == 1
            ? usize(8)
            : (sizeof(typename mut_ptr<T>::storage_type) <= 1024 ? usize(4) : usize(1));

    constexpr explicit RawVec(A allocator)
        : ptr(), cap(), allocation_layout(), allocator(rstd::move(allocator)) {}

    constexpr RawVec(NonNull<T> ptr, usize cap, Layout layout, A allocator)
        : ptr(ptr), cap(cap), allocation_layout(layout), allocator(rstd::move(allocator)) {}

    RawVec(const RawVec&)                    = delete;
    auto operator=(const RawVec&) -> RawVec& = delete;

    constexpr RawVec(RawVec&& other)
        : ptr(other.ptr),
          cap(other.cap),
          allocation_layout(other.allocation_layout),
          allocator(rstd::move(other.allocator)) {
        other.reset_ptr();
    }

    constexpr auto operator=(RawVec&& other) -> RawVec& {
        if (this == rstd::addressof(other)) return *this;
        drop();
        ptr               = other.ptr;
        cap               = other.cap;
        allocation_layout = other.allocation_layout;
        allocator         = rstd::move(other.allocator);
        other.reset_ptr();
        return *this;
    }

    static auto with_capacity(usize capacity, A allocator) -> RawVec {
        if (capacity == usize()) return RawVec(rstd::move(allocator));
        auto layout = Layout::array<T>(capacity).unwrap();
        auto res    = as<Allocator>(allocator).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<T>();
        return RawVec(NonNull<T>::make_unchecked(p), capacity, layout, rstd::move(allocator));
    }

    /// Reallocates the storage to a new capacity.
    auto try_grow(usize new_cap, usize len) -> Result<empty, TryReserveError> {
        if (new_cap <= cap) return Ok(empty {});

        auto checked_layout = Layout::array<T>(new_cap);
        if (checked_layout.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto new_layout = checked_layout.unwrap_unchecked();

        if (allocation_layout.size == usize()) {
            auto res = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);
            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else if constexpr (mtp::triv_copyable<T>) {
            auto old_layout = allocation_layout;
            auto old_ptr    = ptr.as_mut_ptr();

            auto res = as<Allocator>(allocator).grow(old_ptr.as_raw_ptr(), old_layout, new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);

            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else {
            auto old_layout = allocation_layout;
            auto old_ptr    = ptr.as_mut_ptr().as_raw_ptr();
            auto res        = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<T>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                rstd::construct_at(new_ptr + index, rstd::move(old_ptr[index]));
                rstd::destroy_at(old_ptr + index);
            }
            as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            ptr = NonNull<T>::make_unchecked(mut_ptr<T>::from_raw_parts(new_ptr));
        }
        cap               = new_cap;
        allocation_layout = new_layout;
        return Ok(empty {});
    }

    void grow(usize new_cap, usize len) {
        if (try_grow(new_cap, len).is_err()) rstd::panic { "Vec allocation failed" };
    }

    void shrink_to(usize capacity, usize len) {
        auto old_layout = allocation_layout;
        auto new_layout = Layout::array<T>(capacity).unwrap();
        if (old_layout.size == new_layout.size && old_layout.align == new_layout.align) {
            cap = capacity;
            return;
        }
        debug_assert(len <= capacity && capacity <= cap);

        auto old_ptr = ptr.as_mut_ptr().as_raw_ptr();
        if (capacity == usize()) {
            as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            reset_ptr();
            return;
        }

        if constexpr (mtp::triv_copyable<T>) {
            auto res = as<Allocator>(allocator).shrink(old_ptr, old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr = NonNull<T>::make_unchecked(res.unwrap_unchecked().template as_mut_ptr<T>());
        } else {
            auto res = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<T>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                rstd::construct_at(new_ptr + index, rstd::move(old_ptr[index]));
                rstd::destroy_at(old_ptr + index);
            }
            as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            ptr = NonNull<T>::make_unchecked(mut_ptr<T>::from_raw_parts(new_ptr));
        }
        cap               = capacity;
        allocation_layout = new_layout;
    }

    void shrink_to_fit(usize len) { shrink_to(len, len); }

    ~RawVec() {}

    void reset_ptr() {
        rstd::mem::fill(ptr, u8());
        cap               = usize();
        allocation_layout = {};
    }

    void drop() {
        if (! rstd::mem::all(ptr, u8())) {
            debug_assert(allocation_layout.size > usize());
            as<Allocator>(allocator).deallocate(ptr.as_raw_ptr(), allocation_layout);
        }
        reset_ptr();
    }
};

namespace alloc::vec
{

export template<typename T, typename A = ::alloc::Global>
struct VecIntoIter;

export template<typename T, typename A = ::alloc::Global>
class Vec;

namespace details
{
struct InPlaceAccess;

template<typename T, typename A>
auto from_iter(VecIntoIter<T, A> iterator) -> Vec<T, A>;
} // namespace details

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

    template<typename, typename>
    friend class Vec;

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
export template<typename T, typename A>
class Vec {
    RawVec<T, A> m_buf;
    usize        m_len;

    constexpr explicit Vec(RawVec<T, A> buf, usize len): m_buf(rstd::move(buf)), m_len(len) {}

    friend struct VecIntoIter<T, A>;
    friend struct details::InPlaceAccess;
    friend auto details::from_iter<T, A>(VecIntoIter<T, A> iterator) -> Vec<T, A>;

public:
    USE_TRAIT(Vec)

    /// Creates an empty `Vec` with no allocation.
    constexpr Vec(): m_buf(A {}), m_len() {}

    constexpr explicit Vec(A allocator): m_buf(rstd::move(allocator)), m_len() {}

    // no copy
    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    // move
    constexpr Vec(Self&& o): m_buf(rstd::move(o.m_buf)), m_len(o.m_len) { o.m_len = usize(); }
    constexpr Vec& operator=(Self&& o) {
        if (this != &o) {
            // clean
            clear();
            m_buf.drop();

            // assign
            m_buf = rstd::move(o.m_buf);
            m_len = o.m_len;

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
    static constexpr auto new_in(A allocator) -> Self { return Self(rstd::move(allocator)); }
    /// Creates a new empty `Vec` with at least the specified capacity.
    /// \param capacity The minimum number of elements the `Vec` can hold without reallocating.
    /// \return A `Vec` with preallocated capacity.
    static auto with_capacity(usize capacity) -> Self { return with_capacity_in(capacity, A {}); }

    static auto with_capacity_in(usize capacity, A allocator) -> Self {
        return Vec { RawVec<T, A>::with_capacity(capacity, rstd::move(allocator)), usize() };
    }

    template<rstd::iter::has_next I>
    static auto from_iter_in(I iterator, A allocator) -> Self {
        auto result = Self::new_in(rstd::move(allocator));
        result.reserve(rstd::as<rstd::iter::Iterator>(iterator).size_hint().template get<0>());
        for (auto item = rstd::as<rstd::iter::Iterator>(iterator).next(); item.is_some();
             item      = rstd::as<rstd::iter::Iterator>(iterator).next()) {
            result.push(rstd::move(*item));
        }
        return result;
    }

    /// Takes ownership of a boxed slice without copying its elements.
    static auto from_boxed_slice(Box<T[]>&& values) noexcept -> Self
        requires mtp::same_as<A, ::alloc::Global>
    {
        auto raw    = rstd::move(values).into_raw();
        auto length = raw.len();
        if (length == usize()) return {};

        auto pointer = mut_ptr<T>::from_raw_parts(raw.as_raw_ptr());
        return Vec { RawVec<T, A>(NonNull<T>::make_unchecked(pointer),
                                  length,
                                  Layout::array<T>(length).unwrap(),
                                  A {}),
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
        if (new_cap < RawVec<T, A>::MIN_NON_ZERO_CAP) new_cap = RawVec<T, A>::MIN_NON_ZERO_CAP;
        m_buf.grow(new_cap, m_len);
    }

    auto try_reserve_exact(usize additional) -> Result<empty, TryReserveError> {
        auto required = m_len.checked_add(additional);
        if (required.is_none()) return Err(TryReserveError::CapacityOverflow);
        return m_buf.try_grow(*required, m_len);
    }

    auto try_reserve(usize additional) -> Result<empty, TryReserveError> {
        auto required = m_len.checked_add(additional);
        if (required.is_none()) return Err(TryReserveError::CapacityOverflow);
        if (*required <= m_buf.cap) return Ok(empty {});
        auto capacity = m_buf.cap.saturating_mul(usize(2));
        if (capacity < *required) capacity = *required;
        if (capacity < RawVec<T, A>::MIN_NON_ZERO_CAP) capacity = RawVec<T, A>::MIN_NON_ZERO_CAP;
        return m_buf.try_grow(capacity, m_len);
    }

    void reserve_exact(usize additional) {
        if (try_reserve_exact(additional).is_err()) rstd::panic { "Vec allocation failed" };
    }

    void shrink_to_fit() { m_buf.shrink_to_fit(m_len); }

    void shrink_to(usize minimum_capacity) {
        auto capacity = minimum_capacity < m_len ? m_len : minimum_capacity;
        if (capacity >= m_buf.cap) return;
        m_buf.shrink_to(capacity, m_len);
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

    constexpr auto deref() const noexcept [[clang::lifetimebound]] -> ref<T[]> {
        return as_slice();
    }

    constexpr auto deref_mut() noexcept [[clang::lifetimebound]] -> mut_ref<T[]> {
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
    auto into_boxed_slice() noexcept -> Box<T[]>
        requires mtp::same_as<A, ::alloc::Global>
    {
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
    constexpr auto  allocator() const noexcept [[clang::lifetimebound]] -> const A& {
        return m_buf.allocator;
    }
    /// Returns `true` if the vector contains no elements.
    constexpr bool is_empty() const { return m_len == usize(); }

    auto clone_in(A allocator) const -> Vec
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        auto result = Vec::with_capacity_in(m_len, rstd::move(allocator));
        result.extend_from_slice(as_slice());
        return result;
    }

    auto clone() const -> Vec
        requires rstd::Impled<T, rstd::clone::Clone> &&
                 requires(const A& allocator) { A(allocator); }
    {
        return clone_in(A(m_buf.allocator));
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
        auto values = as_mut_slice();
        m_len       = usize();
        rstd::ptr_::drop_in_place(values);
    }

    /// Shortens the vector, dropping elements after `new_len`.
    constexpr void truncate(usize new_len) {
        if (new_len >= m_len) return;
        auto values = mut_ptr<T[]>::from_raw_parts(m_buf.ptr.as_mut_ptr().add(new_len).as_raw_ptr(),
                                                   m_len - new_len);
        m_len       = new_len;
        rstd::ptr_::drop_in_place(values);
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
            auto count = new_len - old_len;
            rstd::mem::memset(m_buf.ptr.as_mut_ptr().add(old_len).as_raw_ptr(), source, count);
            m_len = new_len;
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

    using IntoIter = VecIntoIter<T, A>;

    /// Returns an iterator over `&T`.
    auto iter() const [[clang::lifetimebound]] -> rstd::iter::SliceIter<T> {
        return { begin(), end() };
    }
    /// Returns an iterator over `&mut T`.
    auto iter_mut() [[clang::lifetimebound]] -> rstd::iter::SliceIterMut<T> {
        return { begin(), end() };
    }
    /// Consumes the vector, returning an iterator over owned `T`.
    auto into_iter() && -> VecIntoIter<T, A> { return VecIntoIter<T, A>(rstd::move(*this)); }
};

export extern template class Vec<f64>;
export extern template class Vec<u8>;
export extern template class Vec<usize>;

/// Owning iterator over a `Vec<T>`, yielding elements by value.
export template<typename T, typename A>
struct VecIntoIter : rstd::DefaultInClass<VecIntoIter<T, A>, rstd::iter::Iterator> {
private:
    RawVec<T, A> buffer_;
    usize        front_;
    usize        back_;

    void drop_remaining() noexcept {
        auto values = mut_ptr<T[]>::from_raw_parts(
            buffer_.ptr.as_mut_ptr().add(front_).as_raw_ptr(), back_ - front_);
        front_ = back_;
        rstd::ptr_::drop_in_place(values);
    }

    auto take_buffer() -> RawVec<T, A> {
        auto buffer = rstd::move(buffer_);
        front_      = usize();
        back_       = usize();
        return buffer;
    }

    friend auto details::from_iter<T, A>(VecIntoIter<T, A> iterator) -> Vec<T, A>;
    friend struct details::InPlaceAccess;

public:
    using Item                                = T;
    using AllocatorType                       = A;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    explicit VecIntoIter(Vec<T, A> value)
        : buffer_(rstd::move(value.m_buf)), front_(), back_(value.m_len) {
        value.m_len = usize();
    }

    VecIntoIter(const VecIntoIter&)                    = delete;
    auto operator=(const VecIntoIter&) -> VecIntoIter& = delete;

    VecIntoIter(VecIntoIter&& other)
        : buffer_(rstd::move(other.buffer_)), front_(other.front_), back_(other.back_) {
        other.front_ = usize();
        other.back_  = usize();
    }

    auto operator=(VecIntoIter&& other) -> VecIntoIter& {
        if (this == rstd::addressof(other)) return *this;
        drop_remaining();
        buffer_.drop();
        buffer_      = rstd::move(other.buffer_);
        front_       = other.front_;
        back_        = other.back_;
        other.front_ = usize();
        other.back_  = usize();
        return *this;
    }

    ~VecIntoIter() {
        drop_remaining();
        buffer_.drop();
    }

    auto next() -> rstd::Option<Item> {
        if (front_ == back_) return rstd::None();
        auto source = buffer_.ptr.as_mut_ptr().add(front_);
        T    value  = rstd::ptr_::move_out(source);
        rstd::ptr_::destroy(source);
        ++front_;
        return rstd::Some(rstd::move(value));
    }

    auto next_back() -> rstd::Option<Item> {
        if (front_ == back_) return rstd::None();
        --back_;
        auto source = buffer_.ptr.as_mut_ptr().add(back_);
        T    value  = rstd::ptr_::move_out(source);
        rstd::ptr_::destroy(source);
        return rstd::Some(rstd::move(value));
    }

    auto size_hint() const -> rstd::iter::SizeHint {
        auto n = back_ - front_;
        return { n, rstd::Some(n) };
    }

    auto len() const -> usize { return back_ - front_; }
};

} // namespace alloc::vec

namespace rstd::iter::details
{

export template<typename T, typename A>
struct InPlaceTraits<::alloc::vec::VecIntoIter<T, A>> {
    using Source = ::alloc::vec::VecIntoIter<T, A>;

    static constexpr bool  ENABLED   = true;
    static constexpr usize EXPAND_BY = usize(1);
    static constexpr usize MERGE_BY  = usize(1);

    static auto source(Source& value) -> Source& { return value; }
};

} // namespace rstd::iter::details

namespace alloc::vec
{

namespace details
{

struct InPlaceMapProbe {
    auto operator()(int value) const -> int;
};

struct InPlacePredicateProbe {
    auto operator()(int value) const -> bool;
};

struct InPlaceOptionProbe {
    auto operator()(int value) const -> Option<int>;
};

struct InPlaceInspectProbe {
    void operator()(int value) const;
};

struct InPlaceScanProbe {
    auto operator()(int& state, int value) const -> Option<int>;
};

struct UnknownInPlaceProbe {
    using Item = int;
    auto next() -> Option<int>;
};

using InPlaceProbeRoot = VecIntoIter<int>;
using InPlaceProbeMap =
    decltype(mtp::declval<InPlaceProbeRoot>().map(mtp::declval<InPlaceMapProbe>()));
using InPlaceProbeFilter =
    decltype(mtp::declval<InPlaceProbeRoot>().filter(mtp::declval<InPlacePredicateProbe>()));
using InPlaceProbeFilterMap =
    decltype(mtp::declval<InPlaceProbeRoot>().filter_map(mtp::declval<InPlaceOptionProbe>()));
using InPlaceProbeInspect =
    decltype(mtp::declval<InPlaceProbeRoot>().inspect(mtp::declval<InPlaceInspectProbe>()));
using InPlaceProbeScan =
    decltype(mtp::declval<InPlaceProbeRoot>().scan(int(), mtp::declval<InPlaceScanProbe>()));

static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeRoot>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeMap>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeFilter>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeFilterMap>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().enumerate())>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<decltype(mtp::declval<InPlaceProbeRoot>().zip(
                  mtp::declval<InPlaceProbeRoot>()))>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().take(usize(1)))>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().skip(usize(1)))>::ENABLED);
static_assert(
    rstd::iter::details::InPlaceTraits<decltype(mtp::declval<InPlaceProbeRoot>().take_while(
        mtp::declval<InPlacePredicateProbe>()))>::ENABLED);
static_assert(
    rstd::iter::details::InPlaceTraits<decltype(mtp::declval<InPlaceProbeRoot>().skip_while(
        mtp::declval<InPlacePredicateProbe>()))>::ENABLED);
static_assert(
    rstd::iter::details::InPlaceTraits<decltype(mtp::declval<InPlaceProbeRoot>().map_while(
        mtp::declval<InPlaceOptionProbe>()))>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeInspect>::ENABLED);
static_assert(rstd::iter::details::InPlaceTraits<InPlaceProbeScan>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<UnknownInPlaceProbe>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().peekable())>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().fuse())>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().step_by(usize(2)))>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().rev())>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<decltype(mtp::declval<InPlaceProbeRoot>().chain(
                  mtp::declval<InPlaceProbeRoot>()))>::ENABLED);
static_assert(! rstd::iter::details::InPlaceTraits<
              decltype(mtp::declval<InPlaceProbeRoot>().intersperse(int()))>::ENABLED);

struct InPlaceAccess {
    template<typename T, typename A>
    static auto allocation_base(VecIntoIter<T, A>& source) noexcept {
        return source.buffer_.ptr.as_mut_ptr().as_raw_ptr();
    }

    template<typename T, typename A>
    static auto allocation_layout(VecIntoIter<T, A>& source) noexcept -> Layout {
        return source.buffer_.allocation_layout;
    }

    template<typename T, typename A>
    static auto allocation_bytes(VecIntoIter<T, A>& source) noexcept -> byte* {
        return reinterpret_cast<byte*>(allocation_base(source));
    }

    template<typename T, typename A>
    static auto read_front(VecIntoIter<T, A>& source) noexcept -> usize {
        return source.front_;
    }

    template<typename Destination, typename Source, typename A>
    static auto finish(VecIntoIter<Source, A>& source, usize length) -> Vec<Destination, A> {
        source.drop_remaining();
        auto source_buffer = source.take_buffer();
        if (source_buffer.allocation_layout.size == usize()) {
            debug_assert(length == usize());
            return Vec<Destination, A>::new_in(rstd::move(source_buffer.allocator));
        }

        using DestinationStorage = typename mut_ptr<Destination>::storage_type;
        auto capacity = source_buffer.allocation_layout.size / usize(sizeof(DestinationStorage));
        debug_assert(length <= capacity);

        auto destination_buffer =
            RawVec<Destination, A>(source_buffer.ptr.template cast<Destination>(),
                                   capacity,
                                   source_buffer.allocation_layout,
                                   rstd::move(source_buffer.allocator));
        source_buffer.reset_ptr();
        return Vec<Destination, A>(rstd::move(destination_buffer), length);
    }
};

template<typename T>
class InPlaceDestinationGuard {
    byte* allocation_;
    usize length_;

    auto slot(usize index) const noexcept -> mut_ptr<T> {
        using Storage = typename mut_ptr<T>::storage_type;
        auto* pointer =
            reinterpret_cast<Storage*>(allocation_ + index.to_primitive() * sizeof(Storage));
        return mut_ptr<T>::from_raw_parts(pointer);
    }

public:
    explicit InPlaceDestinationGuard(byte* allocation): allocation_(allocation), length_() {}

    InPlaceDestinationGuard(const InPlaceDestinationGuard&)                    = delete;
    auto operator=(const InPlaceDestinationGuard&) -> InPlaceDestinationGuard& = delete;

    ~InPlaceDestinationGuard() {
        for (auto index = usize(); index < length_; ++index) {
            rstd::ptr_::destroy(slot(index));
        }
    }

    void push(T&& value) {
        rstd::ptr_::construct(slot(length_), rstd::move(value));
        ++length_;
    }

    auto len() const noexcept -> usize { return length_; }
    void release() noexcept { length_ = usize(); }
};

template<typename Destination, typename I>
constexpr bool in_place_collectible() {
    using Traits = rstd::iter::details::InPlaceTraits<I>;
    if constexpr (! Traits::ENABLED) {
        return false;
    } else {
        using Source        = typename Traits::Source::Item;
        using SourceStorage = typename mut_ptr<Source>::storage_type;
        using DestStorage   = typename mut_ptr<Destination>::storage_type;

        constexpr auto merge   = Traits::MERGE_BY.to_primitive();
        constexpr auto expand  = Traits::EXPAND_BY.to_primitive();
        constexpr auto maximum = usize::MAX.to_primitive();
        if constexpr (merge == 0 || expand == 0 || alignof(SourceStorage) != alignof(DestStorage)) {
            return false;
        } else if constexpr (merge > maximum / sizeof(SourceStorage) ||
                             expand > maximum / sizeof(DestStorage)) {
            return false;
        } else {
            return sizeof(SourceStorage) * merge >= sizeof(DestStorage) * expand;
        }
    }
}

template<typename T, typename I>
auto from_iter_in_place(I iterator) {
    using Traits        = rstd::iter::details::InPlaceTraits<I>;
    using Source        = typename Traits::Source::Item;
    using SourceStorage = typename mut_ptr<Source>::storage_type;
    using DestStorage   = typename mut_ptr<T>::storage_type;

    auto& source      = Traits::source(iterator);
    auto* source_base = InPlaceAccess::allocation_base(source);
    auto  layout      = InPlaceAccess::allocation_layout(source);
    auto* destination = InPlaceAccess::allocation_bytes(source);
    auto  capacity    = layout.size / usize(sizeof(DestStorage));

    InPlaceDestinationGuard<T> guard(destination);
    for (;;) {
        auto item = rstd::as<rstd::iter::Iterator>(iterator).next();
        if (item.is_none()) break;

        debug_assert(guard.len() < capacity, "InPlaceIterable exceeded destination capacity");
        auto next_length = guard.len() + usize(1);
        auto write_bytes = next_length * usize(sizeof(DestStorage));
        auto read_bytes  = InPlaceAccess::read_front(source) * usize(sizeof(SourceStorage));
        debug_assert(write_bytes <= read_bytes,
                     "InPlaceIterable write cursor advanced beyond source read cursor");
        guard.push(rstd::move(*item));
    }

    debug_assert(source_base == InPlaceAccess::allocation_base(source),
                 "SourceIter changed its allocation");
    auto result = InPlaceAccess::finish<T>(source, guard.len());
    guard.release();
    return result;
}

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
    if constexpr (in_place_collectible<T, I>()) {
        return from_iter_in_place<T>(rstd::move(iterator));
    } else if constexpr (rstd::Impled<I, rstd::iter::TrustedLen>) {
        return from_trusted_iter<T>(rstd::move(iterator));
    } else {
        auto first = rstd::as<rstd::iter::Iterator>(iterator).next();
        if (first.is_none()) return Vec<T>::make();

        auto lower =
            rstd::as<rstd::iter::Iterator>(iterator).size_hint().template get<0>().saturating_add(
                usize(1));
        auto initial_capacity = lower < RawVec<T, ::alloc::Global>::MIN_NON_ZERO_CAP
                                    ? RawVec<T, ::alloc::Global>::MIN_NON_ZERO_CAP
                                    : lower;
        auto result           = Vec<T>::with_capacity(initial_capacity);
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

template<typename T, typename A>
auto from_iter(VecIntoIter<T, A> iterator) -> Vec<T, A> {
    auto remaining = iterator.len();
    if (iterator.front_ == usize()) {
        return Vec<T, A>(iterator.take_buffer(), remaining);
    }

    const auto compact = [&]() -> Vec<T, A> {
        auto pointer = iterator.buffer_.ptr.as_mut_ptr();
        for (auto index = usize(); index < remaining; ++index) {
            auto source      = pointer.add(iterator.front_ + index);
            auto destination = pointer.add(index);
            auto value       = rstd::ptr_::move_out(source);
            rstd::ptr_::construct(destination, rstd::move(value));
            rstd::ptr_::destroy(source);
        }
        iterator.front_ = iterator.back_;
        return Vec<T, A>(iterator.take_buffer(), remaining);
    };

    if constexpr (! requires(const A& allocator) { A(allocator); }) {
        return compact();
    } else if (remaining >= iterator.buffer_.cap / usize(2)) {
        return compact();
    }

    if constexpr (requires(const A& allocator) { A(allocator); }) {
        auto result = Vec<T, A>::new_in(A(iterator.buffer_.allocator));
        result.reserve(remaining);
        for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
            result.push(rstd::move(*item));
        }
        return result;
    }
}

} // namespace details

} // namespace alloc::vec

namespace rstd
{
template<typename E, typename A>
struct Impl<ops::Deref, ::alloc::vec::Vec<E, A>> : ImplBase<::alloc::vec::Vec<E, A>> {
    using Target = E[];

    constexpr auto deref() const noexcept -> ref<Target> { return this->self().deref(); }
};

template<typename E, typename A>
struct Impl<ops::DerefMut, ::alloc::vec::Vec<E, A>> : ImplBase<::alloc::vec::Vec<E, A>> {
    constexpr auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<::alloc::vec::Vec<E, A>>> {
        return this->self().deref_mut();
    }
};

template<typename U, typename A, mtp::same_as<cmp::PartialEq<::alloc::vec::Vec<U, A>>> T>
struct Impl<T, ::alloc::vec::Vec<U, A>> : DefaultInImpl<T, ::alloc::vec::Vec<U, A>> {
    auto eq(const ::alloc::vec::Vec<U, A>& other) const noexcept -> bool {
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

template<typename E, typename A>
struct Impl<iter::Extend<E>, ::alloc::vec::Vec<E, A>> : ImplBase<::alloc::vec::Vec<E, A>> {
    template<iter::has_next It>
    static void extend(::alloc::vec::Vec<E, A>& collection, It iterator) {
        collection.reserve(as<iter::Iterator>(iterator).size_hint().template get<0>());
        for (auto item = as<iter::Iterator>(iterator).next(); item.is_some();
             item      = as<iter::Iterator>(iterator).next())
            collection.push(rstd::move(*item));
    }

    static void extend_one(::alloc::vec::Vec<E, A>& collection, E&& item) {
        collection.push(rstd::move(item));
    }
};

template<typename E, typename A>
struct Impl<iter::IntoIterator, ::alloc::vec::Vec<E, A>> : ImplBase<::alloc::vec::Vec<E, A>> {
    using IntoIter = ::alloc::vec::VecIntoIter<E, A>;

    auto into_iter() -> IntoIter { return rstd::move(this->self()).into_iter(); }
};

template<typename E, typename A>
struct Impl<iter::IntoIterator, ref<::alloc::vec::Vec<E, A>>>
    : ImplBase<ref<::alloc::vec::Vec<E, A>>> {
    using IntoIter = iter::SliceIter<E>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

template<typename E, typename A>
struct Impl<iter::IntoIterator, mut_ref<::alloc::vec::Vec<E, A>>>
    : ImplBase<mut_ref<::alloc::vec::Vec<E, A>>> {
    using IntoIter = iter::SliceIterMut<E>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter_mut(); }
};

} // namespace rstd
