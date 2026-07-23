export module rstd:io.util;
export import :io.traits;

namespace rstd::io
{

// ── Empty ─────────────────────────────────────────────────────────────────
/// A reader that always returns EOF and a writer that discards all bytes.
export struct Empty {};

/// Creates a value that implements Read (always EOF), Write (discards), and Seek.
export inline auto empty_io() noexcept -> Empty {
    return {};
}

// ── Repeat ────────────────────────────────────────────────────────────────
/// A reader that infinitely yields one byte value.
export struct Repeat {
    u8 byte;
};

/// Creates a reader that infinitely yields the given byte.
/// \param byte The byte value to repeat.
export inline auto repeat(u8 byte) noexcept -> Repeat {
    return { byte };
}

// ── Sink ──────────────────────────────────────────────────────────────────
/// A writer that discards all bytes and never errors.
export struct Sink {};

/// Creates a writer that successfully consumes all data without storing it.
export inline auto sink() noexcept -> Sink {
    return {};
}

// ── copy ──────────────────────────────────────────────────────────────────
/// Copy all bytes from `reader` into `writer`.  Returns bytes copied.
export template<typename R, typename W>
    requires Impled<R, io::Read> && Impled<W, io::Write>
auto copy(R& reader, W& writer) -> Result<u64> {
    constexpr rstd::size_t BUF_SIZE = DEFAULT_BUF_SIZE.to_primitive();
    byte                   buf[BUF_SIZE];
    u64                    total {};
    while (true) {
        auto values = mut_ref<u8[]>::from_raw_parts(buf, usize(BUF_SIZE));
        auto rres   = as<Read>(reader).read(values);
        if (rres.is_err()) {
            auto e = rres.unwrap_err_unchecked();
            if (e.kind() == error::ErrorKind { error::ErrorKind::Interrupted }) continue;
            return Err(rstd::move(e));
        }
        usize n = rres.unwrap_unchecked();
        if (n == usize {}) break;
        auto written_values = slice<u8>::from_raw_parts(buf, n);
        auto wres           = io::write_all(writer, written_values);
        if (wres.is_err()) return Err(wres.unwrap_err_unchecked());
        auto updated = total.checked_add(u64(n.to_primitive()));
        if (updated.is_none()) {
            return Err(error::Error::from_kind(error::ErrorKind { error::ErrorKind::InvalidData }));
        }
        total = rstd::move(updated).unwrap_unchecked();
    }
    return Ok(total);
}

} // namespace rstd::io

// ── Impl specialisations (must be in namespace rstd) ─────────────────────
namespace rstd
{

template<>
struct Impl<io::Read, io::Empty> : ImplBase<io::Empty> {
    auto read(mut_ref<u8[]>) -> io::Result<usize> { return Ok(usize {}); }
};

template<>
struct Impl<io::BufRead, io::Empty> : ImplBase<io::Empty> {
    auto fill_buf() -> io::Result<slice<u8>> {
        return Ok(slice<u8>::from_raw_parts(nullptr, usize {}));
    }
    auto consume(usize) -> void {}
};

template<>
struct Impl<io::Write, io::Empty> : ImplBase<io::Empty> {
    auto write(slice<u8> buf) -> io::Result<usize> { return Ok(buf.len()); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Seek, io::Empty> : ImplBase<io::Empty> {
    auto seek(io::SeekFrom) -> io::Result<u64> { return Ok(u64 {}); }
};

template<>
struct Impl<io::Read, io::Repeat> : ImplBase<io::Repeat> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        rstd::mem::memset(buf.as_raw_ptr(), this->self().byte, buf.len());
        return Ok(buf.len());
    }
};

template<>
struct Impl<io::Write, io::Sink> : ImplBase<io::Sink> {
    auto write(slice<u8> buf) -> io::Result<usize> { return Ok(buf.len()); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

} // namespace rstd
