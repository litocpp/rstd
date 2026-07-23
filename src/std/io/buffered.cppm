module;
#include <rstd/macro.hpp>
export module rstd:io.buffered;
export import :io.traits;
import rstd.alloc;
import :forward;

using rstd_alloc::vec::Vec;

namespace rstd::io
{

// ── BufReader ─────────────────────────────────────────────────────────────
/// A buffered wrapper around a reader, reducing the number of read syscalls.
/// \tparam R The underlying reader type, which must implement `io::Read`.
export template<typename R>
    requires Impled<R, io::Read>
class BufReader {
    R            inner_;
    Vec<u8>      buf_;
    rstd::size_t pos_ {};
    rstd::size_t filled_ {};

    // Refill buffer from inner.  Resets pos_ and filled_.
    auto fill_inner() -> Result<usize> {
        pos_        = 0;
        filled_     = 0;
        auto values = buf_.as_mut_slice().as_mut_ref();
        auto res    = as<Read>(inner_).read(values);
        if (res.is_ok()) filled_ = res.unwrap_unchecked().to_primitive();
        return res;
    }

public:
    USE_TRAIT(BufReader)

    /// Creates a new BufReader with the specified buffer capacity.
    /// \param inner The underlying reader.
    /// \param capacity The buffer size in bytes (defaults to DEFAULT_BUF_SIZE).
    explicit BufReader(R inner, usize capacity = DEFAULT_BUF_SIZE)
        : inner_(rstd::move(inner)), buf_(Vec<u8>::with_capacity(capacity)) {
        for (rstd::size_t i = 0; i < capacity.to_primitive(); ++i) buf_.push(u8 {});
    }

    /// Returns a reference to the underlying reader.
    auto get_ref() const noexcept -> const R& { return inner_; }
    /// Returns a mutable reference to the underlying reader.
    auto get_mut() noexcept -> R& { return inner_; }
    /// Returns the total capacity of the internal buffer.
    auto capacity() const noexcept -> usize { return buf_.len(); }
    /// Returns a slice of the buffered data that has been read but not yet consumed.
    auto buffer() const noexcept -> slice<u8> {
        if (pos_ == filled_) return {};
        return slice<u8>::from_raw_parts(buf_.begin() + pos_, usize(filled_ - pos_));
    }

    /// Discard the internal buffer (call after seeking inner directly).
    void discard_buffer() noexcept { pos_ = filled_ = 0; }

    /// Unwrap the inner reader (discards any buffered data).
    auto into_inner() && -> R { return rstd::move(inner_); }
};

// ── BufWriter ─────────────────────────────────────────────────────────────
/// A buffered wrapper around a writer, reducing the number of write syscalls.
/// \tparam W The underlying writer type, which must implement `io::Write`.
export template<typename W>
    requires Impled<W, io::Write>
class BufWriter {
    W       inner_;
    Vec<u8> buf_;

    auto flush_buf() -> Result<empty> {
        auto         remaining = buf_.len().to_primitive();
        rstd::size_t offset    = 0;
        auto         bytes     = buf_.as_slice();
        while (remaining != 0) {
            auto pending =
                slice<u8>::from_raw_parts(bytes.as_raw_ptr() + offset, usize(remaining));
            auto res = as<Write>(inner_).write(pending);
            if (res.is_err()) {
                if (offset != 0) {
                    auto const left = buf_.len().to_primitive() - offset;
                    for (rstd::size_t i = 0; i < left; ++i) {
                        buf_.begin()[i] = buf_.begin()[offset + i];
                    }
                    for (rstd::size_t i = 0; i < offset; ++i) (void)buf_.pop();
                }
                return Err(res.unwrap_err_unchecked());
            }
            auto const written = res.unwrap_unchecked().to_primitive();
            if (written == 0) return Err(error::Error_WRITE_ALL_EOF);
            if (written > remaining) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
            }
            offset += written;
            remaining -= written;
        }
        buf_.clear();
        return Ok(empty {});
    }

    friend struct rstd::Impl<io::Write, BufWriter<W>>;
    friend struct rstd::Impl<io::Seek, BufWriter<W>>;

public:
    USE_TRAIT(BufWriter)

