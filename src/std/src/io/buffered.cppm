module;
#include <rstd/macro.hpp>
export module rstd.io:buffered;
export import :traits;
import :util;
import rstd.alloc;

using rstd::mem::MaybeUninit;

using ::alloc::vec::Vec;

namespace rstd::io
{

template<typename A>
class ReaderBuffer {
    ::alloc::boxed::Box<MaybeUninit<byte>[], A> storage_;
    usize                                       pos_ {}, filled_ {}, initialized_ {};

public:
    explicit ReaderBuffer(::alloc::boxed::Box<MaybeUninit<byte>[], A> storage)
        : storage_(rstd::move(storage)) {}
    auto capacity() const noexcept -> usize { return storage_.as_ptr().len(); }
    auto buffer() const noexcept -> slice<u8> {
        if (pos_ == filled_) return {};
        auto pointer = reinterpret_cast<const byte*>(storage_.as_ptr().as_raw_ptr());
        return slice<u8>::from_raw_parts(pointer + pos_.to_primitive(), filled_ - pos_);
    }
    void consume(usize amount) noexcept { pos_ += rstd::min(amount, filled_ - pos_); }
    void discard() noexcept { pos_ = filled_ = usize(); }
    template<typename R>
    auto fill(R& reader) -> Result<empty> {
        if (pos_ != filled_) return Ok(empty {});
        auto target = BorrowedBuf::from_uninit(storage_.deref_mut());
        target.set_init_unchecked(initialized_);
        auto result  = as<Read>(reader).read_buf(target.unfilled());
        pos_         = usize();
        filled_      = target.len();
        initialized_ = target.initialized_len();
        return result;
    }
};

// ── BufReader ─────────────────────────────────────────────────────────────
/// A buffered wrapper around a reader, reducing the number of read syscalls.
/// \tparam R The underlying reader type, which must implement `io::Read`.
export template<typename R, typename A = ::alloc::Global>
    requires Impled<R, io::Read>
class BufReader {
    R               inner_;
    ReaderBuffer<A> buf_;
    BufReader(R inner, ReaderBuffer<A> buffer)
        : inner_(rstd::move(inner)), buf_(rstd::move(buffer)) {}

public:
    USE_TRAIT(BufReader)

    /// Creates a new BufReader with the specified buffer capacity.
    /// \param inner The underlying reader.
    /// \param capacity The buffer size in bytes (defaults to DEFAULT_BUF_SIZE).
    explicit BufReader(R inner, usize capacity = DEFAULT_BUF_SIZE)
        requires mtp::same_as<A, ::alloc::Global>
        : BufReader(make_in(rstd::move(inner), A {}, capacity)) {}

    /// Takes ownership of inner and allocator, including on allocation failure.
    static auto try_make_in(R inner, A allocator, usize capacity = DEFAULT_BUF_SIZE)
        -> rstd::Result<BufReader, ::alloc::collections::TryReserveError> {
        auto buffer =
            ::alloc::boxed::Box<byte[], A>::try_make_uninit_in(capacity, rstd::move(allocator));
        if (buffer.is_err()) return Err(buffer.unwrap_err_unchecked());
        return Ok(BufReader(rstd::move(inner), ReaderBuffer<A>(buffer.unwrap_unchecked())));
    }
    static auto make_in(R inner, A allocator, usize capacity = DEFAULT_BUF_SIZE) -> BufReader {
        auto result = try_make_in(rstd::move(inner), rstd::move(allocator), capacity);
        if (result.is_err()) rstd::panic { "I/O buffer allocation failed" };
        return result.unwrap_unchecked();
    }

    /// Returns a reference to the underlying reader.
    auto get_ref() const noexcept -> const R& { return inner_; }
    /// Returns a mutable reference to the underlying reader.
    auto get_mut() noexcept -> R& { return inner_; }
    /// Returns the total capacity of the internal buffer.
    auto capacity() const noexcept -> usize { return buf_.capacity(); }
    /// Returns a slice of the buffered data that has been read but not yet consumed.
    auto buffer() const noexcept -> slice<u8> { return buf_.buffer(); }
    void discard_buffer() noexcept { buf_.discard(); }

    /// Unwrap the inner reader (discards any buffered data).
    auto into_inner() && -> R { return rstd::move(inner_); }
};

// ── BufWriter ─────────────────────────────────────────────────────────────
/// A buffered wrapper around a writer, reducing the number of write syscalls.
/// \tparam W The underlying writer type, which must implement `io::Write`.
export template<typename W, typename A = ::alloc::Global>
    requires Impled<W, io::Write>
class BufWriter {
    W          inner_;
    Vec<u8, A> buf_;

