module;
#include <rstd/macro.hpp>

export module rstd.alloc:vec;
export import :boxed;
export import :collections.error;
export import :alloc;
import :sync;
export import rstd.core;

using rstd::mem::MaybeUninit;

using ::alloc::boxed::Box;

using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;

using namespace rstd::prelude;

using alloc::collections::TryReserveError;

/// A low-level utility for managing the backing storage of a `Vec`.
template<typename T, typename A>
struct RawVec {
    mut_ptr<byte>                 ptr;
    usize                         cap;
    Layout                        allocation_layout;
    RSTD_ATTR_NO_UNIQUE_ADDRESS A allocator;

    static constexpr usize MIN_NON_ZERO_CAP =
        sizeof(typename mut_ptr<T>::storage_type) == 1
            ? usize(8)
            : (sizeof(typename mut_ptr<T>::storage_type) <= 1024 ? usize(4) : usize(1));

    constexpr explicit RawVec(A allocator)
        : ptr(), cap(), allocation_layout(), allocator(rstd::move(allocator)) {}

    constexpr RawVec(mut_ptr<byte> ptr, usize cap, Layout layout, A allocator)
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

    auto ptr_at(usize index = usize()) const noexcept -> mut_ptr<T> {
        return ptr.add(index * usize(sizeof(typename mut_ptr<T>::storage_type))).template cast<T>();
    }

    auto alignment() const noexcept -> usize {
        auto natural = Layout::make<T>().align;
        return allocation_layout.align > natural ? allocation_layout.align : natural;
    }

    auto layout_for_capacity(usize capacity) const -> Option<Layout> {
        auto layout = Layout::array<T>(capacity);
        if (layout.is_none()) return None();
        return Layout::from_size_align(layout->size, alignment());
    }

    static auto with_capacity(usize capacity,
                              A     allocator,
                              usize alignment = Layout::make<T>().align) -> RawVec {
        auto layout =
            Layout::from_size_align(Layout::array<T>(capacity).unwrap().size, alignment).unwrap();
        if (capacity == usize()) return RawVec({}, capacity, layout, rstd::move(allocator));
        auto res = as<Allocator>(allocator).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().template as_mut_ptr<byte>();
        return RawVec(p, capacity, layout, rstd::move(allocator));
    }

