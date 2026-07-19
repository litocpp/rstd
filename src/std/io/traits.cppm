export module rstd:io.traits;
export import :io.error;
export import rstd.core;

namespace rstd::io
{
/// Default internal buffer capacity (8 KiB, matching Rust std).
export inline constexpr usize DEFAULT_BUF_SIZE { rstd::size_t(8192) };
} // namespace rstd::io

namespace rstd::io
{

using error::Error;
using error::ErrorKind;

// ── Read ──────────────────────────────────────────────────────────────────
/// Trait for objects that can be read from.
/// Required method: `read(mut_ref<byte[]> buf) -> Result<usize>`
///   - Returns Ok(0) at EOF or on an empty buffer.
///   - May return fewer bytes than the buffer length without being at EOF.
///   - EINTR is the caller's responsibility to retry.
export struct Read {
    using Trait                  = Read;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Read;
        /// Reads bytes into `buf` and returns the number of bytes read.
        auto read(mut_ref<byte[]> buf) -> Result<usize>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read>;
};

// ── Write ─────────────────────────────────────────────────────────────────
/// Trait for objects that can be written to.
/// Required methods:
///   `write(slice<byte> buf) -> Result<usize>` — write some bytes
///   `flush()               -> Result<empty>`  — flush buffers
export struct Write {
    using Trait                  = Write;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Write;
        auto write(slice<byte> buf) -> Result<usize>;
        auto flush() -> Result<empty>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write, &T::flush>;
};

// ── SeekFrom ──────────────────────────────────────────────────────────────
/// Enumeration of possible methods to seek within an I/O object.
export struct SeekFrom {
    enum class Which : rstd::uint8_t
    {
        Start,
        End,
        Current
    };
    Which which;
    u64   start;
    i64   offset;

