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
export template<typename R>
    requires Impled<R, io::Read>
class BufReader {
    R       inner_;
    Vec<u8> buf_;
    usize   pos_    = 0;
    usize   filled_ = 0;

    // Refill buffer from inner.  Resets pos_ and filled_.
    auto fill_inner() -> Result<usize> {
        pos_     = 0;
        filled_  = 0;
        auto res = as<Read>(inner_).read(buf_.begin(), buf_.len());
        if (res.is_ok()) filled_ = res.unwrap_unchecked();
        return res;
    }

    friend struct rstd::Impl<io::Read, BufReader<R>>;
    friend struct rstd::Impl<io::BufRead, BufReader<R>>;
    friend struct rstd::Impl<io::Seek, BufReader<R>>;

public:
    USE_TRAIT(BufReader)

    explicit BufReader(R inner, usize capacity = DEFAULT_BUF_SIZE)
        : inner_(rstd::move(inner)), buf_(Vec<u8>::with_capacity(capacity)) {
        for (usize i = 0; i < capacity; ++i) buf_.push(u8(0));
    }

    auto get_ref() const noexcept -> const R& { return inner_; }
    auto get_mut() noexcept -> R& { return inner_; }
    auto capacity() const noexcept -> usize { return buf_.len(); }
    auto buffer() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(buf_.begin() + pos_, filled_ - pos_);
    }

    /// Discard the internal buffer (call after seeking inner directly).
    void discard_buffer() noexcept { pos_ = filled_ = 0; }

    /// Unwrap the inner reader (discards any buffered data).
    auto into_inner() && -> R { return rstd::move(inner_); }
};

// ── BufWriter ─────────────────────────────────────────────────────────────
export template<typename W>
    requires Impled<W, io::Write>
class BufWriter {
    W       inner_;
    Vec<u8> buf_;

    auto flush_buf() -> Result<empty> {
        usize rem = buf_.len();
        usize off = 0;
        while (rem > 0) {
            auto res = as<Write>(inner_).write(buf_.begin() + off, rem);
            if (res.is_err()) {
                // Remove already-written bytes.
                if (off > 0) {
                    usize left = buf_.len() - off;
                    for (usize i = 0; i < left; ++i) buf_.begin()[i] = buf_.begin()[off + i];
                    buf_.clear();
                    for (usize i = 0; i < left; ++i) buf_.push(u8(0));
                } else {
                    buf_.clear();
                }
                return Err(res.unwrap_err_unchecked());
            }
            usize n = res.unwrap_unchecked();
            off += n;
            rem -= n;
        }
        buf_.clear();
        return Ok(empty {});
    }

    friend struct rstd::Impl<io::Write, BufWriter<W>>;
    friend struct rstd::Impl<io::Seek, BufWriter<W>>;

public:
    USE_TRAIT(BufWriter)

    explicit BufWriter(W inner, usize capacity = DEFAULT_BUF_SIZE)
        : inner_(rstd::move(inner)), buf_(Vec<u8>::with_capacity(capacity)) {}

    auto get_ref() const noexcept -> const W& { return inner_; }
    auto get_mut() noexcept -> W& { return inner_; }
    auto capacity() const noexcept -> usize { return buf_.capacity(); }
    auto buffer() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(buf_.begin(), buf_.len());
    }

    auto into_inner() && -> W { return rstd::move(inner_); }
};

} // namespace rstd::io

// ── Impl specialisations (must live in namespace rstd) ────────────────────
namespace rstd
{

template<typename R>
    requires Impled<R, io::Read>
struct Impl<io::Read, io::BufReader<R>> : ImplBase<io::BufReader<R>> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        auto& self = this->self();
        // Bypass buffer for large reads when buffer is empty.
        if (self.pos_ == self.filled_ && len >= self.buf_.len()) {
            return as<io::Read>(self.inner_).read(buf, len);
        }
        if (self.pos_ == self.filled_) {
            auto res = self.fill_inner();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
            if (self.filled_ == 0) return Ok(usize(0));
        }
        usize n = rstd::min(len, self.filled_ - self.pos_);
        rstd::mem::memcpy(buf, self.buf_.begin() + self.pos_, n);
        self.pos_ += n;
        return Ok(n);
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
        return Ok(
            slice<u8>::from_raw_parts(self.buf_.begin() + self.pos_, self.filled_ - self.pos_));
    }
    auto consume(usize amt) -> void {
        auto& self = this->self();
        self.pos_  = rstd::min(self.pos_ + amt, self.filled_);
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
    auto write(const u8* buf, usize len) -> io::Result<usize> {
        auto& self = this->self();
        if (self.buf_.len() + len > self.buf_.capacity()) {
            auto res = self.flush_buf();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
        }
        if (len >= self.buf_.capacity()) {
            return as<io::Write>(self.inner_).write(buf, len);
        }
        for (usize i = 0; i < len; ++i) self.buf_.push(u8(buf[i]));
        return Ok(len);
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
