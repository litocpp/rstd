module;
#include <rstd/macro.hpp>
export module rstd.io:cursor;
export import :traits;
import rstd.alloc;
import :util;

using ::alloc::vec::Vec;

namespace rstd::io
{

// ── Cursor ────────────────────────────────────────────────────────────────
/// In-memory reader/writer with a position cursor.
/// Supported inner types: `Vec<u8, A>` (R+W+Seek+BufRead), `slice<u8>` (R+Seek+BufRead).
export template<typename T>
class Cursor {
    T   inner_;
    u64 pos_ {};

public:
    USE_TRAIT(Cursor)

    explicit Cursor(T inner) noexcept: inner_(rstd::move(inner)), pos_() {}

    /// Returns a reference to the underlying value in this cursor.
    auto get_ref() const noexcept -> const T& { return inner_; }
    /// Returns a mutable reference to the underlying value in this cursor.
    auto get_mut() noexcept -> T& { return inner_; }
    /// Returns the current position of this cursor.
    auto position() const noexcept -> u64 { return pos_; }
    /// Sets the position of this cursor.
    /// \param p The new byte offset from the start.
    void set_position(u64 p) noexcept { pos_ = p; }
    /// Consumes this cursor, returning the underlying value.
    auto into_inner() -> T { return rstd::move(inner_); }
};

} // namespace rstd::io

using namespace rstd::prelude;

template<typename A>
inline auto cursor_data(const Vec<u8, A>& v) noexcept -> const byte* {
    return v.begin();
}
template<typename A>
inline auto cursor_len(const Vec<u8, A>& v) noexcept -> usize {
    return v.len();
}
inline auto cursor_data(slice<u8> s) noexcept -> const byte* {
    return s.as_raw_ptr();
}
inline auto cursor_len(slice<u8> s) noexcept -> usize {
    return s.len();
}

template<typename T>
inline constexpr bool cursor_owned_bytes = false;
template<typename A>
inline constexpr bool cursor_owned_bytes<Vec<u8, A>> = true;

// ── Impl specialisations ──────────────────────────────────────────────────
namespace rstd
{

// Read — Vec<u8> and slice<u8>
template<typename T>
    requires(cursor_owned_bytes<T> || mtp::same_as<T, slice<u8>>)
struct Impl<io::Read, io::Cursor<T>> : DefaultInImpl<io::Read, io::Cursor<T>> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        auto& self         = this->self();
        auto  total        = cursor_len(self.inner_);
        auto  raw_position = self.pos_.to_primitive();
        if (raw_position >= total.to_primitive()) return Ok(usize());
        auto position = usize(static_cast<rstd::size_t>(raw_position));
        auto count    = rstd::min(buf.len(), total - position);
        if (count != usize())
            rstd::mem::memcpy(
                buf.as_raw_ptr(), cursor_data(self.inner_) + position.to_primitive(), count);
        self.pos_ += u64(count.to_primitive());
        return Ok(count);
    }
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty> {
        auto& self         = this->self();
        auto  total        = cursor_len(self.inner_);
        auto  raw_position = self.pos_.to_primitive();
        if (raw_position >= total.to_primitive()) return Ok(empty {});
        auto position = usize(static_cast<rstd::size_t>(raw_position));
        auto count    = rstd::min(buf.capacity(), total - position);
        buf.append(
            slice<u8>::from_raw_parts(cursor_data(self.inner_) + position.to_primitive(), count));
        self.pos_ += u64(count.to_primitive());
        return Ok(empty {});
    }
};

template<typename A, typename T>
    requires(cursor_owned_bytes<T> || mtp::same_as<T, slice<u8>>) && Impled<A, alloc::Allocator>
