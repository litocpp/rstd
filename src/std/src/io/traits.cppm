export module rstd.io:traits;
export import rstd.io.error;
export import rstd.core;
export import :buffer;

namespace rstd::io
{
/// Default internal buffer capacity (8 KiB, matching Rust std).
export inline constexpr usize DEFAULT_BUF_SIZE { rstd::size_t(8192) };
} // namespace rstd::io

namespace rstd::io
{

export using error::Error;
export using error::ErrorKind;

// ── Read ──────────────────────────────────────────────────────────────────
/// Trait for objects that can be read from.
/// Implement at least one of read(mut_ref<u8[]>) and read_buf(BorrowedCursor).
/// Inherit DefaultInImpl or DefaultInClass to obtain the other method.
/// read requires initialized storage; read_buf permits an uninitialized tail.
///   - Returns Ok with no progress at EOF or on an empty buffer.
///   - May fill less than the available capacity, including before returning an error.
///   - EINTR is the caller's responsibility to retry.
export struct Read {
    using Trait                  = Read;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Read;
        /// Advances the cursor by the bytes written, including partial progress on error.
        auto read_buf(BorrowedCursor buf) -> Result<empty> {
            return trait_call<0>(this, rstd::move(buf));
        }
        /// On error the initialized slice may contain partial data, but no count is returned.
        auto read(mut_ref<u8[]> buf) -> Result<usize> { return trait_call<1>(this, buf); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read_buf, &T::read>;
};

// ── Write ─────────────────────────────────────────────────────────────────
/// Trait for objects that can be written to.
/// Required methods:
///   `write(slice<u8> buf) -> Result<usize>` — write some bytes
///   `flush()               -> Result<empty>`  — flush buffers
export struct Write {
    using Trait                  = Write;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = Write;
        auto write(slice<u8> buf) -> Result<usize>;
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
        auto read_buf_at(BorrowedCursor buf, u64 offset) const -> Result<empty> {
            return trait_call<0>(this, rstd::move(buf), offset);
        }
        auto read_at(mut_ref<u8[]> buf, u64 offset) const -> Result<usize> {
            return trait_call<1>(this, buf, offset);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read_buf_at, &T::read_at>;
};

/// Trait for positional writes that do not modify a shared cursor.
export struct WriteAt {
    using Trait                  = WriteAt;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = WriteAt;
        auto write_at(slice<u8> buf, u64 offset) const -> Result<usize> {
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
        auto read_buf(BorrowedCursor buf) -> Result<empty> {
            return trait_call<0>(this, rstd::move(buf));
        }
        /// On error the initialized slice may contain partial data, but no count is returned.
        auto read(mut_ref<u8[]> buf) -> Result<usize> { return trait_call<2>(this, buf); }
        auto seek(SeekFrom pos) -> Result<u64> { return trait_call<1>(this, pos); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read_buf, &T::seek, &T::read>;
};

/// Object-safe composition of Write and Seek.
export struct WriteSeek {
    using Trait                  = WriteSeek;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = WriteSeek;
        auto write(slice<u8> buf) -> Result<usize> { return trait_call<0>(this, buf); }
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
        auto read_buf(BorrowedCursor buf) -> Result<empty> {
            return trait_call<0>(this, rstd::move(buf));
        }
        /// On error the initialized slice may contain partial data, but no count is returned.
        auto read(mut_ref<u8[]> buf) -> Result<usize> { return trait_call<4>(this, buf); }
        auto write(slice<u8> buf) -> Result<usize> { return trait_call<1>(this, buf); }
        auto flush() -> Result<empty> { return trait_call<2>(this); }
        auto seek(SeekFrom pos) -> Result<u64> { return trait_call<3>(this, pos); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read_buf, &T::write, &T::flush, &T::seek, &T::read>;
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
auto write_all(W& w, slice<u8> buf) -> Result<empty> {
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
        buf = slice<u8>::from_raw_parts(buf.as_raw_ptr() + written,
                                        usize(buf.len().to_primitive() - written));
    }
    return Ok(empty {});
}

/// Fills the cursor, retaining partial progress on errors and retrying Interrupted.
export template<typename R>
    requires Impled<R, Read>
auto read_buf_exact(R& reader, BorrowedCursor cursor) -> Result<empty> {
    while (cursor.capacity() != usize()) {
        auto before = cursor.written();
        auto result = as<Read>(reader).read_buf(cursor.reborrow());
        if (result.is_err()) {
            auto error = result.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        if (cursor.written() == before) return Err(error::Error_READ_EXACT_EOF);
    }
    return Ok(empty {});
}

export template<typename R>
    requires Impled<R, Read>
auto read_exact(R& reader, mut_ref<u8[]> bytes) -> Result<empty> {
    while (! bytes.is_empty()) {
        auto result = as<Read>(reader).read(bytes);
        if (result.is_err()) {
            auto error = result.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        auto count = result.unwrap_unchecked();
        if (count == usize()) return Err(error::Error_READ_EXACT_EOF);
        if (count > bytes.len()) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        bytes = mut_ref<u8[]>::from_raw_parts(bytes.as_raw_ptr() + count.to_primitive(),
                                              bytes.len() - count);
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

/// Fills the buffer without changing a shared source position.
export template<typename R>
    requires Impled<R, ReadAt>
auto read_exact_at(const R& reader, mut_ref<u8[]> bytes, u64 offset) -> Result<empty> {
    while (! bytes.is_empty()) {
        auto result = as<ReadAt>(reader).read_at(bytes, offset);
        if (result.is_err()) {
            auto error = result.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        auto count = result.unwrap_unchecked();
        if (count == usize()) return Err(error::Error_READ_EXACT_EOF);
        if (count > bytes.len()) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        auto next = offset.checked_add(u64(count.to_primitive()));
        if (next.is_none()) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        offset = next.unwrap_unchecked();
        bytes  = mut_ref<u8[]>::from_raw_parts(bytes.as_raw_ptr() + count.to_primitive(),
                                               bytes.len() - count);
    }
    return Ok(empty {});
}

/// Writes the whole buffer without changing the destination cursor.
export template<typename W>
    requires Impled<W, WriteAt>
auto write_all_at(const W& w, slice<u8> buf, u64 offset) -> Result<empty> {
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
        buf    = slice<u8>::from_raw_parts(buf.as_raw_ptr() + written,
                                           usize(buf.len().to_primitive() - written));
        offset = rstd::move(next_offset).unwrap_unchecked();
    }
    return Ok(empty {});
}

} // namespace rstd::io

namespace rstd::io
{
template<typename Tag>
inline constexpr bool read_default_tag = false;
template<typename T>
inline constexpr bool read_default_tag<default_tag<T>> = true;
template<typename T>
inline constexpr bool read_default_tag<in_class_default_tag<T>> = true;
} // namespace rstd::io

namespace rstd
{
template<typename Tag>
    requires io::read_default_tag<Tag>
struct Impl<io::Read, Tag> : ImplBase<Tag> {
    auto read(mut_ref<u8[]> bytes) -> io::Result<usize> {
        auto&& reader = as_impl<io::Read>(this->self());
        using Source  = mtp::rm_cvf<decltype(reader)>;
        static_assert(! mtp::same_as<decltype(&Source::read_buf), decltype(&Impl::read_buf)>,
                      "Read requires an implementation of read or read_buf");
        auto buffer = io::BorrowedBuf::from_initialized(bytes);
        auto result = reader.read_buf(buffer.unfilled());
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        return Ok(buffer.len());
    }
    auto read_buf(io::BorrowedCursor cursor) -> io::Result<empty> {
        auto&& reader = as_impl<io::Read>(this->self());
        using Source  = mtp::rm_cvf<decltype(reader)>;
        static_assert(! mtp::same_as<decltype(&Source::read), decltype(&Impl::read)>,
                      "Read requires an implementation of read or read_buf");
        auto result = reader.read(cursor.ensure_init());
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        if (! cursor.advance(*result))
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidData }));
        return Ok(empty {});
    }
};
template<typename T>
struct Impl<io::ReadAt, default_tag<T>> : ImplBase<T> {
    auto read_at(mut_ref<u8[]> bytes, u64 offset) const -> io::Result<usize> {
        auto buffer = io::BorrowedBuf::from_initialized(bytes);
        auto result = as_impl<io::ReadAt>(this->self()).read_buf_at(buffer.unfilled(), offset);
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        return Ok(buffer.len());
    }
};

template<typename T>
    requires Impled<T, io::Read> && Impled<T, io::Seek>
struct Impl<io::ReadSeek, T> : ImplBase<T> {
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty> {
        return as<io::Read>(this->self()).read_buf(rstd::move(buf));
    }
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        return as<io::Read>(this->self()).read(buf);
    }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

template<typename T>
    requires Impled<T, io::Write> && Impled<T, io::Seek>
struct Impl<io::WriteSeek, T> : ImplBase<T> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        return as<io::Write>(this->self()).write(buf);
    }
    auto flush() -> io::Result<empty> { return as<io::Write>(this->self()).flush(); }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

template<typename T>
    requires Impled<T, io::Read> && Impled<T, io::Write> && Impled<T, io::Seek>
struct Impl<io::ReadWriteSeek, T> : ImplBase<T> {
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty> {
        return as<io::Read>(this->self()).read_buf(rstd::move(buf));
    }
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        return as<io::Read>(this->self()).read(buf);
    }
    auto write(slice<u8> buf) -> io::Result<usize> {
        return as<io::Write>(this->self()).write(buf);
    }
    auto flush() -> io::Result<empty> { return as<io::Write>(this->self()).flush(); }
    auto seek(io::SeekFrom pos) -> io::Result<u64> { return as<io::Seek>(this->self()).seek(pos); }
};

} // namespace rstd
