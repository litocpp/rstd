module;
#include <rstd/macro.hpp>

export module rstd:io.range;
export import :io.traits;
export import rstd.alloc;

using ::alloc::boxed::Box;
using ::alloc::sync::Arc;
using namespace rstd::prelude;

namespace rstd::io
{

export using SharedReadAt        = Arc<dyn<ReadAt>>;
export using ReadSeekHandle      = Box<dyn<ReadSeek>>;
export using WriteSeekHandle     = Box<dyn<WriteSeek>>;
export using ReadWriteSeekHandle = Box<dyn<ReadWriteSeek>>;

export class RangeReader {
public:
    RangeReader(const RangeReader&)                        = delete;
    auto operator=(const RangeReader&) -> RangeReader&     = delete;
    RangeReader(RangeReader&&) noexcept                    = default;
    auto operator=(RangeReader&&) noexcept -> RangeReader& = default;

    static auto make(SharedReadAt source, u64 offset, u64 len) -> Result<RangeReader> {
        if (len > u64(-1) - offset) {
            return Err(
                error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
        }
        return Ok(RangeReader(rstd::move(source), offset, len));
    }

    auto read(u8* buf, usize len) -> Result<usize> {
        if (m_position == m_len || len == 0) return Ok(usize(0));

        auto remaining = m_len - m_position;
        auto requested = rstd::min(u64(len), remaining);
        auto result    = m_source->read_at(buf, usize(requested), m_offset + m_position);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());

        auto read = rstd::move(result).unwrap_unchecked();
        if (read > requested) {
            return Err(error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidData }));
        }
        m_position += read;
        return Ok(read);
    }

    auto seek(SeekFrom pos) -> Result<u64> {
        u64 next = 0;
        switch (pos.which) {
        case SeekFrom::Which::Start: next = u64(pos.offset); break;
        case SeekFrom::Which::Current: {
            auto result = add_offset(m_position, pos.offset);
            if (result.is_err()) return result;
            next = rstd::move(result).unwrap_unchecked();
            break;
        }
        case SeekFrom::Which::End: {
            auto result = add_offset(m_len, pos.offset);
            if (result.is_err()) return result;
            next = rstd::move(result).unwrap_unchecked();
            break;
        }
        }
        if (next > m_len) return invalid_seek();
        m_position = next;
        return Ok(next);
    }

    auto position() const noexcept -> u64 { return m_position; }
    auto len() const noexcept -> u64 { return m_len; }
    bool is_empty() const noexcept { return m_len == 0; }

private:
    RangeReader(SharedReadAt source, u64 offset, u64 len)
        : m_source(rstd::move(source)), m_offset(offset), m_len(len) {}

    static auto invalid_seek() -> Result<u64> {
        return Err(error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
    }

    static auto add_offset(u64 base, i64 offset) -> Result<u64> {
        if (offset >= 0) {
            auto positive = u64(offset);
            if (positive > u64(-1) - base) return invalid_seek();
            return Ok(base + positive);
        }

        auto magnitude = u64(-(offset + 1)) + 1;
        if (magnitude > base) return invalid_seek();
        return Ok(base - magnitude);
    }

    SharedReadAt m_source;
    u64          m_offset { 0 };
    u64          m_len { 0 };
    u64          m_position { 0 };
};

} // namespace rstd::io

namespace rstd
{

template<>
struct Impl<io::Read, io::RangeReader> : ImplBase<io::RangeReader> {
    auto read(u8* buf, usize len) -> io::Result<usize> { return this->self().read(buf, len); }
};

template<>
struct Impl<io::Seek, io::RangeReader> : ImplBase<io::RangeReader> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return this->self().seek(pos); }
};

} // namespace rstd
