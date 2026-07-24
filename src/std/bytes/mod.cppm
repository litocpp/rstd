export module rstd:bytes;
export import rstd.core;
import rstd.alloc;

using ::alloc::vec::Vec;
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

export class Bytes {
    Vec<u8>      m_buf;
    rstd::size_t m_pos {};

    explicit Bytes(Vec<u8>&& buf): m_buf(rstd::move(buf)) {}

public:
    Bytes() = default;

    Bytes(const Bytes&)            = delete;
    Bytes& operator=(const Bytes&) = delete;

    Bytes(Bytes&&) noexcept            = default;
    Bytes& operator=(Bytes&&) noexcept = default;

    static auto make() -> Bytes { return {}; }

    static auto from_vec(Vec<u8>&& vec) -> Bytes { return Bytes { rstd::move(vec) }; }

    static auto copy_from_slice(slice<u8> src) -> Bytes { return Bytes { Vec<u8>::from(src) }; }

    auto len() const noexcept -> usize { return usize(m_buf.len().to_primitive() - m_pos); }
    auto size() const noexcept -> usize { return len(); }
    auto capacity() const noexcept -> usize { return m_buf.capacity(); }
    auto is_empty() const noexcept -> bool { return len() == usize(); }

    auto data() const noexcept -> const byte* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
    }

    auto as_slice() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        if (is_empty()) return {};
        return slice<u8>::from_raw_parts(data(), len());
    }

    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept [[clang::lifetimebound]] -> slice<u8> { return as_slice(); }

    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "Bytes::advance out of bounds" };
        m_pos += cnt.to_primitive();
        if (m_pos == m_buf.len().to_primitive()) clear();
    }

    void truncate(usize new_len) {
        if (new_len >= len()) return;
        m_buf.truncate(usize(m_pos) + new_len);
    }

    void clear() {
        m_buf.clear();
        m_pos = 0;
    }

    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "Bytes index out of bounds" };
        return u8::from_byte(data()[index.to_primitive()]);
    }
};

export class BytesMut {
    Vec<u8>      m_buf;
    rstd::size_t m_pos {};
    rstd::size_t m_end {};

    static constexpr usize DEFAULT_CHUNK_CAPACITY { 64 };

    explicit BytesMut(Vec<u8>&& buf): m_buf(rstd::move(buf)), m_end(m_buf.len().to_primitive()) {}

    void compact_if_empty() {
        if (m_pos == m_end) clear();
    }

public:
    BytesMut() = default;

    BytesMut(const BytesMut&)            = delete;
    BytesMut& operator=(const BytesMut&) = delete;

    BytesMut(BytesMut&&) noexcept            = default;
    BytesMut& operator=(BytesMut&&) noexcept = default;

    static auto make() -> BytesMut { return {}; }
    static auto with_capacity(usize capacity) -> BytesMut {
        return BytesMut { Vec<u8>::with_capacity(capacity) };
    }

    static auto from_vec(Vec<u8>&& vec) -> BytesMut { return BytesMut { rstd::move(vec) }; }

    auto len() const noexcept -> usize { return usize(m_end - m_pos); }
    auto size() const noexcept -> usize { return len(); }
    auto capacity() const noexcept -> usize { return m_buf.capacity(); }
    auto is_empty() const noexcept -> bool { return len() == usize(); }

    auto data() noexcept -> byte* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
    }
    auto data() const noexcept -> const byte* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
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
        m_pos += cnt.to_primitive();
        compact_if_empty();
    }

    auto remaining_mut() const noexcept -> usize { return m_buf.capacity() - usize(m_end); }

    auto chunk_mut() [[clang::lifetimebound]] -> mut_ref<u8[]> {
        if (remaining_mut() == usize()) {
            reserve(DEFAULT_CHUNK_CAPACITY);
        }
        if (m_buf.len() < m_buf.capacity()) {
            m_buf.resize(m_buf.capacity(), u8 {});
        }
        return mut_ref<u8[]>::from_raw_parts(m_buf.data() + m_end, m_buf.len() - usize(m_end));
    }

    void advance_mut(usize cnt) {
        if (cnt > m_buf.len() - usize(m_end)) {
            rstd::panic { "BytesMut::advance_mut out of bounds" };
        }
        m_end += cnt.to_primitive();
    }

    void reserve(usize additional) {
        if (additional <= remaining_mut()) return;
        m_buf.reserve(usize(m_end) + additional - m_buf.len());
    }

    void put_slice(slice<u8> src) {
        reserve(src.len());
        auto const target = usize(m_end) + src.len();
        if (m_buf.len() < target) m_buf.resize(target, u8 {});
        for (rstd::size_t index = 0; index < src.len().to_primitive(); ++index) {
            m_buf[usize(m_end + index)] = src[usize(index)];
        }
        m_end = target.to_primitive();
    }
    void extend_from_slice(slice<u8> src) { put_slice(src); }
    void resize(usize new_len, u8 value) {
        if (m_pos != 0 && new_len > len()) {
            auto dst = Vec<u8>::with_capacity(new_len);
            dst.extend_from_slice(as_slice());
            dst.resize(new_len, value);
            m_buf = rstd::move(dst);
            m_pos = 0;
            m_end = m_buf.len().to_primitive();
            return;
        }
        auto const old_end = m_end;
        auto const new_end = m_pos + new_len.to_primitive();
        if (new_end > m_buf.len().to_primitive()) {
            m_buf.resize(usize(new_end), value);
        } else {
            for (rstd::size_t index = old_end; index < new_end; ++index) {
                m_buf[usize(index)] = value;
            }
        }
        m_end = new_end;
    }

    void truncate(usize new_len) {
        if (new_len >= len()) return;
        m_end = m_pos + new_len.to_primitive();
    }

    void clear() {
        m_buf.clear();
        m_pos = 0;
        m_end = 0;
    }

    auto split_to(usize at) -> BytesMut {
        if (at > len()) rstd::panic { "BytesMut::split_to out of bounds" };
        auto out = BytesMut::with_capacity(at);
        out.extend_from_slice(slice<u8>::from_raw_parts(data(), at));
        advance(at);
        return out;
    }

    auto split() -> BytesMut { return split_to(len()); }

    auto freeze() -> Bytes {
        if (m_pos == 0) {
            m_buf.truncate(usize(m_end));
            return Bytes::from_vec(rstd::move(m_buf));
        }

        auto vec = Vec<u8>::with_capacity(len());
        vec.extend_from_slice(as_slice());
        clear();
        return Bytes::from_vec(rstd::move(vec));
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

} // namespace rstd::bytes
