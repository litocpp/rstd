export module rstd:bytes;
export import rstd.core;
import rstd.alloc;

using ::alloc::vec::Vec;
using rstd::mem::MaybeUninit;
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

struct BytesProperties {
    usize capacity;
    usize alignment;
    usize allocation_alignment;
};

struct BytesStorage {
    template<typename Self, typename = void>
    struct Api {
        using Trait = BytesStorage;
        void retain() const noexcept { rstd::trait_call<0>(this); }
        void release() const noexcept { rstd::trait_call<1>(this); }
        auto is_unique() const noexcept -> bool { return rstd::trait_call<2>(this); }
        auto properties(slice<u8> view) const noexcept -> BytesProperties {
            return rstd::trait_call<3>(this, view);
        }
        auto allocator() const noexcept -> ArcAllocator { return rstd::trait_call<4>(this); }
        auto empty_view() const noexcept -> slice<u8> { return rstd::trait_call<5>(this); }
        auto try_into_mut(slice<u8> view) const -> Option<BytesMut>;
    };
    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::retain,
                                   &T::release,
                                   &T::is_unique,
                                   &T::properties,
                                   &T::allocator,
                                   &T::empty_view,
                                   &T::try_into_mut>;
};

struct StaticBytesStorage {
    const byte* base {};
    void        retain() const noexcept {}
    void        release() const noexcept {}
    auto        is_unique() const noexcept -> bool { return false; }
    auto        properties(slice<u8> view) const noexcept -> BytesProperties {
        return { view.len(), usize(1), usize(1) };
    }
    auto allocator() const noexcept -> ArcAllocator { return {}; }
    auto empty_view() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(base, usize());
    }
    auto try_into_mut(slice<u8>) const -> Option<BytesMut>;
};

inline const StaticBytesStorage EMPTY_BYTES_STORAGE {};
inline auto                     empty_bytes_storage() noexcept -> ptr<dyn<BytesStorage>> {
    return dyn<BytesStorage>::from_ptr(&EMPTY_BYTES_STORAGE);
}

} // namespace rstd::bytes

template<>
struct rstd::Impl<rstd::bytes::BytesStorage, rstd::bytes::ByteBuffer>
    : rstd::ImplBase<rstd::bytes::ByteBuffer> {
    using Buffer  = rstd::bytes::ByteBuffer;
    using Storage = rstd::bytes::ByteStorage;
    auto pointer() const noexcept -> mut_ptr<Buffer> {
        return mut_ptr<Buffer>::from_raw_parts(const_cast<Buffer*>(rstd::addressof(this->self())));
    }
    auto offset(slice<u8> view) const noexcept -> usize {
        auto* base  = this->self().data();
        auto* start = view.as_raw_ptr();
        return base == start ? usize() : usize(start - base);
    }
    void retain() const noexcept { Storage::increment_strong_count(pointer()); }
    void release() const noexcept { Storage::decrement_strong_count(pointer()); }
    // Bytes never exposes an upgradeable Weak; allocator-only owners cannot access the value.
    auto is_unique() const noexcept -> bool {
        return Storage::strong_count_from_raw(pointer()) == usize(1);
    }
    auto properties(slice<u8> view) const noexcept -> rstd::bytes::BytesProperties {
        auto pos                  = offset(view);
        auto allocation_alignment = this->self().allocation_alignment();
        auto alignment            = allocation_alignment;
        while (pos % alignment != usize()) alignment /= usize(2);
        return { this->self().capacity() - pos, alignment, allocation_alignment };
    }
    auto allocator() const noexcept -> ArcAllocator {
        return Storage::allocator_owner_from_raw(pointer());
    }
    auto empty_view() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(this->self().data(), usize());
    }
    auto try_into_mut(slice<u8> view) const -> Option<rstd::bytes::BytesMut>;
};

namespace rstd::bytes
{

export class Bytes {
    rstd::slice<u8>        view_;
    ptr<dyn<BytesStorage>> storage_ = empty_bytes_storage();

