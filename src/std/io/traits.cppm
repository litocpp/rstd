export module rstd:io.traits;
export import :io.error;
export import rstd.core;

namespace rstd::io
{
// Default internal buffer capacity (8 KiB, matching Rust std).
export inline constexpr usize DEFAULT_BUF_SIZE = 8192;
} // namespace rstd::io

namespace rstd::io
{

using error::Error;
using error::ErrorKind;

// ── Read ──────────────────────────────────────────────────────────────────
/// Trait for objects that can be read from.
/// Required method: `read(u8* buf, usize len) -> Result<usize>`
///   - Returns Ok(0) at EOF or on an empty buffer.
///   - May return Ok(n) where n < len without being at EOF.
///   - EINTR is the caller's responsibility to retry.
export struct Read {
    using Trait                  = Read;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Read;
        /// Read bytes into buf[0..len].  Returns number of bytes read.
        auto read(u8* buf, usize len) -> Result<usize>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read>;
};

// ── Write ─────────────────────────────────────────────────────────────────
/// Trait for objects that can be written to.
/// Required methods:
///   `write(const u8* buf, usize len) -> Result<usize>` — write some bytes
///   `flush()                         -> Result<empty>`  — flush buffers
export struct Write {
    using Trait                  = Write;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Write;
        auto write(const u8* buf, usize len) -> Result<usize>;
        auto flush() -> Result<empty>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write, &T::flush>;
};

// ── SeekFrom ──────────────────────────────────────────────────────────────
export struct SeekFrom {
    enum class Which : u8
    {
        Start,
        End,
        Current
    };
    Which which;
    i64   offset; // treated as u64 for Which::Start

    static auto from_start(u64 n) noexcept -> SeekFrom { return { Which::Start, i64(n) }; }
    static auto from_end(i64 n) noexcept -> SeekFrom { return { Which::End, n }; }
    static auto from_current(i64 n) noexcept -> SeekFrom { return { Which::Current, n }; }
};

// ── Seek ──────────────────────────────────────────────────────────────────
/// Trait for types with a notion of current position.
/// Required: `seek(SeekFrom) -> Result<u64>` — returns new absolute position.
export struct Seek {
    using Trait                  = Seek;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Seek;
        auto seek(SeekFrom pos) -> Result<u64>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::seek>;
};

// ── BufRead ───────────────────────────────────────────────────────────────
/// Trait for buffered readers.
/// Required:
///   `fill_buf()       -> Result<slice<u8>>`  — expose the internal buffer
///   `consume(usize)   -> void`               — mark n bytes consumed
export struct BufRead {
    using Trait                  = BufRead;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = BufRead;
        auto fill_buf() -> Result<slice<u8>>;
        auto consume(usize amt) -> void;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fill_buf, &T::consume>;
};

// ── Default-method helpers ────────────────────────────────────────────────

/// Write all bytes or return an error.
/// Retries short writes until `len` bytes have been written.
export template<typename W>
    requires Impled<W, Write>
auto write_all(W& w, const u8* buf, usize len) -> Result<empty> {
    while (len > 0) {
        auto res = as<Write>(w).write(buf, len);
        if (res.is_err()) return Err(res.unwrap_err_unchecked());
        usize n = res.unwrap_unchecked();
        if (n == 0) {
            return Err(error::Error_WRITE_ALL_EOF);
        }
        buf += n;
        len -= n;
    }
    return Ok(empty {});
}

/// Read exactly `len` bytes or return UnexpectedEof.
/// Retries short reads until `len` bytes have been read.
export template<typename R>
    requires Impled<R, Read>
auto read_exact(R& r, u8* buf, usize len) -> Result<empty> {
    while (len > 0) {
        auto res = as<Read>(r).read(buf, len);
        if (res.is_err()) {
            auto e = res.unwrap_err_unchecked();
            if (e.kind() == error::ErrorKind { error::ErrorKind::Interrupted }) continue;
            return Err(rstd::move(e));
        }
        usize n = res.unwrap_unchecked();
        if (n == 0) return Err(error::Error_READ_EXACT_EOF);
        buf += n;
        len -= n;
    }
    return Ok(empty {});
}

/// Seek to the beginning.
export template<typename S>
    requires Impled<S, Seek>
auto rewind(S& s) -> Result<empty> {
    auto res = as<Seek>(s).seek(SeekFrom::from_start(0));
    if (res.is_err()) return Err(res.unwrap_err_unchecked());
    return Ok(empty {});
}

/// Return the current stream position.
export template<typename S>
    requires Impled<S, Seek>
auto stream_position(S& s) -> Result<u64> {
    return as<Seek>(s).seek(SeekFrom::from_current(0));
}

} // namespace rstd::io
