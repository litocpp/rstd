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

export class RangeReader;

export class ReadRange {
public:
    ReadRange(const ReadRange&)                        = delete;
    auto operator=(const ReadRange&) -> ReadRange&     = delete;
    ReadRange(ReadRange&&) noexcept                    = default;
    auto operator=(ReadRange&&) noexcept -> ReadRange& = default;

    static auto make(SharedReadAt source, u64 offset, u64 len) -> Result<ReadRange> {
        if (len > u64::MAX - offset) {
            return Err(
                error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
        }
        return Ok(ReadRange(rstd::move(source), offset, len));
    }

    auto clone() const -> ReadRange { return ReadRange(m_source.clone(), m_offset, m_len); }

    auto subrange(u64 offset, u64 len) const -> Result<ReadRange> {
        if (offset > m_len || len > m_len - offset) {
            return Err(
                error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
        }
        return Ok(ReadRange(m_source.clone(), m_offset + offset, len));
    }

    auto reader() const -> RangeReader;
    auto into_reader() && -> RangeReader;

    auto offset() const noexcept -> u64 { return m_offset; }
    auto len() const noexcept -> u64 { return m_len; }
    bool is_empty() const noexcept { return m_len == u64 {}; }

private:
    ReadRange(SharedReadAt source, u64 offset, u64 len)
        : m_source(rstd::move(source)), m_offset(offset), m_len(len) {}

    SharedReadAt m_source;
    u64          m_offset {};
    u64          m_len {};
};

export class RangeReader {
public:
    RangeReader(const RangeReader&)                        = delete;
    auto operator=(const RangeReader&) -> RangeReader&     = delete;
    RangeReader(RangeReader&&) noexcept                    = default;
    auto operator=(RangeReader&&) noexcept -> RangeReader& = default;

    static auto make(SharedReadAt source, u64 offset, u64 len) -> Result<RangeReader> {
        if (len > u64::MAX - offset) {
            return Err(
                error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
        }
        return Ok(RangeReader(rstd::move(source), offset, len));
    }

    auto read(mut_ref<u8[]> buf) -> Result<usize> {
        if (m_position == m_len || buf.is_empty()) return Ok(usize {});

        auto       remaining       = m_len - m_position;
        auto const requested_count = rstd::min(
            static_cast<rstd::uint64_t>(buf.len().to_primitive()), remaining.to_primitive());
        usize requested(static_cast<rstd::size_t>(requested_count));
        auto  destination = mut_ref<u8[]>::from_raw_parts(buf.as_raw_ptr(), requested);
        auto  result      = m_source->read_at(destination, m_offset + m_position);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());

        auto read = rstd::move(result).unwrap_unchecked();
        if (read > requested) {
            return Err(error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidData }));
        }
        m_position += u64(read.to_primitive());
        return Ok(read);
    }

    auto seek(SeekFrom pos) -> Result<u64> {
        u64 next {};
        switch (pos.which) {
        case SeekFrom::Which::Start: next = pos.start; break;
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
    bool is_empty() const noexcept { return m_len == u64 {}; }

private:
    friend class ReadRange;

    RangeReader(SharedReadAt source, u64 offset, u64 len)
        : m_source(rstd::move(source)), m_offset(offset), m_len(len) {}

    static auto invalid_seek() -> Result<u64> {
        return Err(error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidInput }));
    }

    static auto add_offset(u64 base, i64 offset) -> Result<u64> {
        auto const value = static_cast<rstd::int128_t>(base.to_primitive()) +
                           static_cast<rstd::int128_t>(offset.to_primitive());
        if (value < 0 || value > static_cast<rstd::int128_t>(~rstd::uint64_t(0))) {
            return invalid_seek();
        }
        return Ok(u64(value));
    }

    SharedReadAt m_source;
    u64          m_offset {};
    u64          m_len {};
    u64          m_position {};
};

auto ReadRange::reader() const -> RangeReader {
    return RangeReader(m_source.clone(), m_offset, m_len);
}

auto ReadRange::into_reader() && -> RangeReader {
    return RangeReader(rstd::move(m_source), m_offset, m_len);
}

} // namespace rstd::io

namespace rstd
{

template<>
struct Impl<io::Read, io::RangeReader> : ImplBase<io::RangeReader> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> { return this->self().read(buf); }
};

template<>
struct Impl<io::Seek, io::RangeReader> : ImplBase<io::RangeReader> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return this->self().seek(pos); }
};

} // namespace rstd