    /// Reallocates the storage to a new capacity.
    auto try_grow(usize new_cap, usize len) -> Result<empty, TryReserveError> {
        if (new_cap <= cap) return Ok(empty {});

        auto checked_layout = layout_for_capacity(new_cap);
        if (checked_layout.is_none()) return Err(TryReserveError::CapacityOverflow);
        auto new_layout = checked_layout.unwrap_unchecked();

        if (allocation_layout.size == usize()) {
            auto res = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);
            ptr = res.unwrap_unchecked().template as_mut_ptr<byte>();
        } else if constexpr (mtp::triv_copyable<T>) {
            auto old_layout = allocation_layout;
            auto old_ptr    = ptr;

            auto res = as<Allocator>(allocator).grow(old_ptr.as_raw_ptr(), old_layout, new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);

            ptr = res.unwrap_unchecked().template as_mut_ptr<byte>();
        } else {
            auto old_layout = allocation_layout;
            auto old_ptr    = ptr.as_raw_ptr();
            auto res        = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<byte>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                auto offset = index * sizeof(typename mut_ptr<T>::storage_type);
                auto source = mut_ptr<byte>::from_raw_parts(old_ptr + offset).template cast<T>();
                auto target = mut_ptr<byte>::from_raw_parts(new_ptr + offset).template cast<T>();
                rstd::construct_at(target.as_raw_ptr(), rstd::move(source.get()));
                rstd::ptr_::destroy(source);
            }
            as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            ptr = mut_ptr<byte>::from_raw_parts(new_ptr);
        }
        cap               = new_cap;
        allocation_layout = new_layout;
        return Ok(empty {});
    }

    void grow(usize new_cap, usize len) {
        if (try_grow(new_cap, len).is_err()) rstd::panic { "Vec allocation failed" };
    }

    auto try_shrink_to(usize capacity, usize len, bool preserve_alignment = true)
        -> Result<empty, TryReserveError> {
        auto old_layout = allocation_layout;
        auto new_layout =
            (preserve_alignment ? layout_for_capacity(capacity) : Layout::array<T>(capacity))
                .unwrap();
        if (old_layout.size == new_layout.size && old_layout.align == new_layout.align) {
            cap = capacity;
            return Ok(empty {});
        }
        debug_assert(len <= capacity && capacity <= cap);

        auto old_ptr = ptr.as_raw_ptr();
        if (capacity == usize()) {
            if (old_layout.size != usize())
                as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            reset_ptr();
            allocation_layout = new_layout;
            return Ok(empty {});
        }

        if constexpr (mtp::triv_copyable<T>) {
            auto res = as<Allocator>(allocator).shrink(old_ptr, old_layout, new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);
            ptr = res.unwrap_unchecked().template as_mut_ptr<byte>();
        } else {
            auto res = as<Allocator>(allocator).allocate(new_layout);
            if (res.is_err()) return Err(TryReserveError::AllocError);

            auto new_ptr = res.unwrap_unchecked().template as_mut_ptr<byte>().as_raw_ptr();
            for (rstd::size_t index = 0; index < len.to_primitive(); ++index) {
                auto offset = index * sizeof(typename mut_ptr<T>::storage_type);
                auto source = mut_ptr<byte>::from_raw_parts(old_ptr + offset).template cast<T>();
                auto target = mut_ptr<byte>::from_raw_parts(new_ptr + offset).template cast<T>();
                rstd::construct_at(target.as_raw_ptr(), rstd::move(source.get()));
                rstd::ptr_::destroy(source);
            }
            as<Allocator>(allocator).deallocate(old_ptr, old_layout);
            ptr = mut_ptr<byte>::from_raw_parts(new_ptr);
        }
        cap               = capacity;
        allocation_layout = new_layout;
        return Ok(empty {});
    }

    void shrink_to(usize capacity, usize len) {
        if (try_shrink_to(capacity, len).is_err()) rstd::panic { "Vec allocation failed" };
    }

    void shrink_to_fit(usize len) { shrink_to(len, len); }

    ~RawVec() {}

    void reset_ptr() {
        ptr               = {};
        cap               = usize();
        allocation_layout = {};
    }

    void drop() {
        if (ptr != nullptr) {
            debug_assert(allocation_layout.size > usize());
            as<Allocator>(allocator).deallocate(ptr.as_raw_ptr(), allocation_layout);
        }
        reset_ptr();
    }
};

namespace alloc::vec
{

/// Scalars whose initialized native representation can be owned as writable bytes.
/// Padded aggregates, pointers, bool, and extended-precision floats are excluded.
export template<typename T>
concept ByteConvertible =
    mtp::same_as<T, mtp::rm_cvf<T>> && mtp::triv_copyable<T> &&
    (mtp::any<T,
              byte,
              u8,
              u16,
              u32,
              u64,
              u128,
              usize,
              i8,
              i16,
              i32,
              i64,
              i128,
              isize,
              f32,
              f64,
              float,
              double> ||
     (rstd::num::PrimitiveInteger<T> && mtp::has_unique_object_representations<T>));

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
    mut_ptr<byte> slot_;

    constexpr explicit SpareSlot(mut_ptr<T> slot) noexcept: slot_(slot.template cast<byte>()) {}

    template<typename>
    friend class SpareCapacity;

public:
    template<typename... Args>
    constexpr decltype(auto) write(Args&&... args) {
        auto value = slot_.template cast<T>();
        rstd::ptr_::construct(value, rstd::forward<Args>(args)...);
        return value.as_mut_ref().get_mut();
    }
};

/// A writable view over uninitialized vector capacity.
export template<typename T>
class SpareCapacity {
    mut_ptr<byte> pointer_;
    usize         length_ {};

    constexpr SpareCapacity(mut_ptr<T> pointer, usize length) noexcept
        : pointer_(pointer.template cast<byte>()), length_(length) {}

    template<typename, typename>
    friend class Vec;

public:
    constexpr SpareCapacity() noexcept = default;
    /// Borrows byte spare capacity as slots, without reading or clearing its contents.
    /// Publish only the written prefix with Vec::set_len_unchecked().
    auto as_uninit() const noexcept -> mut_ref<MaybeUninit<byte>[]>
        requires(mtp::same_as<T, u8> || mtp::same_as<T, byte>)
    {
        return mut_ref<MaybeUninit<byte>[]>::from_bytes(pointer_.as_raw_ptr(), length_);
    }