    BufWriter(W inner, Vec<u8, A> buffer): inner_(rstd::move(inner)), buf_(rstd::move(buffer)) {}

    auto flush_buf() -> Result<empty> {
        auto         remaining = buf_.len().to_primitive();
        rstd::size_t offset    = 0;
        struct WrittenPrefix {
            Vec<u8, A>&   buffer;
            rstd::size_t& offset;
            ~WrittenPrefix() {
                if (offset == 0) return;
                auto left = buffer.len() - usize(offset);
                for (rstd::size_t i = 0; i < left.to_primitive(); ++i)
                    buffer.data()[i] = buffer.data()[offset + i];
                buffer.truncate(left);
            }
        } prefix { buf_, offset };
        auto bytes = buf_.as_slice();
        while (remaining != 0) {
            auto pending = slice<u8>::from_raw_parts(bytes.as_raw_ptr() + offset, usize(remaining));
            auto res     = as<Write>(inner_).write(pending);
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
            auto const written = res.unwrap_unchecked().to_primitive();
            if (written == 0) return Err(error::Error_WRITE_ALL_EOF);
            if (written > remaining) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
            }
            offset += written;
            remaining -= written;
        }
        return Ok(empty {});
    }

    friend struct rstd::Impl<io::Write, BufWriter<W, A>>;
    friend struct rstd::Impl<io::Seek, BufWriter<W, A>>;

public:
    USE_TRAIT(BufWriter)

    /// Creates a new BufWriter with the specified buffer capacity.
    /// \param inner The underlying writer.
    /// \param capacity The buffer size in bytes (defaults to DEFAULT_BUF_SIZE).
    explicit BufWriter(W inner, usize capacity = DEFAULT_BUF_SIZE)
        requires mtp::same_as<A, ::alloc::Global>
        : BufWriter(make_in(rstd::move(inner), A {}, capacity)) {}

    /// Takes ownership of inner and allocator, including on allocation failure.
    static auto try_make_in(W inner, A allocator, usize capacity = DEFAULT_BUF_SIZE)
        -> rstd::Result<BufWriter, ::alloc::collections::TryReserveError> {
        auto buffer   = Vec<u8, A>::new_in(rstd::move(allocator));
        auto reserved = buffer.try_reserve_exact(capacity);
        if (reserved.is_err()) return Err(reserved.unwrap_err_unchecked());
        return Ok(BufWriter(rstd::move(inner), rstd::move(buffer)));
    }
    static auto make_in(W inner, A allocator, usize capacity = DEFAULT_BUF_SIZE) -> BufWriter {
        auto result = try_make_in(rstd::move(inner), rstd::move(allocator), capacity);
        if (result.is_err()) rstd::panic { "I/O buffer allocation failed" };
        return result.unwrap_unchecked();
    }

    /// Returns a reference to the underlying writer.
    auto get_ref() const noexcept -> const W& { return inner_; }
    /// Returns a mutable reference to the underlying writer.
    auto get_mut() noexcept -> W& { return inner_; }
    /// Returns the total capacity of the internal buffer.
    auto capacity() const noexcept -> usize { return buf_.capacity(); }
    /// Returns a slice of the buffered data that has not yet been flushed.
    auto buffer() const noexcept -> slice<u8> { return buf_.as_slice(); }

    /// Returns the underlying writer, discarding buffered bytes. Flush explicitly before extraction.
    auto into_inner() && -> W { return rstd::move(inner_); }
};

} // namespace rstd::io

// ── Impl specialisations (must live in namespace rstd) ────────────────────
namespace rstd
{

template<typename R, typename A>
    requires Impled<R, io::Read>
struct Impl<io::Read, io::BufReader<R, A>> : DefaultInImpl<io::Read, io::BufReader<R, A>> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        auto& self = this->self();
        if (buf.is_empty()) return Ok(usize());
        if (self.buf_.buffer().is_empty() && buf.len() >= self.buf_.capacity())
            return as<io::Read>(self.inner_).read(buf);
        auto result    = self.buf_.fill(self.inner_);
        auto available = self.buf_.buffer();
        auto count     = rstd::min(available.len(), buf.len());
        if (count != usize()) rstd::mem::memcpy(buf.as_raw_ptr(), available.as_raw_ptr(), count);
        self.buf_.consume(count);
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        return Ok(count);
    }
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty> {
        auto& self = this->self();
        if (buf.capacity() == usize()) return Ok(empty {});
        if (self.buf_.buffer().is_empty() && buf.capacity() >= self.buf_.capacity())
            return as<io::Read>(self.inner_).read_buf(rstd::move(buf));
        auto result    = self.buf_.fill(self.inner_);
        auto available = self.buf_.buffer();
        auto count     = rstd::min(available.len(), buf.capacity());
        buf.append(slice<u8>::from_raw_parts(available.as_raw_ptr(), count));
        self.buf_.consume(count);
        return result;
    }
};