struct Impl<io::ReadToEnd<A>, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto read_to_end(Vec<u8, A>& output) -> io::Result<usize> {
        auto& self     = this->self();
        auto  total    = cursor_len(self.inner_);
        auto  position = self.pos_.to_primitive();
        if (position >= total.to_primitive()) return Ok(usize());
        auto count    = total - usize(static_cast<rstd::size_t>(position));
        auto reserved = output.try_reserve(count);
        if (reserved.is_err()) return Err(io::reserve_error(reserved.unwrap_err_unchecked()));
        auto target = output.spare_capacity_mut().as_uninit();
        rstd::mem::memcpy(target.as_raw_ptr(), cursor_data(self.inner_) + position, count);
        output.set_len_unchecked(output.len() + count);
        self.pos_ += u64(count.to_primitive());
        return Ok(count);
    }
};

// BufRead — Vec<u8> and slice<u8>
template<typename T>
    requires(cursor_owned_bytes<T> || mtp::same_as<T, slice<u8>>)
struct Impl<io::BufRead, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto fill_buf() -> io::Result<slice<u8>> {
        auto&      self         = this->self();
        auto const total        = cursor_len(self.inner_).to_primitive();
        auto const raw_position = self.pos_.to_primitive();
        auto const position =
            raw_position >= total ? total : static_cast<rstd::size_t>(raw_position);
        if (position == total) return Ok(slice<u8> {});
        return Ok(slice<u8>::from_raw_parts(cursor_data(self.inner_) + position,
                                            usize(total - position)));
    }
    auto consume(usize amt) -> void {
        auto&      self         = this->self();
        auto const total        = cursor_len(self.inner_).to_primitive();
        auto const raw_position = self.pos_.to_primitive();
        auto const position =
            raw_position >= total ? total : static_cast<rstd::size_t>(raw_position);
        auto const consumed = rstd::min(amt.to_primitive(), total - position);
        self.pos_           = u64(position + consumed);
    }
};

// Seek — Vec<u8> and slice<u8>
template<typename T>
    requires(cursor_owned_bytes<T> || mtp::same_as<T, slice<u8>>)
struct Impl<io::Seek, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto seek(io::SeekFrom sf) -> io::Result<u64> {
        auto& self = this->self();
        if (sf.which == io::SeekFrom::Which::Start) {
            self.pos_ = sf.start;
            return Ok(self.pos_);
        }

        rstd::int128_t base = 0;
        switch (sf.which) {
        case io::SeekFrom::Which::Start: break;
        case io::SeekFrom::Which::End:
            base = static_cast<rstd::int128_t>(cursor_len(self.inner_).to_primitive());
            break;
        case io::SeekFrom::Which::Current:
            base = static_cast<rstd::int128_t>(self.pos_.to_primitive());
            break;
        }
        auto const new_position = base + static_cast<rstd::int128_t>(sf.offset.to_primitive());
        if (new_position < 0 || new_position > static_cast<rstd::int128_t>(~rstd::uint64_t(0))) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        self.pos_ = u64(new_position);
        return Ok(self.pos_);
    }
};

// Write — Vec<u8> only (grows the buffer)
template<typename A>
struct Impl<io::Write, io::Cursor<Vec<u8, A>>> : ImplBase<io::Cursor<Vec<u8, A>>> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        auto&      self         = this->self();
        auto const raw_position = self.pos_.to_primitive();
        if (raw_position > ~rstd::size_t(0)) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        auto const position = static_cast<rstd::size_t>(raw_position);
        auto const count    = buf.len().to_primitive();
        if (count > ~rstd::size_t(0) - position) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        auto const end = position + count;
        if (usize(end) > self.inner_.len()) {
            auto reserved = self.inner_.try_reserve(usize(end) - self.inner_.len());
            if (reserved.is_err()) return Err(io::reserve_error(reserved.unwrap_err_unchecked()));
            self.inner_.resize(usize(end), u8 {});
        }
        if (count != 0) {
            auto destination =
                mut_ref<u8[]>::from_raw_parts(self.inner_.begin() + position, usize(count));
            rstd::mem::memcpy(destination.as_raw_ptr(), buf.as_raw_ptr(), buf.len());
        }
        self.pos_ = u64(end);
        return Ok(buf.len());
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

} // namespace rstd
