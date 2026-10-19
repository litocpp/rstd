export module rstd:bytes;
export import rstd.core;
import rstd.alloc;

using ::alloc::vec::Vec;
using ::alloc::sync::Arc;
using ::alloc::sync::ArcAllocator;
using rstd::alloc::Allocator;
using rstd::alloc::ThreadSafeAllocator;
using namespace rstd::prelude;

namespace rstd::bytes
{

export template<typename T>
concept Buf = requires(T& b, usize n) {
    b.remaining();
    b.chunk();
    b.advance(n);
};

export template<typename T>
concept BufMut = requires(T& b, usize n, slice<u8> s) {
    b.remaining_mut();
    b.chunk_mut();
    b.advance_mut(n);
    b.put_slice(s);
};

using ByteBuffer  = Vec<u8, ref<dyn<Allocator>>>;
using ByteStorage = Arc<ByteBuffer>;

template<typename A>
    requires Impled<A, Allocator, ThreadSafeAllocator>
auto make_storage(usize capacity, A allocator, usize alignment = usize(1)) -> ByteStorage {
    return ByteStorage::make_in_with(
        rstd::move(allocator), [capacity, alignment](ref<dyn<Allocator>> borrowed) {
            return ByteBuffer::with_capacity_aligned_in(capacity, alignment, borrowed);
        });
}

export class BytesMut;

export class Bytes {
    Option<ByteStorage> m_storage;
    usize               m_pos;
    usize               m_end;

    friend class BytesMut;
    Bytes(ByteStorage storage, usize pos, usize end)
        : m_storage(Some(rstd::move(storage))), m_pos(pos), m_end(end) {}

public:
    Bytes()                                = default;
    Bytes(const Bytes&)                    = delete;
    auto operator=(const Bytes&) -> Bytes& = delete;
    Bytes(Bytes&& other) noexcept
        : m_storage(other.m_storage.take()),
          m_pos(rstd::exchange(other.m_pos, usize())),
          m_end(rstd::exchange(other.m_end, usize())) {}
    auto operator=(Bytes&& other) noexcept -> Bytes& {
        if (this != &other) {
            m_storage = other.m_storage.take();
            m_pos     = rstd::exchange(other.m_pos, usize());
            m_end     = rstd::exchange(other.m_end, usize());
        }
        return *this;
    }

    static auto make() -> Bytes { return {}; }
    template<typename A>
        requires Impled<A, Allocator, ThreadSafeAllocator>
    static auto new_in(A allocator) -> Bytes {
        return Bytes(make_storage(usize(), rstd::move(allocator)), usize(), usize());
    }
    template<typename T, typename A>
        requires ::alloc::vec::ByteConvertible<T> && Impled<A, Allocator, ThreadSafeAllocator>
    static auto from_vec(Vec<T, A>&& vec) -> Bytes {
        if constexpr (mtp::same_as<A, ::alloc::Global>) {
            if (vec.capacity() == usize() && vec.allocation_alignment() == usize(1)) return {};
        }
        auto bytes  = rstd::move(vec).into_bytes();
        auto length = bytes.len();
        return Bytes(rstd::move(bytes).into_arc(), usize(), length);
    }
    static auto copy_from_slice(slice<u8> src) -> Bytes {
        if (src.is_empty()) return {};
        return copy_from_slice_in(src, ::alloc::Global {});
    }
    template<typename A>
        requires Impled<A, Allocator, ThreadSafeAllocator>
    static auto copy_from_slice_in(slice<u8> src, A allocator) -> Bytes {
        auto storage = make_storage(src.len(), rstd::move(allocator));
        storage.deref_mut().get_mut().extend_from_slice(src);
        return Bytes(rstd::move(storage), usize(), src.len());
    }
    /// Reuses exclusively held storage without allocating or copying; failure returns this view.
    /// Only the visible range remains initialized. Allocator-only owners do not prevent reuse.
    auto try_into_mut() && -> Result<BytesMut, Bytes>;
    /// Shared storage is copied with its allocator and allocation alignment.
    auto into_mut() && -> BytesMut;
    auto clone() const -> Bytes {
        if (m_storage.is_none()) return {};
        return Bytes(m_storage->clone(), m_pos, m_end);
    }
    auto len() const noexcept -> usize { return m_end - m_pos; }
    auto size() const noexcept -> usize { return len(); }
    /// Capacity from the visible start, excluding the consumed prefix.
    auto capacity() const noexcept -> usize {
        return m_storage.is_some() ? (*m_storage)->capacity() - m_pos : usize();
    }
    /// Guaranteed alignment of the visible start; advancing may reduce it.
    auto alignment() const noexcept -> usize {
        auto alignment = m_storage.is_some() ? (*m_storage)->allocation_alignment() : usize(1);
        while (m_pos % alignment != usize()) alignment /= usize(2);
        return alignment;
    }
    auto allocator() const noexcept -> ArcAllocator {
        return m_storage.is_some() ? m_storage->allocator_owner() : ArcAllocator {};
    }
    auto is_empty() const noexcept -> bool { return len() == usize(); }
    auto data() const noexcept -> const byte* {
        auto* p = m_storage.is_some() ? (*m_storage)->data() : nullptr;
        return p == nullptr ? nullptr : p + m_pos.to_primitive();
    }
    auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        if (is_empty()) return {};
        return slice<u8>::from_raw_parts(data(), len());
    }
    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept [[clang::lifetimebound]] -> slice<u8> { return as_slice(); }
    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "Bytes::advance out of bounds" };
        m_pos += cnt;
        if (m_pos == m_end) clear();
    }
    void truncate(usize new_len) {
        if (new_len < len()) m_end = m_pos + new_len;
    }
    void clear() {
        m_pos = usize();
        m_end = usize();
    }
    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "Bytes index out of bounds" };
        return u8::from_byte(data()[index.to_primitive()]);
    }
};