    /// Creates a SeekFrom that seeks to an absolute position from the start.
    static auto from_start(u64 n) noexcept -> SeekFrom { return { Which::Start, n, i64 {} }; }
    /// Creates a SeekFrom that seeks relative to the end of the stream.
    static auto from_end(i64 n) noexcept -> SeekFrom { return { Which::End, u64 {}, n }; }
    /// Creates a SeekFrom that seeks relative to the current position.
    static auto from_current(i64 n) noexcept -> SeekFrom { return { Which::Current, u64 {}, n }; }
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

/// Trait for positional reads that do not modify a shared cursor.
export struct ReadAt {
    using Trait                  = ReadAt;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = ReadAt;
        auto read_at(mut_ref<byte[]> buf, u64 offset) const -> Result<usize> {
            return trait_call<0>(this, buf, offset);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read_at>;
};

/// Trait for positional writes that do not modify a shared cursor.
export struct WriteAt {
    using Trait                  = WriteAt;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = WriteAt;
        auto write_at(slice<byte> buf, u64 offset) const -> Result<usize> {
            return trait_call<0>(this, buf, offset);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write_at>;
};

/// Object-safe composition of Read and Seek.
export struct ReadSeek {
    using Trait                  = ReadSeek;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = ReadSeek;
        auto read(mut_ref<byte[]> buf) -> Result<usize> { return trait_call<0>(this, buf); }
        auto seek(SeekFrom pos) -> Result<u64> { return trait_call<1>(this, pos); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read, &T::seek>;
};

/// Object-safe composition of Write and Seek.
export struct WriteSeek {
    using Trait                  = WriteSeek;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = WriteSeek;
        auto write(slice<byte> buf) -> Result<usize> { return trait_call<0>(this, buf); }
        auto flush() -> Result<empty> { return trait_call<1>(this); }
        auto seek(SeekFrom pos) -> Result<u64> { return trait_call<2>(this, pos); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write, &T::flush, &T::seek>;
};

/// Object-safe composition used by read-write file handles.
export struct ReadWriteSeek {
    using Trait                  = ReadWriteSeek;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = ReadWriteSeek;
        auto read(mut_ref<byte[]> buf) -> Result<usize> { return trait_call<0>(this, buf); }
        auto write(slice<byte> buf) -> Result<usize> { return trait_call<1>(this, buf); }
        auto flush() -> Result<empty> { return trait_call<2>(this); }
        auto seek(SeekFrom pos) -> Result<u64> { return trait_call<3>(this, pos); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read, &T::write, &T::flush, &T::seek>;
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
/// Retries short writes until the buffer has been written.
export template<typename W>
    requires Impled<W, Write>
auto write_all(W& w, slice<byte> buf) -> Result<empty> {
    while (! buf.is_empty()) {
        auto res = as<Write>(w).write(buf);
        if (res.is_err()) return Err(res.unwrap_err_unchecked());
        auto const written = res.unwrap_unchecked().to_primitive();
        if (written == 0) {
            return Err(error::Error_WRITE_ALL_EOF);
        }
        if (written > buf.len().to_primitive()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        buf = slice<byte>::from_raw_parts(buf.as_raw_ptr() + written,
                                          usize(buf.len().to_primitive() - written));
    }
    return Ok(empty {});
}

/// Reads exactly the buffer length or returns UnexpectedEof.
/// Retries short reads until the buffer has been filled.
export template<typename R>
    requires Impled<R, Read>
auto read_exact(R& r, mut_ref<byte[]> buf) -> Result<empty> {
    while (! buf.is_empty()) {
        auto res = as<Read>(r).read(buf);
        if (res.is_err()) {
            auto e = res.unwrap_err_unchecked();
            if (e.kind() == error::ErrorKind { error::ErrorKind::Interrupted }) continue;
            return Err(rstd::move(e));
        }
        auto const read = res.unwrap_unchecked().to_primitive();
        if (read == 0) return Err(error::Error_READ_EXACT_EOF);
        if (read > buf.len().to_primitive()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        buf = mut_ref<byte[]>::from_raw_parts(buf.as_raw_ptr() + read,
                                              usize(buf.len().to_primitive() - read));
    }
    return Ok(empty {});
}

/// Seek to the beginning.
export template<typename S>
    requires Impled<S, Seek>
auto rewind(S& s) -> Result<empty> {
    auto res = as<Seek>(s).seek(SeekFrom::from_start(u64 {}));
    if (res.is_err()) return Err(res.unwrap_err_unchecked());
    return Ok(empty {});
}

/// Return the current stream position.
export template<typename S>
    requires Impled<S, Seek>
auto stream_position(S& s) -> Result<u64> {
    return as<Seek>(s).seek(SeekFrom::from_current(i64 {}));
}

/// Fills the buffer without changing the source cursor.
export template<typename R>
    requires Impled<R, ReadAt>
auto read_exact_at(const R& r, mut_ref<byte[]> buf, u64 offset) -> Result<empty> {
    while (! buf.is_empty()) {
        auto res = as<ReadAt>(r).read_at(buf, offset);
        if (res.is_err()) {
            auto error = res.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        auto const read = res.unwrap_unchecked().to_primitive();
        if (read == 0) return Err(error::Error_READ_EXACT_EOF);
        if (read > buf.len().to_primitive()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        auto next_offset = offset.checked_add(u64(read));
        if (next_offset.is_none()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        buf    = mut_ref<byte[]>::from_raw_parts(buf.as_raw_ptr() + read,
                                                 usize(buf.len().to_primitive() - read));
        offset = rstd::move(next_offset).unwrap_unchecked();
    }
    return Ok(empty {});
}

/// Writes the whole buffer without changing the destination cursor.
export template<typename W>
    requires Impled<W, WriteAt>
auto write_all_at(const W& w, slice<byte> buf, u64 offset) -> Result<empty> {
    while (! buf.is_empty()) {
        auto res = as<WriteAt>(w).write_at(buf, offset);
        if (res.is_err()) {
            auto error = res.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        auto const written = res.unwrap_unchecked().to_primitive();
        if (written == 0) return Err(error::Error_WRITE_ALL_EOF);
        if (written > buf.len().to_primitive()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        auto next_offset = offset.checked_add(u64(written));
        if (next_offset.is_none()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        }
        buf    = slice<byte>::from_raw_parts(buf.as_raw_ptr() + written,
                                             usize(buf.len().to_primitive() - written));
        offset = rstd::move(next_offset).unwrap_unchecked();
    }
    return Ok(empty {});
}

} // namespace rstd::io

namespace rstd
{

template<typename T>
    requires Impled<T, io::Read> && Impled<T, io::Seek>
struct Impl<io::ReadSeek, T> : ImplBase<T> {
    auto read(mut_ref<byte[]> buf) -> io::Result<usize> {
        return as<io::Read>(this->self()).read(buf);
    }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

template<typename T>
    requires Impled<T, io::Write> && Impled<T, io::Seek>
struct Impl<io::WriteSeek, T> : ImplBase<T> {
    auto write(slice<byte> buf) -> io::Result<usize> {
        return as<io::Write>(this->self()).write(buf);
    }
    auto flush() -> io::Result<empty> { return as<io::Write>(this->self()).flush(); }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

template<typename T>
    requires Impled<T, io::Read> && Impled<T, io::Write> && Impled<T, io::Seek>
struct Impl<io::ReadWriteSeek, T> : ImplBase<T> {
    auto read(mut_ref<byte[]> buf) -> io::Result<usize> {
        return as<io::Read>(this->self()).read(buf);
    }
    auto write(slice<byte> buf) -> io::Result<usize> {
        return as<io::Write>(this->self()).write(buf);
    }
    auto flush() -> io::Result<empty> { return as<io::Write>(this->self()).flush(); }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

} // namespace rstd