    constexpr auto len() const noexcept -> usize { return length_; }
    constexpr auto is_empty() const noexcept -> bool { return length_ == usize(); }

    constexpr auto operator[](usize index) const -> SpareSlot<T> {
        if (index >= length_) rstd::panic { "Vec spare capacity index out of bounds" };
        return SpareSlot<T>(pointer_.add(index * usize(sizeof(typename mut_ptr<T>::storage_type)))
                                .template cast<T>());
    }
};

/// A contiguous growable array type, analogous to Rust's `Vec<T>`.
/// \tparam T The element type, which must be `Sized`.
export template<typename T, typename A>
class Vec {
    RawVec<T, A> m_buf;
    usize        m_len;

    friend struct rstd::Impl<rstd::iter::Extend<T>, Vec>;
    template<typename, typename>
    friend struct VecIntoIter;

    // The caller retires the source range after its elements have been transferred.
    void append_moved(mut_ptr<T> source, usize count) {
        if (count == usize()) return;
        reserve(count);
        auto destination = m_buf.ptr_at(m_len);
        if constexpr (Impled<T, rstd::Copy>) {
            rstd::ptr_::copy_nonoverlapping(source.as_ptr(), destination, count);
            m_len += count;
        } else {
            for (usize index; index < count; ++index) {
                auto slot =
                    source.byte_add(index * usize(sizeof(typename mut_ptr<T>::storage_type)));
                rstd::ptr_::construct(
                    destination.byte_add(index * usize(sizeof(typename mut_ptr<T>::storage_type))),
                    rstd::ptr_::move_out(slot));
                ++m_len;
                rstd::ptr_::destroy(slot);
            }
        }
    }

    template<typename SourceAllocator>
    void extend_iter(VecIntoIter<T, SourceAllocator> iterator) {
        iterator.append_to(*this);
    }

    template<typename I>
        requires(mtp::same_as<I, rstd::iter::SliceIter<T>> ||
                 mtp::same_as<I, rstd::iter::SliceIterMut<T>>) &&
                Impled<T, rstd::Copy>
    void extend_iter(rstd::iter::Copied<I> iterator) {
        auto source = rstd::move(iterator).into_inner();
        extend_from_slice(source.as_slice());
    }

    template<typename I>
        requires(mtp::same_as<I, rstd::iter::SliceIter<T>> ||
                 mtp::same_as<I, rstd::iter::SliceIterMut<T>>) &&
                Impled<T, Clone>
    void extend_iter(rstd::iter::Cloned<I> iterator) {
        auto source = rstd::move(iterator).into_inner();
        extend_from_slice(source.as_slice());
    }