template<typename R, typename A>
    requires Impled<R, io::Read>
struct Impl<io::BufRead, io::BufReader<R, A>> : ImplBase<io::BufReader<R, A>> {
    auto fill_buf() -> io::Result<slice<u8>> {
        auto& self   = this->self();
        auto  result = self.buf_.fill(self.inner_);
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        return Ok(self.buf_.buffer());
    }
    void consume(usize amount) { this->self().buf_.consume(amount); }
};

template<typename OutputAllocator, typename R, typename A>
    requires Impled<R, io::Read> && Impled<OutputAllocator, alloc::Allocator>
struct Impl<io::ReadToEnd<OutputAllocator>, io::BufReader<R, A>> : ImplBase<io::BufReader<R, A>> {
    auto read_to_end(Vec<u8, OutputAllocator>& output) -> io::Result<usize> {
        auto& self     = this->self();
        auto  buffered = self.buf_.buffer();
        auto  count    = buffered.len();
        auto  reserved = output.try_reserve(count);
        if (reserved.is_err()) return Err(io::reserve_error(reserved.unwrap_err_unchecked()));
        if (count != usize()) {
            rstd::mem::memcpy(
                output.spare_capacity_mut().as_uninit().as_raw_ptr(), buffered.as_raw_ptr(), count);
            output.set_len_unchecked(output.len() + count);
        }
        self.buf_.discard();
        auto rest = io::read_to_end(self.inner_, output);
        if (rest.is_err()) return Err(rest.unwrap_err_unchecked());
        return Ok(count + rest.unwrap_unchecked());
    }
};

template<typename R, typename A>
    requires Impled<R, io::Read> && Impled<R, io::Seek>
struct Impl<io::Seek, io::BufReader<R, A>> : ImplBase<io::BufReader<R, A>> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> {
        auto& self = this->self();
        if (pos.which == io::SeekFrom::Which::Current) {
            auto const unread = self.buf_.buffer().len().to_primitive();
            auto const offset = static_cast<rstd::int128_t>(pos.offset.to_primitive()) -
                                static_cast<rstd::int128_t>(unread);
            if (offset < static_cast<rstd::int128_t>(i64::MIN.to_primitive()) ||
                offset > static_cast<rstd::int128_t>(i64::MAX.to_primitive())) {
                return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
            }
            pos = io::SeekFrom::from_current(i64(static_cast<rstd::int64_t>(offset)));
        }
        auto result = as<io::Seek>(self.inner_).seek(pos);
        if (result.is_ok()) self.discard_buffer();
        return result;
    }
};

template<typename W, typename A>
    requires Impled<W, io::Write>
struct Impl<io::Write, io::BufWriter<W, A>> : ImplBase<io::BufWriter<W, A>> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        auto& self = this->self();
        if (buf.len() > self.buf_.capacity() - self.buf_.len()) {
            auto res = self.flush_buf();
            if (res.is_err()) return Err(res.unwrap_err_unchecked());
        }
        if (buf.len() >= self.buf_.capacity()) {
            return as<io::Write>(self.inner_).write(buf);
        }
        if (! buf.is_empty()) {
            auto target = self.buf_.spare_capacity_mut().as_uninit();
            rstd::mem::memcpy(target.as_raw_ptr(), buf.as_raw_ptr(), buf.len());
            self.buf_.set_len_unchecked(self.buf_.len() + buf.len());
        }
        return Ok(buf.len());
    }
    auto flush() -> io::Result<empty> {
        auto& self = this->self();
        auto  res  = self.flush_buf();
        if (res.is_err()) return res;
        return as<io::Write>(self.inner_).flush();
    }
};

template<typename W, typename A>
    requires Impled<W, io::Write> && Impled<W, io::Seek>
struct Impl<io::Seek, io::BufWriter<W, A>> : ImplBase<io::BufWriter<W, A>> {
    auto seek(io::SeekFrom pos) -> io::Result<u64> {
        auto& self = this->self();
        auto  res  = self.flush_buf();
        if (res.is_err()) return Err(res.unwrap_err_unchecked());
        return as<io::Seek>(self.inner_).seek(pos);
    }
};

} // namespace rstd