    friend class BytesMut;
    Bytes(ByteStorage storage, usize pos, usize end) {
        auto* base = storage->data();
        view_ = rstd::slice<u8>::from_raw_parts(pos == usize() ? base : base + pos.to_primitive(),
                                                end - pos);
        auto pointer = storage.into_raw().into_ptr();
        storage_     = dyn<BytesStorage>::from_ptr(pointer).as_ptr();
    }
    void forget_storage() noexcept {
        view_    = {};
        storage_ = empty_bytes_storage();
    }
    auto subview(usize start, usize end) const noexcept -> rstd::slice<u8> {
        auto* p = data();
        return rstd::slice<u8>::from_raw_parts(start == usize() ? p : p + start.to_primitive(),
                                               end - start);
    }

public:
    Bytes()                                = default;
    Bytes(const Bytes&)                    = delete;
    auto operator=(const Bytes&) -> Bytes& = delete;
    Bytes(Bytes&& other) noexcept: view_(other.view_), storage_(other.storage_) {
        other.forget_storage();
    }
    auto operator=(Bytes&& other) noexcept -> Bytes& {
        if (this != &other) {
            storage_.as_ref()->release();
            view_    = other.view_;
            storage_ = other.storage_;
            other.forget_storage();
        }
        return *this;
    }
    ~Bytes() { storage_.as_ref()->release(); }

    static auto make() -> Bytes { return {}; }
    /// Borrows the entire constant byte array, including any terminating zero.
    template<auto& Data>
        requires(__is_array(mtp::rm_ref<decltype(Data)>) &&
                 mtp::is_const<mtp::rm_ref<decltype(Data[0])>> &&
                 ! __is_volatile(mtp::rm_ref<decltype(Data[0])>) &&
                 (mtp::same_as<mtp::rm_cv<mtp::rm_ref<decltype(Data[0])>>, char> ||
                  mtp::same_as<mtp::rm_cv<mtp::rm_ref<decltype(Data[0])>>, unsigned char> ||
                  mtp::same_as<mtp::rm_cv<mtp::rm_ref<decltype(Data[0])>>, signed char> ||
                  mtp::same_as<mtp::rm_cv<mtp::rm_ref<decltype(Data[0])>>, byte> ||
                  mtp::same_as<mtp::rm_cv<mtp::rm_ref<decltype(Data[0])>>, u8>))
    static auto from_static() -> Bytes {
        static const StaticBytesStorage storage { reinterpret_cast<const byte*>(Data) };
        Bytes                           result;
        result.view_    = rstd::slice<u8>::from_raw_parts(storage.base, usize(sizeof(Data)));
        result.storage_ = dyn<BytesStorage>::from_ptr(&storage);
        return result;
    }
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
    static auto copy_from_slice(rstd::slice<u8> src) -> Bytes {
        if (src.is_empty()) return {};
        return copy_from_slice_in(src, ::alloc::Global {});
    }
    template<typename A>
        requires Impled<A, Allocator, ThreadSafeAllocator>
    static auto copy_from_slice_in(rstd::slice<u8> src, A allocator) -> Bytes {
        auto storage = make_storage(src.len(), rstd::move(allocator));
        storage.deref_mut().get_mut().extend_from_slice(src);
        return Bytes(rstd::move(storage), usize(), src.len());
    }
    /// Transfers unique writable storage; failure returns the unchanged view and owner.
    auto try_into_mut() && -> Result<BytesMut, Bytes>;
    /// Otherwise copies the visible bytes with the original allocator and allocation alignment.
    auto into_mut() && -> BytesMut;
    auto clone() const -> Bytes {
        storage_.as_ref()->retain();
        Bytes result;
        result.view_    = view_;
        result.storage_ = storage_;
        return result;
    }
    auto len() const noexcept -> usize { return view_.len(); }
    auto size() const noexcept -> usize { return len(); }
    /// Capacity from the visible start; it does not imply writable or initialized bytes.
    auto capacity() const noexcept -> usize {
        return storage_.as_ref()->properties(view_).capacity;
    }
    auto alignment() const noexcept -> usize {
        return storage_.as_ref()->properties(view_).alignment;
    }
    auto allocator() const noexcept -> ArcAllocator { return storage_.as_ref()->allocator(); }
    auto is_unique() const noexcept -> bool { return storage_.as_ref()->is_unique(); }
    auto is_empty() const noexcept -> bool { return view_.is_empty(); }
    auto data() const noexcept -> const byte* { return view_.as_raw_ptr(); }
    auto as_slice() const noexcept [[clang::lifetimebound]] -> rstd::slice<u8> {
        return is_empty() ? rstd::slice<u8> {} : view_;
    }
    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept [[clang::lifetimebound]] -> rstd::slice<u8> { return as_slice(); }
    /// Shares ownership of a half-open subrange without copying its contents.
    auto slice(usize start, usize end) const -> Bytes {
        if (start > end || end > len()) rstd::panic { "Bytes::slice out of bounds" };
        auto result  = clone();
        result.view_ = subview(start, end);
        return result;
    }
    auto split_to(usize at) -> Bytes {
        if (at > len()) rstd::panic { "Bytes::split_to out of bounds" };
        auto result = slice(usize(), at);
        view_       = subview(at, len());
        return result;
    }
    auto split_off(usize at) -> Bytes {
        if (at > len()) rstd::panic { "Bytes::split_off out of bounds" };
        auto result = slice(at, len());
        view_       = subview(usize(), at);
        return result;
    }
    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "Bytes::advance out of bounds" };
        if (cnt == len())
            clear();
        else
            view_ = subview(cnt, len());
    }
    void truncate(usize new_len) {
        if (new_len < len()) view_ = subview(usize(), new_len);
    }
    void clear() { view_ = storage_.as_ref()->empty_view(); }
    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "Bytes index out of bounds" };
        return view_[index];
    }
};