    template<rstd::iter::has_next I>
    void extend_iter(I iterator) {
        if constexpr (rstd::Impled<I, rstd::iter::TrustedLen>) {
            auto hint  = rstd::as<rstd::iter::Iterator>(iterator).size_hint();
            auto upper = hint.template get<1>();
            if (upper.is_none()) rstd::panic { "Vec capacity overflow" };
            debug_assert(hint.template get<0>() == *upper);
            reserve(*upper);
            struct InitializedLength {
                usize& target;
                usize  value;
                ~InitializedLength() { target = value; }
            } initialized { m_len, m_len };
            auto  destination = m_buf.ptr_at(m_len);
            usize written;
            rstd::as<rstd::iter::Iterator>(iterator).for_each([&](T item) {
                debug_assert(written < *upper, "TrustedLen exceeded its length");
                rstd::ptr_::construct(
                    destination.byte_add(written *
                                         usize(sizeof(typename mut_ptr<T>::storage_type))),
                    rstd::move(item));
                ++written;
                ++initialized.value;
            });
            debug_assert(written == *upper, "TrustedLen ended before its length");
        } else {
            for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
                if (m_len == m_buf.cap) {
                    auto additional = rstd::as<rstd::iter::Iterator>(iterator)
                                          .size_hint()
                                          .template get<0>()
                                          .saturating_add(usize(1));
                    reserve(additional);
                }
                rstd::ptr_::construct(m_buf.ptr_at(m_len), rstd::move(*item));
                ++m_len;
            }
        }
    }

    auto take_boxed_slice() -> Box<T[], A> {
        auto length = m_len;
        auto raw    = length == usize() ? NonNull<T>::dangling().as_mut_ptr() : m_buf.ptr_at();
        m_buf.reset_ptr();
        m_len = usize();
        return Box<T[], A>::from_raw_in(mut_ptr<T[]>::from_raw_parts(raw.as_raw_ptr(), length),
                                        rstd::move(m_buf.allocator));
    }

    constexpr explicit Vec(RawVec<T, A> buf, usize len): m_buf(rstd::move(buf)), m_len(len) {}

    template<typename, typename>
    friend class Vec;

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

    /// Allocates byte storage with an alignment retained by growth and shrinking.
    static auto with_capacity_aligned_in(usize capacity, usize alignment, A allocator) -> Self
        requires mtp::same_as<T, u8>
    {
        return Vec { RawVec<T, A>::with_capacity(capacity, rstd::move(allocator), alignment),
                     usize() };
    }

    /// Transfers initialized native representations without copying or changing the allocator.
    /// Length and capacity become byte counts; the original allocation layout is retained.
    /// This does not serialize values or provide a typed view of the resulting bytes.
    auto into_bytes() && -> Vec<u8, A>
        requires ByteConvertible<T>
    {
        constexpr auto width  = usize(sizeof(typename mut_ptr<T>::storage_type));
        auto           layout = m_buf.allocation_layout;
        layout.align          = m_buf.alignment();
        auto result           = Vec<u8, A>(
            RawVec<u8, A>(m_buf.ptr, m_buf.cap * width, layout, rstd::move(m_buf.allocator)),
            m_len * width);
        m_buf.reset_ptr();
        m_len = usize();
        return result;
    }

    template<rstd::iter::has_next I>
    static auto from_iter_in(I iterator, A allocator) -> Self {
        auto result = Self::new_in(rstd::move(allocator));
        result.extend_iter(rstd::move(iterator));
        return result;
    }

    /// Moves the allocator into the Arc control block; the vector borrows it until destruction.
    /// The element storage, capacity, and allocation layout are preserved.
    auto into_arc() && -> ::alloc::sync::Arc<Vec<T, ref<dyn<Allocator>>>>
        requires Impled<A, Allocator, rstd::alloc::ThreadSafeAllocator>
    {
        using Borrowed = ref<dyn<Allocator>>;
        using Buffer   = Vec<T, Borrowed>;
        return ::alloc::sync::Arc<Buffer>::make_in_with(
            rstd::move(m_buf.allocator), [&](Borrowed allocator) noexcept {
                auto result = Buffer(
                    RawVec<T, Borrowed>(m_buf.ptr, m_buf.cap, m_buf.allocation_layout, allocator),
                    m_len);
                m_buf.reset_ptr();
                m_len = usize();
                return result;
            });
    }

    /// Takes ownership of a boxed slice without copying its elements.
    static auto from_boxed_slice(Box<T[], A>&& values) -> Self {
        auto parts     = rstd::move(values).into_raw_with_allocator();
        auto raw       = parts.template get<0>();
        auto allocator = rstd::move(parts.template get<1>());
        auto length    = raw.len();
        if (length == usize()) {
            return Vec::new_in(rstd::move(allocator));
        }
        auto pointer = mut_ptr<T>::from_raw_parts(raw.as_raw_ptr());
        return Vec { RawVec<T, A>(pointer.template cast<byte>(),
                                  length,
                                  Layout::array<T>(length).unwrap(),
                                  rstd::move(allocator)),
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
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<T> {
        if (m_len == usize()) return {};
        return slice<T>::from_raw_parts(m_buf.ptr_at().as_ptr().as_raw_ptr(), m_len);
    }

    /// Returns a mutable slice containing the entire vector.
    /// \return A mutable pointer to a slice over all elements.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto as_mut_slice() noexcept [[clang::lifetimebound]] -> mut_ptr<T[]> {
        if (m_len == usize()) return {};
        return mut_ptr<T[]>::from_raw_parts(m_buf.ptr_at().as_raw_ptr(), m_len);
    }

    constexpr auto deref() const noexcept [[clang::lifetimebound]] -> ref<T[]> {
        return as_slice();
    }

    constexpr auto deref_mut() noexcept [[clang::lifetimebound]] -> mut_ref<T[]> {
        return as_mut_slice().as_mut_ref();
    }

    /// Borrows an endpoint without removing it; empty vectors return None.
    /// The borrow is invalidated by destruction, reallocation, or removal of the element.
    constexpr auto first() const noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        return as_slice().first();
    }
    constexpr auto last() const noexcept [[clang::lifetimebound]] -> Option<ref<T>> {
        return as_slice().last();
    }
    constexpr auto first_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        return as_mut_slice().as_mut_ref().first_mut();
    }
    constexpr auto last_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<T>> {
        return as_mut_slice().as_mut_ref().last_mut();
    }

    /// Returns a const pointer to the first element of the vector.
    /// \return A const pointer to the underlying buffer.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return m_buf.ptr_at().as_ptr();
    }

    /// Returns a mutable pointer to the first element of the vector.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto as_mut_ptr() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return m_buf.ptr_at();
    }

    /// Returns the initialized contiguous storage as a raw pointer.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto data() noexcept [[clang::lifetimebound]] {
        return m_buf.ptr_at().as_raw_ptr();
    }

    /// Returns the initialized contiguous storage as a raw pointer.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr auto data() const noexcept [[clang::lifetimebound]] {
        return m_buf.ptr_at().as_ptr().as_raw_ptr();
    }

    /// Returns writable spare capacity after the initialized range.
    ///
    /// The returned memory is uninitialized. After writing initialized values into it, callers must
    /// publish the written length with `set_len_unchecked`.
    constexpr auto spare_capacity_mut() noexcept [[clang::lifetimebound]] -> SpareCapacity<T> {
        if (m_buf.cap == m_len) return {};
        return SpareCapacity<T>(m_buf.ptr_at(m_len), m_buf.cap - m_len);
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

    /// Failure returns the original vector, including its allocator and reserved capacity.
    auto try_into_boxed_slice() -> Result<Box<T[], A>, Vec> {
        // Box reconstructs its layout from T and length, so normalize any retained alignment.
        if (m_buf.try_shrink_to(m_len, m_len, false).is_err()) return Err(rstd::move(*this));
        return Ok(take_boxed_slice());
    }

    auto into_boxed_slice() -> Box<T[], A> {
        if (m_buf.try_shrink_to(m_len, m_len, false).is_err())
            rstd::panic { "Vec allocation failed" };
        return take_boxed_slice();
    }

    /// Constructs an element in-place at the back of the vector.
    template<typename... Args>
    constexpr decltype(auto) emplace_back(Args&&... args) {
        if (m_len == m_buf.cap) reserve(usize(1));
        auto slot = m_buf.ptr_at(m_len);
        rstd::ptr_::construct(slot, rstd::forward<Args>(args)...);
        ++m_len;
        return slot.as_mut_ref().get_mut();
    }

    /// Appends an element to the back of the vector by moving it.
    /// \param value The value to append.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr void push(T&& value) {
        (void)emplace_back(rstd::move(value));
    }

    /// Inserts before `index`, preserving order. Panics if `index > len()`.
    /// Takes O(len()) time; reallocation invalidates existing borrows.
    constexpr void insert(usize index, T&& value) { (void)insert_mut(index, rstd::move(value)); }

    /// Inserts and borrows the new element. Shifted slots no longer hold their original values.
    constexpr auto insert_mut(usize index, T&& value) [[clang::lifetimebound]] -> mut_ref<T> {
        if (index > m_len) {
            rstd::panic { "Vec insertion index {} exceeds length {}", index, m_len };
        }
        // The input may refer to an element invalidated by growth or shifting.
        T owned = rstd::move(value);
        if (m_len == m_buf.cap) reserve(usize(1));
        auto pointer = m_buf.ptr_at();
        for (auto current = m_len; current > index; --current) {
            auto source = pointer.byte_add((current - usize(1)) *
                                           usize(sizeof(typename mut_ptr<T>::storage_type)));
            rstd::ptr_::construct(
                pointer.byte_add((current)*usize(sizeof(typename mut_ptr<T>::storage_type))),
                rstd::ptr_::move_out(source));
            rstd::ptr_::destroy(source);
        }
        auto slot = pointer.byte_add(index * usize(sizeof(typename mut_ptr<T>::storage_type)));
        rstd::ptr_::construct(slot, rstd::move(owned));
        ++m_len;
        return slot.as_mut_ref();
    }

    /// Removes the last element from the vector and returns it, or `None` if empty.
    /// \return An `Option<T>` containing the removed element.
    constexpr auto pop() -> Option<T> {
        if (m_len == usize()) {
            return None();
        } else {
            --m_len;
            auto p     = m_buf.ptr_at(m_len);
            T    value = rstd::ptr_::move_out(p);
            rstd::ptr_::destroy(p);
            return Some(rstd::move(value));
        }
    }

    /// Appends a cloned copy of the element to the back of the vector.
    /// \param value The value to clone and append.
    [[clang::exclude_from_explicit_instantiation]]
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

    /// Moves all elements from `other`, preserving its capacity. Self-append panics.
    void append(Vec& other) {
        if (this == rstd::addressof(other)) rstd::panic { "Vec cannot append itself" };
        append_moved(other.m_buf.ptr_at(), other.m_len);
        other.m_len = usize();
    }

    /// Clones the original half-open range `[start, end)` onto the end.
    void extend_from_within(usize start, usize end)
        requires Impled<T, Clone>
    {
        if (start > end || end > m_len) rstd::panic { "Vec extend range out of bounds" };
        if (start == end) return;
        extend_from_slice(
            slice<T>::from_raw_parts(m_buf.ptr_at(start).as_ptr().as_raw_ptr(), end - start));
    }

    /// Initializes new elements with `generator`; shrinking never calls it.
    /// The generator must not structurally modify this vector through another reference.
    template<typename F>
        requires mtp::same_as<mtp::invoke_result_t<F&>, T>
    void resize_with(usize new_len, F generator) {
        if (new_len <= m_len) {
            truncate(new_len);
            return;
        }
        extend_iter(rstd::iter::repeat_with(rstd::move(generator)).take(new_len - m_len));
    }

    /// Appends a clone of all elements in `values`.
    void extend_from_slice(slice<T> values)
        requires Impled<T, Clone>
    {
        if (values.is_empty()) return;

        bool         source_is_self = false;
        rstd::size_t source_offset  = 0;
        if (m_len != usize()) {
            auto const buffer_address =
                reinterpret_cast<uintptr_t>(m_buf.ptr_at().as_ptr().as_raw_ptr());
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
            values = slice<T>::from_raw_parts(m_buf.ptr_at(usize(source_offset)).as_raw_ptr(),
                                              values.len());
        }

        auto destination = m_buf.ptr_at(m_len);
        if constexpr (Impled<T, rstd::Copy>) {
            rstd::ptr_::copy_nonoverlapping(
                ptr<T>::from_raw_parts(values.as_raw_ptr()), destination, values.len());
            m_len += values.len();
        } else {
            for (rstd::size_t index = 0; index < values.len().to_primitive(); ++index) {
                rstd::ptr_::construct(
                    destination.byte_add((usize(index)) *
                                         usize(sizeof(typename mut_ptr<T>::storage_type))),
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
    [[clang::exclude_from_explicit_instantiation]]
    constexpr decltype(auto) at(usize index) [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr_at(index).get();
    }
    /// Returns a const reference to the element at the given index, panicking if out of bounds.
    /// \param index The index of the element.
    /// \return A const reference to the element.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr decltype(auto) at(usize index) const [[clang::lifetimebound]] {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        return m_buf.ptr_at(index).as_ptr().get();
    }

    /// Indexes into the vector, panicking if out of bounds.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr decltype(auto) operator[](usize index) [[clang::lifetimebound]] {
        return at(index);
    }
    /// Indexes into the vector (const), panicking if out of bounds.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr decltype(auto) operator[](usize index) const [[clang::lifetimebound]] {
        return at(index);
    }

    /// Returns the number of elements in the vector.
    /// \return The length of the vector.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr usize len() const {
        return m_len;
    }
    /// Returns the number of elements the vector can hold without reallocating.
    /// \return The current capacity.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr usize capacity() const {
        return m_buf.cap;
    }
    auto           allocation_alignment() const noexcept -> usize { return m_buf.alignment(); }
    constexpr auto allocator() const noexcept [[clang::lifetimebound]] -> const A& {
        return m_buf.allocator;
    }
    /// Returns `true` if the vector contains no elements.
    [[clang::exclude_from_explicit_instantiation]]
    constexpr bool is_empty() const {
        return m_len == usize();
    }

    template<typename B>
    auto clone_in(B allocator) const -> Vec<T, B>
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        auto result = Vec<T, B>::with_capacity_in(m_len, rstd::move(allocator));
        result.extend_from_slice(as_slice());
        return result;
    }

    auto clone() const -> Vec
        requires rstd::Impled<T, rstd::clone::Clone> && rstd::alloc::CloneableAllocator<A>
    {
        return clone_in(rstd::alloc::clone_allocator(m_buf.allocator));
    }

    void clone_from(const Vec& source)
        requires rstd::Impled<T, rstd::clone::Clone>
    {
        if (this == rstd::addressof(source)) return;

        truncate(source.len());
        auto const initialized = m_len;
        rstd::slice_::clone_from_slice(
            mut_ref<T[]>::from_raw_parts(m_buf.ptr_at().as_raw_ptr(), initialized),
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
        auto values =
            mut_ptr<T[]>::from_raw_parts(m_buf.ptr_at(new_len).as_raw_ptr(), m_len - new_len);
        m_len = new_len;
        rstd::ptr_::drop_in_place(values);
    }

    /// Retains only the elements for which `predicate` returns true, preserving their order.
    template<typename F>
    constexpr void retain(F predicate) {
        auto               values  = m_buf.ptr_at();
        rstd::size_t       write   = 0;
        const rstd::size_t old_len = m_len.to_primitive();
        for (rstd::size_t read = 0; read < old_len; ++read) {
            auto read_ptr =
                values.byte_add((usize(read)) * usize(sizeof(typename mut_ptr<T>::storage_type)));
            if (predicate(read_ptr.as_ptr().get())) {
                if (write != read) {
                    rstd::ptr_::construct(
                        values.byte_add((usize(write)) *
                                        usize(sizeof(typename mut_ptr<T>::storage_type))),
                        rstd::ptr_::move_out(read_ptr));
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
            rstd::mem::memset(m_buf.ptr_at(old_len).as_raw_ptr(), source, count);
            m_len = new_len;
            return;
        } else {
            bool         value_is_self = false;
            rstd::size_t value_offset  = 0;
            if (m_len != usize()) {
                auto const buffer_address =
                    reinterpret_cast<uintptr_t>(m_buf.ptr_at().as_ptr().as_raw_ptr());
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
            auto p = m_buf.ptr_at();
            for (rstd::size_t index = old_len.to_primitive(); index < new_len.to_primitive();
                 ++index) {
                auto const* source = value_is_self ? m_buf.ptr_at(usize(value_offset)).as_raw_ptr()
                                                   : rstd::addressof(value);
                rstd::ptr_::construct(
                    p.byte_add((usize(index)) * usize(sizeof(typename mut_ptr<T>::storage_type))),
                    as<Clone>(*source).clone());
                ++m_len;
            }
        }
    }

    /// Removes and returns the element at the given index, shifting subsequent elements left.
    /// \param index The index of the element to remove.
    /// \return The removed element.
    constexpr T remove(usize index) {
        if (index >= m_len) rstd::panic { "Vec index out of bounds" };
        auto p     = m_buf.ptr_at();
        T    value = rstd::ptr_::move_out(
            p.byte_add(index * usize(sizeof(typename mut_ptr<T>::storage_type))));
        for (rstd::size_t current = index.to_primitive(); current + 1 < m_len.to_primitive();
             ++current) {
            rstd::ptr_::write(
                p.byte_add((usize(current)) * usize(sizeof(typename mut_ptr<T>::storage_type))),
                rstd::ptr_::move_out(p.byte_add((usize(current + 1)) *
                                                usize(sizeof(typename mut_ptr<T>::storage_type)))));
        }
        rstd::ptr_::destroy(
            p.byte_add((m_len - usize(1)) * usize(sizeof(typename mut_ptr<T>::storage_type))));
        --m_len;
        return value;
    }

    /// Returns a mutable iterator to the beginning.
    constexpr auto begin() noexcept [[clang::lifetimebound]] -> mut_ptr<T> { return as_mut_ptr(); }
    /// Returns a mutable iterator to the end.
    constexpr auto end() noexcept [[clang::lifetimebound]] -> mut_ptr<T> {
        return m_buf.ptr_at(m_len);
    }
    /// Returns a const iterator to the beginning.
    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> ptr<T> { return as_ptr(); }
    /// Returns a const iterator to the end.
    constexpr auto end() const noexcept [[clang::lifetimebound]] -> ptr<T> {
        return m_buf.ptr_at(m_len).as_ptr();
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

// Hot members instantiate at their call sites despite these shared instantiations.
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
        auto values =
            mut_ptr<T[]>::from_raw_parts(buffer_.ptr_at(front_).as_raw_ptr(), back_ - front_);
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
        auto source = buffer_.ptr_at(front_);
        T    value  = rstd::ptr_::move_out(source);
        rstd::ptr_::destroy(source);
        ++front_;
        return rstd::Some(rstd::move(value));
    }

    auto next_back() -> rstd::Option<Item> {
        if (front_ == back_) return rstd::None();
        --back_;
        auto source = buffer_.ptr_at(back_);
        T    value  = rstd::ptr_::move_out(source);
        rstd::ptr_::destroy(source);
        return rstd::Some(rstd::move(value));
    }

    auto size_hint() const -> rstd::iter::SizeHint {
        auto n = back_ - front_;
        return { n, rstd::Some(n) };
    }

    /// Moves the remaining elements into `destination`, retaining ownership of the allocation.
    template<typename DestinationAllocator>
    void append_to(Vec<T, DestinationAllocator>& destination) {
        destination.append_moved(buffer_.ptr_at(front_), len());
        front_ = back_;
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
        return source.buffer_.ptr_at().as_raw_ptr();
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

        auto destination_buffer = RawVec<Destination, A>(source_buffer.ptr,
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
    rstd::iter::extend(result, rstd::move(iterator));
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
        auto pointer = iterator.buffer_.ptr_at();
        for (auto index = usize(); index < remaining; ++index) {
            auto source = pointer.byte_add((iterator.front_ + index) *
                                           usize(sizeof(typename mut_ptr<T>::storage_type)));
            auto destination =
                pointer.byte_add(index * usize(sizeof(typename mut_ptr<T>::storage_type)));
            auto value = rstd::ptr_::move_out(source);
            rstd::ptr_::construct(destination, rstd::move(value));
            rstd::ptr_::destroy(source);
        }
        iterator.front_ = iterator.back_;
        return Vec<T, A>(iterator.take_buffer(), remaining);
    };

    if constexpr (! rstd::alloc::CloneableAllocator<A>) {
        return compact();
    } else if (remaining >= iterator.buffer_.cap / usize(2)) {
        return compact();
    }

    if constexpr (rstd::alloc::CloneableAllocator<A>) {
        auto result = Vec<T, A>::new_in(rstd::alloc::clone_allocator(iterator.buffer_.allocator));
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

template<typename E, typename A, mtp::same_as<From<::alloc::boxed::Box<E[], A>>> T>
struct Impl<T, ::alloc::vec::Vec<E, A>> : ImplBase<::alloc::vec::Vec<E, A>> {
    static auto from(::alloc::boxed::Box<E[], A> b) -> ::alloc::vec::Vec<E, A> {
        return ::alloc::vec::Vec<E, A>::from_boxed_slice(rstd::move(b));
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
        collection.extend_iter(rstd::move(iterator));
    }

    static void extend_one(::alloc::vec::Vec<E, A>& collection, E&& item) {
        collection.push(rstd::move(item));
    }
};

template<typename E, typename A>
    requires(! mtp::same_as<A, ::alloc::Global>)
struct Impl<iter::FromIterator<E>, ::alloc::vec::Vec<E, A>> {
    static auto from_iter(::alloc::vec::VecIntoIter<E, A> iterator) -> ::alloc::vec::Vec<E, A> {
        return ::alloc::vec::details::from_iter(rstd::move(iterator));
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