    /// Creates a new BufWriter with the specified buffer capacity.
    /// \param inner The underlying writer.
    /// \param capacity The buffer size in bytes (defaults to DEFAULT_BUF_SIZE).
    explicit BufWriter(W inner, usize capacity = DEFAULT_BUF_SIZE)
        : inner_(rstd::move(inner)), buf_(Vec<u8>::with_capacity(capacity)) {}

    /// Returns a reference to the underlying writer.
    auto get_ref() const noexcept -> const W& { return inner_; }
    /// Returns a mutable reference to the underlying writer.
    auto get_mut() noexcept -> W& { return inner_; }
    /// Returns the total capacity of the internal buffer.
    auto capacity() const noexcept -> usize { return buf_.capacity(); }
    /// Returns a slice of the buffered data that has not yet been flushed.
    auto buffer() const noexcept -> slice<u8> { return buf_.as_slice(); }

    /// Consumes this BufWriter, flushing and returning the underlying writer.
    auto into_inner() && -> W { return rstd::move(inner_); }
};

} // namespace rstd::io

// ── Impl specialisations (must live in namespace rstd) ────────────────────
namespace rstd
{

template<typename R>
    requires Impled<R, io::Read>
struct Impl<io::Read, io::BufReader<R>> : ImplBase<io::BufReader<R>> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        auto& self = this->self();
        if (buf.is_empty()) return Ok(usize {});
        // Bypass buffer for large reads when buffer is empty.
        if (self.pos_ == self.filled_ && buf.len() >= self.buf_.len()) {
            return as<io::Read>(self.inner_).read(buf);
        }
        if (self.pos_ == self.filled_) {
            auto res = self.fill_inner();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
            if (self.filled_ == 0) return Ok(usize {});
        }
        auto const count  = rstd::min(buf.len().to_primitive(), self.filled_ - self.pos_);
        auto       source = slice<u8>::from_raw_parts(self.buf_.begin() + self.pos_, usize(count));
        rstd::mem::memcpy(buf.as_raw_ptr(), source.as_raw_ptr(), usize(count));
        self.pos_ += count;
        return Ok(usize(count));
    }
};

template<typename R>
    requires Impled<R, io::Read>
struct Impl<io::BufRead, io::BufReader<R>> : ImplBase<io::BufReader<R>> {
    auto fill_buf() -> io::Result<slice<u8>> {
        auto& self = this->self();
        if (self.pos_ == self.filled_) {
            auto res = self.fill_inner();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
        }
        if (self.pos_ == self.filled_) return Ok(slice<u8> {});
        return Ok(slice<u8>::from_raw_parts(self.buf_.begin() + self.pos_,
                                            usize(self.filled_ - self.pos_)));
    }
    auto consume(usize amt) -> void {
        auto&      self      = this->self();
        auto const available = self.filled_ - self.pos_;
        self.pos_ += rstd::min(amt.to_primitive(), available);
    }
};

template<typename R>
    requires Impled<R, io::Read> && Impled<R, io::Seek>
struct Impl<io::Seek, io::BufReader<R>> : ImplBase<io::BufReader<R>> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> {
        auto& self = this->self();
        self.discard_buffer();
        return as<io::Seek>(self.inner_).seek(pos);
    }
};

template<typename W>
    requires Impled<W, io::Write>
struct Impl<io::Write, io::BufWriter<W>> : ImplBase<io::BufWriter<W>> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        auto& self = this->self();
        if (self.buf_.len() + buf.len() > self.buf_.capacity()) {
            auto res = self.flush_buf();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
        }
        if (buf.len() >= self.buf_.capacity()) {
            return as<io::Write>(self.inner_).write(buf);
        }
        self.buf_.extend_from_slice(buf);
        return Ok(buf.len());
    }
    auto flush() -> io::Result<empty> {
        auto& self = this->self();
        auto  res  = self.flush_buf();
        if (res.is_err()) return res;
        return as<io::Write>(self.inner_).flush();
    }
};

template<typename W>
    requires Impled<W, io::Write> && Impled<W, io::Seek>
struct Impl<io::Seek, io::BufWriter<W>> : ImplBase<io::BufWriter<W>> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> {
        auto& self = this->self();
        auto  res  = self.flush_buf();
        if (res.is_err()) return Err(res.unwrap_err_unchecked());
        return as<io::Seek>(self.inner_).seek(pos);
    }
};

} // namespace rstd