static_assert(sizeof(Bytes) == sizeof(slice<u8>) + sizeof(ptr<dyn<BytesStorage>>));

export class BytesMut {
    Option<ByteStorage>    m_storage;
    usize                  m_pos;
    usize                  m_end;
    static constexpr usize DEFAULT_CHUNK_CAPACITY { 64 };

    friend class Bytes;
    friend struct rstd::Impl<BytesStorage, ByteBuffer>;
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
    /// Uninitialized tail without allocation or clearing. Initialize before advancing.
    auto spare_capacity_mut() noexcept [[clang::lifetimebound]] -> mut_ref<MaybeUninit<byte>[]> {
        if (m_storage.is_none()) return {};
        buffer().truncate(m_end);
        return buffer().spare_capacity_mut().as_uninit();
    }
    /// The caller must have initialized count bytes of the writable tail.
    void advance_mut_unchecked(usize count) {
        if (count > remaining_mut())
            rstd::panic { "BytesMut::advance_mut_unchecked out of bounds" };
        m_end += count;
        if (m_storage.is_some() && buffer().len() < m_end) buffer().set_len_unchecked(m_end);
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
template<typename Self, typename Ignored>
auto BytesStorage::Api<Self, Ignored>::try_into_mut(slice<u8> view) const -> Option<BytesMut> {
    return rstd::trait_call<6>(this, view);
}

inline auto StaticBytesStorage::try_into_mut(slice<u8>) const -> Option<BytesMut> {
    if (base == nullptr) return Some(BytesMut {});
    return None();
}

inline auto Bytes::try_into_mut() && -> Result<BytesMut, Bytes> {
    auto writable = storage_.as_ref()->try_into_mut(view_);
    if (writable.is_none()) return Err(rstd::move(*this));
    forget_storage();
    return Ok(rstd::move(writable).unwrap_unchecked());
}

inline auto Bytes::into_mut() && -> BytesMut {
    auto result = rstd::move(*this).try_into_mut();
    if (result.is_ok()) return rstd::move(result).unwrap();
    auto source = rstd::move(result).unwrap_err();
    auto storage =
        make_storage(source.len(),
                     source.allocator(),
                     source.storage_.as_ref()->properties(source.view_).allocation_alignment);
    BytesMut writable(rstd::move(storage));
    writable.put_slice(source.as_slice());
    return writable;
}
} // namespace rstd::bytes

inline auto
rstd::Impl<rstd::bytes::BytesStorage, rstd::bytes::ByteBuffer>::try_into_mut(slice<u8> view) const
    -> Option<rstd::bytes::BytesMut> {
    if (! is_unique()) return None();
    auto pos     = offset(view);
    auto storage = Storage::from_raw(::alloc::sync::ArcRaw<Buffer>::from_ptr(pointer()));
    rstd::bytes::BytesMut result(rstd::move(storage));
    result.m_pos = pos;
    result.m_end = pos + view.len();
    result.buffer().truncate(result.m_end);
    return Some(rstd::move(result));
}

template<>
struct rstd::Impl<rstd::clone::Clone, rstd::bytes::Bytes>
    : rstd::DefaultInImpl<rstd::clone::Clone, rstd::bytes::Bytes> {
    auto clone() const -> rstd::bytes::Bytes { return this->self().clone(); }
};
