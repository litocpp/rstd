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
    Vec<u8> m_buf;
    usize   m_pos { 0 };

    explicit Bytes(Vec<u8>&& buf): m_buf(rstd::move(buf)) {}

public:
    Bytes() = default;

    Bytes(const Bytes&)            = delete;
    Bytes& operator=(const Bytes&) = delete;

    Bytes(Bytes&&) noexcept            = default;
    Bytes& operator=(Bytes&&) noexcept = default;

    static auto make() -> Bytes { return {}; }

    static auto from_vec(Vec<u8>&& vec) -> Bytes { return Bytes { rstd::move(vec) }; }

    static auto copy_from_slice(slice<u8> src) -> Bytes {
        auto vec = Vec<u8>::with_capacity(src.len());
        vec.extend_from_slice(src);
        return Bytes { rstd::move(vec) };
    }

    auto len() const noexcept -> usize { return m_buf.len() - m_pos; }
    auto size() const noexcept -> usize { return len(); }
    auto capacity() const noexcept -> usize { return m_buf.capacity(); }
    auto is_empty() const noexcept -> bool { return len() == 0; }

    auto data() const noexcept -> const u8* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
    }

    auto as_slice() const noexcept -> slice<u8> { return slice<u8>::from_raw_parts(data(), len()); }

    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept -> slice<u8> { return as_slice(); }

    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "Bytes::advance out of bounds" };
        m_pos += cnt;
        if (m_pos == m_buf.len()) clear();
    }

    void truncate(usize new_len) {
        if (new_len >= len()) return;
        m_buf.truncate(m_pos + new_len);
    }

    void clear() {
        m_buf.clear();
        m_pos = 0;
    }

    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "Bytes index out of bounds" };
        return data()[index];
    }
};

export class BytesMut {
    Vec<u8> m_buf;
    usize   m_pos { 0 };

    static constexpr usize DEFAULT_CHUNK_CAPACITY { 64 };

    explicit BytesMut(Vec<u8>&& buf): m_buf(rstd::move(buf)) {}

    void compact_if_empty() {
        if (m_pos == m_buf.len()) clear();
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

    auto len() const noexcept -> usize { return m_buf.len() - m_pos; }
    auto size() const noexcept -> usize { return len(); }
    auto capacity() const noexcept -> usize { return m_buf.capacity(); }
    auto is_empty() const noexcept -> bool { return len() == 0; }

    auto data() noexcept -> u8* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
    }
    auto data() const noexcept -> const u8* {
        auto* p = m_buf.data();
        return p == nullptr ? nullptr : p + m_pos;
    }

    auto as_slice() const noexcept -> slice<u8> { return slice<u8>::from_raw_parts(data(), len()); }

    auto as_mut_slice() noexcept -> mut_ptr<u8[]> {
        return mut_ptr<u8[]>::from_raw_parts(data(), len());
    }

    auto remaining() const noexcept -> usize { return len(); }
    auto chunk() const noexcept -> slice<u8> { return as_slice(); }

    void advance(usize cnt) {
        if (cnt > len()) rstd::panic { "BytesMut::advance out of bounds" };
        m_pos += cnt;
        compact_if_empty();
    }

    auto remaining_mut() const noexcept -> usize { return m_buf.capacity() - m_buf.len(); }

    auto chunk_mut() -> mut_ptr<u8[]> {
        if (remaining_mut() == 0) {
            reserve(DEFAULT_CHUNK_CAPACITY);
        }
        return m_buf.spare_capacity_mut();
    }

    void advance_mut(usize cnt) {
        if (cnt > remaining_mut()) rstd::panic { "BytesMut::advance_mut out of bounds" };
        m_buf.set_len_unchecked(m_buf.len() + cnt);
    }

    void reserve(usize additional) { m_buf.reserve(additional); }

    void put_slice(slice<u8> src) { m_buf.extend_from_slice(src); }
    void extend_from_slice(slice<u8> src) { put_slice(src); }
    void extend_from_slice(const u8* src, usize count) { m_buf.extend_from_slice(src, count); }

    void resize(usize new_len, u8 value) {
        if (m_pos != 0 && new_len > len()) {
            auto dst = Vec<u8>::with_capacity(new_len);
            dst.extend_from_slice(as_slice());
            dst.resize(new_len, value);
            m_buf = rstd::move(dst);
            m_pos = 0;
            return;
        }
        m_buf.resize(m_pos + new_len, value);
        if (new_len < len()) m_buf.truncate(m_pos + new_len);
    }

    void truncate(usize new_len) {
        if (new_len >= len()) return;
        m_buf.truncate(m_pos + new_len);
    }

    void clear() {
        m_buf.clear();
        m_pos = 0;
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
            return Bytes::from_vec(rstd::move(m_buf));
        }

        auto vec = Vec<u8>::with_capacity(len());
        vec.extend_from_slice(as_slice());
        clear();
        return Bytes::from_vec(rstd::move(vec));
    }

    auto operator[](usize index) -> u8& {
        if (index >= len()) rstd::panic { "BytesMut index out of bounds" };
        return data()[index];
    }

    auto operator[](usize index) const -> u8 {
        if (index >= len()) rstd::panic { "BytesMut index out of bounds" };
        return data()[index];
    }
};

} // namespace rstd::bytes