export class BytesMut {
    Option<ByteStorage>    m_storage;
    usize                  m_pos;
    usize                  m_end;
    static constexpr usize DEFAULT_CHUNK_CAPACITY { 64 };

    friend class Bytes;
    explicit BytesMut(ByteStorage storage)
        : m_storage(Some(rstd::move(storage))), m_end((*m_storage)->len()) {}
    // Only Bytes can share the private Arc; writable access requires a single strong owner.
    auto buffer() noexcept -> ByteBuffer& { return m_storage->deref_mut().get_mut(); }
    auto buffer() const noexcept -> const ByteBuffer& { return m_storage->deref().get(); }
    void ensure_storage() {
        if (m_storage.is_none()) m_storage = Some(make_storage(usize(), ::alloc::Global {}));
    }
    void compact() {
        if (m_pos == usize()) return;
        auto length = len();
        if (length != usize()) __builtin_memmove(buffer().data(), data(), length.to_primitive());
        buffer().truncate(length);
        m_pos = usize();
        m_end = length;
    }

public:
    BytesMut()                                   = default;
    BytesMut(const BytesMut&)                    = delete;
    auto operator=(const BytesMut&) -> BytesMut& = delete;
    BytesMut(BytesMut&& other) noexcept
        : m_storage(other.m_storage.take()),
          m_pos(rstd::exchange(other.m_pos, usize())),
          m_end(rstd::exchange(other.m_end, usize())) {}
    auto operator=(BytesMut&& other) noexcept -> BytesMut& {
        if (this != &other) {
            m_storage = other.m_storage.take();
            m_pos     = rstd::exchange(other.m_pos, usize());
            m_end     = rstd::exchange(other.m_end, usize());
        }
        return *this;
    }
    static auto make() -> BytesMut { return {}; }
    template<typename A>
        requires Impled<A, Allocator, ThreadSafeAllocator>
    static auto new_in(A allocator) -> BytesMut {
        return BytesMut(make_storage(usize(), rstd::move(allocator)));
    }
    static auto with_capacity(usize capacity) -> BytesMut {
        if (capacity == usize()) return {};
        return with_capacity_in(capacity, ::alloc::Global {});
    }
    template<typename A>
        requires Impled<A, Allocator, ThreadSafeAllocator>
    static auto with_capacity_in(usize capacity, A allocator) -> BytesMut {
        return BytesMut(make_storage(capacity, rstd::move(allocator)));
    }
    template<typename T, typename A>
        requires ::alloc::vec::ByteConvertible<T> && Impled<A, Allocator, ThreadSafeAllocator>
    static auto from_vec(Vec<T, A>&& vec) -> BytesMut {
        if constexpr (mtp::same_as<A, ::alloc::Global>) {
            if (vec.capacity() == usize() && vec.allocation_alignment() == usize(1)) return {};
        }
        return BytesMut(rstd::move(vec).into_bytes().into_arc());
    }
    auto len() const noexcept -> usize { return m_end - m_pos; }
    auto size() const noexcept -> usize { return len(); }
    /// Capacity from the visible start, excluding the consumed prefix.
    auto capacity() const noexcept -> usize {
        return m_storage.is_some() ? buffer().capacity() - m_pos : usize();
    }
    /// Guaranteed alignment of the visible start; advancing may reduce it.
    auto alignment() const noexcept -> usize {
        auto alignment = m_storage.is_some() ? (*m_storage)->allocation_alignment() : usize(1);
        while (m_pos % alignment != usize()) alignment /= usize(2);
        return alignment;
    }
    auto allocator() const noexcept -> ArcAllocator {
        return m_storage.is_some() ? m_storage->allocator_owner() : ArcAllocator {};
    }
    auto is_empty() const noexcept -> bool { return len() == usize(); }
    auto data() noexcept -> byte* {
        auto* p = m_storage.is_some() ? buffer().data() : nullptr;
        return p == nullptr ? nullptr : p + m_pos.to_primitive();
    }
    auto data() const noexcept -> const byte* {
        auto* p = m_storage.is_some() ? buffer().data() : nullptr;
        return p == nullptr ? nullptr : p + m_pos.to_primitive();
    }
    auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        if (is_empty()) return {};
        return slice<u8>::from_raw_parts(data(), len());
    }
    auto as_mut_slice() noexcept [[clang::lifetimebound]] -> mut_ref<u8[]> {
        if (is_empty()) return {};
        return mut_ref<u8[]>::from_raw_parts(data(), len());
    }
    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept [[clang::lifetimebound]] -> slice<u8> { return as_slice(); }
    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "BytesMut::advance out of bounds" };
        m_pos += cnt;
        if (m_pos == m_end) clear();
    }
    /// Appendable tail capacity; reserve may reallocate and resize may compact the prefix.
    auto remaining_mut() const noexcept -> usize { return capacity() - len(); }
    auto chunk_mut() [[clang::lifetimebound]] -> mut_ref<u8[]> {
        if (remaining_mut() == usize()) reserve(DEFAULT_CHUNK_CAPACITY);
        if (buffer().len() < buffer().capacity()) buffer().resize(buffer().capacity(), u8 {});
        return mut_ref<u8[]>::from_raw_parts(buffer().data() + m_end.to_primitive(),
                                             buffer().len() - m_end);
    }
    void advance_mut(usize cnt) {
        auto available = m_storage.is_some() ? buffer().len() - m_end : usize();
        if (cnt > available) rstd::panic { "BytesMut::advance_mut out of bounds" };
        m_end += cnt;
    }
    void reserve(usize additional) {
        if (additional <= remaining_mut()) return;
        ensure_storage();
        buffer().reserve(m_end + additional - buffer().len());
    }
    void put_slice(slice<u8> src) {
        if (src.is_empty()) return;
        reserve(src.len());
        auto target = m_end + src.len();
        rstd::mem::memcpy(buffer().data() + m_end.to_primitive(), src.as_raw_ptr(), src.len());
        if (buffer().len() < target) buffer().set_len_unchecked(target);
        m_end = target;
    }
    void extend_from_slice(slice<u8> src) { put_slice(src); }
    void resize(usize new_len, u8 value) {
        if (new_len == usize()) {
            truncate(new_len);
            return;
        }
        ensure_storage();
        if (m_pos != usize() && new_len > len()) compact();
        auto old_end = m_end;
        auto new_end = m_pos + new_len;
        if (new_end > buffer().len())
            buffer().resize(new_end, value);
        else if (new_end > old_end)
            rstd::mem::memset(buffer().data() + old_end.to_primitive(), value, new_end - old_end);
        m_end = new_end;
        if (new_end < old_end) buffer().truncate(new_end);
    }
    void truncate(usize new_len) {
        if (new_len < len()) {
            m_end = m_pos + new_len;
            buffer().truncate(m_end);
        }
    }
    void clear() {
        if (m_storage.is_some()) buffer().clear();
        m_pos = usize();
        m_end = usize();
    }
    auto split_to(usize at) -> BytesMut {
        if (at > len()) rstd::panic { "BytesMut::split_to out of bounds" };
        auto out = BytesMut(make_storage(
            at, allocator(), m_storage.is_some() ? buffer().allocation_alignment() : usize(1)));
        out.extend_from_slice(slice<u8>::from_raw_parts(data(), at));
        advance(at);
        return out;
    }
    auto split() -> BytesMut { return split_to(len()); }
    auto freeze() -> Bytes {
        if (m_storage.is_none()) return {};
        buffer().truncate(m_end);
        return Bytes(m_storage.take().unwrap_unchecked(),
                     rstd::exchange(m_pos, usize()),
                     rstd::exchange(m_end, usize()));
    }
    auto operator[](usize index) -> mut_ref<u8> {
        if (index >= len()) rstd::panic { "BytesMut index out of bounds" };
        return mut_ref<u8>::from_raw_parts(data() + index.to_primitive());
    }
    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "BytesMut index out of bounds" };
        return u8::from_byte(data()[index.to_primitive()]);
    }
};
inline auto Bytes::try_into_mut() && -> Result<BytesMut, Bytes> {
    if (m_storage.is_none()) return Ok(BytesMut {});
    // Allocator handles retain only the control block and cannot upgrade to the byte value.
    if (m_storage->strong_count() != usize(1)) return Err(rstd::move(*this));
    BytesMut result(m_storage.take().unwrap_unchecked());
    result.m_pos = rstd::exchange(m_pos, usize());
    result.m_end = rstd::exchange(m_end, usize());
    result.buffer().truncate(result.m_end);
    return Ok(rstd::move(result));
}

inline auto Bytes::into_mut() && -> BytesMut {
    auto result = rstd::move(*this).try_into_mut();
    if (result.is_ok()) return rstd::move(result).unwrap();
    auto source = rstd::move(result).unwrap_err();
    auto storage =
        make_storage(source.len(), source.allocator(), (*source.m_storage)->allocation_alignment());
    BytesMut writable(rstd::move(storage));
    writable.put_slice(source.as_slice());
    return writable;
}
} // namespace rstd::bytes

template<>
struct rstd::Impl<rstd::clone::Clone, rstd::bytes::Bytes>
    : rstd::DefaultInImpl<rstd::clone::Clone, rstd::bytes::Bytes> {
    auto clone() const -> rstd::bytes::Bytes { return this->self().clone(); }
};
