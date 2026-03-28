module;
#include <rstd/macro.hpp>
export module rstd:io.util;
export import :io.traits;

namespace rstd::io
{

// ── Empty ─────────────────────────────────────────────────────────────────
/// A reader that always returns EOF and a writer that discards all bytes.
export struct Empty {};

export inline auto empty_io() noexcept -> Empty { return {}; }

// ── Repeat ────────────────────────────────────────────────────────────────
/// A reader that infinitely yields one byte value.
export struct Repeat { u8 byte; };

export inline auto repeat(u8 byte) noexcept -> Repeat { return { byte }; }

// ── Sink ──────────────────────────────────────────────────────────────────
/// A writer that discards all bytes and never errors.
export struct Sink {};

export inline auto sink() noexcept -> Sink { return {}; }

// ── copy ──────────────────────────────────────────────────────────────────
/// Copy all bytes from `reader` into `writer`.  Returns bytes copied.
export template<typename R, typename W>
    requires Impled<R, io::Read> && Impled<W, io::Write>
auto copy(R& reader, W& writer) -> Result<u64> {
    constexpr usize BUF_SIZE = DEFAULT_BUF_SIZE;
    u8    buf[BUF_SIZE];
    u64   total = 0;
    while (true) {
        auto rres = as<Read>(reader).read(buf, BUF_SIZE);
        if (rres.is_err()) {
            auto e = rres.unwrap_err_unchecked();
            if (e.kind() == error::ErrorKind { error::ErrorKind::Interrupted }) continue;
            return Err(rstd::move(e));
        }
        usize n = rres.unwrap_unchecked();
        if (n == 0) break;
        auto wres = io::write_all(writer, buf, n);
        if (wres.is_err()) return Err(wres.unwrap_err_unchecked());
        total += n;
    }
    return Ok(total);
}

} // namespace rstd::io

// ── Impl specialisations (must be in namespace rstd) ─────────────────────
namespace rstd
{

template<>
struct Impl<io::Read, io::Empty> : ImplBase<io::Empty> {
    auto read(u8*, usize) -> io::Result<usize> { return Ok(usize(0)); }
};

template<>
struct Impl<io::BufRead, io::Empty> : ImplBase<io::Empty> {
    auto fill_buf() -> io::Result<slice<u8>> {
        return Ok(slice<u8>::from_raw_parts(nullptr, usize(0)));
    }
    auto consume(usize) -> void {}
};

template<>
struct Impl<io::Write, io::Empty> : ImplBase<io::Empty> {
    auto write(const u8*, usize len) -> io::Result<usize> { return Ok(len); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Seek, io::Empty> : ImplBase<io::Empty> {
    auto seek(io::SeekFrom) -> io::Result<u64> { return Ok(u64(0)); }
};

template<>
struct Impl<io::Read, io::Repeat> : ImplBase<io::Repeat> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        rstd::mem::memset(buf, int(this->self().byte), len);
        return Ok(len);
    }
};

template<>
struct Impl<io::Write, io::Sink> : ImplBase<io::Sink> {
    auto write(const u8*, usize len) -> io::Result<usize> { return Ok(len); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

} // namespace rstd
